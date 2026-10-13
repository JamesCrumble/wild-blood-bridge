#!/bin/sh
# syms.sh <substr>... : game library symbols containing any substring (name, value, size), TinyXML filtered out.
P=${WB_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}
python3 -I $P/tools/elfsym.py $P/wbdata/lib/armeabi-v7a/libWB_neon.so "$@" \
  | grep -v TiXml | awk '{print $NF, $2, $3}' | sort -u | head -${N:-80}
