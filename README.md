# NamStack

Plugin **LV2 / VST3 / Standalone** (JUCE) de modélisation d'ampli guitare, inspiré de
[neural-amp-modeler-lv2](https://github.com/mikeoliphant/neural-amp-modeler-lv2).

## Chaîne de signal

```
Entrée → Gain d'entrée → [Tone stack si "Pre"] → Modèle neuronal (NAM / AIDA-X)
       → [Tone stack si "Post"] → Mixeur d'IR (4 slots, volume + panoramique)
       → Doubleur → Gain de sortie → Sortie stéréo
```

## Fonctionnalités

### Modèles neuronaux
- **NAM** (`.nam`) via [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore)
  (WaveNet, LSTM, ConvNet…)
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

Le tone stack est **commutable avant (« Pre ») ou après (« Post »)** le modèle
neuronal. Comme le circuit réel est passif, il atténue le signal (creux de
médiums caractéristique) — compensez avec le gain de sortie si nécessaire.

### Impulse responses (IR)
- **4 slots** de convolution en parallèle (fichiers `.wav`, `.aiff`, `.flac`)
- Par slot : **on/off, niveau (dB), panoramique** (loi à puissance constante)
- Les IR stéréo sont supportées ; les IR mono sont dupliquées sur les deux
  canaux avant panoramique. Chargement sans interruption audio (échange en
  arrière-plan par `juce::dsp::Convolution`).

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

Binaires produits dans `build/NamStack_artefacts/Release/` :
- `LV2/NamStack.lv2/` — copier dans `~/.lv2/`
- `VST3/NamStack.vst3/` — copier dans `~/.vst3/`
- `Standalone/NamStack`

Dépendances Linux : `libasound2-dev libx11-dev libxext-dev libxrandr-dev
libxinerama-dev libxcursor-dev libfreetype-dev`.

## MOD (modgui)

Le bundle LV2 embarque une interface web **modgui** pour
[mod-ui](https://github.com/mod-audio/mod-ui) (MOD Audio) :

- `modgui/` : template (`icon-namstack.html`), feuille de style, sprites de
  potentiomètres/interrupteurs, `screenshot-namstack.png` (840×360) et
  `thumbnail-namstack.png` (256×64), régénérables avec
  `python3 modgui/tools/generate_assets.py` (Pillow requis).
- Les **jacks audio du template sont générés par mod-ui lui-même** : le
  template itère sur `effect.ports.audio.input` / `effect.ports.audio.output`
  fournis par mod-ui, si bien que les symboles de ports (`audio_in_1`,
  `audio_out_1`, `audio_out_2`) correspondent toujours à ceux du `dsp.ttl`
  généré par JUCE.
- JUCE 8 expose les paramètres LV2 en propriétés `patch:writable` (et non en
  ControlPorts) ; les contrôles du modgui utilisent donc
  `mod-role="input-parameter"` avec l'URI du paramètre
  (`urn:pilali:NamStack:<id>`), supporté par mod-ui.
- Limitation : le chargement des fichiers de modèles et d'IR passe par
  l'état du plugin (éditeur desktop), pas par des paramètres `atom:Path` —
  sur un appareil MOD, les fichiers ne peuvent pas être choisis depuis le
  modgui.

## Notes

- Le chemin du modèle et des IR est sauvegardé dans l'état du plugin et
  rechargé avec la session.
- URI LV2 : `urn:pilali:NamStack`.
- Entrée mono ou stéréo (sommée en mono avant le modèle), sortie stéréo
  (ou mono, sommée en fin de chaîne).
