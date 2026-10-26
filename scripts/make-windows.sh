#!/bin/bash
# Build a portable TCIdigi for 64-bit Windows in an MSYS2 UCRT64 shell:
# the program, its Qt plugins and every DLL it loads, in one folder, zipped.
#
# Writes dist/TCIdigi-<version>-windows-x64.zip
set -euo pipefail
cd "$(dirname "$0")/.."

VERSION=$(sed -n 's/^#define TCIDIGI_VERSION "\([^"]*\)".*/\1/p' src/include/tcidigi_version.h)
[ -n "$VERSION" ] || { echo "Could not read TCIDIGI_VERSION" >&2; exit 1; }
JOBS=$(nproc)

autoreconf -fi
# timeBeginPeriod and timeEndPeriod (the modems' timers) are in winmm.
./configure LIBS="-lwinmm"
make -C src -j"$JOBS"
cmake -S src/qtui -B build-qt -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-qt --target fldigi-qt -j"$JOBS"

EXE=$(find build-qt -maxdepth 1 -name "TCIdigi*.exe" | head -1)
[ -n "$EXE" ] || { echo "Missing TCIdigi .exe in build-qt" >&2; exit 1; }

OUTDIR="dist/TCIdigi-$VERSION"
rm -rf "$OUTDIR"
mkdir -p "$OUTDIR"
cp "$EXE" "$OUTDIR/TCIdigi.exe"
strip "$OUTDIR/TCIdigi.exe"
windeployqt6 --release --no-translations --no-system-d3d-compiler --no-opengl-sw \
	"$OUTDIR/TCIdigi.exe"

# Every MinGW DLL the program and its plugins load, from the UCRT64 tree.
copy_deps() {
	ntldd -R "$1" 2>/dev/null | awk '{print $3}' | tr '\\' '/' \
		| grep -i "/ucrt64/bin/" | while read -r dll; do
		dll=$(cygpath -u "$dll")
		[ -f "$OUTDIR/$(basename "$dll")" ] || cp "$dll" "$OUTDIR/"
	done
}
copy_deps "$OUTDIR/TCIdigi.exe"
find "$OUTDIR" -name "*.dll" | while read -r f; do copy_deps "$f"; done

mkdir -p "$OUTDIR/share"
cp data/NAVTEX_Stations.csv data/nsd_bbsss.txt data/station_table.txt \
	data/ToR-Stats-SHIP.csv data/wmo_list.txt "$OUTDIR/share/" 2>/dev/null || true
cp COPYING "$OUTDIR/COPYING.txt"
cat > "$OUTDIR/README.txt" <<TXT
TCIdigi $VERSION for Windows (64-bit)

Run TCIdigi.exe. Nothing needs installing: keep this folder together.
Settings are kept in your user folder, under .tcidigi.
The radio and the audio go over TCI (Configure, TCI).
TXT

OUT="dist/TCIdigi-$VERSION-windows-x64.zip"
rm -f "$OUT"
(cd dist && zip -qr "TCIdigi-$VERSION-windows-x64.zip" "TCIdigi-$VERSION")
echo "Wrote $OUT"
sha256sum "$OUT"
