import re,sys
L=open(sys.argv[1]).read().split('\n')
cnt=0; last={}
for l in L:
    s=l.strip()
    if s.startswith('bl ') or s.startswith('bctrl') or s.startswith('.cfi_endproc') or s.startswith('blr'): last={}; continue
    m=re.match(r'(\w+)\s+(\d+),(.*)',s)
    if not m: continue
    op,rt,rest=m.groups()
    mm=re.search(r'(-?\d+)\((\d+)\)',rest)
    if mm and op in('lwz','stw','lbz','stb','lhz','sth','lwzu','lfs','lfd','stfs','stfd'):
        b=mm.group(2)
        if last.get(b)=='ld' and 3<=int(b)<=12: cnt+=1
    if op=='ld': last[rt]='ld'
    elif not op.startswith('st') and not op.startswith('cmp'): last[rt]='x'
print(cnt)
