#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT
compiler="${CXX:-c++}"
flags=(-std=c++11 -Wall -Wextra -Werror -pedantic -Itests/include -I.)
"$compiler" "${flags[@]}" -x c++ VRA-meter.ino vra.cpp tests/firmware_test.cpp -o "$build_dir/firmware"
"$compiler" "${flags[@]}" -D__AVR_ATmega328P__ -DF_CPU=16000000UL ads1115.cpp tests/ads1115_test.cpp -o "$build_dir/driver"
"$build_dir/firmware"
"$build_dir/driver"
