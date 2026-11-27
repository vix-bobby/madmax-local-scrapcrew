#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Build the mod into build/winhttp.dll. `./build.sh test` also runs the tests (WSL: runs a
# Windows test program from a temporary folder).
set -eu
cd "$(dirname "$0")"
CC=x86_64-w64-mingw32-gcc
CFLAGS="-std=gnu11 -O2 -Wall -Wextra"
mkdir -p build

$CC $CFLAGS -shared -static -static-libgcc -s -o build/winhttp.dll \
    src/proxy.c src/trace.c src/winhttp.def
$CC $CFLAGS -static -o build/harness.exe test/harness.c -lwinhttp -lws2_32

if [ "${1:-}" = test ]; then
    wintemp=$(wslpath "$(cd /mnt/c && cmd.exe /c 'echo %TEMP%' 2>/dev/null | tr -d '\r')")
    scratch=$(mktemp -d "$wintemp/madmax-test.XXXXXX")
    trap 'rm -rf "$scratch"' EXIT
    # a folder name that fits no single code page: the log must still land there
    run="$scratch/Игра Łódź"
    mkdir "$run"
    cp build/winhttp.dll build/harness.exe "$run/"
    status=0
    (cd "$run" && ./harness.exe) || status=$?
    log="$run/madmax_scrapcrew.log"
    cp "$log" build/test.log
    [ "$status" = 0 ] || exit "$status"
    for expected in 'WinHttpOpen agent "harness"' 'from harness.exe' 'from system winhttp.dll' 'option 6 length 4 value 0x1388' '(ASYNC)' 'x-ag-ticket: 0801' \
        'Authorization: ...' 'Set-Cookie' 'SENDREQUEST_COMPLETE' 'READ_COMPLETE' 'REQUEST_ERROR' \
        'HANDLE_CLOSING' 'X-Hydra-Error-Code' 'value 200'; do
        grep -qF -- "$expected" build/test.log || { echo "FAIL: not in the log: $expected" >&2; exit 1; }
    done
    if grep -E 'SECRET|ping|5ec25ec1|option 4097 .* value' build/test.log; then
        echo "FAIL: the log holds something it must not" >&2
        exit 1
    fi
    echo "log checks passed (copy in build/test.log)"
fi
