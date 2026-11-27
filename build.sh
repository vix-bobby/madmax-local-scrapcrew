#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Build the mod into build/winhttp.dll.
#   ./build.sh trace   also build/trace/winhttp.dll, which logs every WinHTTP call of the game
#   ./build.sh test    build both and run the tests (WSL: runs a Windows program from a
#                      temporary folder)
set -eu
cd "$(dirname "$0")"
CC=x86_64-w64-mingw32-gcc
CFLAGS="-std=gnu11 -O2 -Wall -Wextra -D__USE_MINGW_ANSI_STDIO=1"
mkdir -p build

# build the DLL into $1, with any further arguments for the compiler
build_dll() {
    out=$1
    shift
    mkdir -p "$out"
    $CC $CFLAGS "$@" -shared -static -static-libgcc -s -o "$out/winhttp.dll" \
        src/proxy.c src/trace.c src/redirect.c src/server.c src/responder.c src/winhttp.def \
        -lws2_32
}

build_dll build
case "${1:-}" in
    trace | test) build_dll build/trace -DMOD_TRACE ;;
esac
$CC $CFLAGS -static -o build/harness.exe test/harness.c -lwinhttp -lws2_32

if [ "${1:-}" = test ]; then
    # the replies: native build with memory checks
    gcc -std=gnu11 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover \
        -o build/responder_test test/responder_test.c src/responder.c
    # old AddressSanitizer crashes at random on recent kernels unless ASLR is off
    setarch -R build/responder_test

    wintemp=$(wslpath "$(cd /mnt/c && cmd.exe /c 'echo %TEMP%' 2>/dev/null | tr -d '\r')")
    scratch=$(mktemp -d "$wintemp/madmax-test.XXXXXX")
    trap 'rm -rf "$scratch"' EXIT
    fail() {
        echo "FAIL: $*" >&2
        exit 1
    }
    # run the test program twice, as two launches, with the DLL in folder $1; copy the first
    # launch's log to $2
    run_harness() {
        # a folder name that fits no single code page: the log must still land there
        run="$scratch/$(basename "$1") Игра Łódź"
        mkdir "$run"
        cp "$1/winhttp.dll" build/harness.exe "$run/"
        log="$run/madmax_scrapcrew.log"
        status=0
        (cd "$run" && ./harness.exe) || status=$?
        cp "$log" "$2"
        [ "$status" = 0 ] || exit "$status"
        state="$run/madmax_scrapcrew.dat"
        grep -q '^account=76561190000000001$' "$state" \
            && grep -qE '^scrap_rewarded=20[0-9]{12}$' "$state" \
            || fail "the state file is not as expected"
        [ ! -e "$state.new" ] || fail "a temporary state file was left"
        (cd "$run" && ./harness.exe again)
        grep -qF 'saved state read: profile yes' "$log" || fail "state not read back"
    }
    # every further argument must appear in the log $1
    expect() {
        file=$1
        shift
        for expected; do
            grep -qF -- "$expected" "$file" || fail "not in $file: $expected"
        done
    }
    # nothing in the log $1 may match $2
    forbid() {
        if grep -E "$2" "$1"; then
            fail "$1 holds something it must not"
        fi
    }
    private='SECRET|ping|5ec25ec1|76561190000000001|option 4097 .* value|cannot turn the proxy off'

    echo "-- normal build"
    run_harness build build/test.log
    expect build/test.log 'listener on 127.0.0.1 port' \
        'redirecting blackjack.hydra.agoragames.com to the listener' \
        'redirecting cls.psn.turbine.com to the listener' \
        'listener: POST /984815842354416438/auth/pc_steam' 'reward time recorded' \
        'listener: POST /1/nothing/here, '
    forbid build/test.log "$private"
    # no tracing: no calls, callbacks or headers
    forbid build/test.log 'WinHttp[A-Za-z]+[( ]|callback |x-ag-ticket'
    [ "$(grep -c 'redirecting ' build/test.log)" = 2 ] || fail "a redirect was reported twice"

    echo "-- trace build"
    run_harness build/trace build/test-trace.log
    expect build/test-trace.log '(trace build)' 'WinHttpOpen agent "harness"' 'from harness.exe' \
        'from system winhttp.dll' 'option 6 length 4 value 0x1388' '(ASYNC)' 'x-ag-ticket: 0801' \
        'Authorization: ...' 'Set-Cookie' 'SENDREQUEST_COMPLETE' 'READ_COMPLETE' 'REQUEST_ERROR' \
        'HANDLE_CLOSING' 'X-Hydra-Error-Code' 'value 200' 'redirected to the listener' \
        'listener: POST /984815842354416438/auth/pc_steam' 'reward time recorded'
    forbid build/test-trace.log "$private"
    echo "log checks passed (logs in build/test.log and build/test-trace.log)"
fi
