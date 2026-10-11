# Virtual-tools

Modding tools for **Virtual Pool 4 Multiplayer** — turn any 3D model into a
playable room, plus a freecam and raised engine limits.

**Client-side visual mod only.** Other players are not affected, it works on
official multiplayer servers, and the game never re-downloads the files you
replace.

## Contents

| File | What it is |
|---|---|
| [`dsound.dll`](dsound.dll) | ASI loader (proxy DLL, drop into the game folder) |
| [`version.dll`](version.dll) | Alternative ASI loader (use this if `dsound.dll` conflicts with audio) |
| [`freecam/`](freecam) | Free camera mod (`freecam.asi` + source) |
| [`vp4limits.asi`](vp4limits.asi) | Raises engine limits (big custom scenery stays visible) |
| [`tools/daetool.py`](tools/daetool.py) | Converts OBJ → VP4 room mesh (`.dae`) + game textures (`.tga`) |
| [`tools/arktool.py`](tools/arktool.py) | Extracts/packs game `.ark` packages |
| [`tools/lightmap_bake.py`](tools/lightmap_bake.py) | Optional: bakes an AO lightmap for a room (headless Blender) |
| [`dust2_src/`](dust2_src) | Example: the converted Dust2 (CS) room, ready to pack |

## Installation (mods)

1. In Steam: Library → *Virtual Pool 4 Multiplayer* → right click →
   Manage → **Browse local files**.
2. Copy **`dsound.dll`**, **`freecam.asi`** and **`vp4limits.asi`** into that
   folder (next to the game exe). Drag and drop, nothing else to configure.
3. Start the game.

**Linux (Proton):** same — copy the files into the install folder. If the
loader doesn't fire, add the launch option
(Steam → Properties → General):

```
WINEDLLOVERRIDES="dsound=n,b" %command%
```

(only needed for `dsound.dll`; with `version.dll` try `version=n,b` instead)

### Freecam usage

- **F4** — toggle the free camera
- **W A S D** — move · **Q** / **E** — down / up / **R** — reset position 

## Converting your own room (quickstart)

Requirements: **Python 3** (stdlib only) and **ImageMagick** (`magick` in
PATH). The converter reuses the stock texture names and flips UVs
automatically — no game code is patched.

```bash
# 1. export your model from Blender as OBJ (Y-Up + Write Materials)
# 2. split into one object per material, isolating the floor under origin:
python3 tools/daetool.py split model.obj model_split.obj --floor GROUND_MAT

# 3. extract the stock room you'll replace and note its GUID:
python3 tools/arktool.py extract Garage.ark src/
python3 tools/arktool.py info Garage.ark

# 4. convert + validate:
python3 tools/daetool.py convert model_split.obj src/garage.dae --floor piso
python3 tools/daetool.py roundtrip src/garage.dae        # must print OK

# 5. pack with the ORIGINAL GUID (this stops the game re-downloading it):
python3 tools/arktool.py pack src/ MyRoom.ark 71e28f8c-f0cd-47f4-92ee-5db310f63313

# 6. install the .ark in BOTH locations, then fully restart the game:
#    Windows:  C:\ProgramData\Celeris\Virtual Pool 4 Online\game\art\Rooms\<name>.ark
#              <Steam>\steamapps\common\Virtual Pool 4 Multiplayer\art\rooms\<name>.ark
```

**The one rule:** put a *flat* piece of floor under the origin
`(0, Y≈−27.8, 0)` — the stock table is dropped on the average height of that
floor, so ramps make it float or sink.

Optional **baked lightmap** (real contact shadows, like the stock rooms):

```bash
blender -b --factory-startup --python tools/lightmap_bake.py -- \
    model_split.obj lightmap/ 2048 60 16
python3 tools/daetool.py convert model_split.obj src/garage.dae --floor piso \
    --uv2 lightmap/lightmap_uv.obj --lightmap lightmap/lightmap.png
```

**Full walkthrough** (with the Dust2 example, table-height rule,
troubleshooting and command reference): see **[TUTORIAL.md](TUTORIAL.en.md)**
([versão em português](TUTORIAL.pt.md)).

## How it works

- An `.ark` is the game's package format with a **GUID** in the header.
  Keep the GUID and the updater never re-downloads the file — swap contents
  freely.
- Rooms are `xml config + .dae mesh + .tga textures`. The table is a
  *separate stock package* (`Tables/Garage.ark`) that never changes; the
  game places it on the room floor it finds under the origin (the
  `ConcreteFloor` material).
- Textures are loaded **by name**: the converter reuses names that already
  exist in the stock pack and only replaces their content — which is why no
  game code needs patching.

## Safety

- Visual-only: no physics/gameplay/tournament
- Keep backups of any `.ark` you replace — installing is just copying them
  back.
- Use at your own risk. Not affiliated with Celeris.

## Credits

Tools by **ferromentos**. Room converter and `.ark` packer developed for
this modding effort. Freely usable and modifiable.
