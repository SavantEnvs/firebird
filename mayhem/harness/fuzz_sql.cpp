// Feeds fuzzer bytes to the embedded engine's SQL front end: lexer + parser, DSQL node
// construction, BLR generation and request compilation (isc_dsql_prepare). Nothing is executed.
#include "fb_embedded.h"

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
  fbfx::init_or_exit();
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
  if (size == 0 || size > 0x7fff) return -1;

  // The lexer peeks one byte past the end (Parser.cpp, quoted-string scan), so the statement
  // text is handed over NUL-terminated as the API's callers do; the length excludes the NUL.
  std::string sql((const char*)data, size);

  ISC_STATUS_ARRAY st;
  isc_stmt_handle stmt = 0;
  if (isc_dsql_allocate_statement(st, &fbfx::g_db, &stmt)) return -1;

  XSQLDA* da = (XSQLDA*)calloc(1, XSQLDA_LENGTH(8));
  da->version = SQLDA_VERSION1;
  da->sqln = 8;

  // A rejected statement is a normal outcome and still exercises the parser, so it returns 0:
  // libFuzzer would discard the coverage of every syntax-error path if it returned -1.
  isc_dsql_prepare(st, &fbfx::g_tr, &stmt, (unsigned short)size, sql.c_str(), 3, da);

  free(da);
  isc_dsql_free_statement(st, &stmt, DSQL_drop);
  return 0;
}
