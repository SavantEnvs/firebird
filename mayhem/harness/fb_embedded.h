// Shared fixture for the embedded-engine harnesses: one scratch database per process, created
// under a writable directory with enough free space (the image dir is read-only while Mayhem
// runs), removed at exit. Directories left behind by killed processes (a crash skips atexit) are
// swept at start, so repeated crash/restart cycles cannot fill a small scratch disk.
#pragma once
#include <ibase.h>
#include <dirent.h>
#include <errno.h>
#include <ftw.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <string>

namespace fbfx {

static std::string g_dir;
static isc_db_handle g_db = 0;
static isc_tr_handle g_tr = 0;

static int rm_cb(const char* p, const struct stat*, int, struct FTW*) { return remove(p); }

static void cleanup()
{
  ISC_STATUS_ARRAY st;
  if (g_tr) { if (isc_rollback_transaction(st, &g_tr)) isc_print_status(st); g_tr = 0; }
  if (g_db) { if (isc_detach_database(st, &g_db)) isc_print_status(st); g_db = 0; }
  if (!g_dir.empty()) nftw(g_dir.c_str(), rm_cb, 16, FTW_DEPTH | FTW_PHYS);
}

// The engine maps its lock/event tables (a few MB) and preallocates the database file in the scratch
// directory: only use a directory that has room for them.
static const off_t kMinFree = 64 * 1024 * 1024;

static bool usable(const char* dir)
{
  struct statvfs sv;
  if (!dir || !*dir || access(dir, W_OK | X_OK) != 0 || statvfs(dir, &sv) != 0) return false;
  return (off_t) sv.f_bavail * (off_t) sv.f_frsize >= kMinFree;
}

// Removes fbfuzz.<pid>.* directories whose owner process is gone.
static void sweep_stale(const std::string& root)
{
  DIR* d = opendir(root.c_str());
  if (!d) return;
  while (const struct dirent* e = readdir(d))
  {
    long pid = 0;
    if (sscanf(e->d_name, "fbfuzz.%ld.", &pid) != 1 || pid <= 0) continue;
    if (kill((pid_t) pid, 0) == 0 || errno != ESRCH) continue;
    nftw((root + "/" + e->d_name).c_str(), rm_cb, 16, FTW_DEPTH | FTW_PHYS);
  }
  closedir(d);
}

// Returns 0 on success. Never fails silently: a broken fixture must stop the run, not yield 0 edges.
static int init()
{
  const char* cands[] = { getenv("TMPDIR"), "/tmp", "/dev/shm", "/var/tmp" };
  std::string root;
  for (const char* c : cands)
  {
    if (!c || !*c) continue;
    sweep_stale(c);
    if (usable(c)) { root = c; break; }
  }
  if (root.empty()) { fprintf(stderr, "fbfx: no writable scratch directory with 64 MiB free\n"); return 1; }
  std::string buf = root + "/fbfuzz." + std::to_string((long) getpid()) + ".XXXXXX";
  if (!mkdtemp(&buf[0])) { perror("mkdtemp"); return 1; }
  g_dir = buf;
  std::string lock = g_dir + "/lock";
  mkdir(lock.c_str(), 0700);
  setenv("FIREBIRD_LOCK", lock.c_str(), 1);
  setenv("FIREBIRD_TMP", g_dir.c_str(), 1);

  std::string path = g_dir + "/fuzz.fdb";
  char dpb[24]; char* p = dpb;
  *p++ = isc_dpb_version1;
  *p++ = isc_dpb_page_size; *p++ = 2; *p++ = 0x00; *p++ = 0x20;
  *p++ = isc_dpb_set_db_sql_dialect; *p++ = 1; *p++ = 3;
  *p++ = isc_dpb_sql_dialect; *p++ = 1; *p++ = 3;
  ISC_STATUS_ARRAY st;
  if (isc_create_database(st, 0, path.c_str(), &g_db, (short)(p - dpb), dpb, 0)) {
    isc_print_status(st);
    return 1;
  }
  if (isc_start_transaction(st, &g_tr, 1, &g_db, 0, NULL)) {
    isc_print_status(st);
    return 1;
  }
  // Registered last on purpose: exit handlers run in reverse order, and the engine's own teardown
  // (registered during the first API call above) must find the attachment already closed.
  atexit(cleanup);
  return 0;
}

// The engine installs SIGINT/SIGTERM handlers on its first API call; they start a shutdown thread that
// purges live attachments. A signal landing while the fixture is being set up (or torn down) would make
// that thread race cleanup() and crash in IntlUtil::finiCharset -- a teardown artefact, not an input bug.
// Block both signals across engine setup, then restore the default action so a signal in that window
// (delivered on unblock) simply ends the process.
struct SignalGuard
{
  sigset_t old_;
  SignalGuard()
  {
    sigset_t s;
    sigemptyset(&s); sigaddset(&s, SIGINT); sigaddset(&s, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &s, &old_);
  }
  ~SignalGuard()
  {
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    pthread_sigmask(SIG_SETMASK, &old_, NULL);
  }
};

// Called from LLVMFuzzerInitialize. A fixture that cannot be set up is the environment's failure,
// not a defect of the engine: report it and exit instead of aborting (a SIGABRT would be recorded
// as a crash).
static void init_or_exit()
{
  SignalGuard guard;
  if (init()) { fprintf(stderr, "fbfx: fixture initialisation failed, exiting\n"); fflush(stderr); _exit(1); }
}

}  // namespace fbfx
