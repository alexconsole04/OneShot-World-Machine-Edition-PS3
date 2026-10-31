import sys, struct, numpy as np
import os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rrc import load
from PIL import Image
c=load(sys.argv[1]); out=sys.argv[2]
W,H=1280,720
fb=np.zeros((H,W,3),np.float32)
SZ=0x2800000
mem={0:bytearray(SZ),1:bytearray(SZ)}
def read(loc,off,n):
    if off+n>SZ: return None
    return bytes(mem[loc][off:off+n])
regs={}
draws=[]; missing=0
for a,b,ms in c['cmds']:
    for k in ms:
        if k in c['mem']:
            off,loc,ds=c['mem'][k]
            if ds in c['data']:
                dd=c['data'][ds]; mem[loc&1][off:off+len(dd)]=dd
    meth=a&0x3fffc
    regs[meth]=b
    if meth==0x1814:
        first=b&0xffffff; cnt=(b>>24)+1
        draws.append((first,cnt,dict(regs)))
print('draws',len(draws))
def attr(r,i):
    off=r.get(0x1680+i*4,0); fmt=r.get(0x1740+i*4,0)
    return off&0x7fffffff,(off>>31)&1,(fmt>>8)&0xff
nq=0
for di,(first,cnt,r) in enumerate(draws):
    off,loc,stride=attr(r,0)
    toff=r.get(0x1A00,0); tfmt=r.get(0x1A04,0); tloc=(tfmt&3)-1
    rect=r.get(0x1A18,0); tw,th=rect>>16,rect&0xffff
    pitch=r.get(0x1840,0)>>8 if False else r.get(0x1840,0)&0xfffff
    tex=read(tloc,toff,pitch*th) if pitch and th else None
    T=None
    if tex is not None:
        T=np.frombuffer(tex,np.uint8).reshape(th,pitch//4,4)[:, :tw].astype(np.float32)/255  # A R G B
    tnz=int(np.count_nonzero(np.frombuffer(tex,np.uint8))) if tex is not None else -1
    print('draw',di,'first',first,'cnt',cnt,'tex',hex(toff),'loc',tloc,tw,th,'pitch',pitch,'nonzero',tnz,'fmt',hex(tfmt),'blend',hex(r.get(0x310,0)),hex(r.get(0x314,0)),hex(r.get(0x318,0)))
    if r.get(0x314,0)!=0x10302 and False: continue
    vb=read(loc,off+first*stride,cnt*stride)
    if vb is None: print('draw',di,'no vertex data',hex(off),first,cnt); missing+=1; continue
    v=np.frombuffer(vb,'>f4').reshape(cnt,stride//4)
    for q in range(cnt//4):
        p=v[q*4:q*4+4]
        x0=(p[0,0]+1)/2*W; x1=(p[1,0]+1)/2*W; y0=(1-p[0,1])/2*H; y1=(1-p[2,1])/2*H
        u0,v0,u1,v1=p[0,2],p[0,3],p[2,2],p[2,3]
        col=p[0,4:8]
        X0,X1,Y0,Y1=int(round(x0)),int(round(x1)),int(round(y0)),int(round(y1))
        X0=max(X0,0);Y0=max(Y0,0);X1=min(X1,W);Y1=min(Y1,H)
        if X1<=X0 or Y1<=Y0: continue
        nq+=1
        if (Y0<720 and Y0>=430 and X0>=280 and X0<700) or (Y0>=280 and Y0<330 and X0>=600 and X0<670): print('  q',di,q,X0,Y0,X1,Y1,'uv',u0,v0,u1,v1,'col',col,'tone',p[0,8:12])
        xs=(np.arange(X0,X1)+0.5-x0)/(x1-x0)*(u1-u0)+u0
        ys=(np.arange(Y0,Y1)+0.5-y0)/(y1-y0)*(v1-v0)+v0
        if T is not None:
            ix=np.clip(xs.astype(int),0,tw-1); iy=np.clip(ys.astype(int),0,th-1)
            s=T[iy][:,ix]
            a=s[...,0]*col[3]; rgb=s[...,1:4]*col[:3]
        else:
            a=np.full((Y1-Y0,X1-X0),col[3]); rgb=np.ones((Y1-Y0,X1-X0,3))*col[:3]
        fb[Y0:Y1,X0:X1]=fb[Y0:Y1,X0:X1]*(1-a[...,None])+rgb*a[...,None]
print('quads drawn',nq,'draws missing vb',missing)
Image.fromarray((np.clip(fb,0,1)*255).astype(np.uint8)).save(out)
