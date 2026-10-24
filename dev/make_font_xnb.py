# Build a test SpriteFont .xnb (XNA 4 layout) from a TTF, Color or DXT3.
import struct, sys
from PIL import Image, ImageDraw, ImageFont

def s7(n):
    out = b''
    while True:
        b = n & 0x7f; n >>= 7
        if n: out += bytes([b | 0x80])
        else: return out + bytes([b])

def xstr(s):
    b = s.encode(); return s7(len(b)) + b

def dxt3(img):
    w, h = img.size; px = img.load(); out = b''
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            alpha = 0
            for k in range(16):
                x, y = bx + k % 4, by + k // 4
                a = px[x, y][3] if x < w and y < h else 0
                alpha |= (a >> 4) << (4 * k)
            out += struct.pack('<Q', alpha)
            # colour: white/white, all index 0
            out += struct.pack('<HHI', 0xffff, 0xffff, 0)
    return out

def main(ttf, size, out, fmt='color'):
    font = ImageFont.truetype(ttf, size)
    chars = [chr(c) for c in range(32, 127)] + ['…', 'é']
    asc, desc = font.getmetrics(); lh = asc + desc
    cells = []
    x = y = 0; W = 256
    for ch in chars:
        adv = int(round(font.getlength(ch)))
        if x + adv + 1 > W: x = 0; y += lh + 1
        cells.append((x, y, adv)); x += adv + 1
    H = y + lh + 1; H = (H + 3) // 4 * 4
    img = Image.new('RGBA', (W, H), (255, 255, 255, 0))
    d = ImageDraw.Draw(img)
    for ch, (cx, cy, adv) in zip(chars, cells):
        d.text((cx, cy), ch, font=font, fill=(255, 255, 255, 255))
    # pixel-font look: threshold alpha
    px = img.load()
    for j in range(H):
        for i in range(W):
            r, g, b, a = px[i, j]; px[i, j] = (255, 255, 255, 255 if a > 110 else 0)
    readers = ['Microsoft.Xna.Framework.Content.SpriteFontReader',
               'Microsoft.Xna.Framework.Content.Texture2DReader',
               'Microsoft.Xna.Framework.Content.ListReader`1[[Microsoft.Xna.Framework.Rectangle]]',
               'Microsoft.Xna.Framework.Content.RectangleReader',
               'Microsoft.Xna.Framework.Content.ListReader`1[[System.Char]]',
               'Microsoft.Xna.Framework.Content.CharReader',
               'Microsoft.Xna.Framework.Content.ListReader`1[[Microsoft.Xna.Framework.Vector3]]',
               'Microsoft.Xna.Framework.Content.Vector3Reader']
    body = s7(len(readers)) + b''.join(xstr(r) + struct.pack('<i', 0) for r in readers) + s7(0) + s7(1)
    if fmt == 'color':
        data = img.tobytes(); sf = 0
    else:
        data = dxt3(img); sf = 5
    body += s7(2) + struct.pack('<iIIII', sf, W, H, 1, len(data)) + data
    n = len(chars)
    body += s7(3) + struct.pack('<i', n) + b''.join(struct.pack('<4i', cx, cy, adv, lh) for cx, cy, adv in cells)
    body += s7(3) + struct.pack('<i', n) + b''.join(struct.pack('<4i', 0, 0, adv, lh) for cx, cy, adv in cells)
    body += s7(5) + struct.pack('<i', n) + b''.join(ch.encode('utf-8') for ch in chars)
    body += struct.pack('<if', lh, 0.0)
    body += s7(7) + struct.pack('<i', n) + b''.join(struct.pack('<3f', 0, adv, 0) for cx, cy, adv in cells)
    body += b'\x01' + b'?'
    hdr = b'XNBw\x05\x00'
    open(out, 'wb').write(hdr + struct.pack('<I', len(hdr) + 4 + len(body)) + body)

if __name__ == '__main__':
    main(sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4] if len(sys.argv) > 4 else 'color')
