# Bake headless de lightmap (AO) pro Virtual Pool 4.
#
# Uso (blender CLI; flatpak: flatpak run org.blender.Blender):
#   blender -b --factory-startup --python lightmap_bake.py -- \
#       modelo_split.obj out_dir 2048 [ao_distance] [samples]
#
# Passos:
#   1. importa o OBJ (um objeto por linha `o` / usemtl)
#   2. cria a camada de UV 'Lightmap' em cada malha (Smart UV Project,
#      ilhas com margem -> atlas sem overlap)
#   3. bake Cycles de AO (sombras de contato; nao duplica a luz dinamica
#      do jogo — TableLight continua agindo por cima) num atlas
#      compartilhado por todos os materiais
#   4. salva out_dir/lightmap.png
#   5. exporta out_dir/lightmap_uv.obj (mesma geometria, camada 'Lightmap'
#      ativa -> vira as UV2 la no daetool)
#
# Depois, no diretorio do ark:
#   python3 daetool.py convert modelo_split.obj garage.dae --floor piso \
#       --uv2 out_dir/lightmap_uv.obj --lightmap out_dir/lightmap.png
import os
import sys

import bpy


def _import_obj(path):
    if hasattr(bpy.ops.wm, 'obj_import'):
        bpy.ops.wm.obj_import(filepath=path)
    else:  # Blender < 3.x
        bpy.ops.import_scene.obj(filepath=path)


def _export_obj(path):
    kw = dict(export_materials=False, export_uv=True, export_normals=True)
    if hasattr(bpy.ops.wm, 'obj_export'):
        try:
            bpy.ops.wm.obj_export(filepath=path, **kw)
            return
        except TypeError:
            kw = {}
    bpy.ops.export_scene.obj(filepath=path, use_materials=False,
                             use_uvs=True, use_normals=True)


argv = sys.argv[sys.argv.index('--') + 1:]
obj_in = argv[0]
out_dir = argv[1]
size = int(argv[2]) if len(argv) > 2 else 2048
ao_dist = float(argv[3]) if len(argv) > 3 else 60.0
samples = int(argv[4]) if len(argv) > 4 else 16
os.makedirs(out_dir, exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)
_import_obj(obj_in)

meshes = [o for o in bpy.data.objects if o.type == 'MESH']
if not meshes:
    raise SystemExit('nenhuma malha importada de %s' % obj_in)

# imagem atlas compartilhada (branca fora das ilhas: bleed de mip claro)
img = bpy.data.images.new('lm_atlas', width=size, height=size, alpha=False,
                          float_buffer=False)
img.generated_color = (1.0, 1.0, 1.0, 1.0)

for ob in meshes:
    me = ob.data
    me.uv_layers.new(name='Lightmap')
    me.uv_layers.active_index = me.uv_layers.find('Lightmap')
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.select_all(action='DESELECT')
    ob.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=1.15192, island_margin=0.004)
    bpy.ops.object.mode_set(mode='OBJECT')
    for mat in me.materials:
        if mat is None:
            continue
        mat.use_nodes = True
        nt = mat.node_tree
        node = nt.nodes.new('ShaderNodeTexImage')
        node.image = img
        for n in nt.nodes:
            n.select = False
        node.select = True
        nt.nodes.active = node

sc = bpy.context.scene
sc.render.engine = 'CYCLES'
sc.cycles.device = 'CPU'
sc.cycles.samples = samples
sc.cycles.use_denoising = False
try:
    sc.cycles.distance = ao_dist
except AttributeError:
    pass

bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.mode_set(mode='OBJECT')
bpy.ops.object.bake(type='AO', margin=12, use_clear=False)

png = os.path.join(out_dir, 'lightmap.png')
img.filepath_raw = png
img.file_format = 'PNG'
img.save()

# exporta com a camada 'Lightmap' ativa em todas as malhas
for ob in meshes:
    me = ob.data
    for i, lay in enumerate(me.uv_layers):
        lay.active_render = (lay.name == 'Lightmap')
        if lay.name == 'Lightmap':
            me.uv_layers.active_index = i
_export_obj(os.path.join(out_dir, 'lightmap_uv.obj'))
print('lightmap ok: %s + lightmap_uv.obj' % png)
