#!/usr/bin/env python3
"""ark.py — unpacker/repacker para arquivos .ark do Virtual Pool 4.

Formato (reverse-engineered de vp4onlineS.exe, build 7992962):

  Offset  Tamanho  Conteudo
  0x00    4        u32  = 0 (sempre 0 nos arquivos stock)
  0x04    4        u32  = 0x00000103 (versao)
  0x08    16       GUID  (mixed-endian MS = texto do .uuid correspondente)
  0x18    4        u32  = N (numero de entradas)
  0x1C    56*N     TOC: por entrada -> nome[48] (NUL-terminado, resto 0xFE)
                            + u32 offset_absoluto + u32 tamanho
  ...     ...      dados (blobs em sequencia exatamente na ordem do TOC,
                   offset_0 = 0x1C + 56*N, sem padding)

Tipos de blob (detectados pelo primeiro u32 little-endian):
  0xNNABCD?? -> imagem 0xABCD00NN  (NN = metodo 1,2,3,4,7; cabecalho 0x34
                ou 0xB4 bytes seguido de pixels crus)
  0x474E5089 -> PNG puro (89 50 4E 47)
  0x00000107 -> pacote de cena compilado (shaders HLSL + materiais + malhas)
  outro      -> texto plano (ex.: *.xml salvo sem envoltorio)

Uso:
  ark.py list   arquivo.ark
  ark.py extract arquivo.ark diretorio
  ark.py pack   diretorio arquivo.ark       (usa meta.json do extract)
"""

import struct
import sys
import os
import json

MAGIC_IMG = 0xABCD0000
MAGIC_PNG = 0x474E5089
MAGIC_SCENE = 0x00000107


def parse(data):
    if len(data) < 0x1C:
        raise ValueError("arquivo pequeno demais")
    zero, ver = struct.unpack_from("<II", data, 0)
    guid = data[8:24]
    n = struct.unpack_from("<I", data, 0x18)[0]
    off = 0x1C
    entries = []
    for _ in range(n):
        if off + 56 > len(data):
            raise ValueError("TOC truncado")
        name_raw = data[off:off + 48]
        name = name_raw.split(b"\0")[0].decode("utf-8", "replace")
        a, b = struct.unpack_from("<II", data, off + 48)
        entries.append({"name": name, "name_raw": name_raw.hex(),
                        "off": a, "size": b})
        off += 56
    toc_end = off
    run = toc_end
    for e in entries:
        if e["off"] != run:
            raise ValueError(
                "offset inesperado para %r: %d (esperado %d)"
                % (e["name"], e["off"], run))
        if e["off"] + e["size"] > len(data):
            raise ValueError("blob fora do arquivo: %r" % e["name"])
        run += e["size"]
    if run != len(data):
        raise ValueError("sobra de %d bytes no fim" % (len(data) - run))
    return {"zero": zero, "ver": ver, "guid": guid,
            "entries": entries, "toc_end": toc_end, "raw": data}


def detect(blob):
    if len(blob) < 4:
        return "vazio"
    m = struct.unpack_from("<I", blob, 0)[0]
    if m == MAGIC_PNG:
        return "png"
    if (m & 0xFFFF0000) == MAGIC_IMG:
        return "img-m%d" % (m & 0xFF)
    if m == MAGIC_SCENE:
        return "cena-0x107"
    head = blob[:64]
    if head[:5] == b"<?xml" or head[:1] in (b"<", b"*"):
        return "texto"
    if all(32 <= c < 127 or c in (9, 10, 13) for c in head):
        return "texto"
    return "outro"


def cmd_list(path):
    d = parse(open(path, "rb").read())
    print("%s: ver=0x%x guid=%s entradas=%d toc_end=0x%x" % (
        path, d["ver"], guid_str(d["guid"]), len(d["entries"]), d["toc_end"]))
    total = 0
    for i, e in enumerate(d["entries"]):
        blob = d["raw"][e["off"]:e["off"] + e["size"]]
        total += e["size"]
        print("  [%3d] %-40s off=%-10d size=%-9d %s" % (
            i, e["name"], e["off"], e["size"], detect(blob)))
    print("  total dados: %d bytes" % total)


def guid_str(g):
    d1 = struct.unpack_from("<I", g, 0)[0]
    d2 = struct.unpack_from("<H", g, 4)[0]
    d3 = struct.unpack_from("<H", g, 6)[0]
    rest = g[8:16].hex()
    return "%08x-%04x-%04x-%s-%s" % (d1, d2, d3, g[8:10].hex(), g[10:16].hex())


def guid_bytes(s):
    p = s.split("-")
    b = struct.pack("<I", int(p[0], 16)) + struct.pack("<H", int(p[1], 16)) \
        + struct.pack("<H", int(p[2], 16)) + bytes.fromhex("".join(p[3:]))
    return b


def safe_name(name, used):
    nm = name.replace("\\", "_").replace("/", "_")
    if nm in used:
        base, ext = os.path.splitext(nm)
        i = 1
        while "%s_%d%s" % (base, i, ext) in used:
            i += 1
        nm = "%s_%d%s" % (base, i, ext)
    used.add(nm)
    return nm


def cmd_extract(path, outdir):
    d = parse(open(path, "rb").read())
    os.makedirs(outdir, exist_ok=True)
    used = set()
    meta = {"src": os.path.abspath(path), "zero": d["zero"], "ver": d["ver"],
            "guid": guid_str(d["guid"]), "order": []}
    for e in d["entries"]:
        blob = d["raw"][e["off"]:e["off"] + e["size"]]
        fn = safe_name(e["name"], used)
        with open(os.path.join(outdir, fn), "wb") as f:
            f.write(blob)
        meta["order"].append({"name": e["name"], "file": fn,
                              "fmt": detect(blob), "size": e["size"]})
    with open(os.path.join(outdir, "meta.json"), "w") as f:
        json.dump(meta, f, indent=1, ensure_ascii=False)
    print("extraido: %d entradas -> %s" % (len(meta["order"]), outdir))
    fams = {}
    for o in meta["order"]:
        fams[o["fmt"]] = fams.get(o["fmt"], 0) + 1
    print("familias:", ", ".join("%s x%d" % kv for kv in sorted(fams.items())))


def cmd_pack(indir, outpath):
    with open(os.path.join(indir, "meta.json")) as f:
        meta = json.load(f)
    blobs = []
    for o in meta["order"]:
        with open(os.path.join(indir, o["file"]), "rb") as f:
            blobs.append(f.read())
    n = len(blobs)
    toc_end = 0x1C + 56 * n
    out = bytearray()
    out += struct.pack("<II", meta["zero"], meta["ver"])
    out += guid_bytes(meta["guid"])
    out += struct.pack("<I", n)
    off = toc_end
    for o, blob in zip(meta["order"], blobs):
        nm = o["name"].encode("utf-8")
        if len(nm) > 47:
            raise ValueError("nome longo demais: %r" % o["name"])
        field = nm + b"\0" + b"\xfe" * (47 - len(nm))
        out += field
        out += struct.pack("<II", off, len(blob))
        off += len(blob)
    assert len(out) == toc_end, (len(out), toc_end)
    for blob in blobs:
        out += blob
    with open(outpath, "wb") as f:
        f.write(out)
    print("pack: %d entradas, %d bytes -> %s" % (n, len(out), outpath))


def main(argv):
    if len(argv) >= 3 and argv[1] == "list":
        cmd_list(argv[2])
    elif len(argv) >= 4 and argv[1] == "extract":
        cmd_extract(argv[2], argv[3])
    elif len(argv) >= 4 and argv[1] == "pack":
        cmd_pack(argv[2], argv[3])
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
