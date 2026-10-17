#!/bin/sh
# Package dist/VelvetChords-<version>.zip from build/arm (run via `make release`).
set -e
VER="$1"
D="dist/VelvetChords-$VER"
[ -f build/arm/velvetchords.so ] || { echo "build/arm/velvetchords.so missing: run make arm"; exit 1; }
rm -rf "$D" && mkdir -p "$D/payload/vst" "$D/payload/data" "$D/payload/Synths"
# MPC skin layout (as used by working skins on MPC OS):
#   <Maker> - VST - <Name>/version.xml   and   <Maker> - VST - <Name>/Plugin Skins/{TUI.json, Q-Links*.json, *.png}
SKIN="Velvet Chords - VST - Velvet Chords"
mkdir -p "$D/payload/Synths/$SKIN/Plugin Skins"
cp -r "package/skin/$SKIN/." "$D/payload/Synths/$SKIN/Plugin Skins/"
printf "<?xml version='1.0' encoding='utf-8'?>\n<plugincontent version=\"1.0\">\n\t<identifier>velvetchords.vst.velvetchords</identifier>\n\t<version>1.0.0.0</version>\n</plugincontent>\n" > "$D/payload/Synths/$SKIN/version.xml"
cp build/arm/velvetchords.so "$D/payload/vst/"
./build/native/vc_export "$D/payload/data" >/dev/null
for f in install.sh uninstall.sh; do sed "s/@VERSION@/$VER/" "package/$f" > "$D/$f"; chmod +x "$D/$f"; done
cp package/plugin.xml package/plugin_list.awk docs/INSTALL.md "$D/"
(cd "$D" && find payload plugin.xml plugin_list.awk -type f -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS)
(cd dist && rm -f "VelvetChords-$VER.zip" && zip -qr "VelvetChords-$VER.zip" "VelvetChords-$VER")
echo "dist/VelvetChords-$VER.zip"
