#!/bin/sh
# Build a macOS icon from the TCIdigi PNG. Usage: make-icns.sh source.png dest.icns
set -e
png=$1
dest=$2
iconset=$(mktemp -d)/TCIdigi.iconset
mkdir -p "$iconset" "$(dirname "$dest")"
for spec in \
	"16 icon_16x16.png" \
	"32 icon_16x16@2x.png" \
	"32 icon_32x32.png" \
	"64 icon_32x32@2x.png" \
	"128 icon_128x128.png" \
	"256 icon_128x128@2x.png" \
	"256 icon_256x256.png" \
	"512 icon_256x256@2x.png" \
	"512 icon_512x512.png" \
	"1024 icon_512x512@2x.png"
do
	size=${spec%% *}
	name=${spec#* }
	sips -z "$size" "$size" "$png" --out "$iconset/$name" >/dev/null
done
iconutil -c icns -o "$dest" "$iconset"
rm -rf "$(dirname "$iconset")"
