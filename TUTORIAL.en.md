# Tutorial: your custom room in Virtual Pool 4 (real example: Dust2)

This tutorial shows how to turn **any 3D model** (exported from Blender as
`.obj`) into a playable VP4 room, replacing one of the stock scenarios. The
example used here is **Dust2** (CS 1.6 → VP4), converted and installed as
the **Garage**.

Tools (in this folder / [repo](https://github.com/ferromentos/Virtual-tools)):

- `daetool.py` — converts OBJ → game DAE + textures in the VP4 format
- `arktool.py` — extracts/packs the `.ark` (game packages)

Requirements: `python3` and ImageMagick 7 (`magick` on PATH).

---

## 0. How a room works in VP4 (summary)

```
art/rooms/Garage.ark            <- the package we replace
    garage.xml                  <- room config (table floor, light)
    garage.dae                  <- mesh (all objects, 1 material each)
    garage_spec.dae             <- specular mesh (stays stock)
    *.tga                       <- textures (loaded by NAME)

Tables/Garage.ark               <- the table (stock, DO NOT touch)
```

- The **table is not in your model**: it is the stock Garage table, 28.6
  units tall, placed automatically on top of the **room floor** at X=0 Z=0.
- The game finds the floor by matching the XML
  `<textureNameFloor>ConcreteFloor.tga</textureNameFloor>` against a ground
  material. In the stock DAE that is its own object called `ConcreteFloor`
  with texture `ConcreteFloor.tga`. Our converter reproduces this
  automatically.
- The `.ark` has a GUID in its header. As long as the GUID stays the same,
  the game updater **never re-downloads the file** — you can swap the
  contents freely.

Rules your model must follow (the converter takes care of them):

| Rule | Why |
|---|---|
| Export OBJ **Y-up** (Blender default) | rooms load with no axis swap |
| One UV (`vt`) per vertex | diffuse; the lightmap UV is generated |
| Floor under the origin (0,0), at Y≈−27 to −29 | the stock table touches at X=0 Z=0 with its feet at Y=−28.07 |
| Meaningful material names | they become material names in-game |
| Textures in files referenced by the `.mtl` (`map_Kd`) | that is where each object's texture comes from |

---

## 1. Export from Blender

Select everything in your model and:

**File → Export → Wavefront (.obj)**

- ✅ Y-Up (default)
- ✅ Write Materials (writes the `.mtl` alongside)
- ✅ Triangulated Faces (optional — the converter triangulates by itself)

In the example I exported to `~/dust2virtualpool.obj` +
`~/dust2virtualpool.mtl`, with textures in `~/Dust 2/*.png`.

> Tip: if the model is **one object with many materials** (Dust2 was a
> single "Dust2" object with 25 materials + a sky "Sphere"), you don't need
> to separate anything in Blender — step 2 does it automatically.

## 2. Split by material and isolate the floor

The VP4 format uses **1 material per object** (the stock Garage has 42
objects, each with its own texture). The `split` command regroups the model:

```bash
cd ~/vp4_tournament_mod/ark
python3 daetool.py split ~/dust2virtualpool.obj ~/dust2virtualpool_split.obj --floor material_10
```

Expected output:

```
  piso: 31 faces isolated (plane under origin Y=-27.82)
split: ... -> ~/dust2virtualpool_split.obj (27 objects, 1024 faces)
```

- The model becomes one object per material: `Dust2__material_0`,
  `Sphere__Material.002`…
- `--floor material_10`: the ground material under the origin is analyzed —
  the connected horizontal faces forming the floor the table stands on are
  split into their own object called **`piso`**. Without this, in a large
  map like Dust2 the game could "see" as floor the terrain on the other side
  of the map.

### How to pick the `--floor` material?

The ground under your model's origin (0,0). In Dust2 the plaza was already
positioned at Y=−27.82 under the origin (perfect for the table). If your
model has no floor at the origin yet, move the model in Blender: the floor
must pass through (0, Y≈−27.8, 0).

### `--flip` option (spherical skybox) (optional)

Dust2 has a sky sphere around the map. It already came with normals facing
inward (viewed from inside), so nothing was needed. If your model has a sky
sphere with outward normals (Blender default), the game won't show it
(backface culling). Fix it during conversion — note this is only for
skybox effect; if your scenario is an enclosed place you can skip this.

```bash
python3 daetool.py split modelo.obj modelo_split.obj --floor chao --flip Sphere
```

## 3. Extract the stock scenario you will replace

```bash
python3 arktool.py extract Garage_stock.ark dust2_src/
```

This creates `dust2_src/` with the 52 files of the original Garage: the
stock textures (which become "slots" for your contents), the `garage.xml`
and the `garage_spec.dae`.

Write down the **GUID** (used in step 5):

```bash
python3 arktool.py info Garage_stock.ark
# guid    : 71e28f8c-f0cd-47f4-92ee-5db310f63313
```

## 4. Convert

```bash
python3 daetool.py convert ~/dust2virtualpool_split.obj dust2_src/garage.dae --floor piso
```

Expected output (real, from Dust2):

```
  piso forcado (--floor): piso (area up=380413)
convertido: ... -> dust2_src/garage.dae
  objetos=27 submeshes=27 verts=4273 triangulos=2512 swap=False vtype=4
    Dust2__material_32       -> 2x4box.tga             <- Dust2_material_32.png
    Dust2__material_33       -> _floorstain01.tga      <- Dust2_material_33.png
    ...
    Sphere__Material.002     -> lamp_emis.tga          <- 6673105be8098.jpeg
    ConcreteFloor            -> ConcreteFloor.tga      <- Dust2_material_10.png
```

What happened:

- Every object got **one texture slot** = the name of a `.tga` that already
  exists in the stock package (never new names — the game only loads what
  came in the original `.ark`). The **content** of that `.tga` is replaced
  by your image, converted to the game format (DXT1 + mips, up to 512×512,
  automatic 1-bit alpha).
- The **V of every UV is flipped automatically** (Blender: V=0 at bottom;
  the game/D3D: V=0 at top) — without this every texture comes upside down.
  This is automatic since the Oct/2026 converter; if you still see flipped
  textures, update your `daetool.py`.
- `piso` became `ConcreteFloor`/`ConcreteFloor.tga` — but kept **your** floor
  texture (the same image as the source material), so it looks identical to
  the rest of the map; the game just recognizes it as the floor.
- The lightmap (`GarageLighting.tga`) and specular (`_specular.tga`) stay
  stock — the converter points every vertex at a white texel of them.
- Protected textures (never overwritten): `garagelighting.tga`,
  `_specular.tga`, `garage_icon.tga`, `concretefloor.tga` (except the floor).

### Where the table height comes from (important!)

The stock table is placed by the game on top of the floor, and the position
used seems to be the **mean Y of the whole `ConcreteFloor` object**
(confirmed empirically: in Dust2 the table floated ~2.2 units and the user
raised the model by exactly mean−surface). Practical consequences:

- Keep the floor **flat** (no ramps in the `piso` object) — ramps pull the
  mean away from the real surface and the table sinks or floats.
- If in-game the table floats/sinks a little, compute:
  `model Y offset = mean(piso Y) − real floor height` and move the model in
  Blender (or your importer) by that value, re-exporting.

### Useful convert options

```bash
--floor NOME     forces which object becomes the floor (default: largest flat area)
--skip TRECHO    discards objects (e.g. --skip Sphere to remove the sky sphere)
--map f.json     manual control: {"object_name": "texture_in_pack.tga"}
--uv2 2nd.obj    supplies the lightmap UVs (see next section)
--lightmap img   atlas image -> becomes the content of garagelighting.tga
```

### Lightmap (baked lighting — optional)

The game has a *lightmap*: the second UV pair of each vertex (vtype 4)
samples `garagelighting.tga` and **multiplies** the diffuse texture. The
stock rooms have a real baked 4096×4096 atlas (shadow/AO). Without
`--uv2`, the converter points all UV2s at a white texel → flat lighting
(only the table's dynamic light on top). To bake a real lightmap (AO =
contact shadow; does not duplicate the game's dynamic light):

```bash
# 1. bake in Blender (headless; flatpak: flatpak run org.blender.Blender)
blender -b --factory-startup --python lightmap_bake.py -- \
    ~/dust2virtualpool_split.obj ~/vp4_tournament_mod/ark/lightmap 2048 60 16
#    -> lightmap/lightmap.png (AO atlas) + lightmap/lightmap_uv.obj
#    extra args: [AO distance] [samples]

# 2. convert fed with the 2nd export and the image
python3 daetool.py convert ~/dust2virtualpool_split.obj dust2_src/garage.dae \
    --floor piso --uv2 lightmap/lightmap_uv.obj --lightmap lightmap/lightmap.png
#    -> lightmap UV2: verts=...  U[...] V[...]   (must VARY)
#    -> lightmap: lightmap.png -> garagelighting.tga (2048x2048)
```

What the script does: imports the OBJ, creates a `Lightmap` UV layer on
each object (Smart UV Project — non-overlapping islands), bakes Cycles AO
into a shared atlas, saves the PNG and re-exports the OBJ with the
`Lightmap` layer active. The `--uv2` matches UVs **per corner** between the
two OBJs (with a position check: if the order doesn't match it aborts
instead of writing garbage) and `--lightmap` swaps only the content of
`garagelighting.tga` (material slots 2/7 already point to it — same safe
method as diffuse textures).

Tweaks:

- shadows too open? lower the AO distance (60 → 20 = contact only); AO can
  never make the room darker than pure diffuse.
- want baked sun/directional shadow: add lamps in the script and use
  `bake(type='COMBINED')` — careful, the game's TableLight still adds on
  top; baked + dynamic light doubles brightness.
- resolution: 2048 is plenty for a whole map; the lightmap goes into the
  pack as DXT1 like the stock ones.

I recommend doing it manually using Blender's own baking for best results.

## 5. Validate

```bash
python3 daetool.py roundtrip dust2_src/garage.dae
# ROUNDTRIP OK: dust2_src/garage.dae (...27 objects, 27 materials)
```

It must say **OK** (the DAE read back is byte-for-byte identical to the
written one).

Checklist:

- [ ] `roundtrip` OK
- [ ] a `ConcreteFloor` material exists in the convert output
- [ ] no "texture name pool exhausted" text (more distinct textures than
      free slots in the pack — if it happens, point fewer materials to
      different textures in the `.mtl`)
- [ ] the pack contains only `concretefloor.tga` (lowercase, a single file)

## 6. Pack

```bash
python3 arktool.py pack dust2_src/ Dust2_custom.ark 71e28f8c-f0cd-47f4-92ee-5db310f63313
# 52 files -> Dust2_custom.ark (...) guid=71e28f8c-...
```

The GUID **must be the one of the original scenario** (otherwise the game
downloads everything again). It packs everything loose in the folder.

## 7. Install (TWO locations) and check

```bash
SRC=~/vp4_tournament_mod/ark/Dust2_custom.ark
PFX=~/hdtera/SteamLibrary/steamapps/compatdata/432310/pfx/drive_c/ProgramData/Celeris/"Virtual Pool 4 Online"/game/art/Rooms/Garage.ark
INST=~/hdtera/SteamLibrary/steamapps/common/"Virtual Pool 4 Multiplayer"/art/rooms/Garage.ark

cp "$SRC" "$PFX" && cp "$SRC" "$INST"
md5sum "$SRC" "$PFX" "$INST"      # all 3 must match
```

On Windows the authoritative location is
`C:\ProgramData\Celeris\Virtual Pool 4 Online\game\art\Rooms\` and the
install-dir copy is `<Steam>\steamapps\common\Virtual Pool 4 Multiplayer\art\rooms\`.

- **ProgramData** is where the game reads from (authoritative)
- the **install dir** copy is kept identical as a guarantee
- same file name + same GUID = the game doesn't even notice it changed

## 8. Test

1. Enter the **Garage** room and check: textures OK? table resting on the
   floor?
2. Use the Freecam ASI (F4 + WASD) to inspect up close.
3. For debug: `DebugLevel=5` in `online_config.ini` → the loads appear in
   `.../AppData/Local/Celeris/Virtual Pool 4 Online/message.log`.

## 9. Iterate

To adjust something, repeat only the middle of the flow:

```
Blender exports OBJ (same path)
  → daetool split (same command)
  → daetool convert (same command)
  → roundtrip
  → pack
  → cp to 2 locations + md5
  → full game restart
```

---

## Dust2 result (reference)

- 27 objects (25 map materials + sky sphere + floor), 4245 vertices,
  2508 triangles, vtype 4, V flipped on all UVs
- Floor: 36 flat faces under the origin at Y=−25.59 (the user raised the
  model 2.23 units compensating the anchor mean); floor mean=−26.04 →
  table feet (−28.07→anchor −26.04) end up 0.45 inside the floor, just like
  stock
- 26 textures converted into the stock slots; final package 20.8 MB
- Sky: the sphere was kept (normals inward = seen from inside). If
  it doesn't show in-game (short far plane) or causes weirdness, remove it
  with `--skip Sphere` and reconvert.

## Known limitations

| Limitation | Detail |
|---|---|
| Table is always the room's stock one | comes from `Tables/Garage.ark`; only the floor changes |
| Spec dae stays stock | specular highlights may be misaligned (cosmetic only) |
| Alpha = 1 bit | binary transparency; no half-tones |
| New texture names untested | use only `.tga` names that already existed in the pack |
| Giant scenery (CS-map scale) | the camera stays around the table (zoom≈245); far geometry may be cut by the far plane — cosmetic only |
| 1 material per object | use `split` (step 2) — it is automatic |

## Command reference

```bash
# --- daetool ---
python3 daetool.py info      file.dae            # materials/objects/textures
python3 daetool.py roundtrip file.dae            # validates parse+write (True=ok)
python3 daetool.py split     in.obj out.obj [--floor MAT] [--flip PART]
python3 daetool.py convert   in.obj out.dae [--floor NAME] [--skip PART] \
                             [--map f.json] [--uv2 2nd.obj] [--lightmap img] \
                             [--type N] [--swap]

# --- blender (flatpak: flatpak run org.blender.Blender) ---
blender -b --factory-startup --python lightmap_bake.py -- \
        model_split.obj out_dir [size] [ao_dist] [samples]   # lightmap bake

# --- arktool ---
python3 arktool.py info    file.ark             # GUID + file list
python3 arktool.py extract file.ark  dir/       # unpack
python3 arktool.py pack    dir/  out.ark [GUID] # repack (paired .uuid GUID if omitted)
```

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| Table floating | floor away from origin, or floor with a ramp (the mean rises); fix a flat floor under (0,0) |
| Table inside the floor | floor mean below the surface — raise the floor or move the model (see "Where the table height comes from") |
| Table floating or at wrong height | floor without `ConcreteFloor.tga`; use `split --floor` + `convert --floor piso`; check the floor passes through (0, Y≈−27.8, 0) |
| Wrong texture on an object | fix `map_Kd` in the `.mtl` or force with `--map` |
| "texture name pool exhausted" | model uses more distinct textures than slots in the pack — fewer materials with a unique texture in the `.mtl` |
| Room with flat lighting | no lightmap (UV2s point at the white texel) — run the bake from the "Lightmap" section or bake |
| Room too dark with lightmap | AO distance too large for the map scale — lower it (60→20) and rebake |
| The game re-downloaded the file | wrong GUID in the `pack` |
| Nothing changed in-game | game not fully restarted |
| Sky doesn't show | object with normals pointing out (example `--flip Sphere`) or beyond the far plane |
| An object disappeared | `--skip` caught it; review the PART passed |
