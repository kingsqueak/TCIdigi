#!/bin/bash
# Build TCIdigi-<version>.dmg on macOS (Apple silicon) from Homebrew's
# libraries and Qt. Run it on a macOS 26 system: Homebrew bottles carry the
# minimum macOS of the system they were built for, and the DMG must run on
# macOS 26.0. On macOS 27 Homebrew's glib needs the rebuilt copy described
# in README.md; this script stops if the audit finds a newer symbol.
#
# Writes dist/TCIdigi-<version>.dmg
set -euo pipefail
cd "$(dirname "$0")/.."

VERSION=$(sed -n 's/^#define TCIDIGI_VERSION "\([^"]*\)".*/\1/p' src/include/tcidigi_version.h)
[ -n "$VERSION" ] || { echo "Could not read TCIDIGI_VERSION" >&2; exit 1; }
BREW=$(brew --prefix)
QT=$(brew --prefix qt)
JOBS=$(sysctl -n hw.ncpu)

# The engine (fldigi's autotools build), then the Qt shell.
autoreconf -fi
./configure LDFLAGS="-L$BREW/opt/libpng/lib"
make -C src -j"$JOBS"
cmake -S src/qtui -B build-qt -DCMAKE_PREFIX_PATH="$QT" -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0
cmake --build build-qt --target fldigi-qt -j"$JOBS"

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
cmake --install build-qt --prefix "$STAGE/install" >/dev/null
APP="$STAGE/install/TCIdigi-$VERSION.app"
C="$APP/Contents"
[ -d "$APP" ] || { echo "Missing $APP" >&2; exit 1; }
chmod -R u+w "$APP"

# SVG drawing for the Qt stylesheet's arrows: macdeployqt misses it.
mkdir -p "$C/PlugIns/imageformats"
cp "$QT/share/qt/plugins/imageformats/libqsvg.dylib" "$C/PlugIns/imageformats/"
chmod u+w "$C/PlugIns/imageformats/libqsvg.dylib"
"$QT/bin/macdeployqt" "$APP" -libpath="$BREW/lib" \
	-executable="$C/PlugIns/imageformats/libqsvg.dylib"

# What nothing loads.
rm -f "$C/PlugIns/imageformats/libqpdf.dylib"
rm -rf "$C/PlugIns/platforminputcontexts"
for f in QtOpenGL QtPdf QtQml QtQmlMeta QtQmlModels QtQmlWorkerScript QtQuick \
	QtVirtualKeyboard QtVirtualKeyboardQml; do
	rm -rf "$C/Frameworks/$f.framework"
done
printf '[Paths]\nPlugins = PlugIns\nImports = Resources/qml\nQmlImports = Resources/qml\n' \
	> "$C/Resources/qt.conf"
# macdeployqt leaves these install names pointing at Homebrew.
for f in QtDBus QtSvg; do
	if [ -f "$C/Frameworks/$f.framework/Versions/A/$f" ]; then
		install_name_tool -id "@executable_path/../Frameworks/$f.framework/Versions/A/$f" \
			"$C/Frameworks/$f.framework/Versions/A/$f" 2>/dev/null
	fi
done

# No library may still load from Homebrew, and none may need a newer macOS.
if find "$C" -type f \( -name "*.dylib" -o -perm -u+x \) -print0 \
	| xargs -0 otool -L 2>/dev/null | grep -v ":$" | grep -q "$BREW"; then
	echo "A library in the bundle still loads from $BREW:" >&2
	find "$C" -type f \( -name "*.dylib" -o -perm -u+x \) -print0 \
		| xargs -0 otool -L 2>/dev/null | grep -B1 "$BREW" >&2
	exit 1
fi
python3 scripts/mac-audit-bundle.py "$APP" --sdk "$(xcrun --show-sdk-path)"

codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP"

mkdir -p dist
DMGDIR="$STAGE/dmg"
mkdir -p "$DMGDIR"
cp -R "$APP" "$DMGDIR/"
ln -s /Applications "$DMGDIR/Applications"
cp "data/mac/Install Notes.txt" "$DMGDIR/"
OUT="dist/TCIdigi-$VERSION.dmg"
rm -f "$OUT"
hdiutil create -volname "TCIdigi $VERSION" -srcfolder "$DMGDIR" -ov -format UDZO "$OUT"
echo "Wrote $OUT"
shasum -a 256 "$OUT"
