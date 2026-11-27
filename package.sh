#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Make the release zip in dist/ from build/winhttp.dll. Run ./build.sh test first.
set -eu
cd "$(dirname "$0")"
dll=build/winhttp.dll

fail() {
    echo "$*" >&2
    exit 1
}
version=$(sed -n 's/^#define MOD_VERSION "\(.*\)"$/\1/p' src/proxy.h)
[ -n "$version" ] && [ -f "$dll" ] || fail "build first: ./build.sh test"
[ -z "$(find src build.sh -newer "$dll")" ] || fail "$dll is older than the sources: build again"
grep -qa "madmax scrap crew mod $version" "$dll" || fail "$dll is not version $version"
! grep -qa "(trace build)" "$dll" || fail "$dll is a trace build"
grep -q "local Scrap Crew $version\$" release/README.txt || fail "release/README.txt: not $version"
grep -qF "[b]$version[/b]" release/nexus_description.bbcode \
    || fail "release/nexus_description.bbcode: no changelog entry for $version"
! grep -n '<SOURCE URL>' release/* || fail "the source address is missing"
[ -z "$(git status --porcelain)" ] \
    || fail "uncommitted changes: commit first, so that the release matches published source"

mkdir -p dist
python3 -I - "$dll" "$version" <<'PY'
import sys, zipfile
from pathlib import Path

dll, version = Path(sys.argv[1]), sys.argv[2]
out = Path("dist") / f"MadMaxLocalScrapCrew-{version}.zip"
# prefixed names: the archive is often unpacked straight into the game folder
texts = {"madmax_scrapcrew-README.txt": Path("release/README.txt"),
         "madmax_scrapcrew-LICENSE.txt": Path("LICENSE")}
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    z.write(dll, "winhttp.dll")
    for name, path in texts.items():  # Windows line endings for Notepad
        z.writestr(name, path.read_text(encoding="utf-8").replace("\n", "\r\n"))
    print(out.resolve())
    for info in z.infolist():
        print(f"  {info.file_size:7}  {info.filename}")
PY
