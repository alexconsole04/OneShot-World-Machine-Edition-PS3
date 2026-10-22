# mk_snd0.py <atracdenc> <ambience.wav (44.1 kHz)> <SND0.AT3>
# The XMB plays SND0.AT3 at 48 kHz, so the audio is resampled to 48 kHz first.
# atracdenc only accepts 44.1 kHz WAV headers, but ATRAC3 frames don't depend
# on the rate: the 48 kHz samples are encoded as-is and the header is patched.
import wave, struct, subprocess, sys, numpy as np
A, src, out = sys.argv[1], sys.argv[2], sys.argv[3]
subprocess.check_call(['ffmpeg', '-v', 'error', '-y', '-i', src, '-af', 'aresample=48000:resampler=soxr:precision=28',
                       '-c:a', 'pcm_s16le', 'amb48.wav'])
w = wave.open('amb48.wav'); a = np.frombuffer(w.readframes(w.getnframes()), np.int16).reshape(-1, 2).astype(np.float64)
Lb = len(a) // 1024 * 1024          # whole 1024-sample frames
a = a[:Lb]
# Seamless loop that still begins with the very first sample: over the last
# 2 s the ending fades out while a mirrored copy of the opening fades in, so
# the final samples run ..., a[2], a[1] and then the loop continues a[0], a[1], ...
X = 96000
i = np.arange(X)
t = ((i + 0.5) / X * np.pi / 2)[:, None]
mirror = a[X - i]                   # i = X-1 -> a[1]
B = a.copy()
B[Lb - X:] = a[Lb - X:] * np.cos(t) + mirror * np.sin(t)
D = 1162                            # atracdenc + decoder delay in samples (measured)
R = 2048 - D                        # pre-roll: the loop starts on a frame boundary
P = np.concatenate([B[Lb - R:], B, B[:2048 - R]])
P = np.clip(np.round(P), -32768, 32767).astype(np.int16)
for name, data in (('loop_in.wav', P), ('loop_ref.wav', np.clip(np.round(B), -32768, 32767).astype(np.int16))):
    ww = wave.open(name, 'wb'); ww.setnchannels(2); ww.setsampwidth(2); ww.setframerate(44100); ww.writeframes(data.tobytes()); ww.close()
subprocess.check_call([A, '-e', 'atrac3', '-i', 'loop_in.wav', '-o', 'raw.at3', '--container', 'riff'], stdout=subprocess.DEVNULL)
d = open('raw.at3', 'rb').read()
chunks = []; p = 12
while p < len(d):
    cid, sz = d[p:p+4], struct.unpack('<I', d[p+4:p+8])[0]
    chunks.append((cid, d[p+8:p+8+sz])); p += 8 + sz + (sz & 1)
cd = dict(chunks)
fmt = bytearray(cd[b'fmt '])
block = struct.unpack('<H', fmt[12:14])[0]
struct.pack_into('<II', fmt, 4, 48000, block * 48000 // 1024)   # sample rate, byte rate
# Sony's decoder skips the fact delay + 69 more samples, so output starts at
# decoded sample 2048 = B[0]. 12-byte "fact" as in PS3 games: count, base delay, delay.
skip = 2048 - 69
fact = struct.pack('<III', Lb, skip, skip)
# Loop points in "smpl" are absolute: they include the fact delay.
smpl = struct.pack('<9I', 0, 0, 20833, 60, 0, 0, 0, 1, 0) + struct.pack('<6I', 0, 0, skip, skip + Lb - 1, 0, 0)
body = b'WAVE'
for cid, data in ((b'fmt ', bytes(fmt)), (b'fact', fact), (b'smpl', smpl), (b'data', cd[b'data'])):
    body += cid + struct.pack('<I', len(data)) + data + (b'\0' if len(data) & 1 else b'')
open(out, 'wb').write(b'RIFF' + struct.pack('<I', len(body)) + body)
print('loop', Lb, 'samples at 48 kHz =', Lb / 48000, 's;', len(cd[b'data']) // block, 'frames;', len(body) + 8, 'bytes')
