#!/usr/bin/env python3
"""arktool — unpacker/packer dos .ark do Virtual Pool 4 (x64)

Formato (decifrado por hexdump, validado em3 arquivos):
    +0x00 u32 zero
    +0x04 u32 0x0103 (versao)
    +0x08 u8[16] GUID  (texto no .uuid pareado, mixed-endian)
    +0x18 u32  entryCount
    +0x1C  entryCount x { char nome[48] (NUL, resto 0xFE); u32 offset; u32 size }
    dados = chunks concatenados (zlib se comecar com 0x78, senao cru)

Uso:
    arktool.py info    arquivo.ark
    arktool.py extract arquivo.ark  dir_destino
    arktool.py pack    dir_origem   arquivo.ark      (reempacota mantendo GUID)
"""
import sys, os, struct, zlib, uuid as uuidmod

ENTRY = 56
HDR = 28  # 24 + u32 count


def read_toc(path):
    with open(path, 'rb') as f:
        data = f.read()
    if len(data) < HDR:
        raise SystemExit('arquivo curto demais')
    zero, ver = struct.unpack_from('<II', data, 0)
    guid = data[8:24]
    (count,) = struct.unpack_from('<I', data, 24)
    entries = []
    for i in range(count):
        off = HDR + i * ENTRY
        raw = data[off:off + 48]
        if len(raw) < 48:
            raise SystemExit(f'TOC truncado na entrada {i}')
        name = raw.split(b'\x00', 1)[0].decode('latin-1')
        # sanity: resto deve ser NUL ou 0xFE
        tail = raw[len(name) + 1:]
        if any(b not in (0x00, 0xFE) for b in tail):
            raise SystemExit(f'entrada {i} nome invalido: {name!r} (tail corrompido)')
        offset, size = struct.unpack_from('<II', data, off + 48)
        entries.append((name, offset, size))
    return dict(zero=zero, ver=ver, guid=guid, count=count, entries=entries, raw=data)


def guid_text(g):
    # mixed-endian: 4-2-2-2-6 com primeiros3 campos LE
    d1, d2, d3 = struct.unpack_from('<IHH', g, 0)
    rest = g[8:]
    return f'{d1:08x}-{d2:04x}-{d3:04x}-{rest[0]:02x}{rest[1]:02x}-{rest[2:].hex()}'


def chunk(raw, offset, size):
    b = raw[offset:offset + size]
    if len(b) != size:
        raise SystemExit(f'chunk fora do arquivo: off={offset} size={size}')
    return b


def maybe_inflate(b):
    if len(b) >= 2 and b[0] == 0x78 and b[1] in (0x01, 0x5E, 0x9C, 0xDA):
        try:
            return zlib.decompress(b), True
        except zlib.error:
            pass
    return b, False


def cmd_info(path):
    t = read_toc(path)
    print(f'arquivo : {path}  ({len(t["raw"])} bytes)')
    print(f'versao  : 0x{t["ver"]:X}   entradas: {t["count"]}')
    print(f'guid    : {guid_text(t["guid"])}')
    uuidp = os.path.splitext(path)[0] + '.uuid'
    if os.path.exists(uuidp):
        u = open(uuidp, 'rb').read().split(b'\x00')[0].decode()
        print(f'uuid    : {u}  {"OK" if u == guid_text(t["guid"]) else "*** DIFERE ***"}')
    end = HDR
    for name, off, size in t['entries']:
        flag = ''
        if off < end or off >= len(t['raw']):
            flag = '  <== OFFSET RUIM'
        end = max(end, off + size)
        print(f'  {off:>10} {size:>9}  {name}{flag}')
    print(f'fim dos dados: {end} / {len(t["raw"])}')


def cmd_extract(path, outdir):
    t = read_toc(path)
    os.makedirs(outdir, exist_ok=True)
    n_z = 0
    for name, off, size in t['entries']:
        b = chunk(t['raw'], off, size)
        b2, z = maybe_inflate(b)
        n_z += z
        dst = os.path.join(outdir, name.replace('/', os.sep))
        os.makedirs(os.path.dirname(dst) or '.', exist_ok=True)
        with open(dst, 'wb') as f:
            f.write(b2)
    print(f'{len(t["entries"])} arquivos extraidos ({n_z} zlib) -> {outdir}')


def cmd_pack(indir, outpath, guid_str=None):
    names = sorted(n for n in os.listdir(indir) if os.path.isfile(os.path.join(indir, n)))
    # lê GUID do ark original se existir
    if guid_str is None:
        orig = os.path.splitext(outpath)[0] + '.uuid'
        if os.path.exists(orig):
            guid_str = open(orig, 'rb').read().split(b'\x00')[0].decode()
        else:
            raise SystemExit('sem .uuid/origem para guid')
    u = uuidmod.UUID(guid_str)
    g = u.bytes_le  # header guarda o GUID como bytes_le (u32,u16,u16 LE + resto)
    chunks, toc = b'', []
    off = HDR + len(names) * ENTRY
    for n in names:
        data = open(os.path.join(indir, n), 'rb').read()
        # stock guarda TUDO cru (zlib=0 em todos os arks); o jogo NAO descomprime
        # em todos os paths de leitura (ex: ReadRoomDataFile falha em xml comprimido)
        c = data
        nm = n.encode('latin-1')
        if len(nm) > 47:
            raise SystemExit(f'nome >47 chars nao cabe no TOC: {n}')
        toc.append(nm + b'\x00' * (48 - len(nm)))
        toc.append(struct.pack('<II', off, len(c)))
        chunks += c
        off += len(c)
    hdr = struct.pack('<II', 0, 0x0103) + g + struct.pack('<I', len(names))
    with open(outpath, 'wb') as f:
        f.write(hdr + b''.join(toc) + chunks)
    print(f'{len(names)} arquivos -> {outpath} ({off} bytes) guid={guid_str}')


if __name__ == '__main__':
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    if sys.argv[1] == 'info':
        cmd_info(sys.argv[2])
    elif sys.argv[1] == 'extract':
        cmd_extract(sys.argv[2], sys.argv[3])
    elif sys.argv[1] == 'pack':
        cmd_pack(sys.argv[2], sys.argv[3], sys.argv[4] if len(sys.argv) > 4 else None)
    else:
        raise SystemExit(__doc__)
