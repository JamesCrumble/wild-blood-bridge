#!/bin/sh
# Copies the phone's game log to logs/phone.log
P=${WB_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}
A="$P/tools/adb"
D=/sdcard/BridgeGames/com.jamescrumble.wildblood
[ -n "$($A shell ls $D/wb.log 2>/dev/null)" ] || D=/sdcard/Android/data/com.jamescrumble.wildblood/files
$A shell cat $D/wb.log | tr -d '\r' > "$P/logs/phone.log"
wc -l "$P/logs/phone.log"
