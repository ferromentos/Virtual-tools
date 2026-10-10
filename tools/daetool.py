#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
daetool.py — formato .dae binario do Virtual Pool 4 (versao 0x107)

Formato decodificado de ssMeshDX::Create / MeshFactory (vp4onlineS.exe):

  u32  version = 0x107
  u32  defaultVertexType      (sobrescrito por objeto)
  u32  unknown (9 no garage_spec / verifique o original)
  u32  numMaterials
  por material:
      f32  params[17]                       (0x44 bytes)
      9 slots de string:  u32 len; if len>0: u32 lenB; u8 texto[len] (com NUL)
  u32  numObjects
  por objeto:
      u32  numSubmeshes
      u8   header[0x80]
      u32  vertexType (0..9)
      u32  numVertices
      u8   vertices[numVertices * stride(vertexType)]
      u8   header2[0x30]
      por submesh:
          u16  ids[numSubmeshes]      (lido por submesh, repetido)
          u32  indexCount
          u32  matIdx[10]             (idx do registro + 9 slots de textura)
          u32  indices[indexCount]

Vertex types (stride): 0:32 1:24 2:36 3:40 4:28 5:36 6:48 7:56 8:64 9:24

O jogo (MeshFactory, flag=3) converte o arquivo Z-up trocando y<->z de
posicoes/normais e invertendo o winding de cada triangulo.

Uso:
  daetool.py info     arquivo.dae
  daetool.py roundtrip arquivo.dae          (valida parse+write byte-a-byte)
  daetool.py convert  modelo.obj saida.dae [--no-swap] [--map mapa.json]
"""
import struct
import sys
import os
import json
import argparse
import math
import re
import subprocess

STRIDE = {0: 32, 1: 24, 2: 36, 3: 40, 4: 28, 5: 36, 6: 48, 7: 56, 8: 64, 9: 24}

# texel branco (255,255,255) do garagelighting.tga (bloco 944,860 de 4096x4096)
LIGHTMAP_UV = (0.9224, 0.8403)


# ----------------------------------------------------------------- parse/write
def parse(data):
    o = [0]

    def u32():
        v = struct.unpack_from('<I', data, o[0])[0]
        o[0] += 4
        return v

    def f32():
        v = struct.unpack_from('<f', data, o[0])[0]
        o[0] += 4
        return v

    def u16():
        v = struct.unpack_from('<H', data, o[0])[0]
        o[0] += 2
        return v

    def raw(n):
        v = data[o[0]:o[0] + n]
        o[0] += n
        return v

    m = {}
    m['ver'] = u32()
    m['A'] = u32()
    m['B'] = u32()
    num_mats = u32()
    mats = []
    for _ in range(num_mats):
        fl = [f32() for _ in range(17)]
        slots = []
        for _ in range(9):
            ln = u32()
            if ln:
                ln_b = u32()
                txt = raw(ln)
                slots.append((ln_b, txt))
            else:
                slots.append(None)
        mats.append((fl, slots))
    m['mats'] = mats
    num_objects = u32()
    objs = []
    for _ in range(num_objects):
        ob = {}
        ob['numSub'] = u32()
        ob['header'] = raw(0x80)
        ob['vtype'] = u32()
        ob['numVerts'] = u32()
        ob['verts'] = raw(ob['numVerts'] * STRIDE[ob['vtype']])
        ob['header2'] = raw(0x30)
        subs = []
        for _ in range(ob['numSub']):
            sb = {}
            sb['ids'] = [u16() for _ in range(ob['numSub'])]
            sb['idxCount'] = u32()
            sb['matIdx'] = [u32() for _ in range(10)]
            sb['indices'] = [u32() for _ in range(sb['idxCount'])]
            subs.append(sb)
        ob['subs'] = subs
        objs.append(ob)
    m['objs'] = objs
    m['consumed'] = o[0]
    m['total'] = len(data)
    return m


def write(m):
    out = bytearray()

    def pu32(v):
        out.extend(struct.pack('<I', v & 0xFFFFFFFF))

    def pf32(v):
        out.extend(struct.pack('<f', float(v)))

    def pu16(v):
        out.extend(struct.pack('<H', v & 0xFFFF))

    pu32(m['ver'])
    pu32(m['A'])
    pu32(m['B'])
    pu32(len(m['mats']))
    for fl, slots in m['mats']:
        for v in fl:
            pf32(v)
        for s in slots:
            if s is None:
                pu32(0)
            else:
                ln_b, txt = s
                pu32(len(txt))
                pu32(ln_b)
                out.extend(txt)
    pu32(len(m['objs']))
    for ob in m['objs']:
        pu32(ob['numSub'])
        out.extend(ob['header'])
        pu32(ob['vtype'])
        pu32(ob['numVerts'])
        out.extend(ob['verts'])
        out.extend(ob['header2'])
        for sb in ob['subs']:
            for i in sb['ids']:
                pu16(i)
            pu32(sb['idxCount'])
            for i in sb['matIdx']:
                pu32(i)
            for i in sb['indices']:
                pu32(i)
    return bytes(out)


# -------------------------------------------------------------------- helpers
def verts_iter(m, ob):
    """Yield tuplas float por vertice conforme o tipo."""
    st = STRIDE[ob['vtype']]
    n = st // 4
    for i in range(ob['numVerts']):
        yield struct.unpack_from('<%df' % n, ob['verts'], i * st)


def txt(s):
    if s is None:
        return None
    return s[1].rstrip(b'\x00').decode('latin-1', 'replace')


def cmd_info(path):
    data = open(path, 'rb').read()
    m = parse(data)
    print('arquivo: %s (%d bytes, consumidos %d%s)' % (
        path, m['total'], m['consumed'],
        '' if m['consumed'] == m['total'] else '  *** SOBRAM %d bytes' % (m['total'] - m['consumed'])))
    print('version=0x%X  A=%d  B=%d  materiais=%d  objetos=%d' % (
        m['ver'], m['A'], m['B'], len(m['mats']), len(m['objs'])))
    print('\n--- materiais ---')
    for i, (fl, slots) in enumerate(m['mats']):
        names = ['%d:%s' % (j, txt(s)) for j, s in enumerate(slots) if s]
        print('  [%d] diff=(%.2f,%.2f,%.2f,%.2f) spec?=(%.2f,%.2f,%.2f) floats[8..12]=(%.2f,%.2f,%.2f,%.2f) f16=%.1f  %s' % (
            i, fl[0], fl[1], fl[2], fl[3], fl[8], fl[9], fl[10], fl[12], fl[13], fl[14], fl[15], fl[16],
            ' '.join(names)))
    print('\n--- objetos ---')
    for i, ob in enumerate(m['objs']):
        mn = [1e30] * 3
        mx = [-1e30] * 3
        for v in verts_iter(m, ob):
            for k in range(3):
                mn[k] = min(mn[k], v[k])
                mx[k] = max(mx[k], v[k])
        print('  obj[%d] submeshes=%d vtype=%d stride=%d verts=%d  bbox=(%.1f,%.1f,%.1f)..(%.1f,%.1f,%.1f)' % (
            i, ob['numSub'], ob['vtype'], STRIDE[ob['vtype']], ob['numVerts'],
            mn[0], mn[1], mn[2], mx[0], mx[1], mx[2]))
        hdr = struct.unpack('<%df' % (0x80 // 4), ob['header'])
        print('        header f32[0..7]=' + ' '.join('%.2f' % x for x in hdr[:8]))
        print('        header u32[0..7]=' + ' '.join('%d' % x for x in struct.unpack('<8I', ob['header'][:32])))
        for j, sb in enumerate(ob['subs']):
            imn = min(sb['indices']) if sb['indices'] else -1
            imx = max(sb['indices']) if sb['indices'] else -1
            print('        sub[%d] idx=%d (min=%d max=%d) matIdx=%s ids=%s' % (
                j, sb['idxCount'], imn, imx, sb['matIdx'][0], sb['ids']))


def cmd_roundtrip(path):
    data = open(path, 'rb').read()
    m = parse(data)
    out = write(m)
    if out == data:
        print('ROUNDTRIP OK: %s (%d bytes, %d objetos, %d materiais)' % (
            path, len(data), len(m['objs']), len(m['mats'])))
        return 0
    print('ROUNDTRIP FALHOU: %d -> %d bytes' % (len(data), len(out)))
    n = min(len(data), len(out))
    for i in range(n):
        if data[i] != out[i]:
            print('primeira diferenca em 0x%X: %02X vs %02X' % (i, data[i], out[i]))
            break
    return 1


# --------------------------------------------------------------- conversor OBJ
def parse_obj(path):
    verts, uvs, norms = [], [], []
    objects = []  # (name, [faces])  face = [(vi,ti,ni), ...] 0-based, -1 = ausente
    cur = None
    cur_m = None   # usemtl vigente (persiste entre objetos, como no OBJ)
    for line in open(path, 'r', errors='replace'):
        p = line.split()
        if not p or p[0] == '#':
            continue
        if p[0] == 'v':
            verts.append((float(p[1]), float(p[2]), float(p[3])))
        elif p[0] == 'vt':
            uvs.append((float(p[1]), float(p[2])))
        elif p[0] == 'vn':
            norms.append((float(p[1]), float(p[2]), float(p[3])))
        elif p[0] == 'usemtl':
            cur_m = line[7:].strip()
        elif p[0] in ('o', 'g'):
            cur = {'name': p[1] if len(p) > 1 else 'obj%d' % len(objects),
                   'faces': [], 'mats': []}
            objects.append(cur)
        elif p[0] == 'f':
            if cur is None:
                cur = {'name': 'default', 'faces': [], 'mats': []}
                objects.append(cur)
            idx = []
            for tok in p[1:]:
                a = tok.split('/')

                def rel(s, n):
                    if not s:
                        return -1
                    i = int(s)
                    return i - 1 if i > 0 else n + i
                vi = rel(a[0], len(verts))
                ti = rel(a[1], len(uvs)) if len(a) > 1 else -1
                ni = rel(a[2], len(norms)) if len(a) > 2 else -1
                idx.append((vi, ti, ni))
            # triangula em leque
            for k in range(1, len(idx) - 1):
                cur['faces'].append((idx[0], idx[k], idx[k + 1]))
                cur['mats'].append(cur_m)
    return verts, uvs, norms, objects


def load_mtl(obj_path):
    """Le o .mtl do OBJ: nome do material -> caminho do map_Kd."""
    mtl = {}
    obj_dir = os.path.dirname(os.path.abspath(obj_path))
    for line in open(obj_path, 'r', errors='replace'):
        if line.startswith('mtllib'):
            mtl_path = os.path.join(obj_dir, line.split(None, 1)[1].strip())
            break
    else:
        return mtl
    if not os.path.exists(mtl_path):
        return mtl
    cur = None
    for line in open(mtl_path, 'r', errors='replace'):
        s = line.strip()
        if s.startswith('newmtl '):
            cur = s[7:].strip()
            mtl.setdefault(cur, {})
        elif s.startswith('map_Kd ') and cur:
            mtl[cur]['kd'] = s[7:].strip()
    return mtl


def load_poolians_fallback(obj_path):
    """Fallback: room1.material do editor original (objeto -> textura difusa)."""
    tex_dir = os.path.join(os.path.dirname(os.path.abspath(obj_path)),
                           'poolians_room1_tex')
    mat_file = os.path.join(tex_dir, 'room1.material')
    fb = {}
    if not os.path.exists(mat_file):
        return fb
    txt = open(mat_file, 'r', errors='replace').read()
    for m in re.finditer(r'material\s+(\S+)\s*\{', txt):
        start = m.end()
        nxt = txt.find('\nmaterial ', start)
        block = txt[start: nxt if nxt > 0 else len(txt)]
        texs = re.findall(r'texture\s+(\S+)', block)
        if texs:
            fb[m.group(1)] = os.path.join(tex_dir, texs[0])
    return fb


def ci_find(dirpath, name):
    """Prova nome (case-insensitive) em dirpath; None se nao achar."""
    if not dirpath or not os.path.isdir(dirpath):
        return None
    low = name.lower()
    for f in os.listdir(dirpath):
        if f.lower() == low:
            return os.path.join(dirpath, f)
    return None


def resolve_texture(obj_dir, obj_name, mtl_mat, mtl_map, fallback):
    """Resolve a textura difusa de um objeto:
    map_Kd exato -> case-insensitive -> room1.material original -> None."""
    entry = mtl_map.get(mtl_mat) or {}
    path = entry.get('kd')
    if path:
        d, b = os.path.split(path)
        cand = os.path.normpath(os.path.join(obj_dir, d, b))
        if os.path.exists(cand):
            return cand
        cand = ci_find(os.path.dirname(cand), b)
        if cand:
            return cand
    path = fallback.get(obj_name)
    if path and os.path.exists(path):
        return path
    if path:
        cand = ci_find(os.path.dirname(path), os.path.basename(path))
        if cand:
            return cand
    return None


# nomes stock do garage usados como "slots" para o conteudo das texturas do
# usuario (troca de conteudo ja provada: o jogo carrega por nome do ark).
# NUNCA usar como slot: garagelighting (texel branco do lightmap),
# _specular (spec mesh), garage_icon (UI), concretefloor (piso da mesa).
TEXTURE_TARGETS = {
    'floor':     'concretefloor.tga',   # ancora da mesa: NOME precisa ser este
    'stone':     'wall1.tga',
    'wood3':     'wall2.tga',
    'wood4':     'box_diff.tga',
    'wood6':     'cabinet_diff.tga',
    'skin1':     'dogfood_diff.tga',
    'chfoot':    'workbench.tga',
    'clock1':    'clock_diff.tga',
    'door1':     'door_diff.tga',
    'drink3':    'cup.tga',
    'drink4a':   'logo01.tga',
    'drink5a':   'logo02.tga',
    'drink6a':   'logo03.tga',
    'drink7a':   'logo04.tga',
    'ldz':       'logo03_crown.tga',
    'pic0':      'posters_diff.tga',
    'pic4':      '_logotiger.tga',
    'pic5':      '_logoviking.tga',
    'pic6':      'paint_diff.tga',
    'pic7':      'litter_diff.tga',
    'sb1':       'pizza_diff.tga',
    'sofa2':     'stool_diff.tga',
    'tv':        'room_diff.tga',
    'lightbase': 'lamp_diff.tga',
    'lightmap':  'insulation.tga',
    'f3':        'garagefloor_diff.tga',
    'void':      '_floorstain02.tga',
}
# pool para texturas sem nome mapeado (e o cinza de material sem textura)
TEX_POOL = ['framesupport.tga', 'stepladder_diff.tga', 'toolcart_diff.tga',
            'toolbox_diff.tga', 'weightset.tga', 'tire_diff.tga',
            'gardentools_diff.tga', 'electrical_diff.tga',
            '_floorstain01.tga', '_tireskid_diff.tga', '2x4box.tga',
            '_hangingtools_diff.tga', 'fridge_diff.tga',
            'garagedoor_diff.tga', 'ceiling_diff.tga', 'frame1.tga',
            'garagewall01_diff.tga', 'garagewall02_diff.tga', 'lamp_emis.tga']
GRAY_SRC = 'xc:#cccccc'   # material sem textura (Kd 0.8 do .mtl)

# NUNCA sobrescrever (lightmap, spec mesh, UI e o slot do piso da mesa)
RESERVED_TEX = {'garagelighting.tga', '_specular.tga', 'garage_icon.tga',
                'concretefloor.tga'}


def build_pool(out_dir):
    """Slots livres = .tga que ja existem no pacote da sala alvo (troca de
    conteudo provada: o jogo carrega por nome). Sem extracao no dir de saida,
    cai no TEX_POOL fixo do garage."""
    if os.path.isdir(out_dir):
        names = sorted([f for f in os.listdir(out_dir)
                        if f.lower().endswith('.tga')
                        and f.lower() not in RESERVED_TEX], key=str.lower)
        if names:
            return names
    return list(TEX_POOL)


def nearest_pot(v, lo=32, hi=512):
    p = 2 ** round(math.log(max(v, 2), 2))
    return max(lo, min(hi, p))


def vp4_tga(src_path, dst_path, hi=512):
    """Converte imagem -> .tga VP4 (wrapper 52B + DDS DXT1 + mips p/ 1x1).
    Alfa vira DXT1 1-bit (como os decals stock). hi = POT maximo (512
    padrao; lightmaps aceitam 4096 como o garagelighting stock)."""
    probe = subprocess.run(
        ['magick', src_path, '-format', '%w %h %[channels]', 'info:'],
        capture_output=True, text=True, check=True).stdout.split()
    w, h = int(probe[0]), int(probe[1])
    has_alpha = 'a' in probe[2]
    nw, nh = nearest_pot(w, hi=hi), nearest_pot(h, hi=hi)
    tmp = dst_path + '.tmp.dds'
    cmd = ['magick', src_path, '-resize', '%dx%d!' % (nw, nh),
           '-define', 'dds:compression=dxt1',
           '-define', 'dds:mipmaps=%d' % int(math.log(max(nw, nh), 2))]
    if has_alpha:
        cmd += ['-define', 'dds:alpha-bitdepth=1']
    cmd += [tmp]
    subprocess.run(cmd, check=True, capture_output=True)
    dds = open(tmp, 'rb').read()
    os.remove(tmp)
    if dds[:4] != b'DDS ':
        raise SystemExit('ImageMagick nao gerou DDS: %s' % src_path)
    hdr = struct.pack('<IHHIII', 0xabcd0007, 0x0101, 0, nw, nh, 3) + b'\x00' * 32
    open(dst_path, 'wb').write(hdr + dds)
    return nw, nh, has_alpha


def pack_vert(vtype, pos, nrm, uv, uv2=None):
    """Empacota um vertice no formato do tipo (coords ja em espaco de arquivo).
    V invertido: OBJ/Blender tem V=0 embaixo, D3D/jogo V=0 no topo — sem
    isso toda textura sai de cabeca pra baixo.
    uv2 = par de UV do lightmap (None = texel branco do garagelighting)."""
    uv = (uv[0], 1.0 - uv[1])
    if uv2 is None:
        uv2 = (LIGHTMAP_UV[0], LIGHTMAP_UV[1])
    if vtype == 0:   # pos3 normal3 uv2
        return struct.pack('<8f', *pos, *nrm, *uv)
    if vtype == 1:   # pos3 color(u32) uv2
        return struct.pack('<6fI2f', *pos, 0xFFFFFFFF, *uv)
    if vtype == 2:   # pos3 normal3 color4f uv2
        return struct.pack('<12f', *pos, *nrm, 1.0, 1.0, 1.0, 1.0, *uv)
    if vtype == 3:   # pos3 normal3 uv2 uv2
        return struct.pack('<12f', *pos, *nrm, *uv, *uv)
    if vtype == 4:   # pos3 uv2 uv2(lightmap)  <- tipos do jogo (garage)
        # UV2 aponta p/ um texel BRANCO (255) do garagelighting.tga:
        # o centro (0.5,0.5) e PRETO -> quarto inteiro ficava preto.
        # Com --uv2/--lightmap o uv2 vem do atlas assado no Blender.
        return struct.pack('<7f', *pos, uv[0], uv[1], uv2[0], uv2[1])
    if vtype == 9:   # pos3 normal3
        return struct.pack('<6f', *pos, *nrm)
    raise SystemExit('tipo de vertice %d nao suportado pelo conversor' % vtype)


def dominant_axis(nrm):
    ax = max(range(3), key=lambda k: abs(nrm[k]))
    return ax, (1 if nrm[ax] >= 0 else -1)


def cmd_convert(obj_path, dae_path, do_swap=False, map_path=None, vtype=None,
                tex_out=True, skip=None, floor=None, uv2_path=None,
                lightmap_path=None):
    verts, uvs, norms, objects = parse_obj(obj_path)
    if not objects:
        raise SystemExit('obj sem objetos')
    if skip:
        pats = [s.strip().lower() for s in skip.split(',') if s.strip()]
        n0 = len(objects)
        objects = [o for o in objects
                   if not any(p in o['name'].lower() for p in pats)]
        print('  --skip: %d objetos descartados (%d restantes)' % (
            n0 - len(objects), len(objects)))
        if not objects:
            raise SystemExit('--skip removeu todos os objetos')

    # UV2 do lightmap: segundo OBJ exportado do Blender com a camada de UV
    # 'Lightmap' ativa (mesma geometria e ordem de faces; so os vt mudam).
    # As UV2 sao casadas POR CANTO entre os dois arquivos (com checagem de
    # posicao p/ abortar se a ordem nao bater) e levam o mesmo flip de V do
    # difuso (convencao OBJ V=0 embaixo vs D3D V=0 no topo).
    uv2_faces = None
    if uv2_path:
        verts2, uvs2, _, objects2 = parse_obj(uv2_path)
        if skip:
            objects2 = [o for o in objects2
                        if not any(p in o['name'].lower() for p in pats)]
        # o exportador do Blender reordena (alfabetico) — casa por NOME
        by_name2 = {}
        for o in objects2:
            if o['name'] in by_name2:
                raise SystemExit('--uv2: nome de objeto duplicado no 2o OBJ: %s'
                                 % o['name'])
            by_name2[o['name']] = o
        falta = [o['name'] for o in objects if o['name'] not in by_name2]
        if falta:
            raise SystemExit(
                '--uv2: objetos do 2o OBJ nao encontrados: %s' % ', '.join(falta[:5]))
        objects2 = [by_name2[o['name']] for o in objects]
        uv2_faces = []
        for ob, ob2 in zip(objects, objects2):
            if len(ob['faces']) != len(ob2['faces']):
                raise SystemExit('--uv2: contagem de faces nao bate em %s'
                                 % ob['name'])
            faces2 = []
            for f1, f2 in zip(ob['faces'], ob2['faces']):
                if len(f1) != len(f2):
                    raise SystemExit('--uv2: face com numero de cantos '
                                     'diferente em %s' % ob['name'])
                c2 = []
                for (vi1, _, _), (vi2, ti2, _) in zip(f1, f2):
                    a, b = verts[vi1], verts2[vi2]
                    if max(abs(a[k] - b[k]) for k in range(3)) > 1e-3:
                        raise SystemExit(
                            '--uv2: geometria nao corresponde entre os OBJS '
                            '(v %.3f,%.3f,%.3f vs %.3f,%.3f,%.3f)' % (a + b))
                    u, v = uvs2[ti2] if 0 <= ti2 < len(uvs2) else (0.0, 0.0)
                    c2.append((u, 1.0 - v))
                faces2.append(c2)
            uv2_faces.append(faces2)
    # tipo padrao: vtype 4 (pos+uv+lightmap), o mesmo do garage stock
    if vtype is None:
        vtype = 4
    tex_map = None
    if map_path:
        tex_map = json.load(open(map_path))

    obj_dir = os.path.dirname(os.path.abspath(obj_path))
    mtl_map = load_mtl(obj_path)
    fallback = load_poolians_fallback(obj_path)

    # material maioria por objeto + textura difusa resolvida
    def obj_material(ob):
        ms = [m for m in ob['mats'] if m]
        if not ms:
            return None
        return max(set(ms), key=ms.count)

    # textura por objeto: (src_imagem, nome_slot_no_ark)
    # slots livres vem do proprio pacote alvo (dir de saida = ark extraido)
    tex_pool = build_pool(os.path.dirname(os.path.abspath(dae_path)))
    tex_by_obj = {}
    slot_by_src = {}    # src -> nome de slot
    used_slots = set()
    pool_i = [0]
    for ob in objects:
        mat = obj_material(ob)
        entry = mtl_map.get(mat) if mat else None
        src = None
        if entry is not None and 'kd' in entry:
            # material com textura no .mtl: resolve exato -> ci -> original
            path = entry['kd']
            d, b = os.path.split(path)
            src = os.path.normpath(os.path.join(obj_dir, d, b))
            if not os.path.exists(src):
                src = ci_find(os.path.dirname(src), b)
            if src is None:
                src = resolve_texture(obj_dir, ob['name'], mat, mtl_map, fallback)
        elif entry is not None:
            src = GRAY_SRC   # material no .mtl SEM textura (Kd 0.8 do usuario)
        else:
            # objeto sem material no .mtl: room1.material original
            src = resolve_texture(obj_dir, ob['name'], mat, mtl_map, fallback)
        if src is None:
            src = GRAY_SRC
        if src not in slot_by_src:
            base = ''
            if src != GRAY_SRC:
                base = os.path.splitext(os.path.basename(src))[0].lower()
                base = re.sub(r'[^a-z0-9]', '', base)
            slot = TEXTURE_TARGETS.get(base)
            if slot is None:
                while (pool_i[0] < len(tex_pool)
                       and tex_pool[pool_i[0]] in used_slots):
                    pool_i[0] += 1
                if pool_i[0] >= len(tex_pool):
                    raise SystemExit(
                        'pool de nomes de textura esgotado: %d slots livres '
                        'no pacote alvo (extraia o .ark stock da sala no dir '
                        'de saida ou use --map)' % len(tex_pool))
                slot = tex_pool[pool_i[0]]
            if slot in used_slots:
                raise SystemExit('slot de textura duplicado: %s (%s)' % (slot, src))
            used_slots.add(slot)
            slot_by_src[src] = slot
        tex_by_obj[ob['name']] = (src, slot_by_src[src])

    # um pool de vertices POR OBJETO (como o stock: N objs x numSub=1;
    # numSub=21 num objeto so nunca foi validado no jogo)
    obj_pools = []   # [(blob, idxs)] na ordem dos objetos
    mat_info = []  # (name, texture_slot, src_imagem)

    up_area = [0.0] * len(objects)   # area horizontal voltada pra cima (Y-up)
    uv2_vals = []                    # uv2 reais usados (stats)
    for oi, ob in enumerate(objects):
        pool = []
        lookup = {}
        idxs = []
        ob_u2 = uv2_faces[oi] if uv2_faces else None
        for fi, face in enumerate(ob['faces']):
            out_face = []
            for ci, (vi, ti, ni) in enumerate(face):
                t2 = ob_u2[fi][ci] if ob_u2 else None
                key = (vi, ti, ni, t2) if t2 else (vi, ti, ni)
                if key not in lookup:
                    p = verts[vi]
                    n = norms[ni] if 0 <= ni < len(norms) else (0.0, 1.0, 0.0)
                    t = uvs[ti] if 0 <= ti < len(uvs) else (0.0, 0.0)
                    if do_swap:        # opcional: gravar Z-up p/ caminho com flag3
                        p = (p[0], p[2], p[1])
                        n = (n[0], n[2], n[1])
                    lookup[key] = len(pool)
                    if t2:
                        uv2_vals.append(t2)
                    pool.append(pack_vert(vtype, p, n, t, uv2=t2))
                out_face.append(lookup[key])
            # salas carregam SEM flag3: sem swap nao ha inversao de winding
            if do_swap:
                idxs.append([out_face[0], out_face[2], out_face[1]])
            else:
                idxs.append(out_face)
            # acumula a area de faces horizontais voltadas pra cima
            if len(face) == 3:
                p0, p1, p2 = [verts[f[0]] for f in face]
                ux, uy, uz = p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2]
                vx, vy, vz = p2[0]-p0[0], p2[1]-p0[1], p2[2]-p0[2]
                cx, cy, cz = uy*vz-uz*vy, uz*vx-ux*vz, ux*vy-uy*vx
                L = (cx*cx + cy*cy + cz*cz) ** 0.5
                if L > 1e-12 and cy > 0.98 * L:
                    up_area[oi] += 0.5 * L
        # textura difusa vem do .mtl do usuario (com fallback p/ room1.material)
        src, slot = tex_by_obj[ob['name']]
        if tex_map and ob['name'] in tex_map:
            slot = tex_map[ob['name']]
        mat_info.append((ob['name'], slot, src))
        obj_pools.append((b''.join(pool), idxs))

    # o jogo ancora a mesa no piso identificado pelo XML textureNameFloor
    # (ConcreteFloor.tga): o grupo de MAIOR area horizontal voltada pra cima
    # vira 'ConcreteFloor'/'ConcreteFloor.tga' (nome+textura iguais ao stock).
    # sem isso a mesa flutua several unidades acima do chao na tela.
    floor_i = max(range(len(objects)), key=lambda i: up_area[i])
    if floor:
        cand = [i for i, o in enumerate(objects) if o['name'] == floor]
        if not cand:
            cand = [i for i, o in enumerate(objects)
                    if floor.lower() in o['name'].lower()]
        if not cand:
            raise SystemExit('--floor: objeto nao encontrado: %s' % floor)
        floor_i = cand[0]
        print('  piso forcado (--floor): %s (area up=%.0f)' % (
            objects[floor_i]['name'], up_area[floor_i]))
    fixed = []
    for i, (name, tex, src) in enumerate(mat_info):
        if i == floor_i:
            fixed.append(('ConcreteFloor', 'ConcreteFloor.tga', src))
        elif tex == 'concretefloor.tga':
            fixed.append(('Ceiling', tex, src))
        else:
            fixed.append((name, tex, src))
    mat_info = fixed

    # grava as texturas convertidas (VP4: wrapper 52B + DDS DXT1 + mips)
    # no mesmo diretorio do .dae de saida (fonte do pack)
    # nomes de arquivo no pack sao todos lowercase (o lookup do jogo e
    # case-insensitive; slots do DAE mantem a grafia stock)
    tex_out_dir = os.path.dirname(os.path.abspath(dae_path))
    written = {}
    for name, tex, src in mat_info:
        if tex in written:
            continue
        dst = os.path.join(tex_out_dir, tex.lower())
        if src == GRAY_SRC:
            tmp_png = dst + '.tmp.png'
            subprocess.run(['magick', '-size', '32x32', src, tmp_png], check=True)
            vp4_tga(tmp_png, dst)
            os.remove(tmp_png)
            written[tex] = ('<cinza>', 32, 32, False)
        else:
            written[tex] = (os.path.basename(src),) + vp4_tga(src, dst)

    # lightmap assado no Blender: substitui o conteudo do garagelighting.tga
    # (slots 2/7 dos materiais ja apontam p/ ele — conteudo trocado e seguro,
    # mesmo metodo das texturas difusas; aceita ate 4096 como o stock)
    if lightmap_path:
        ldst = os.path.join(tex_out_dir, 'garagelighting.tga')
        lw, lh, la = vp4_tga(lightmap_path, ldst, hi=4096)
        written['garagelighting.tga'] = (os.path.basename(lightmap_path),
                                         lw, lh, la)
        print('  lightmap: %s -> garagelighting.tga (%dx%d)'
              % (os.path.basename(lightmap_path), lw, lh))

    # 17 floats iguais ao variante mais comum do garage stock
    # (block2 0.1 = intensidade; 0 deixava o quarto sem luz)
    base_fl = [0.6, 0.6, 0.6, 1.0, 0.1, 0.1, 0.1, 1.0,
               0.4, 0.4, 0.4, 1.0, 0.0, 0.0, 0.0, 1.0, 1000.0]
    mats = []
    for name, tex, src in mat_info:
        slots = [None] * 9
        slots[0] = (0, (name + '\x00').encode('latin-1'))
        slots[1] = (0, (tex + '\x00').encode('latin-1'))
        slots[2] = (0, b'GarageLighting.tga\x00')
        slots[7] = (0, b'GarageLighting.tga\x00')
        mats.append((list(base_fl), slots))

    # header de objeto: copia do garage stock se disponivel
    header = b'\x00' * 0x80
    header2 = b'\x00' * 0x30
    stock_path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                              'garage_room', 'garage.dae')
    A = vtype
    if os.path.exists(stock_path):
        sm = parse(open(stock_path, 'rb').read())
        if sm['objs']:
            header = sm['objs'][0]['header']
            header2 = sm['objs'][0]['header2']

    objs = []
    for si, (blob, idxs) in enumerate(obj_pools):
        flat = [i for f in idxs for i in f]
        sub = dict(ids=[0], idxCount=len(flat),
                   matIdx=[si, si, si, 0, 0, 0, 0, 0, 0, 0], indices=flat)
        objs.append(dict(numSub=1, header=header, vtype=vtype,
                         numVerts=len(blob) // STRIDE[vtype], verts=blob,
                         header2=header2, subs=[sub]))

    m = dict(ver=0x107, A=A, B=len(mats), mats=mats, objs=objs)
    data = write(m)
    open(dae_path, 'wb').write(data)
    tri = sum(sub['idxCount'] // 3 for o in objs for sub in o['subs'])
    print('convertido: %s -> %s' % (obj_path, dae_path))
    print('  objetos=%d submeshes=%d verts=%d triangulos=%d swap=%s vtype=%d' % (
        len(objs), len(objs), sum(o['numVerts'] for o in objs), tri, do_swap, vtype))
    if uv2_vals:
        us = [q[0] for q in uv2_vals]
        vs = [q[1] for q in uv2_vals]
        print('  lightmap UV2: verts=%d  U[%.4f,%.4f]  V[%.4f,%.4f]' % (
            len(uv2_vals), min(us), max(us), min(vs), max(vs)))
    for name, tex, src in mat_info:
        s = src if src == GRAY_SRC else os.path.basename(src)
        print('    %-24s -> %-22s <- %s' % (name, tex, s))


def cmd_split(obj_path, out_path, floor_mat=None, flip=None):
    """Regrupa um OBJ: um objeto POR material (o formato VP4 tem 1 material
    por submesh, como o garage stock). O .mtl e reaproveitado sem mudanca.

    --floor MATERIAL: separa faces horizontais proximas do plano sob a
      origem (0,0) num objeto proprio chamado 'piso' (ancora da mesa).
    --flip TRECHO: inverte o winding dos objetos cujo nome contem TRECHO
      (ex.: skybox esferico, visto de dentro)."""
    vlines, face_lines, verts = [], [], []
    mtllib = None
    cur_o, cur_m = 'default', None
    for line in open(obj_path, 'r', errors='replace'):
        p = line.split()
        if not p or p[0] == '#':
            continue
        if p[0] == 'mtllib':
            mtllib = line
        elif p[0] == 'v':
            vlines.append(line.rstrip('\n'))
            verts.append((float(p[1]), float(p[2]), float(p[3])))
        elif p[0] in ('vt', 'vn'):
            vlines.append(line.rstrip('\n'))
        elif p[0] in ('o', 'g'):
            cur_o = '_'.join(p[1:]) if len(p) > 1 else 'obj%d' % len(vlines)
            cur_m = None
        elif p[0] == 'usemtl':
            cur_m = '_'.join(p[1:])
        elif p[0] == 'f':
            face_lines.append((cur_o, cur_m, list(p[1:])))
    if not face_lines:
        raise SystemExit('obj sem faces')

    def tok_vi(tok):
        i = int(tok.split('/')[0])
        return i - 1 if i > 0 else len(verts) + i

    groups = []   # [nome, material, [faces]]
    gidx = {}
    for cur_o, cur_m, toks in face_lines:
        name = '%s__%s' % (cur_o, cur_m if cur_m else 'nomat')
        if name not in gidx:
            gidx[name] = len(groups)
            groups.append([name, cur_m, []])
        groups[gidx[name]][2].append(toks)

    def tris_of(toks):
        idx = [tok_vi(t) for t in toks]
        return [(verts[idx[0]], verts[idx[k]], verts[idx[k + 1]])
                for k in range(1, len(idx) - 1)]

    if floor_mat:
        cand = []
        for gi, (name, mat, fcs) in enumerate(groups):
            if mat == floor_mat:
                cand += [(gi, fi) for fi in range(len(fcs))]
        if not cand:
            raise SystemExit('--floor: nenhuma face do material %s' % floor_mat)

        def ray_top(px, pz, tri_list):
            best = None
            for a, b, c in tri_list:
                def sgn(ox, oz, ux, uz, vx, vz):
                    return (ox - vx) * (uz - vz) - (ux - vx) * (oz - vz)
                d1 = sgn(px, pz, a[0], a[2], b[0], b[2])
                d2 = sgn(px, pz, b[0], b[2], c[0], c[2])
                d3 = sgn(px, pz, c[0], c[2], a[0], a[2])
                if ((d1 < 0) or (d2 < 0) or (d3 < 0)) and \
                   ((d1 > 0) or (d2 > 0) or (d3 > 0)):
                    continue
                den = (b[2] - c[2]) * (a[0] - c[0]) + \
                    (c[0] - b[0]) * (a[2] - c[2])
                if abs(den) < 1e-12:
                    continue
                l1 = ((b[2] - c[2]) * (px - c[0]) +
                      (c[0] - b[0]) * (pz - c[2])) / den
                l2 = ((c[2] - a[2]) * (px - c[0]) +
                      (a[0] - c[0]) * (pz - c[2])) / den
                y = l1 * a[1] + l2 * b[1] + (1 - l1 - l2) * c[1]
                if best is None or y > best:
                    best = y
            return best

        y0 = ray_top(0.0, 0.0,
                     [t for gi, fi in cand for t in tris_of(groups[gi][2][fi])])
        if y0 is None:
            raise SystemExit('--floor: nada sob a origem (0,0) no material %s'
                             % floor_mat)
        # chao em que a mesa encosta = componente CONEXA de faces
        # horizontais que contem a origem (rampas entram por vizinhanca;
        # plataformas desconexas em outras alturas ficam de fora)
        up_faces = []   # (gi, fi, cantos, centroide_y)
        for gi, fi in cand:
            toks = groups[gi][2][fi]
            corners = [verts[tok_vi(t)] for t in toks]
            # orientacao do poligono INTEIRO (Newell): leque em triangulo
            # falha em hexagonos nao-convexos comuns em geometria de nivel
            nx = ny = nz = 0.0
            for i in range(len(corners)):
                a, b = corners[i], corners[(i + 1) % len(corners)]
                nx += (a[1] - b[1]) * (a[2] + b[2])
                ny += (a[2] - b[2]) * (a[0] + b[0])
                nz += (a[0] - b[0]) * (a[1] + b[1])
            L = (nx * nx + ny * ny + nz * nz) ** 0.5
            if not (L > 1e-12 and ny > 0.8 * L):
                continue
            cy_ = sum(p[1] for p in corners) / len(corners)
            if not (y0 - 2.5 <= cy_ <= y0 + 8.0):
                continue
            up_faces.append((gi, fi, corners, cy_))

        def edge_key(p, q):
            a = (round(p[0], 3), round(p[1], 3), round(p[2], 3))
            b = (round(q[0], 3), round(q[1], 3), round(q[2], 3))
            return (a, b) if a <= b else (b, a)

        from collections import defaultdict, deque
        emap = defaultdict(list)
        for k, (gi, fi, corners, _) in enumerate(up_faces):
            for i in range(len(corners)):
                emap[edge_key(corners[i], corners[(i + 1) % len(corners)])].append(k)
        start = None
        for k, (gi, fi, corners, _) in enumerate(up_faces):
            if ray_top(0.0, 0.0, tris_of(groups[gi][2][fi])) is not None:
                start = k
                break
        if start is None:
            raise SystemExit('--floor: face horizontal sob a origem nao '
                             'encontrada no material %s' % floor_mat)
        keep = {start}
        queue = deque([start])
        while queue:
            k = queue.popleft()
            gi, fi, corners, cyk = up_faces[k]
            for i in range(len(corners)):
                ek = edge_key(corners[i], corners[(i + 1) % len(corners)])
                for n in emap[ek]:
                    if n in keep:
                        continue
                    if abs(up_faces[n][3] - cyk) < 4.0:
                        keep.add(n)
                        queue.append(n)
        piso_faces, drop = [], {}
        for k, (gi, fi, corners, cy_) in enumerate(up_faces):
            if k in keep:
                piso_faces.append(groups[gi][2][fi])
                drop.setdefault(gi, set()).add(fi)
        for gi, fis in drop.items():
            groups[gi][2] = [f for fi, f in enumerate(groups[gi][2])
                             if fi not in fis]
        if piso_faces:
            groups.append(['piso', floor_mat, piso_faces])
            print('  piso: %d faces isoladas (plano sob origem Y=%.2f)'
                  % (len(piso_faces), y0))
        else:
            print('  AVISO: nenhuma face isolada como piso')

    if flip:
        for g in groups:
            if flip.lower() in g[0].lower():
                g[2] = [list(reversed(f)) for f in g[2]]

    with open(out_path, 'w') as out:
        out.write('# gerado por daetool.py split (1 objeto por material)\n')
        if mtllib:
            out.write(mtllib)
        for l in vlines:
            out.write(l + '\n')
        for name, mat, fcs in groups:
            if not fcs:
                continue
            out.write('o %s\n' % name)
            if mat:
                out.write('usemtl %s\n' % mat)
            for toks in fcs:
                out.write('f %s\n' % ' '.join(toks))
    nobj = sum(1 for g in groups if g[2])
    print('split: %s -> %s (%d objetos, %d faces)' % (
        obj_path, out_path, nobj, len(face_lines)))


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('info')
    p.add_argument('dae')
    p = sub.add_parser('roundtrip')
    p.add_argument('dae')
    p = sub.add_parser('split')
    p.add_argument('obj')
    p.add_argument('out')
    p.add_argument('--floor', dest='floor_mat', default=None,
                   help='material cujo piso (faces planas sob a origem) '
                        'vira o objeto proprio "piso"')
    p.add_argument('--flip', default=None,
                   help='inverte winding dos objetos cujo nome contem TRECHO')
    p = sub.add_parser('convert')
    p.add_argument('obj')
    p.add_argument('dae')
    p.add_argument('--swap', action='store_true',
                   help='gravar Z-up com winding invertido (so para caminhos com flag3)')
    p.add_argument('--type', type=int, default=None)
    p.add_argument('--map', dest='mapa', default=None)
    p.add_argument('--skip', default=None,
                   help='descarta objetos cujo nome contem TRECHO[,TRECHO]')
    p.add_argument('--floor', default=None,
                   help='forca qual objeto vira o piso (ConcreteFloor) da mesa')
    p.add_argument('--uv2', default=None,
                   help='2o OBJ exportado do Blender com a camada Lightmap '
                        'ativa (mesma geometria/ordem); fornece as UV do '
                        'lightmap por canto')
    p.add_argument('--lightmap', default=None,
                   help='imagem do atlas de lightmap (PNG) que vira o '
                        'conteudo de garagelighting.tga')
    a = ap.parse_args()
    if a.cmd == 'info':
        cmd_info(a.dae)
    elif a.cmd == 'roundtrip':
        sys.exit(cmd_roundtrip(a.dae))
    elif a.cmd == 'split':
        cmd_split(a.obj, a.out, a.floor_mat, a.flip)
    elif a.cmd == 'convert':
        cmd_convert(a.obj, a.dae, do_swap=a.swap, map_path=a.mapa,
                    vtype=a.type, skip=a.skip, floor=a.floor,
                    uv2_path=a.uv2, lightmap_path=a.lightmap)


if __name__ == '__main__':
    main()
