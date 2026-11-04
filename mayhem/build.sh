#!/usr/bin/env bash
#
# mayhem/build.sh — build CLK's media-layer fuzz harnesses, their -standalone reproducers and the
# known-answer test program. Runs in the commit image as `mayhem` in /mayhem; offline and re-runnable.
#
# CLK is a whole-machine emulator; what is fuzzable is its media layer (Analyser::Static, Storage/*).
# mayhem/CMakeLists.txt builds just that layer plus the CPU/reflection code it links against, as a
# static library, from the sources listed in upstream's cmake/CLK_SOURCES.cmake.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"

# CLK's Reflection::StructImpl downcasts `this` to the derived type from a base-class constructor
# (a CRTP idiom); UBSan's `vptr` check rejects that on every machine's state struct, which would
# abort each input before any parsing. That is the only check relaxed; ASan and the rest of UBSan halt.
RELAX="-fno-sanitize=vptr"
COMMON_CXX="-std=c++20 -O1"

# 1) The media-layer library, sanitized + DWARF 3 + libFuzzer coverage instrumentation.
cmake -S mayhem -B build/fuzz -G Ninja \
	-DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" -DCMAKE_BUILD_TYPE=None \
	-DCMAKE_C_FLAGS="-O1 $SANITIZER_FLAGS $DEBUG_FLAGS $RELAX -fsanitize=fuzzer-no-link" \
	-DCMAKE_CXX_FLAGS="-O1 $SANITIZER_FLAGS $DEBUG_FLAGS $RELAX -fsanitize=fuzzer-no-link"
cmake --build build/fuzz -j"$MAYHEM_JOBS"

# 2) Leak detection off at build time, and the run-once driver as a C object.
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.cc -o build/lsan_off.o
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o build/standalone_main.o

# 3) One harness source, three targets (each takes a different extension set), each in a fuzzer
#    and a -standalone flavour.
for class in disk tape other; do
	upper=$(echo "$class" | tr a-z A-Z)
	$CXX $COMMON_CXX $SANITIZER_FLAGS $DEBUG_FLAGS $RELAX -DCLASS_$upper -I"$SRC" \
		$LIB_FUZZING_ENGINE mayhem/fuzz_media.cpp build/lsan_off.o build/fuzz/libclk_media.a -lz \
		-o "/mayhem/fuzz_$class"
	$CXX $COMMON_CXX $SANITIZER_FLAGS $DEBUG_FLAGS $RELAX -DCLASS_$upper -I"$SRC" \
		mayhem/fuzz_media.cpp build/standalone_main.o build/lsan_off.o build/fuzz/libclk_media.a -lz \
		-o "/mayhem/fuzz_$class-standalone"
done

# 4) The known-answer test, built from a separate library with the project's normal flags.
cmake -S mayhem -B build/plain -G Ninja \
	-DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" -DCMAKE_BUILD_TYPE=None \
	-DCMAKE_C_FLAGS="-O1 $COVERAGE_FLAGS" -DCMAKE_CXX_FLAGS="-O1 $COVERAGE_FLAGS"
cmake --build build/plain -j"$MAYHEM_JOBS"
$CXX $COMMON_CXX $COVERAGE_FLAGS -I"$SRC" mayhem/kat.cpp build/plain/libclk_media.a -lz -o build/kat
