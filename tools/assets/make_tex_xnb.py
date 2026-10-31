# PNG (or generated) -> Texture2D .xnb (Color). usage: make_tex_xnb.py in.png out.xnb
import struct, sys
from PIL import Image
def s7(n):
    out=b''
    while True:
        b=n&0x7f; n>>=7
        if n: out+=bytes([b|0x80])
        else: return out+bytes([b])
def write(img, out):
    img=img.convert('RGBA'); W,H=img.size; data=img.tobytes()
    r='Microsoft.Xna.Framework.Content.Texture2DReader, Microsoft.Xna.Framework.Graphics, Version=4.0.0.0, Culture=neutral, PublicKeyToken=842cf8be1de50553'.encode()
    body=s7(1)+s7(len(r))+r+struct.pack('<i',0)+s7(0)+s7(1)+struct.pack('<iIIII',0,W,H,1,len(data))+data
    hdr=b'XNBw\x05\x00'
    open(out,'wb').write(hdr+struct.pack('<I',len(hdr)+4+len(body))+body)
if __name__=='__main__':
    write(Image.open(sys.argv[1]), sys.argv[2])
