#!/bin/bash
# usage: flow.sh "<input>" "f1,f2,..." [extra env assignments...]
IN="$1"; FR="$2"; shift 2
rm -f /tmp/claude-0/s/*.png
SH=""; i=0
for f in ${FR//,/ }; do SH="$SH$f:/tmp/claude-0/s/$(printf %02d $i).png,"; i=$((i+1)); done
cd /home/claude/gd3 && env OSPS3_SAVEDIR=/tmp/claude-0/saves OSPS3_DATA=/home/claude/gd3 "$@" OSPS3_INPUT="$IN" OSPS3_SHOTS="${SH%,}" timeout ${TIMEOUT:-120} /home/claude/osps3/build-host/oneshot-host > /tmp/claude-0/out.txt 2>&1
echo "rc=$?"
grep -v "missing texture\|slow frame\|audio: opened\|screenshot" /tmp/claude-0/out.txt | tail -${TAILN:-25}
python3 - <<'PY'
from PIL import Image
import glob
fs=sorted(glob.glob('/tmp/claude-0/s/[0-9]*.png'))
if fs:
    n=len(fs); rows=(n+1)//2
    W=Image.new('RGB',(1280,360*rows))
    for i,f in enumerate(fs): W.paste(Image.open(f).resize((640,360)),((i%2)*640,(i//2)*360))
    W.save('/tmp/claude-0/s/grid.png')
PY
