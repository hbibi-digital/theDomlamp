#!/bin/sh
# Builds and runs the host test of the Domlamp core. Needs only a C++ compiler; no WLED, no hardware.
# The address and undefined-behaviour sanitizers are on: the parsers read text from the network.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
out=${TMPDIR:-/tmp}/domlamp-fw-test
mkdir -p "$out"
${CXX:-clang++} -std=c++17 -Wall -Wextra -Wshadow -Werror -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined \
  "$here/test_core.cpp" "$here/../usermod_domlamp/domlamp_core.cpp" -o "$out/test_core"
"$out/test_core" "$here"
