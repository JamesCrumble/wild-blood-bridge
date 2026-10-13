#!/bin/sh
# Copies the player's game files to the phone: the game's APK from gamebin/ and the unpacked cache
# from wbdata/files, then opens the folders adb created to the app (they belong to the shell user).
# The OBB next to the unpacked files is the same data zipped and is not copied.
# About 1.7 GB. Usage: tools/push_data.sh
P=${WB_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}
A=$P/tools/adb
D=/sdcard/Android/data/com.jamescrumble.wildblood/files
G=com.gameloft.android.ANMP.GloftWBHM
APK=$(ls "$P"/gamebin/*.apk 2>/dev/null | head -1)
[ -n "$APK" ] || { echo "no game APK in gamebin/"; exit 1; }
[ -f "$P/wbdata/files/AOWGame/Audio/sound.config" ] || { echo "no unpacked cache in wbdata/files"; exit 1; }
$A shell mkdir -p "$D/$G/files" || exit 1
$A push "$(wslpath -w "$APK")" "$D/wb-1.1.5.apk" || exit 1
F=$(readlink -f "$P/wbdata/files")
for f in "$F"/*; do
  case "$f" in *.obb) continue ;; esac
  $A push "$(wslpath -w "$f")" "$D/$G/files/" || exit 1
done
# The app's own folder too: made here before the app is installed, it belongs to the shell user as well.
$A shell "chmod 2777 ${D%/files} $D; find $D/$G -type d -exec chmod 2777 {} +"
$A shell "du -sh $D"
