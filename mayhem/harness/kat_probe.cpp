// Known-answer probe for mayhem/test.sh: runs fixed SQL through the embedded engine (parser,
// optimizer, execution, DDL, constraints, PSQL) and prints one "<id> <result>" line per check.
#include "fb_embedded.h"
#include <vector>

using namespace fbfx;

static void report_error(const char* id, const ISC_STATUS* st)
{
  printf("%s ERR %ld\n", id, (long) st[1]);
}

static bool ddl(const char* id, const char* sql)
{
  ISC_STATUS_ARRAY st;
  if (isc_dsql_execute_immediate(st, &g_db, &g_tr, 0, sql, 3, NULL)) { report_error(id, st); return false; }
  if (isc_commit_retaining(st, &g_tr)) { report_error(id, st); return false; }
  return true;
}

// Runs one statement and prints every row as "col|col|...", all columns converted to text.
static void query(const char* id, const char* sql)
{
  ISC_STATUS_ARRAY st;
  isc_stmt_handle stmt = 0;
  if (isc_dsql_allocate_statement(st, &g_db, &stmt)) { report_error(id, st); return; }

  XSQLDA* da = (XSQLDA*) calloc(1, XSQLDA_LENGTH(8));
  da->version = SQLDA_VERSION1;
  da->sqln = 8;
  if (isc_dsql_prepare(st, &g_tr, &stmt, 0, sql, 3, da) || da->sqld > da->sqln)
  {
    report_error(id, st);
    free(da);
    return;
  }

  std::vector<std::vector<char> > bufs(da->sqld);
  std::vector<short> inds(da->sqld);
  for (int i = 0; i < da->sqld; i++)
  {
    bufs[i].assign(2 + 128, 0);
    da->sqlvar[i].sqltype = SQL_VARYING + 1;
    da->sqlvar[i].sqllen = 128;
    da->sqlvar[i].sqldata = bufs[i].data();
    da->sqlvar[i].sqlind = &inds[i];
  }

  if (isc_dsql_execute(st, &g_tr, &stmt, 3, NULL)) { report_error(id, st); free(da); return; }

  int rows = 0;
  std::string out;
  while (isc_dsql_fetch(st, &stmt, 3, da) == 0)
  {
    if (rows++) out += ";";
    for (int i = 0; i < da->sqld; i++)
    {
      if (i) out += "|";
      if (inds[i] < 0) { out += "NULL"; continue; }
      unsigned short len = *(unsigned short*) bufs[i].data();
      out.append(bufs[i].data() + 2, len);
    }
  }
  printf("%s %s\n", id, rows ? out.c_str() : "(none)");
  free(da);
  isc_dsql_free_statement(st, &stmt, DSQL_drop);
}

int main()
{
  if (init()) return 1;

  query("arith", "select 6 * 7 from rdb$database");
  query("string", "select upper('firebird') || '-' || substring('abcdef' from 2 for 3) from rdb$database");
  query("date", "select cast(dateadd(1 day to date '2020-02-28') as varchar(10)) from rdb$database");
  query("numeric", "select cast(cast(10 as numeric(10,2)) / 3 as varchar(20)) from rdb$database");
  query("recursive",
        "with recursive n(i) as (select 1 from rdb$database union all select i + 1 from n where i < 5) "
        "select sum(i * i) from n");

  if (!ddl("ddl_table", "create table t (a int not null primary key, b varchar(10), c numeric(9,2) default 1.5)") ||
      !ddl("ddl_proc",
           "create procedure p (x int) returns (y int) as begin y = x * 2; suspend; end") ||
      !ddl("ddl_trigger",
           "create trigger tr for t before insert as begin new.b = upper(new.b); end"))
    return 2;

  ISC_STATUS_ARRAY st;
  const char* ins[] = {"insert into t (a, b) values (1, 'one')", "insert into t (a, b) values (2, 'two')",
                       "insert into t (a, b, c) values (3, 'three', 7.25)"};
  for (const char* s : ins)
    if (isc_dsql_execute_immediate(st, &g_db, &g_tr, 0, s, 3, NULL)) { report_error("insert", st); return 2; }

  query("agg", "select count(*), sum(a), cast(sum(c) as varchar(20)) from t");
  query("trigger", "select b from t order by a");
  query("join", "select t.a, pp.y from t join p(t.a) as pp on 1 = 1 order by t.a desc rows 2");

  if (isc_dsql_execute_immediate(st, &g_db, &g_tr, 0, "insert into t (a, b) values (1, 'dup')", 3, NULL))
    report_error("unique", st);
  else
    printf("unique OK\n");

  query("syntax", "selec 1");
  return 0;
}
