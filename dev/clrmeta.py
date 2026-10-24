#!/usr/bin/env python3
"""Minimal ECMA-335 metadata reader + IL disassembler (no external deps).

Usage:
  clrmeta.py EXE types            -> namespace/type/field/method listing
  clrmeta.py EXE strings          -> #US user-string heap
  clrmeta.py EXE il TYPE[::METHOD] -> IL disassembly of matching methods
  clrmeta.py EXE info             -> runtime version, references
"""
import struct, sys

class PE:
    def __init__(s, data):
        s.d = data
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        assert data[pe:pe+4] == b'PE\0\0'
        nsec = struct.unpack_from('<H', data, pe+6)[0]
        optsz = struct.unpack_from('<H', data, pe+20)[0]
        opt = pe + 24
        magic = struct.unpack_from('<H', data, opt)[0]
        dd = opt + (96 if magic == 0x10b else 112)
        s.cli_rva, s.cli_sz = struct.unpack_from('<II', data, dd + 14*8)
        s.secs = []
        so = opt + optsz
        for i in range(nsec):
            name = data[so:so+8].rstrip(b'\0').decode()
            vsz, va, rsz, rptr = struct.unpack_from('<IIII', data, so+8)
            s.secs.append((name, va, max(vsz, rsz), rptr))
            so += 40
    def off(s, rva):
        for n, va, sz, rp in s.secs:
            if va <= rva < va + sz:
                return rva - va + rp
        raise ValueError(hex(rva))

TABLES = {}
def T(i, name, cols): TABLES[i] = (name, cols)
# column kinds: 2/4 fixed, 'S' string, 'G' guid, 'B' blob, ('t',idx) simple index, ('c',name) coded
T(0x00,'Module',[2,'S','G','G','G'])
T(0x01,'TypeRef',[('c','ResolutionScope'),'S','S'])
T(0x02,'TypeDef',[4,'S','S',('c','TypeDefOrRef'),('t',0x04),('t',0x06)])
T(0x03,'FieldPtr',[('t',0x04)])
T(0x04,'Field',[2,'S','B'])
T(0x05,'MethodPtr',[('t',0x06)])
T(0x06,'MethodDef',[4,2,2,'S','B',('t',0x08)])
T(0x07,'ParamPtr',[('t',0x08)])
T(0x08,'Param',[2,2,'S'])
T(0x09,'InterfaceImpl',[('t',0x02),('c','TypeDefOrRef')])
T(0x0a,'MemberRef',[('c','MemberRefParent'),'S','B'])
T(0x0b,'Constant',[2,('c','HasConstant'),'B'])
T(0x0c,'CustomAttribute',[('c','HasCustomAttribute'),('c','CustomAttributeType'),'B'])
T(0x0d,'FieldMarshal',[('c','HasFieldMarshal'),'B'])
T(0x0e,'DeclSecurity',[2,('c','HasDeclSecurity'),'B'])
T(0x0f,'ClassLayout',[2,4,('t',0x02)])
T(0x10,'FieldLayout',[4,('t',0x04)])
T(0x11,'StandAloneSig',['B'])
T(0x12,'EventMap',[('t',0x02),('t',0x14)])
T(0x13,'EventPtr',[('t',0x14)])
T(0x14,'Event',[2,'S',('c','TypeDefOrRef')])
T(0x15,'PropertyMap',[('t',0x02),('t',0x17)])
T(0x16,'PropertyPtr',[('t',0x17)])
T(0x17,'Property',[2,'S','B'])
T(0x18,'MethodSemantics',[2,('t',0x06),('c','HasSemantics')])
T(0x19,'MethodImpl',[('t',0x02),('c','MethodDefOrRef'),('c','MethodDefOrRef')])
T(0x1a,'ModuleRef',['S'])
T(0x1b,'TypeSpec',['B'])
T(0x1c,'ImplMap',[2,('c','MemberForwarded'),'S',('t',0x1a)])
T(0x1d,'FieldRVA',[4,('t',0x04)])
T(0x20,'Assembly',[4,2,2,2,2,4,'B','S','S'])
T(0x21,'AssemblyProcessor',[4])
T(0x22,'AssemblyOS',[4,4,4])
T(0x23,'AssemblyRef',[2,2,2,2,4,'B','S','S','B'])
T(0x24,'AssemblyRefProcessor',[4,('t',0x23)])
T(0x25,'AssemblyRefOS',[4,4,4,('t',0x23)])
T(0x26,'File',[4,'S','B'])
T(0x27,'ExportedType',[4,4,'S','S',('c','Implementation')])
T(0x28,'ManifestResource',[4,4,'S',('c','Implementation')])
T(0x29,'NestedClass',[('t',0x02),('t',0x02)])
T(0x2a,'GenericParam',[2,2,('c','TypeOrMethodDef'),'S'])
T(0x2b,'MethodSpec',[('c','MethodDefOrRef'),'B'])
T(0x2c,'GenericParamConstraint',[('t',0x2a),('c','TypeDefOrRef')])

CODED = {
 'TypeDefOrRef':(2,[0x02,0x01,0x1b]),
 'HasConstant':(2,[0x04,0x08,0x17]),
 'HasCustomAttribute':(5,[0x06,0x04,0x01,0x02,0x08,0x09,0x0a,0x00,0x0e,0x17,0x14,0x11,0x1a,0x1b,0x20,0x23,0x26,0x27,0x28,0x2a,0x2c,0x2b]),
 'HasFieldMarshal':(1,[0x04,0x08]),
 'HasDeclSecurity':(2,[0x02,0x06,0x20]),
 'MemberRefParent':(3,[0x02,0x01,0x1a,0x06,0x1b]),
 'HasSemantics':(1,[0x14,0x17]),
 'MethodDefOrRef':(1,[0x06,0x0a]),
 'MemberForwarded':(1,[0x04,0x06]),
 'Implementation':(2,[0x26,0x23,0x27]),
 'CustomAttributeType':(3,[None,None,0x06,0x0a,None]),
 'ResolutionScope':(2,[0x00,0x1a,0x23,0x01]),
 'TypeOrMethodDef':(1,[0x02,0x06]),
}

class Meta:
    def __init__(s, path):
        s.data = open(path,'rb').read()
        s.pe = PE(s.data)
        cli = s.pe.off(s.pe.cli_rva)
        md_rva, md_sz = struct.unpack_from('<II', s.data, cli+8)
        s.entry_tok = struct.unpack_from('<I', s.data, cli+20)[0]
        mo = s.pe.off(md_rva)
        vlen = struct.unpack_from('<I', s.data, mo+12)[0]
        s.version = s.data[mo+16:mo+16+vlen].rstrip(b'\0').decode()
        p = mo + 16 + vlen + 2
        nstreams = struct.unpack_from('<H', s.data, p)[0]; p += 2
        s.streams = {}
        for _ in range(nstreams):
            off, sz = struct.unpack_from('<II', s.data, p); p += 8
            e = s.data.index(b'\0', p)
            name = s.data[p:e].decode(); p = (e + 4) & ~3
            s.streams[name] = (mo+off, sz)
        s._tables()

    def _tables(s):
        to, _ = s.streams.get('#~') or s.streams['#-']
        d = s.data
        hs = d[to+6]
        s.sidx = 4 if hs & 1 else 2; s.gidx = 4 if hs & 2 else 2; s.bidx = 4 if hs & 4 else 2
        valid = struct.unpack_from('<Q', d, to+8)[0]
        p = to + 24
        s.rows = {}
        for i in range(64):
            if valid >> i & 1:
                s.rows[i] = struct.unpack_from('<I', d, p)[0]; p += 4
        if hs & 0x20: p += 4  # extra data
        def tidx(t): return 4 if s.rows.get(t,0) > 0xffff else 2
        def cidx(c):
            bits, ts = CODED[c]
            m = max(s.rows.get(t,0) for t in ts if t is not None)
            return 4 if m >= (1 << (16-bits)) else 2
        s.tab = {}
        for i in sorted(s.rows):
            name, cols = TABLES[i]
            sizes = []
            for c in cols:
                if c in (2,4): sizes.append(c)
                elif c == 'S': sizes.append(s.sidx)
                elif c == 'G': sizes.append(s.gidx)
                elif c == 'B': sizes.append(s.bidx)
                elif c[0] == 't': sizes.append(tidx(c[1]))
                else: sizes.append(cidx(c[1]))
            rs = sum(sizes); rows = []
            for r in range(s.rows[i]):
                q = p + r*rs; row = []
                for sz in sizes:
                    row.append(struct.unpack_from('<H' if sz==2 else '<I', d, q)[0]); q += sz
                rows.append(row)
            s.tab[name] = rows
            p += rs * s.rows[i]

    def str(s, i):
        o, _ = s.streams['#Strings']; o += i
        return s.data[o:s.data.index(b'\0', o)].decode('utf-8','replace')
    def blob(s, i):
        o, _ = s.streams['#Blob']; o += i
        n, k = s._cu(o)
        return s.data[o+k:o+k+n]
    def us(s, i):
        o, _ = s.streams['#US']; o += i
        n, k = s._cu(o)
        return s.data[o+k:o+k+n-1].decode('utf-16le','replace') if n else ''
    def _cu(s, o):
        b = s.data[o]
        if b & 0x80 == 0: return b, 1
        if b & 0xc0 == 0x80: return ((b & 0x3f) << 8) | s.data[o+1], 2
        return ((b & 0x1f) << 24) | (s.data[o+1] << 16) | (s.data[o+2] << 8) | s.data[o+3], 4

    def all_us(s):
        o, sz = s.streams['#US']; p = 1; out = []
        while p < sz:
            n, k = s._cu(o+p)
            if n: out.append((p, s.data[o+p+k:o+p+k+n-1].decode('utf-16le','replace')))
            p += k + n
        return out

    # ---- naming helpers
    def typedef_name(s, i):  # 1-based
        r = s.tab['TypeDef'][i-1]
        nm = s.str(r[1]); ns = s.str(r[2])
        enc = s.nested_parent().get(i)
        if enc: return s.typedef_name(enc) + '/' + nm
        return (ns + '.' if ns else '') + nm
    def nested_parent(s):
        if not hasattr(s, '_np'):
            s._np = {r[0]: r[1] for r in s.tab.get('NestedClass', [])}
        return s._np
    def typeref_name(s, i):
        r = s.tab['TypeRef'][i-1]
        ns = s.str(r[2]); nm = s.str(r[1])
        return (ns + '.' if ns else '') + nm
    def tdor(s, v):
        tag, idx = v & 3, v >> 2
        if idx == 0: return None
        return [s.typedef_name, s.typeref_name, lambda i: 'TypeSpec#%d' % i][tag](idx)
    def method_owner(s):
        if not hasattr(s, '_mo'):
            s._mo = {}
            td = s.tab['TypeDef']; nm = len(s.tab['MethodDef'])
            for i, r in enumerate(td):
                start = r[5]; end = td[i+1][5] if i+1 < len(td) else nm+1
                for m in range(start, end): s._mo[m] = i+1
        return s._mo
    def field_owner(s):
        if not hasattr(s, '_fo'):
            s._fo = {}
            td = s.tab['TypeDef']; nf = len(s.tab['Field'])
            for i, r in enumerate(td):
                start = r[4]; end = td[i+1][4] if i+1 < len(td) else nf+1
                for f in range(start, end): s._fo[f] = i+1
        return s._fo
    def methoddef_name(s, i):
        return s.typedef_name(s.method_owner()[i]) + '::' + s.str(s.tab['MethodDef'][i-1][3])
    def memberref_name(s, i):
        r = s.tab['MemberRef'][i-1]
        tag, idx = r[0] & 7, r[0] >> 3
        par = {0: s.typedef_name, 1: s.typeref_name, 2: lambda i: 'ModuleRef#%d'%i,
               3: s.methoddef_name, 4: lambda i: s.typespec_name(i)}[tag](idx)
        return par + '::' + s.str(r[1])
    def typespec_name(s, i):
        b = s.blob(s.tab['TypeSpec'][i-1][0])
        try:
            return s._sigtype(b, 0)[0]
        except Exception:
            return 'TypeSpec#%d' % i
    PRIM = {1:'void',2:'bool',3:'char',4:'sbyte',5:'byte',6:'short',7:'ushort',8:'int',9:'uint',
            10:'long',11:'ulong',12:'float',13:'double',14:'string',0x18:'nint',0x19:'nuint',0x1c:'object',0x16:'TypedRef'}
    def _cub(s, b, p):
        x = b[p]
        if x & 0x80 == 0: return x, p+1
        if x & 0xc0 == 0x80: return ((x&0x3f)<<8)|b[p+1], p+2
        return ((x&0x1f)<<24)|(b[p+1]<<16)|(b[p+2]<<8)|b[p+3], p+4
    def _sigtype(s, b, p):
        e = b[p]; p += 1
        if e in s.PRIM: return s.PRIM[e], p
        if e in (0x11, 0x12):
            v, p = s._cub(b, p); return s.tdor(v), p
        if e == 0x1d:
            t, p = s._sigtype(b, p); return t + '[]', p
        if e == 0x0f: t, p = s._sigtype(b, p); return t + '*', p
        if e == 0x10: t, p = s._sigtype(b, p); return 'ref ' + t, p
        if e == 0x15:
            t, p = s._sigtype(b, p); n, p = s._cub(b, p); a = []
            for _ in range(n):
                x, p = s._sigtype(b, p); a.append(x)
            return '%s<%s>' % (t.split('`')[0], ','.join(a)), p
        if e == 0x13: n, p = s._cub(b, p); return '!%d' % n, p
        if e == 0x1e: n, p = s._cub(b, p); return '!!%d' % n, p
        if e == 0x14:
            t, p = s._sigtype(b, p); r, p = s._cub(b, p); ns, p = s._cub(b, p)
            for _ in range(ns): _, p = s._cub(b, p)
            nl, p = s._cub(b, p)
            for _ in range(nl): _, p = s._cub(b, p)
            return t + '[' + ','*(r-1) + ']', p
        if e in (0x1f, 0x20):
            _, p = s._cub(b, p); return s._sigtype(b, p)
        if e == 0x45: return s._sigtype(b, p)
        if e == 0x41: return s._sigtype(b, p)
        if e == 0x1b: return 'fnptr', len(b)
        return '?%x' % e, p
    def method_sig(s, blobi):
        b = s.blob(blobi); p = 0
        cc = b[p]; p += 1
        if cc & 0x10: _, p = s._cub(b, p)
        n, p = s._cub(b, p)
        ret, p = s._sigtype(b, p); args = []
        for _ in range(n):
            if p < len(b) and b[p] == 0x41: p += 1
            a, p = s._sigtype(b, p); args.append(a)
        return ret, args, bool(cc & 0x20)
    def field_sig(s, blobi):
        b = s.blob(blobi)
        try: return s._sigtype(b, 1)[0]
        except Exception: return '?'

    def token_name(s, tok):
        t, i = tok >> 24, tok & 0xffffff
        try:
            if t == 0x06: return s.methoddef_name(i)
            if t == 0x0a: return s.memberref_name(i)
            if t == 0x04:
                return s.typedef_name(s.field_owner()[i]) + '::' + s.str(s.tab['Field'][i-1][1])
            if t == 0x02: return s.typedef_name(i)
            if t == 0x01: return s.typeref_name(i)
            if t == 0x1b: return s.typespec_name(i)
            if t == 0x2b:
                r = s.tab['MethodSpec'][i-1]
                tag, idx = r[0] & 1, r[0] >> 1
                return (s.methoddef_name(idx) if tag == 0 else s.memberref_name(idx)) + '<>'
            if t == 0x70: return repr(s.us(i))
            if t == 0x11: return 'sig#%d' % i
        except Exception as e:
            return 'tok%08x(%s)' % (tok, e)
        return 'tok%08x' % tok

# ---- IL opcodes
OP1 = {}; OP2 = {}
def _ops():
    one = """00 nop -;01 break -;02 ldarg.0 -;03 ldarg.1 -;04 ldarg.2 -;05 ldarg.3 -;06 ldloc.0 -;07 ldloc.1 -;08 ldloc.2 -;09 ldloc.3 -;
0a stloc.0 -;0b stloc.1 -;0c stloc.2 -;0d stloc.3 -;0e ldarg.s u1;0f ldarga.s u1;10 starg.s u1;11 ldloc.s u1;12 ldloca.s u1;13 stloc.s u1;
14 ldnull -;15 ldc.i4.m1 -;16 ldc.i4.0 -;17 ldc.i4.1 -;18 ldc.i4.2 -;19 ldc.i4.3 -;1a ldc.i4.4 -;1b ldc.i4.5 -;1c ldc.i4.6 -;1d ldc.i4.7 -;
1e ldc.i4.8 -;1f ldc.i4.s i1;20 ldc.i4 i4;21 ldc.i8 i8;22 ldc.r4 r4;23 ldc.r8 r8;25 dup -;26 pop -;27 jmp tok;28 call tok;29 calli tok;2a ret -;
2b br.s b1;2c brfalse.s b1;2d brtrue.s b1;2e beq.s b1;2f bge.s b1;30 bgt.s b1;31 ble.s b1;32 blt.s b1;33 bne.un.s b1;34 bge.un.s b1;
35 bgt.un.s b1;36 ble.un.s b1;37 blt.un.s b1;38 br b4;39 brfalse b4;3a brtrue b4;3b beq b4;3c bge b4;3d bgt b4;3e ble b4;3f blt b4;
40 bne.un b4;41 bge.un b4;42 bgt.un b4;43 ble.un b4;44 blt.un b4;45 switch sw;46 ldind.i1 -;47 ldind.u1 -;48 ldind.i2 -;49 ldind.u2 -;
4a ldind.i4 -;4b ldind.u4 -;4c ldind.i8 -;4d ldind.i -;4e ldind.r4 -;4f ldind.r8 -;50 ldind.ref -;51 stind.ref -;52 stind.i1 -;53 stind.i2 -;
54 stind.i4 -;55 stind.i8 -;56 stind.r4 -;57 stind.r8 -;58 add -;59 sub -;5a mul -;5b div -;5c div.un -;5d rem -;5e rem.un -;5f and -;
60 or -;61 xor -;62 shl -;63 shr -;64 shr.un -;65 neg -;66 not -;67 conv.i1 -;68 conv.i2 -;69 conv.i4 -;6a conv.i8 -;6b conv.r4 -;
6c conv.r8 -;6d conv.u4 -;6e conv.u8 -;6f callvirt tok;70 cpobj tok;71 ldobj tok;72 ldstr tok;73 newobj tok;74 castclass tok;75 isinst tok;
76 conv.r.un -;79 unbox tok;7a throw -;7b ldfld tok;7c ldflda tok;7d stfld tok;7e ldsfld tok;7f ldsflda tok;80 stsfld tok;81 stobj tok;
82 conv.ovf.i1.un -;83 conv.ovf.i2.un -;84 conv.ovf.i4.un -;85 conv.ovf.i8.un -;86 conv.ovf.u1.un -;87 conv.ovf.u2.un -;88 conv.ovf.u4.un -;
89 conv.ovf.u8.un -;8a conv.ovf.i.un -;8b conv.ovf.u.un -;8c box tok;8d newarr tok;8e ldlen -;8f ldelema tok;90 ldelem.i1 -;91 ldelem.u1 -;
92 ldelem.i2 -;93 ldelem.u2 -;94 ldelem.i4 -;95 ldelem.u4 -;96 ldelem.i8 -;97 ldelem.i -;98 ldelem.r4 -;99 ldelem.r8 -;9a ldelem.ref -;
9b stelem.i -;9c stelem.i1 -;9d stelem.i2 -;9e stelem.i4 -;9f stelem.i8 -;a0 stelem.r4 -;a1 stelem.r8 -;a2 stelem.ref -;a3 ldelem tok;
a4 stelem tok;a5 unbox.any tok;b3 conv.ovf.i1 -;b4 conv.ovf.u1 -;b5 conv.ovf.i2 -;b6 conv.ovf.u2 -;b7 conv.ovf.i4 -;b8 conv.ovf.u4 -;
b9 conv.ovf.i8 -;ba conv.ovf.u8 -;c2 refanyval tok;c3 ckfinite -;c6 mkrefany tok;d0 ldtoken tok;d1 conv.u2 -;d2 conv.u1 -;d3 conv.i -;
d4 conv.ovf.i -;d5 conv.ovf.u -;d6 add.ovf -;d7 add.ovf.un -;d8 mul.ovf -;d9 mul.ovf.un -;da sub.ovf -;db sub.ovf.un -;dc endfinally -;
dd leave b4;de leave.s b1;df stind.i -;e0 conv.u -"""
    for e in one.replace('\n','').split(';'):
        c, n, k = e.split(); OP1[int(c,16)] = (n, k)
    two = """00 arglist -;01 ceq -;02 cgt -;03 cgt.un -;04 clt -;05 clt.un -;06 ldftn tok;07 ldvirtftn tok;09 ldarg u2;0a ldarga u2;0b starg u2;
0c ldloc u2;0d ldloca u2;0e stloc u2;0f localloc -;11 endfilter -;12 unaligned. u1;13 volatile. -;14 tail. -;15 initobj tok;16 constrained. tok;
17 cpblk -;18 initblk -;19 no. u1;1a rethrow -;1c sizeof tok;1d refanytype -;1e readonly. -"""
    for e in two.replace('\n','').split(';'):
        c, n, k = e.split(); OP2[int(c,16)] = (n, k)
_ops()

def disasm(m, mi):
    r = m.tab['MethodDef'][mi-1]
    rva = r[0]
    if rva == 0: return ['    (no body)']
    o = m.pe.off(rva); d = m.data
    h = d[o]
    if h & 3 == 2:
        size = h >> 2; code = o + 1
    else:
        flags = struct.unpack_from('<H', d, o)[0]
        size = struct.unpack_from('<I', d, o+4)[0]
        code = o + (flags >> 12) * 4
    p = code; end = code + size; out = []
    while p < end:
        at = p - code
        b = d[p]; p += 1
        if b == 0xfe:
            n, k = OP2[d[p]]; p += 1
        else:
            n, k = OP1.get(b, ('?%02x' % b, '-'))
        arg = ''
        if k == 'u1': arg = str(d[p]); p += 1
        elif k == 'i1': arg = str(struct.unpack_from('<b', d, p)[0]); p += 1
        elif k == 'u2': arg = str(struct.unpack_from('<H', d, p)[0]); p += 2
        elif k == 'i4': arg = str(struct.unpack_from('<i', d, p)[0]); p += 4
        elif k == 'i8': arg = str(struct.unpack_from('<q', d, p)[0]); p += 8
        elif k == 'r4': arg = repr(struct.unpack_from('<f', d, p)[0]); p += 4
        elif k == 'r8': arg = repr(struct.unpack_from('<d', d, p)[0]); p += 8
        elif k == 'b1':
            v = struct.unpack_from('<b', d, p)[0]; p += 1; arg = 'IL_%04x' % (p - code + v)
        elif k == 'b4':
            v = struct.unpack_from('<i', d, p)[0]; p += 4; arg = 'IL_%04x' % (p - code + v)
        elif k == 'tok':
            t = struct.unpack_from('<I', d, p)[0]; p += 4; arg = m.token_name(t)
        elif k == 'sw':
            cnt = struct.unpack_from('<I', d, p)[0]; p += 4
            tg = [struct.unpack_from('<i', d, p+4*j)[0] for j in range(cnt)]; p += 4*cnt
            arg = '(' + ', '.join('IL_%04x' % (p - code + t) for t in tg) + ')'
        out.append('    IL_%04x: %s %s' % (at, n, arg))
    return out

def method_header(m, mi):
    r = m.tab['MethodDef'][mi-1]
    try:
        ret, args, inst = m.method_sig(r[4])
    except Exception:
        ret, args, inst = '?', ['?'], True
    st = '' if inst else 'static '
    vi = 'virtual ' if r[2] & 0x40 else ''
    return '%s%s%s %s(%s)' % (st, vi, ret, m.str(r[3]), ', '.join(args))

def main():
    m = Meta(sys.argv[1]); cmd = sys.argv[2]
    if cmd == 'info':
        print('runtime:', m.version)
        print('entry:', m.token_name(m.entry_tok))
        for r in m.tab.get('AssemblyRef', []):
            print('ref: %s %d.%d.%d.%d' % (m.str(r[6]), r[0], r[1], r[2], r[3]))
        for r in m.tab.get('ModuleRef', []): print('moduleref:', m.str(r[0]))
        for r in m.tab.get('ImplMap', []):
            tag, idx = r[1] & 1, r[1] >> 1
            nm = m.methoddef_name(idx) if tag else '?'
            print('pinvoke: %s -> %s!%s' % (nm, m.str(m.tab['ModuleRef'][r[3]-1][0]), m.str(r[2])))
        for r in m.tab.get('ManifestResource', []): print('resource:', m.str(r[2]))
        for k, v in sorted(m.rows.items()): print('table %s: %d' % (TABLES[k][0], v))
    elif cmd == 'types':
        fo = m.field_owner(); mo = m.method_owner()
        fields = {}; meths = {}
        for f, t in fo.items(): fields.setdefault(t, []).append(f)
        for x, t in mo.items(): meths.setdefault(t, []).append(x)
        for i, r in enumerate(m.tab['TypeDef'], 1):
            base = m.tdor(r[3])
            print('class %s%s' % (m.typedef_name(i), ' : ' + base if base else ''))
            for f in fields.get(i, []):
                fr = m.tab['Field'][f-1]
                print('  field %s%s %s' % ('static ' if fr[0] & 0x10 else '', m.field_sig(fr[2]), m.str(fr[1])))
            for x in meths.get(i, []):
                rr = m.tab['MethodDef'][x-1]
                sz = 0
                if rr[0]:
                    o = m.pe.off(rr[0]); h = m.data[o]
                    sz = h >> 2 if h & 3 == 2 else struct.unpack_from('<I', m.data, o+4)[0]
                print('  method %s  [%d bytes IL]' % (method_header(m, x), sz))
    elif cmd == 'strings':
        for o, s in m.all_us(): print('%06x %r' % (o, s))
    elif cmd == 'il':
        pat = sys.argv[3]
        tpat, _, mpat = pat.partition('::')
        for x, t in sorted(m.method_owner().items()):
            tn = m.typedef_name(t); mn = m.str(m.tab['MethodDef'][x-1][3])
            if tn == tpat or tn.endswith('.' + tpat) or tn.endswith('/' + tpat):
                if not mpat or mn == mpat:
                    print('%s::%s' % (tn, method_header(m, x)))
                    print('\n'.join(disasm(m, x))); print()

if __name__ == '__main__':
    main()
