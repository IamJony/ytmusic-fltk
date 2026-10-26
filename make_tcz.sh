#!/bin/sh

set -e

D=$(cd "$(dirname "$0")" && pwd)
APP=ytmusic-fltk
VER=1.0.0
OUT="$D/tcz"
SHARE=usr/local/share/$APP

[ -x "$D/build/$APP" ] || { echo "ERROR: run make first"; exit 1; }
command -v mksquashfs >/dev/null || tce-load -i squashfs-tools >/dev/null

P=$(mktemp -d)
chmod 755 "$P"
install -d "$P/usr/local/bin" \
           "$P/usr/local/share/pixmaps" \
           "$P/usr/local/share/applications"

install -m 755 "$D/build/$APP" "$P/usr/local/bin/$APP"
strip "$P/usr/local/bin/$APP"


[ -f "$D/data/$APP.png" ]      && install -m 644 "$D/data/$APP.png"      "$P/usr/local/share/pixmaps/$APP.png"
[ -f "$D/data/$APP.desktop" ]  && install -m 644 "$D/data/$APP.desktop"  "$P/usr/local/share/applications/$APP.desktop"

find "$P" -type d -exec chmod 755 {} +
mkdir -p "$OUT"

mksquashfs "$P" "$OUT/$APP.tcz" -all-root -noappend -no-progress -b 4096 -no-xattrs >/dev/null
(cd "$P" && find usr -type f | sed 's|^|/|' | sort) > "$OUT/$APP.tcz.list"
rm -rf "$P"

# Dependencias
printf '%s\n' curl.tcz mplayer-cli.tcz ffmpeg4.tcz libwebp1.tcz gcc_libs.tcz > "$OUT/$APP.tcz.dep"

(cd "$OUT" && md5sum $APP.tcz > $APP.tcz.md5.txt)
SIZE=$(du -k "$OUT/$APP.tcz" | cut -f1)

cat > "$OUT/$APP.tcz.info" <<INFO
Title:          $APP.tcz
Description:    YouTube Music client for FLinux in FLTK
Version:        $VER
Author:         IamJony
Original-site:  https://github.com/IamJony
Copying-policy: MIT
Size:           ${SIZE}K
Extension_by:   IamJony
Tags:           music youtube fltk mpv player
Comments:       Reproductor de YouTube Music escrito en FLTK.
                Búsqueda, playlists locales, favoritos, descargas
                locales, cola con shuffle y repeat, temas.
Change-log:     2026/10/23 1.0.0 primera versión
Current:        2026/10/23 1.0.0 primera versión
INFO

echo "OK: $OUT/$APP.tcz (${SIZE}K)"
