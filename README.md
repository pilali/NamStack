# NamStack

Plugin **LV2 / VST3 / Standalone** (JUCE) de modélisation d'ampli guitare, inspiré de
[neural-amp-modeler-lv2](https://github.com/mikeoliphant/neural-amp-modeler-lv2).

## Chaîne de signal

```
Entrée → Gain d'entrée → [Tone stack si "Pre"] → [EQ 5 bandes si "Pre"]
       → Modèle neuronal (NAM / AIDA-X)
       → [Tone stack si "Post"] → [EQ 5 bandes si "Post"]
       → Mixeur d'IR (4 slots, volume + panoramique)
       → Doubleur → Gain de sortie → Sortie stéréo
```

Les deux égaliseurs ont chacun leur on/off et leur commutateur Pre/Post ; à
position égale, l'EQ 5 bandes passe après le tone stack (voir plus bas).

## Fonctionnalités

### Modèles neuronaux
- **NAM** (`.nam`) via [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore)
  (WaveNet, LSTM, ConvNet, **A2** standard/nano et conteneurs slimmables)
- Architecture **A2** : la voie rapide spécialisée est activée
  (`NAM_ENABLE_A2_FAST`, désactivable avec
  `-DNAMSTACK_ENABLE_A2_FAST=OFF`) ; les fichiers A2 « SlimmableContainer »
  exposent en plus un potentiomètre **Quality** (0–1) qui échange de la
  qualité contre du CPU — appliqué hors du thread audio (worker LV2 côté
  MOD, thread de message côté JUCE). Sans effet sur les autres modèles.
- **AIDA-X / RTNeural** (`.aidax`, `.json`) via [RTNeural](https://github.com/jatinchowdhury18/RTNeural)
- Rééchantillonnage automatique (Lanczos) lorsque la fréquence du modèle
  (généralement 48 kHz) diffère de celle de l'hôte ; la latence induite est
  déclarée à l'hôte.
- Pour les modèles AIDA-X « conditionnés » (une ou deux entrées de contrôle
  entraînées, type gain/master), les potentiomètres **Param 1** et
  **Param 2** alimentent les entrées de conditionnement. Ils sont grisés
  quand le modèle chargé n'est pas conditionné.

### Tone stack (égalisation d'ampli)
Simulation du circuit passif Bass/Mid/Treble classique, d'après
*D. T. Yeh & J. O. Smith, « Discretization of the '59 Fender Bassman Tone
Stack » (DAFx-06)*, discrétisé par transformation bilinéaire. Les valeurs de
composants sont les jeux documentés (Duncan Tone Stack Calculator, guitarix,
bibliothèques Faust) :

| Modèle | R1 | R2 | R3 | R4 | C1 | C2 | C3 |
|---|---|---|---|---|---|---|---|
| Fender Bassman 5F6-A | 250k | 1M | 25k | 56k | 250p | 20n | 20n |
| Fender Twin Reverb | 250k | 250k | 10k | 100k | 120p | 100n | 47n |
| Fender Princeton | 250k | 250k | 4.8k | 100k | 250p | 100n | 47n |
| Marshall JCM800 2203 | 220k | 1M | 22k | 33k | 470p | 22n | 22n |
| Mesa Boogie Mark | 250k | 250k | 25k | 100k | 250p | 100n | 47n |
| Vox AC30 Top Boost | 1M | 1M | 10k | 100k | 50p | 22n | 22n |
| Ampeg SVT | 250k | 1M | 25k | 32k | 470p | 22n | 22n |
| Soldano SLO-100 | 250k | 1M | 25k | 47k | 470p | 20n | 20n |

Le tone stack a son propre **on/off** et est **commutable avant (« Pre ») ou
après (« Post »)** le modèle neuronal. Comme le circuit réel est passif, il
atténue le signal (creux de médiums caractéristique) — compensez avec le gain
de sortie si nécessaire.

### Égaliseur graphique 5 bandes (Mesa/Boogie)

Le second égaliseur reprend les fréquences documentées du **graphic EQ des Mesa
Boogie Mark** (Mark IIC+ / III / IV), à curseurs verticaux :

| | | | | |
|---|---|---|---|---|
| **80 Hz** | **240 Hz** | **750 Hz** | **2200 Hz** | **6600 Hz** |

Course de **±12 dB** par bande.

Ce n'est **pas** une cascade de filtres indépendants, mais un modèle du *circuit*
que ces égaliseurs utilisent réellement : un **ampli-op unique entouré d'un
gyrateur par bande** (un LC série simulé). L'interaction entre les bandes fait
tout le caractère de l'engin — la reproduire était l'objectif.

**Topologie.** Le potentiomètre de chaque bande est monté *en travers* de
l'ampli, de l'entrée vers la sortie ; son curseur rejoint le nœud sommateur `S`
à travers une branche résonante `Zᵢ(s) = Rs + sL + 1/(sC)` accordée sur le
centre de la bande. Hors résonance, `Zᵢ` est grande et la bande ne fait rien ; à
la résonance elle devient petite et le curseur injecte du courant dans `S` —
prélevé du côté entrée (**boost**) ou du côté sortie, donc en contre-réaction
(**cut**). En écrivant le générateur de Thévenin du curseur et la somme des
courants en `S` (masse virtuelle), la réponse vient en forme close :

```
H(s) = − [ 1/Rin + Σᵢ (1−kᵢ)·Yᵢ(s) ] / [ 1/Rf + Σᵢ kᵢ·Yᵢ(s) ]

Yᵢ(s) = 1 / ( kᵢ(1−kᵢ)·Rp + Rs + sLᵢ + 1/(sCᵢ) )
```

`kᵢ` est la position du curseur : 0 = boost maxi, ½ = neutre, 1 = cut maxi.
**Chaque bande figure au numérateur *et* au dénominateur de la même fraction**,
et ce seul fait produit tout ce que la cascade ne pouvait pas donner :

| | mesuré |
|---|---|
| Les bandes **interagissent** : deux boosts voisins ne s'additionnent pas | deux bandes à +12 dB → **+8,8 dB** entre elles, là où la somme ferait +13,5 |
| Le **Q est proportionnel**, pas constant (la résistance de Thévenin `k(1−k)Rp` est maximale au neutre et s'annule aux extrêmes) | la bande passe de **4,5 octaves** à +3 dB à **1,7 octave** à +12 dB |
| **Engager l'EQ déplace le niveau** — c'est ce qui fait sonner le « V » | réglage `+12/+6/−12/+6/+12` → le creux ne descend qu'à **−9 dB**, remonté par ses voisines, et les extrêmes montent à **+13,7 dB** |
| Curseurs au neutre : **exactement plat** | déviation **0,000000000 dB** de 20 Hz à 20 kHz |

La discrétisation **préserve la topologie** : chaque branche est intégrée à la
règle des trapèzes et s'écrit « conductance instantanée + terme d'état », ce qui
permet de résoudre le nœud sommateur directement à chaque échantillon — sans
approximation de boucle sans retard ni recherche de racines. Coût mesuré :
**0,2 % d'un cœur** de Raspberry Pi 5.

Le dB inscrit sur un curseur est ce que fait cette bande **les quatre autres au
neutre** (les branches voisines chargent le nœud même centrées, ce dont le
calibrage tient compte). Dès que plusieurs curseurs bougent, ils se tirent
dessus — c'est tout l'intérêt.

Il possède lui aussi un **on/off** et une option **Pre/Post** indépendante de
celle du tone stack.

**Ordre dans la chaîne** — chaque égaliseur choisit son côté du modèle neuronal.
Lorsque les deux se retrouvent **du même côté**, l'égaliseur graphique passe
**après** le tone stack :

| Tone stack | EQ graphique | Chaîne |
|---|---|---|
| Pre | Pre | `→ tone stack → EQ → modèle →` |
| Pre | Post | `→ tone stack → modèle → EQ →` |
| Post | Pre | `→ EQ → modèle → tone stack →` |
| Post | Post | `→ modèle → tone stack → EQ →` |

### Impulse responses (IR)
- **4 slots** de convolution en parallèle (fichiers `.wav`, `.aiff`, `.flac`)
- Par slot : **on/off, niveau (dB), panoramique** (loi à puissance constante)
- Les IR stéréo sont supportées ; les IR mono sont dupliquées sur les deux
  canaux avant panoramique. Chargement sans interruption audio (échange en
  arrière-plan par `juce::dsp::Convolution`).
- **Chaque slot peut être vidé**, et l'on peut revenir à **zéro IR** : dans
  mod-ui, l'entrée *« -- none -- »* en tête de chaque liste de fichiers efface
  le slot (elle envoie un chemin vide, que le plugin traite comme un
  déchargement) ; dans l'éditeur JUCE, c'est le bouton **X** de la ligne. Quand
  plus aucun slot n'est actif, le mixeur d'IR laisse passer le signal tel quel —
  pas de silence.

### Doubleur
Effet « doubler » de fin de chaîne dans l'esprit de celui des suites
Neural DSP : deux « prises » artificielles du signal, micro-désaccordées en
sens opposés (pitch-shifter à ligne de retard et double prise de son
crossfadée), retardées de quelques dizaines de millisecondes, panoramiquées
à gauche et à droite, puis mélangées au signal direct.

- **Mix** : dosage direct/doublé (loi à puissance constante)
- **Time** : retard de base des prises (5–50 ms)
- **Detune** : micro-désaccord (± cents, opposé entre gauche et droite)
- **Humanize** : dérive lente et aléatoire du timing et du désaccord,
  imitant l'imprécision d'un vrai doublage
- **Width** : écartement stéréo des deux prises

## Compilation

```bash
git clone --recurse-submodules <ce dépôt>
# ou après un clone simple :
git submodule update --init --recursive

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Binaires produits :
- `build/NamStack_artefacts/Release/LV2/NamStack.lv2/` — copier dans `~/.lv2/`
- `build/NamStack_artefacts/Release/VST3/NamStack.vst3/` — copier dans `~/.vst3/`
- `build/NamStack_artefacts/Release/Standalone/NamStack`
- `build/mod_artefacts/namstack-mod.lv2/` — LV2 natif (MOD / headless)

Dépendances Linux : `libasound2-dev libx11-dev libxext-dev libxrandr-dev
libxinerama-dev libxcursor-dev libfreetype-dev`.

### Intégration continue

`.github/workflows/build.yml` compile tout à chaque push :

| Job | Cible | Artefact |
|---|---|---|
| `mod-lv2 (x86_64)` | LV2 MOD, natif | `namstack-mod.lv2-linux-x86_64` |
| `mod-lv2 (aarch64)` | LV2 MOD, compilation croisée | `namstack-mod.lv2-linux-aarch64` |
| `juce (linux)` | VST3 / LV2 / Standalone | `NamStack-linux` |
| `juce (windows)` | VST3 / Standalone (MSVC) | `NamStack-windows` |
| `juce (macos)` | VST3 / LV2 / Standalone | `NamStack-macos` |

Le LV2 MOD étant sans dépendance hors libc/libstdc++ (pas de JUCE), la variante
aarch64 est une **simple compilation croisée** (`g++-aarch64-linux-gnu`), sans
sysroot : l'artefact se dépose tel quel dans `~/.lv2/` d'un Raspberry Pi.

La CI **échoue si le `.so` exporte autre chose que `lv2_descriptor`** — c'est le
garde-fou contre la régression décrite plus bas, qui faisait planter mod-host.

## MOD Audio (Dwarf, Duo, DuoX)

Le dépôt contient **deux plugins LV2** :

| | JUCE LV2 (`NamStack.lv2`) | LV2 natif MOD (`namstack-mod.lv2`) |
|---|---|---|
| URI | `urn:pilali:NamStack` | `urn:pilali:NamStackMOD` |
| Cible | desktop (GUI X11) | **périphériques MOD** et desktop headless |
| Paramètres | `patch:writable` (JUCE 8) | **ControlPorts** (adressables sur les potards matériels) |
| Fichiers | état du plugin (éditeur) | **`patch:Set` / `atom:Path`** : modèle + 4 IR chargés depuis mod-ui, avec `mod:fileTypes` (`nammodel`, `aidadspmodel`, `cabsim`, `ir`…) |
| Convolution | juce::dsp::Convolution | moteur UPOLS maison (pffft), **latence zéro** quand bloc hôte = partition (cas MOD : 128) |
| Chargement | thread de message | **worker LV2** (échange sans blocage du thread audio) |

La variante MOD suit l'architecture de
[neural-amp-modeler-lv2](https://github.com/mikeoliphant/neural-amp-modeler-lv2)
(worker + patch + state avec `mapPath`, notifications `patch:Set` sur le port
notify, restauration des chemins avec la pedalboard).

### Compilation native sur Raspberry Pi (Pi 4 / Pi 5, pi-Stomp…)

Pour faire tourner **uniquement** la variante MOD (`namstack-mod.lv2`, LV2
headless + modgui) directement sur un Raspberry Pi sous mod-ui, on compile en
natif en désactivant les formats JUCE (pas de X11 requis) :

```bash
git clone --recurse-submodules <ce dépôt> NamStack
cd NamStack        # ou: git submodule update --init --recursive

cmake -B build -DCMAKE_BUILD_TYPE=Release \
      -DNAMSTACK_BUILD_JUCE=OFF -DNAMSTACK_BUILD_MOD_LV2=ON
cmake --build build --parallel

# le bundle prêt à déployer :
cp -r build/mod_artefacts/namstack-mod.lv2 ~/.lv2/
# puis relancer mod-ui pour qu'il rescanne les plugins
```

Aucune dépendance X11/JUCE n'est nécessaire dans ce mode ; il suffit d'un
compilateur C++20 (`build-essential`, `cmake`, `git`).

Vérifier le bundle produit (facultatif, nécessite `lilv-utils`) :

```bash
LV2_PATH=$PWD/build/mod_artefacts lv2ls        # doit lister urn:pilali:NamStackMOD
LV2_PATH=$PWD/build/mod_artefacts lv2info urn:pilali:NamStackMOD
```

**Note toolchain (GCC ≥ 11, donc Debian Bookworm / Raspberry Pi OS actuels).**
Le header `ResamplingContainer.h` de l'AudioDSPTools embarqué par NAM core
contient un alias de membre (`using LanczosResampler = LanczosResampler<…>`) qui
masque la classe homonyme. GCC 11+ en fait une erreur dure gouvernée par
`-fpermissive`, et le `#pragma GCC diagnostic ignored "-Wchanges-meaning"`
présent dans le code ne la neutralise qu'à partir de **GCC 14** (le drapeau
n'existe pas avant). Le `CMakeLists.txt` applique donc `-fpermissive` au seul
`src/dsp/NeuralModel.cpp` sous GCC — c'est transparent, mais cela explique
l'avertissement `changes meaning of 'LanczosResampler'` qui subsiste sous
GCC 11–13 (bénin). Sur la toolchain GCC 9 de mod-plugin-builder, rien de tout
cela ne se déclenche.

### Visibilité des symboles (crash de mod-host)

Un plugin LV2 est chargé par `dlopen()` dans le processus de l'hôte, **aux côtés
des autres plugins**. Or `neural-amp-modeler-lv2` embarque lui aussi
NeuralAmpModelerCore. Si les deux `.so` exportent tout le namespace `nam::`,
l'éditeur de liens dynamique les *interpose* :

- le singleton `nam::ConfigParserRegistry` est émis comme symbole
  **`STB_GNU_UNIQUE`**, que la glibc unifie entre bibliothèques **même en
  `RTLD_LOCAL`**. Le second plugin chargé ré-enregistre alors ses parseurs dans
  un registre déjà rempli, et **fait planter l'hôte** :

  ```
  terminate called after throwing an instance of 'std::runtime_error'
    what():  Config parser already registered for: SlimmableContainer
  mod-host.service: Main process exited, code=dumped, status=6/ABRT
  ```

- même sans ce crash, `nam::create_dsp` & co. se lieraient à la copie chargée en
  premier, mélangeant silencieusement **deux versions différentes de NAM core**.

Le `CMakeLists.txt` compile donc tout en visibilité masquée
(`CMAKE_CXX_VISIBILITY_PRESET hidden`, `-fno-gnu-unique`) et lie le module avec
`-Wl,--exclude-libs,ALL`. Seul le point d'entrée `lv2_descriptor` reste exporté
(il porte `LV2_SYMBOL_EXPORT`). Contrôle rapide — la commande ne doit afficher
qu'une seule ligne :

```bash
nm -D --defined-only build/mod_artefacts/namstack-mod.lv2/namstack.so
# 0000000000012650 T lv2_descriptor
```

C'est une règle générale : **un plugin LV2 ne doit exporter que
`lv2_descriptor`.**

### Compilation avec mod-plugin-builder (MOD Dwarf)

```bash
# dans votre clone de mod-plugin-builder :
cp -r /chemin/vers/NamStack/mod-plugin-builder/namstack plugins/package/
./build moddwarf namstack
```

La recette `mod-plugin-builder/namstack/namstack.mk` :

- construit uniquement le LV2 natif (`-DNAMSTACK_BUILD_JUCE=OFF`, pas de X11) ;
- applique les drapeaux d'optimisation éprouvés sur Cortex-A35 (mêmes que le
  paquet `neural-amp-modeler-lv2` officiel : `-ftree-vectorize`,
  `-funsafe-math-optimizations`, `-fno-math-errno`, LTO,
  `-fsingle-precision-constant`, `EIGEN_DONT_PARALLELIZE`…) ;
- épingle la longueur maximale des IR (`NAMSTACK_MAX_IR_SAMPLES`, 8192 par
  défaut ≈ 170 ms @ 48 kHz — descendez à 4096 pour économiser du CPU avec de
  gros modèles NAM sur le Dwarf).

Conseils CPU pour le Dwarf : privilégiez les modèles NAM « feather/nano » ou
les modèles AIDA-X LSTM légers ; le tone stack, le mixeur d'IR (jusqu'à
4 slots) et le doubleur sont peu coûteux en comparaison du modèle neuronal.
Sur les modèles **A2 slimmables**, le potentiomètre **Quality** est le levier
CPU le plus efficace.

### Compilation croisée avec un GCC récent (hors mod-plugin-builder)

La toolchain de mod-plugin-builder est ancienne (GCC ~9). Certaines
optimisations n'ont d'intérêt qu'avec un auto-vectoriseur moderne — par
exemple, dans NeuralAudio (le moteur du plugin *neural-amp-modeler-lv2*),
`ENABLE_MULTIFRAME_8X8_CONVOLUTION` n'est activée par défaut qu'à partir de
**GCC 15 / Clang 21**. Ce n'est pas une exigence du langage (le code est du
C++17 standard, on peut forcer l'option avec n'importe quel compilateur) mais
un seuil de *performance* : en dessous, le noyau manuel est souvent plus lent
que la voie Eigen qu'il remplace.

Pour bénéficier d'un compilateur récent **sans toucher à MOD OS ni à la
toolchain MPB**, la méthode sûre est : *GCC récent + sysroot MPB + runtime
C++ statique*. Les deux règles ABI qui rendent cela sûr :

1. **glibc** : le binaire ne doit pas exiger de symboles glibc plus récents
   que ceux du périphérique → on compile avec `--sysroot` pointant sur le
   *staging* de mod-plugin-builder (la glibc du Dwarf). Un GCC récent
   fonctionne très bien contre une vieille glibc en sysroot.
2. **libstdc++/libgcc** : celles du Dwarf sont trop vieilles pour un GCC
   récent → on les lie **statiquement** dans le `.so`
   (`-static-libstdc++ -static-libgcc`), sans ré-exporter leurs symboles
   (`-Wl,--exclude-libs,ALL`). L'ABI LV2 étant du C pur, embarquer une
   libstdc++ privée dans le plugin est sans danger pour mod-host.

Le dépôt fournit l'outillage complet :

- `cmake/aarch64-moddwarf.cmake` — fichier toolchain CMake (Cortex-A35,
  drapeaux d'optimisation MOD, sysroot, runtime statique) ;
- `scripts/build-moddwarf-external.sh` — build de bout en bout + audits
  (version glibc maximale requise par le binaire, absence de dépendance
  dynamique à libstdc++) + déploiement optionnel.

```bash
# 1. Préparer une fois le sysroot du Dwarf avec mod-plugin-builder :
#    ./bootstrap.sh moddwarf   (crée ~/mod-workdir/moddwarf/staging)

# 2. Installer un cross-GCC aarch64 récent (crosstool-NG, Bootlin,
#    ARM GNU toolchain…) et vérifier son préfixe (ex: aarch64-none-linux-gnu-)

# 3. Compiler :
CROSS_PREFIX=aarch64-none-linux-gnu- \
MOD_SYSROOT=$HOME/mod-workdir/moddwarf/staging \
./scripts/build-moddwarf-external.sh

# 4. (optionnel) déployer directement sur le Dwarf :
MOD_DEVICE=root@192.168.51.1 ... ./scripts/build-moddwarf-external.sh
# puis redémarrer l'appareil pour que mod-ui rescanne les plugins
```

Le script vérifie automatiquement que le `.so` produit ne requiert pas de
version glibc supérieure à celle du sysroot et que libstdc++ n'apparaît pas
dans ses dépendances dynamiques (`objdump -T` / `-p`).

La même procédure s'applique au plugin *neural-amp-modeler-lv2* officiel si
vous voulez y forcer `-DENABLE_MULTIFRAME_8X8_CONVOLUTION=ON` avec un GCC 15 :
compilez-le hors MPB avec ce type de toolchain file, en ajoutant l'option à
la ligne CMake. Avec le GCC 9 de MPB, forcer l'option compile aussi — mais
mesurez la charge CPU sur l'appareil avant de l'adopter, la valeur par défaut
(OFF sous GCC < 15) reflète un vrai risque de régression.

Notes de portage utiles (découvertes en validant la cross-compilation) :

- `-fsigned-char` est requis sur aarch64 (le `char` y est non signé par
  défaut, ce que le code WDL embarqué par NAM core refuse) ;
- `-fsingle-precision-constant` est **incompatible** avec cette base de code
  (il casse le resampler Lanczos/WDL et la déduction de templates) — ne
  l'ajoutez pas aux drapeaux, même s'il figure dans d'autres recettes MOD.

### modgui

Chaque bundle embarque une interface web pour
[mod-ui](https://github.com/mod-audio/mod-ui) :

- Les **jacks audio des templates sont générés par mod-ui lui-même** : les
  templates itèrent sur `effect.ports.audio.input` /
  `effect.ports.audio.output` fournis par mod-ui au rendu, si bien que les
  symboles des ports correspondent toujours au TTL du plugin (`audio_in`,
  `audio_out_l`, `audio_out_r` pour la variante MOD).
- Variante MOD : potentiomètres en `mod-role="input-control-port"` +
  **5 sélecteurs de fichiers** (`mod-widget="custom-select-path"`, listes de
  fichiers fournies par mod-ui via `effect.parameters`).
- Bundle JUCE : contrôles en `mod-role="input-parameter"` (paramètres
  `patch:writable` de JUCE 8).
- Assets (sprites film-strip, screenshots 840×360 / 840×400, thumbnail
  256×64) régénérables avec `python3 modgui/tools/generate_assets.py`
  (Pillow requis).

## Notes

- Le chemin du modèle et des IR est sauvegardé dans l'état du plugin et
  rechargé avec la session.
- URI LV2 : `urn:pilali:NamStack`.
- Entrée mono ou stéréo (sommée en mono avant le modèle), sortie stéréo
  (ou mono, sommée en fin de chaîne).
