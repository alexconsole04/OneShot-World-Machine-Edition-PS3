#!/bin/bash
# usage: flow.sh "<input>" "f1,f2,..." [extra env assignments...]
IN="$1"; FR="$2"; shift 2
# Paths can be overridden: OSPS3_REPO, GAME_DATA (content/ + gamedata/), WORK (screenshots, saves, output).
REPO="${OSPS3_REPO:-$(cd "$(dirname "$0")/../.." && pwd)}"
DATA="${GAME_DATA:-/home/claude/gd3}"
WORK="${WORK:-/tmp/claude-0}"
mkdir -p "$WORK/s" "$WORK/saves"
rm -f "$WORK"/s/*.png
SH=""; i=0
for f in ${FR//,/ }; do SH="$SH$f:$WORK/s/$(printf %02d $i).png,"; i=$((i+1)); done
cd "$DATA" && env OSPS3_SAVEDIR="$WORK/saves" OSPS3_DATA="$DATA" OSPS3_PORT_DIR="$REPO/pkg_files/USRDIR/port" "$@" OSPS3_INPUT="$IN" OSPS3_SHOTS="${SH%,}" timeout ${TIMEOUT:-120} "$REPO/build-host/oneshot-host" > "$WORK/out.txt" 2>&1
echo "rc=$?"
grep -v "missing texture\|slow frame\|audio: opened\|screenshot" "$WORK/out.txt" | tail -${TAILN:-25}
WORK="$WORK" python3 - <<'PY'
from PIL import Image
import glob, os
W0=os.environ['WORK']
fs=sorted(glob.glob(W0+'/s/[0-9]*.png'))
if fs:
    n=len(fs); rows=(n+1)//2
    W=Image.new('RGB',(1280,360*rows))
    for i,f in enumerate(fs): W.paste(Image.open(f).resize((640,360)),((i%2)*640,(i//2)*360))
    W.save(W0+'/s/grid.png')
PY
