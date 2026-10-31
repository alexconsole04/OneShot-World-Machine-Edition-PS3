import re,sys
lines=open(sys.argv[1]).read().split('\n')
ld=re.compile(r'^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} ){4}\s*(\w+)\s+(.*)$')
mem=re.compile(r'(-?\d+)\((r\d+)\)')
last={}  # reg -> (addr, op)
hits=[]
for l in lines:
    m=ld.match(l)
    if not m:
        if re.match(r'^[0-9a-f]+ <',l): last={}
        continue
    addr,op,args=m.groups()
    a=[x.strip() for x in args.split(',')]
    # memory use
    mm=mem.search(args)
    if mm and op not in ('ld','ldu','std','stdu') or (mm and op in('ld','std') and mm.group(2)!='r1'):
        base=mm.group(2)
        if base in last and last[base][1]=='ld' and base not in ('r1','r2'):
            hits.append((last[base][0],addr,op,args))
    # defs
    if op in ('b','bl','bctrl','blr','bctr') or op.startswith('b'):
        if op in('bl','bctrl'): last={}
        continue
    if a and re.match(r'r\d+$',a[0]) and not op.startswith('st') and not op.startswith('cmp'):
        last[a[0]]=(addr,op if op in('ld','ldx','ldu') else 'other')
for h in hits: print(*h)
print(len(hits),file=sys.stderr)
