#!/usr/bin/env bash
#
# mayhem/build.sh — builds Firebird (sanitized + fuzzer-instrumented, in place) and the harnesses.
# Runs in the commit image as `mayhem` in /mayhem (ENV contract from the org base image).
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${STANDALONE_FUZZ_MAIN:=/opt/mayhem/StandaloneFuzzTargetMain.c}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

# Build-time helper tools (gpre, build_file, ...) run under ASan: no leak reports from them.
export ASAN_OPTIONS=detect_leaks=0

cd "$SRC"
H="$SRC/mayhem/harness"
FBROOT="$SRC/gen/Release/firebird"   # baked into the engine as its root (configure --prefix)

# Relaxed UBSan checks (benign, flood on every statement): nonnull-attribute (memcpy/memcmp with a
# null pointer and length 0 in Firebird's Array/string templates) and alignment (packed message
# buffers read through typed pointers). Everything else in ASan+UBSan stays halting.
# mayhem/ubsan.ignorelist exempts the one `null` site that fires on every statement (jrd/scl.cpp).
# -fsanitize=fuzzer-no-link gives the engine libraries libFuzzer coverage counters.
PROJECT_FLAGS="$SANITIZER_FLAGS -fno-sanitize=nonnull-attribute,alignment -fsanitize-ignorelist=$SRC/mayhem/ubsan.ignorelist $DEBUG_FLAGS -fsanitize=fuzzer-no-link"

# 1) Firebird itself. autogen.sh regenerates the (gitignored) configure script; the build is
#    idempotent on a re-run (configure/make are no-ops when up to date). LINK_EMPTY_SYMBOLS= drops
#    the export version script from executables: with it, sanitizer runtime symbols referenced by
#    the instrumented libraries cannot be resolved when linking the exes.
if [ ! -x configure ]; then NOCONFIGURE=1 sh autogen.sh; fi
if [ ! -f gen/Makefile ]; then
  ./configure CC="$CC" CXX="$CXX" CFLAGS="$PROJECT_FLAGS" CXXFLAGS="$PROJECT_FLAGS" \
      LDFLAGS="$SANITIZER_FLAGS" --prefix="$FBROOT" \
      --with-builtin-tommath --with-builtin-tomcrypt
fi
# The same sequence as the top-level `make` (gen/Makefile master_process), minus the utilities,
# plugins and examples the harnesses never load.
mkdir -p "$SRC/src/include/gen"
ln -sf "$SRC/gen/autoconfig.auto" "$SRC/src/include/gen/autoconfig.h"
# cloop's cmake cache from a previous run flips its test build on and fails; regenerating it is cheap and rebuilds nothing downstream.
rm -rf "$SRC/gen/Release/cloop"
for t in updateBuildNum export_lists external rest yvalve boot engine fbintl; do
  make -j"$MAYHEM_JOBS" TARGET=Release LINK_EMPTY_SYMBOLS= "$t"
done

# 2) Harnesses: libFuzzer binary + run-once -standalone reproducer per target. They link the
#    sanitized libfbclient (which loads the in-process engine plugin); -rdynamic exports the
#    sanitizer-coverage symbols to that dlopen()ed plugin.
INC="-I$SRC/src/include -I$SRC/src/include/firebird -I$FBROOT/include/firebird"
LIBS="-L$FBROOT/lib -Wl,-rpath,$FBROOT/lib -lfbclient -ltommath -ltomcrypt -rdynamic"
HFLAGS="$SANITIZER_FLAGS -fno-sanitize=nonnull-attribute,alignment $DEBUG_FLAGS"

$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o /tmp/standalone_main.o
$CXX $SANITIZER_FLAGS -c "$H/lsan_off.cc" -o /tmp/lsan_off.o

for t in fuzz_sql fuzz_blr fuzz_pb; do
  $CXX $HFLAGS $LIB_FUZZING_ENGINE $INC "$H/$t.cpp" /tmp/lsan_off.o $LIBS -o "$SRC/$t"
  $CXX $HFLAGS $INC "$H/$t.cpp" /tmp/standalone_main.o /tmp/lsan_off.o $LIBS -o "$SRC/$t-standalone"
done

# 3) Known-answer probe for mayhem/test.sh: drives the same instrumented engine with fixed SQL.
$CXX $HFLAGS $INC "$H/kat_probe.cpp" /tmp/lsan_off.o $LIBS -o "$SRC/kat_probe"
