#!/bin/sh
# Velvet Chords uninstaller. Run on the device as root:  sh uninstall.sh [-y]
# Removes the plugin and its MPC.settings entry (after a backup). Your exports and song files are kept.
set -e
cd "$(dirname "$0")"
NAME='Velvet Chords'; SO_DIR='/sdcard/vst'; SO='velvetchords.so'; SKIN='Velvet Chords - VST - Velvet Chords'
YES=0; [ "$1" = "-y" ] && YES=1
die() { echo "error: $*" >&2; exit 1; }

[ "$(id -u)" = 0 ] || die "run as root"
SETTINGS=$(ls /media/az01-internal/Settings/*/MPC.settings 2>/dev/null | head -n 1)
[ -n "$SETTINGS" ] || die "MPC.settings not found"
if [ $YES = 0 ]; then
    printf "Remove %s? MPC will be stopped and restarted. Save your project first. [y/N] " "$NAME"
    read -r ok; case "$ok" in y|Y|yes) ;; *) echo "cancelled"; exit 1 ;; esac
fi

systemctl stop acvs
trap 'systemctl start acvs' EXIT
i=0; while pidof MPC >/dev/null && [ $i -lt 30 ]; do sleep 1; i=$((i + 1)); done
pidof MPC >/dev/null && die "MPC did not stop"

BAK="$SETTINGS.bak-velvetchords-$(date +%Y%m%d-%H%M%S)"
cp "$SETTINGS" "$BAK"
awk -v mode=remove -v file="$SO_DIR/$SO" -f plugin_list.awk "$SETTINGS" > "$SETTINGS.new"
n=$(grep -c "file=\"$SO_DIR/$SO\"" "$SETTINGS.new" || true)
[ "$n" = 0 ] || { rm -f "$SETTINGS.new"; die "settings edit failed; MPC.settings unchanged"; }
if command -v python3 >/dev/null; then
    python3 -c 'import sys, xml.etree.ElementTree as E; E.parse(sys.argv[1])' "$SETTINGS.new" 2>/dev/null ||
        { rm -f "$SETTINGS.new"; die "edited settings aren't valid XML; MPC.settings unchanged"; }
fi
mv "$SETTINGS.new" "$SETTINGS"
rm -f "$SO_DIR/$SO"
rm -rf "/sdcard/Synths/$SKIN"
sync
echo "Removed $NAME. Settings backup: $BAK"
echo "Song files and exports were kept in '/media/az01-internal/Velvet Chords' (or '/sdcard/Velvet Chords'); delete them by hand if you like."
