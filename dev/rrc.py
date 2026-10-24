# RPCS3 RSX capture (.rrc, version 6) reader.
import struct, sys
class R:
    def __init__(s,d): s.d=d; s.p=0
    def u32(s): v=struct.unpack_from('<I',s.d,s.p)[0]; s.p+=4; return v
    def u64(s): v=struct.unpack_from('<Q',s.d,s.p)[0]; s.p+=8; return v
    def raw(s,n): b=s.d[s.p:s.p+n]; s.p+=n; return b
    def vle(s):
        r=0; sh=0
        while True:
            b=s.d[s.p]; s.p+=1; r|=(b&0x7f)<<sh; sh+=7
            if not b&0x80: return r
def load(path):
    r=R(open(path,'rb').read())
    magic,ver,le=r.u32(),r.u32(),r.u32()
    cap={'ver':ver}
    n=r.vle(); cap['tile']=[(r.raw(432),r.u64()) for _ in range(n)]
    n=r.vle(); mm={}
    for _ in range(n):
        off,loc,ds=struct.unpack('<IIQ',r.raw(16)); k=r.u64(); mm[k]=(off,loc,ds)
    cap['mem']=mm
    n=r.vle(); md={}
    for _ in range(n):
        sz=r.vle(); data=r.raw(sz); k=r.u64(); md[k]=data
    cap['data']=md
    n=r.vle(); cap['disp']=[(r.raw(132),r.u64()) for _ in range(n)]
    n=r.vle(); cmds=[]
    for _ in range(n):
        a,b=r.u32(),r.u32(); m=r.vle(); ms=[r.u64() for _ in range(m)]; ts=r.u64(); ds=r.u64()
        cmds.append((a,b,ms))
    cap['cmds']=cmds
    cap['rest']=len(r.d)-r.p
    cap['reg_state_raw']=r.d[r.p:]
    return cap
if __name__=='__main__':
    c=load(sys.argv[1])
    print('mem',len(c['mem']),'data',len(c['data']),'cmds',len(c['cmds']),'rest',c['rest'])
    from collections import Counter
    print(Counter((a) for a,b,m in c['cmds']).most_common(40))
