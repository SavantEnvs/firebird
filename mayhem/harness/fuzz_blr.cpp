// Feeds fuzzer bytes to the engine's BLR parser as a raw request (isc_compile_request); the
// request is never started.
#include "fb_embedded.h"

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
  fbfx::init_or_exit();
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
  if (size == 0 || size > 0x7fff) return -1;

  ISC_STATUS_ARRAY st;
  isc_req_handle req = 0;
  if (!isc_compile_request(st, &fbfx::g_db, &req, (short)size, (const char*)data))
    isc_release_request(st, &req);
  return 0;
}
