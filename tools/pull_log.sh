#!/bin/sh
# Copies the phone's game log to logs/phone.log
P=${WB_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}
"$P/tools/adb" shell cat /sdcard/Android/data/com.jamescrumble.wildblood/files/wb.log | tr -d '\r' > "$P/logs/phone.log"
wc -l "$P/logs/phone.log"
