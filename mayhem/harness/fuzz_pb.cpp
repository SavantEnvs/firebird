// Feeds fuzzer bytes to the engine's parameter-block parsers (ClumpletReader/Writer users) through
// the public API: DPB (create/attach), TPB (start transaction), SPB (service attach) and BPB
// (create blob). The first byte selects the entry point (weighted), the rest is the block.
#include "fb_embedded.h"

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
  fbfx::init_or_exit();
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
  if (size < 2 || size > 0x7fff) return -1;

  // Creating a database costs ~20 ms, the other entry points well under 1 ms: keep creation rare.
  const unsigned sel = data[0] % 16;
  const unsigned kind = sel == 0 ? 0 : sel < 6 ? 1 : sel < 10 ? 2 : sel < 13 ? 3 : 4;
  const char* blk = (const char*) data + 1;
  const short len = (short) (size - 1);
  ISC_STATUS_ARRAY st;

  switch (kind)
  {
  case 0:   // database parameter block, creating a scratch database that is dropped again
  {
    isc_db_handle db = 0;
    const std::string path = fbfx::g_dir + "/pb.fdb";
    if (!isc_create_database(st, 0, path.c_str(), &db, len, blk, 0))
      isc_drop_database(st, &db);
    break;
  }
  case 1:   // database parameter block, attach path (the file does not exist)
  {
    isc_db_handle db = 0;
    const std::string path = fbfx::g_dir + "/absent.fdb";
    if (!isc_attach_database(st, 0, path.c_str(), &db, len, blk))
      isc_detach_database(st, &db);
    break;
  }
  case 2:   // transaction parameter block
  {
    isc_tr_handle tr = 0;
    if (!isc_start_transaction(st, &tr, 1, &fbfx::g_db, len, blk))
      isc_rollback_transaction(st, &tr);
    break;
  }
  case 3:   // service parameter block
  {
    isc_svc_handle svc = 0;
    if (!isc_service_attach(st, 0, "service_mgr", &svc, len, blk))
      isc_service_detach(st, &svc);
    break;
  }
  default:  // blob parameter block
  {
    isc_blob_handle blob = 0;
    ISC_QUAD id;
    if (!isc_create_blob2(st, &fbfx::g_db, &fbfx::g_tr, &blob, &id, len, blk))
      isc_cancel_blob(st, &blob);
    break;
  }
  }
  return 0;
}
