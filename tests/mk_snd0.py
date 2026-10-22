import wave, struct, subprocess, sys, numpy as np
A, src, out = sys.argv[1], sys.argv[2], sys.argv[3]
w = wave.open(src); a = np.frombuffer(w.readframes(w.getnframes()), np.int16).reshape(-1, 2).astype(np.float64)
L = len(a)
X = 88064                       # 2 s crossfade, keeps the loop a whole number of 1024-sample frames
Lb = L - X
assert Lb % 1024 == 0
t = np.linspace(0, np.pi / 2, X)[:, None]
B = a[:Lb].copy()
B[:X] = a[:X] * np.sin(t) + a[Lb:] * np.cos(t)   # equal-power: end of the file flows into the start
D = 1162                        # measured encoder+decoder delay (samples, ffmpeg decode)
R = 2048 - D                    # pre-roll so the loop starts on a frame boundary of the decoded stream
P = np.concatenate([B[Lb - R:], B, B[:2048 - R]])
P = np.clip(np.round(P), -32768, 32767).astype(np.int16)
ww = wave.open('loop_in.wav', 'wb'); ww.setnchannels(2); ww.setsampwidth(2); ww.setframerate(44100); ww.writeframes(P.tobytes()); ww.close()
Bi = np.clip(np.round(B), -32768, 32767).astype(np.int16)
ww = wave.open('loop_ref.wav', 'wb'); ww.setnchannels(2); ww.setsampwidth(2); ww.setframerate(44100); ww.writeframes(Bi.tobytes()); ww.close()
subprocess.check_call([A, '-e', 'atrac3', '-i', 'loop_in.wav', '-o', 'raw.at3', '--container', 'riff'], stdout=subprocess.DEVNULL)
d = open('raw.at3', 'rb').read()
assert d[:4] == b'RIFF' and d[8:12] == b'WAVE'
chunks = []; p = 12
while p < len(d):
    cid, sz = d[p:p+4], struct.unpack('<I', d[p+4:p+8])[0]
    chunks.append((cid, d[p+8:p+8+sz])); p += 8 + sz + (sz & 1)
cd = dict(chunks)
first = 2048 - 69               # Sony convention: decoder skips fact.firstSample + 69 samples
fact = struct.pack('<II', Lb, first)
smpl = struct.pack('<9I', 0, 0, 22675, 60, 0, 0, 0, 1, 0) + struct.pack('<6I', 0, 0, 0, Lb - 1, 0, 0)
body = b'WAVE'
for cid, data in ((b'fmt ', cd[b'fmt ']), (b'fact', fact), (b'smpl', smpl), (b'data', cd[b'data'])):
    body += cid + struct.pack('<I', len(data)) + data + (b'\0' if len(data) & 1 else b'')
open(out, 'wb').write(b'RIFF' + struct.pack('<I', len(body)) + body)
print('loop', Lb, 'samples =', Lb / 44100, 's; frames', len(cd[b'data']) // 384, 'file', len(body) + 8, 'bytes')
