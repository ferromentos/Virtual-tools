# Tutorial: seu cenário custom no Virtual Pool 4 (exemplo real: Dust2)

Este tutorial mostra como transformar **qualquer modelo 3D** (exportado do
Blender como `.obj`) numa sala jogável do VP4, substituindo um dos cenários
stock. O exemplo usado aqui é o **Dust2** (CS 1.6 → VP4), convertido e
instalado como o **Garage**.

Ferramentas ( nesta pasta ):

- `daetool.py` — converte OBJ → DAE do jogo + texturas no formato VP4
- `arktool.py` — extrai/empacota os `.ark` (pacotes do jogo)

Requisitos: `python3` e ImageMagick7 (`magick` no PATH).

---

##0. Como funciona uma sala no VP4 (resumo)

```
art/rooms/Garage.ark            <- pacote que a gente substitui
    garage.xml                  <- config da sala (piso da mesa, luz)
    garage.dae                  <- mesh (todos os objetos, 1 material cada)
    garage_spec.dae             <- mesh de brilho (fica stock)
    *.tga                       <- texturas (o jogo carrega por NOME)

Tables/Garage.ark               <- a mesa (stock, NÃO mexer)
```

- A **mesa não vem no modelo**: é a mesa stock do Garage, com28.6 de altura,
  colocada automaticamente em cima do **piso da sala**, em X=0 Z=0.
- O jogo acha o piso comparando o XML `<textureNameFloor>ConcreteFloor.tga</textureNameFloor>`
  com o material do chão. No DAE stock isso é um objeto próprio chamado
  `ConcreteFloor` com a textura `ConcreteFloor.tga`. Nosso conversor reproduz
  isso automaticamente.
- O `.ark` tem um GUID no cabeçalho. Enquanto o GUID for o mesmo, o
  atualizador do jogo **nunca baixa o arquivo de novo** — pode trocar o
  conteúdo à vontade.

Regras de formatação que o modelo precisa seguir (o conversor cuida disso):

| Regra | Por quê |
|---|---|
| Exportar OBJ **Y-up** (padrão do Blender) | salas carregam sem troca de eixo |
| Uma UV (vt) por vértice | difusa; a UV de lightmap é gerada |
| Piso sob a origem (0,0), na altura Y≈−27 a −29 | a mesa stock encosta em X=0 Z=0 com o pé em Y=−28.07 |
| Nomes de material significativos | viram nomes de material no jogo |
| Texturas em arquivos que o `.mtl` referencia (`map_Kd`) | é de onde sai a textura de cada objeto |

---

##1. Exportar do Blender

No seu modelo, selecione tudo e:

**File → Export → Wavefront (.obj)**

- ✅ Y-Up (padrão)
- ✅ Write Materials (gera o `.mtl` junto)
- ✅ Triangulated Faces (opcional — o conversor triangula sozinho)

No exemplo eu exportei para `~/dust2virtualpool.obj` +
`~/dust2virtualpool.mtl`, com as texturas em `~/Dust 2/*.png`.

> Dica: se o modelo tem um **objeto só com vários materiais** (o Dust2 tinha
> um objeto "Dust2" com 25 materiais + um "Esfera" de céu), não precisa
> separar no Blender — o passo2 divide automaticamente.

##2. Dividir por material e isolar o piso

O formato do VP4 usa **1 material por objeto** (o garage stock tem42
objetos, cada um com sua textura). O comando `split` regroupa o modelo:

```bash
cd ~/vp4_tournament_mod/ark
python3 daetool.py split ~/dust2virtualpool.obj ~/dust2virtualpool_split.obj --floor material_10
```

Saída esperada:

```
  piso: 31 faces isoladas (plano sob origem Y=-27.82)
split: ... -> ~/dust2virtualpool_split.obj (27 objetos, 1024 faces)
```

- O modelo vira um objeto por material: `Dust2__material_0`,
  `Sphere__Material.002`…
- `--floor material_10`: o material do chão sob a origem é analisado — as
  faces horizontais conectadas que formam o chão em que a mesa encosta são
  separadas num objeto chamado **`piso`**. Sem isso, em mapa grande como o
  Dust2 o jogo poderia "enxergar" como piso a terra lá do outro lado do mapa.

### Como escolher o material do `--floor`?

O chão embaixo da origem (0,0) do seu modelo. No Dust2 o usuário já havia
posicionado a praça em Y=−27.82 sob a origem (perfeito pra mesa). Se o seu
modelo ainda não tem chão na origem, mova o modelo no Blender: o piso deve
passar por (0, Y≈−27.8, 0).

### Opção `--flip` (skybox esférico) (opcional)

O Dust2 tem uma esfera de céu ao redor do mapa. Ela já veio com as normais
pra dentro (vista de dentro), então não precisou de nada. Se o seu modelo
tiver uma esfera de céu com normais pra fora (padrão do Blender), o jogo não
la mostra (backface culling). Corrija na conversão, tenha em mente que isto e
so pra criar efeito de skybox, se seu cenario for um lugar fechado pode pular
essa parte.

```bash
python3 daetool.py split modelo.obj modelo_split.obj --floor chao --flip Sphere
```

##3. Extrair o cenário stock que você vai substituir

```bash
python3 arktool.py extract Garage_stock.ark dust2_src/
```

Isso cria `dust2_src/` com os52 arquivos do Garage original: as texturas
stock (que virarão "slots" pros seus conteúdos), o `garage.xml` e o
`garage_spec.dae`.

Anote o **GUID** (vai no passo5):

```bash
python3 arktool.py info Garage_stock.ark
# guid    : 71e28f8c-f0cd-47f4-92ee-5db310f63313
```

##4. Converter

```bash
python3 daetool.py convert ~/dust2virtualpool_split.obj dust2_src/garage.dae --floor piso
```

Saída esperada (real, do Dust2):

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

O que aconteceu:

- Cada objeto recebeu **um slot de textura** = o nome de um `.tga` que já
  existe no pacote stock (nunca nomes novos — o jogo só carrega o que veio
  no `.ark` original). O **conteúdo** daquele `.tga` é substituído pela sua
  imagem, convertida pro formato do jogo (DXT1 + mips, até512×512, alfa
  1-bit automático).
- O **V de cada UV é invertido automaticamente** (Blender: V=0 embaixo;
  o jogo/D3D: V=0 no topo) — sem isso toda textura sai de cabeça pra baixo.
  Isso é automático desde a versão do conversor de out/2026; se ainda ver
  textura invertida, atualize o `daetool.py`.
- O `piso` virou `ConcreteFloor`/`ConcreteFloor.tga` — mas continuou com a
  **sua** textura de chão (a mesma imagem do material de origem), então
  parece idêntico ao resto do mapa, só que o jogo o reconhece como piso.
- O lightmap (`GarageLighting.tga`) e o spec (`_specular.tga`) ficam stock —
  o conversor aponta todos os vértices pra um texel branco deles.
- Texturas protegidas (nunca sobrescritas): `garagelighting.tga`,
  `_specular.tga`, `garage_icon.tga`, `concretefloor.tga` (exceto o piso).

### De onde vem a altura da mesa (importante!)

A mesa stock é posicionada pelo jogo em cima do piso, e a posição usada
parece ser a **média do Y de todo o objeto `ConcreteFloor`** (confirmado
empiricamente: no Dust2 a mesa flutuava ~2.2 unidades e o usuário subiu o
modelo exatamente a média−superfície). Consequências práticas:

- Mantenha o piso **plano** (sem rampas no objeto `piso`) — rampas puxam a
  média pra longe da superfície real e a mesa afunda ou flutua.
- Se em jogo a mesa flutuar/afundar um pouco, calcule:
  `offset Y do modelo = média(Y do piso) − altura real do chão` e mova o
  modelo no Blender (ou no seu importador) por esse valor, reexportando.

### Opções úteis do convert

```bash
--floor NOME     forca qual objeto vira o piso (senão: maior área plana)
--skip TRECHO    descarta objetos (ex.: --skip Sphere pra remover a esfera de céu)
--map f.json     controle manual: {"nome_do_objeto": "textura_no_pack.tga"}
--uv2 2o.obj     fornece as UV do lightmap (ver próxima seção)
--lightmap img   imagem do atlas -> vira o conteúdo de garagelighting.tga
```

### Lightmap (iluminação assada — opcional)

O jogo tem um *lightmap*: o segundo par de UVs (vtype 4) de cada vértice
amostra `garagelighting.tga` e **multiplica** a textura difusa. O stock
tem um atlas 4096×4096 assado de verdade (sombra/AO). Sem `--uv2`, o
conversor aponta todas as UV2 para um texel branco → luz chapada (só a
luz dinâmica da mesa em cima). Para assar um lightmap real (AO =
sombra de contato; não duplica a luz dinâmica do jogo):

```bash
# 1. bake no Blender (headless; flatpak: flatpak run org.blender.Blender)
blender -b --factory-startup --python lightmap_bake.py -- \
    ~/dust2virtualpool_split.obj ~/vp4_tournament_mod/ark/lightmap 2048 60 16
#    -> lightmap/lightmap.png (atlas AO) + lightmap/lightmap_uv.obj
#    args extras: [distância do AO] [samples]

# 2. convert alimentado com o2º export e a imagem
python3 daetool.py convert ~/dust2virtualpool_split.obj dust2_src/garage.dae \
    --floor piso --uv2 lightmap/lightmap_uv.obj --lightmap lightmap/lightmap.png
#    -> lightmap UV2: verts=...  U[...] V[...]   (tem que VARIAR)
#    -> lightmap: lightmap.png -> garagelighting.tga (2048x2048)
```

O que o script faz: importa o OBJ, cria a camada de UV `Lightmap` em
cada objeto (Smart UV Project — ilhas sem overlap), assa AO do Cycles
num atlas compartilhado, salva o PNG e reexporta o OBJ com a camada
`Lightmap` ativa. O `--uv2` casa as UV **por canto** entre os dois OBJs
(com checagem de posição: se a ordem não bater, ele aborta em vez de
gravar lixo) e o `--lightmap` troca só o conteúdo de
`garagelighting.tga` (slots 2/7 dos materiais já apontam para ele —
mesmo método seguro das texturas difusas).

Ajustes:

- sombra muito aberta? diminua a distância do AO (60 → 20 = só contato);
  AO nunca deixa o quarto mais escuro que o difuso puro.
- quer sol/sombra direcional assada: acrescente lâmpadas no script e use
  `bake(type='COMBINED')` — cuidado, a TableLight do jogo continua em
  cima e luz assada + dinâmica duplica brilho.
- resolução: 2048 dá e sobra para mapa inteiro; o lightmap vai pro pack
  em DXT1 como o stock.

Recomendo fazer manunalmente usando baking do  proprio blender para resultados
melhores 

##5. Validar

```bash
python3 daetool.py roundtrip dust2_src/garage.dae
# ROUNDTRIP OK: dust2_src/garage.dae (...27 objetos, 27 materiais)
```

Tem que sair **OK** (o DAE lido de volta é byte a byte igual ao gravado).

Checklist:

- [ ] `roundtrip` OK
- [ ] existe um material `ConcreteFloor` na saída do convert
- [ ] nenhum texto "pool de nomes de textura esgotado" (mais texturas
      distintas do que slots livres no pack — se acontecer, aponte menos
      materiais pra texturas diferentes no `.mtl`)
- [ ] no pack só existe `concretefloor.tga` (lowercase, um único arquivo)

##6. Empacotar

```bash
python3 arktool.py pack dust2_src/ Dust2_custom.ark 71e28f8c-f0cd-47f4-92ee-5db310f63313
# 52 arquivos -> Dust2_custom.ark (...) guid=71e28f8c-...
```

O GUID **tem que ser o do cenário original** (senão o jogo baixa tudo de
novo). Empacota tudo que está solto na pasta.

##7. Instalar (DOIS locais) e conferir

```bash
SRC=~/vp4_tournament_mod/ark/Dust2_custom.ark
PFX=~/hdtera/SteamLibrary/steamapps/compatdata/432310/pfx/drive_c/ProgramData/Celeris/"Virtual Pool 4 Online"/game/art/Rooms/Garage.ark
INST=~/hdtera/SteamLibrary/steamapps/common/"Virtual Pool 4 Multiplayer"/art/rooms/Garage.ark

cp "$SRC" "$PFX" && cp "$SRC" "$INST"
md5sum "$SRC" "$PFX" "$INST"      # os3 têm que bater
```

- **ProgramData** é de onde o jogo lê (autoritativo)
- **install dir** mantém igual por garantia
- Mesmo nome de arquivo + mesmo GUID = o jogo nem percebe que trocou

##8. Testar

1. Entrar na sala **Garage** e conferir: texturas ok? mesa encostada no piso?
3. Use Freecam ASI (F4 + WASD) pra inspecionar de perto.
2. Pra debug: `DebugLevel=5` no `online_config.ini` → os loads aparecem no
   `.../AppData/Local/Celeris/Virtual Pool 4 Online/message.log`.

##9. Iterar

Pra ajustar algo, repita só o meio do fluxo:

```
Blender exporta OBJ (mesmo caminho)
  → daetool split (mesmo comando)
  → daetool convert (mesmo comando)
  → roundtrip
  → pack
  → cp nos2 locais + md5
  → reiniciar jogo completo
```

---

## Resultado do Dust2 (referência)

-27 objetos (25 materiais do mapa + esfera de céu + piso),4245 vértices,
2508 triângulos, vtype4, V invertido em todas as UVs
- Piso:36 faces planas sob a origem em Y=−25.59 (o usuário subiu o modelo
2.23 unidades compensando a média da âncora); média do piso=−26.04 →
pés da mesa (−28.07→âncora −26.04) ficam0.45 dentro do chão, igual ao stock
-26 texturas convertidas pros slots stock; pacote final20.8 MB, md5
  `4ece18e77c49c8a493369f58d835f68a`
- Céu: a esfera do usuário foi mantida (normais pra dentro = vista de dentro).
  Se ela não aparecer em jogo (far plane curto) ou causar estranheza,
  remova com `--skip Sphere` e reconverse.

## Limitações conhecidas

| Limitação | Detalhe |
|---|---|
| Mesa é sempre a stock da sala | vem de `Tables/Garage.ark`; só o piso muda |
| Spec dae fica stock | brilho especular pode ficar desalinhado (só cosmético) |
| Alfa =1 bit | transparência binária; sem meio-tom |
| Nomes novos de textura não testados | use só `.tga` que já existiam no pack |
| Cenário gigante (tipo mapa de CS) | a câmera fica em volta da mesa (zoom≈245); distante pode ser cortado pelo far plane — só cosmético |
|1 material por objeto | use o `split` (passo2) — é automático |

## Comandos de referência

```bash
# --- daetool ---
python3 daetool.py info      arquivo.dae          # materiais/objetos/texturas
python3 daetool.py roundtrip arquivo.dae          # valida parse+write (True=ok)
python3 daetool.py split     in.obj out.obj [--floor MAT] [--flip TRECHO]
python3 daetool.py convert   in.obj saida.dae [--floor NOME] [--skip TRECHO] \
                             [--map f.json] [--uv2 2o.obj] [--lightmap img] \
                             [--type N] [--swap]

# --- blender (flatpak: flatpak run org.blender.Blender) ---
blender -b --factory-startup --python lightmap_bake.py -- \
        modelo_split.obj out_dir [tamanho] [ao_dist] [samples]   # bake de lightmap

# --- arktool ---
python3 arktool.py info    arquivo.ark            # GUID + lista de arquivos
python3 arktool.py extract arquivo.ark  dir/      # descompacta
python3 arktool.py pack    dir/  saida.ark [GUID] # reempacota (GUID do .uuid pareado se omitido)
```

## Troubleshooting

| Sintoma | Causa provável |
|---|---|
| Mesa flutuando | chão fora da origem, ou piso com rampa (a média do piso sobe); ajeite o chão plano sob (0,0) |
| Mesa dentro do chão | média do piso abaixo da superfície — suba o chão ou mova o modelo (ver "De onde vem a altura da mesa") |
| Mesa flutuando ou no chão errado | piso sem `ConcreteFloor.tga`; use `split --floor` + `convert --floor piso`; confira que o chão passa por (0, Y≈−27.8, 0) |
| Textura errada num objeto | corrigir `map_Kd` no `.mtl` ou forçar com `--map` |
| "pool de nomes de textura esgotado" | modelo usa mais texturas distintas que slots no pack — menos materiais com textura única no `.mtl` |
| Quarto com luz chapada | sem lightmap (UV2 apontam p/ texel branco) — rode o bake da seção "Lightmap" |
| Quarto escuro com lightmap | distância do AO grande demais para a escala do mapa — diminua (60→20) e rebake |
| O jogo baixou o arquivo de novo | GUID errado no `pack` |
| Nada mudou em jogo | processo não reiniciado completo |
| Céu não aparece | esfera com normais pra fora (`--flip Sphere`) ou além do far plane |
| Objeto sumiu | `--skip` pegou ele; revise o TRECHO passado |
