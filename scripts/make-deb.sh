#!/bin/sh
# Build a .deb of TCIdigi for the Ubuntu this script is running on.
# fldigi's engine is compiled first, with its data path set to
# /usr/share/tcidigi, then the Qt shell is linked without an rpath.
#
# Writes dist/tcidigi_<version>-1_<arch>.deb
set -eu
cd "$(dirname "$0")/.."

VERSION=$(sed -n 's/^#define TCIDIGI_VERSION "\([^"]*\)".*/\1/p' src/include/tcidigi_version.h)
ARCH=$(dpkg --print-architecture)
[ -n "$VERSION" ] || { echo "Could not read TCIDIGI_VERSION" >&2; exit 1; }

autoreconf -fi
./configure
make -C src -j"$(nproc)" pkgdatadir=/usr/share/tcidigi

cmake -S src/qtui -B build-qt -DCMAKE_BUILD_TYPE=Release -DCMAKE_SKIP_RPATH=ON
cmake --build build-qt --target fldigi-qt -j"$(nproc)"

BIN="build-qt/TCIdigi-$VERSION"
[ -x "$BIN" ] || { echo "Missing $BIN" >&2; exit 1; }

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/DEBIAN" \
    "$STAGE/usr/bin" \
    "$STAGE/usr/share/applications" \
    "$STAGE/usr/share/icons/hicolor/1024x1024/apps" \
    "$STAGE/usr/share/doc/tcidigi" \
    "$STAGE/usr/share/tcidigi/kml"

install -m 0755 "$BIN" "$STAGE/usr/bin/TCIdigi"
strip --strip-unneeded "$STAGE/usr/bin/TCIdigi"
install -m 0644 src/qtui/icons/TCIdigi.png \
    "$STAGE/usr/share/icons/hicolor/1024x1024/apps/tcidigi.png"
install -m 0644 \
    data/NAVTEX_Stations.csv \
    data/nsd_bbsss.txt \
    data/station_table.txt \
    data/ToR-Stats-SHIP.csv \
    data/wmo_list.txt \
    "$STAGE/usr/share/tcidigi/"
install -m 0644 src/kml/styles.kml "$STAGE/usr/share/tcidigi/kml/styles.kml"

cat > "$STAGE/usr/share/applications/tcidigi.desktop" << 'EOF'
[Desktop Entry]
Type=Application
Name=TCIdigi
GenericName=Digital modes over TCI
Comment=fldigi modems with a Qt interface, using TCI for the radio and the audio
Exec=TCIdigi
Icon=tcidigi
Terminal=false
Categories=HamRadio;Network;
Keywords=fldigi;tci;psk;rtty;radio;
EOF

cat > "$STAGE/usr/share/doc/tcidigi/copyright" << 'EOF'
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: TCIdigi
Upstream-Contact: KC2RGW Chris <kc2rgw@gmail.com>
Source: https://sourceforge.net/projects/tcidigi/

Files: *
Copyright: 2026 KC2RGW Chris Esser
License: GPL-3+
 On Debian systems, the full text of the GNU General Public License
 version 3 can be found in /usr/share/common-licenses/GPL-3.
EOF

cat > "$STAGE/usr/share/doc/tcidigi/README.Debian" << 'EOF'
TCIdigi keeps its settings in ~/.tcidigi. It does not read ~/.fldigi.

The radio and the audio go over TCI. This package does not use hamlib,
flrig, or a sound card. Point it at a TCI server, such as 590Ctrl on
127.0.0.1:50001.

Station tables for NAVTEX and weather modes are in /usr/share/tcidigi.
EOF

gzip -n -9 -c > "$STAGE/usr/share/doc/tcidigi/changelog.Debian.gz" << EOF
tcidigi (${VERSION}-1) resolute; urgency=medium

  * Binary package of TCIdigi ${VERSION}.

 -- KC2RGW Chris <kc2rgw@gmail.com>  $(date -R)
EOF

find "$STAGE" -type d -exec chmod 0755 {} +
find "$STAGE" -type f -exec chmod a-x,go-w {} +
chmod 0755 "$STAGE/usr/bin/TCIdigi"

SHLIB=$(mktemp -d)
mkdir -p "$SHLIB/debian"
printf 'Source: tcidigi\n\nPackage: tcidigi\nArchitecture: %s\n' "$ARCH" > "$SHLIB/debian/control"
DEPS=$(cd "$SHLIB" && dpkg-shlibdeps -O "$STAGE/usr/bin/TCIdigi" | sed -n 's/^shlibs:Depends=//p')
rm -rf "$SHLIB"
DEPS="$DEPS, libqt6waylandclient6, libxcb-cursor0"

SIZE=$(du -sk "$STAGE/usr" | awk '{print $1}')
cat > "$STAGE/DEBIAN/control" << EOF
Package: tcidigi
Version: ${VERSION}-1
Section: hamradio
Priority: optional
Architecture: ${ARCH}
Maintainer: KC2RGW Chris <kc2rgw@gmail.com>
Homepage: https://sourceforge.net/projects/tcidigi/
Installed-Size: ${SIZE}
Depends: ${DEPS}
Recommends: qt6-gtk-platformtheme, qt6-qpa-plugins
Description: Digital modem program with a Qt interface over TCI
 Qt 6 front end on fldigi's modems. The radio and the audio use TCI,
 the protocol used by 590Ctrl, ZeusSDR and ExpertSDR3. This build does
 not include hamlib, flrig, or the sound-card backends.
 .
 Built against this Ubuntu release's Qt 6 and FLTK. apt installs the
 libraries. Settings are kept in ~/.tcidigi.
EOF

mkdir -p dist
OUT="dist/tcidigi_${VERSION}-1_${ARCH}.deb"
rm -f "$OUT"
dpkg-deb --root-owner-group -Zxz --build "$STAGE" "$OUT"
echo "Wrote $OUT"
sha256sum "$OUT"
