# Banc — lecture des touches par IPROPI (ADR-0032)

Aide-mémoire pour la campagne **N2** (matériel). La validation hôte et les builds
sont acquis ; **rien ci-dessous n'est encore démontré sur banc**. À faire dans
l'ordre : les mesures M1–M3 fixent les constantes, G1 décide de la faisabilité,
puis on règle les seuils.

## 0. Préparation

- **Build Clavier pour les essais** : configurer `build/clavier_studio` avec
  `-DCLAVIER_SERIAL_STUDIO=ON` (commande complète dans
  `tests/build_clavier_windows.md` §5), puis `cmake --build` et flasher
  `clavier.uf2` (BOOTSEL). Le build final se refait avec
  `-DCLAVIER_SERIAL_STUDIO=OFF` (§8).
- **Flasher le Moteur** avec l'arbre courant (`build/moteur/moteur.elf`, SWD).
- **Keymap** : placer soi-même `REC_SOLE` (0x1000) ou `REC_MUTE` (0x1100) dans
  `firmware/clavier/src/keymap.c`, ou utiliser la console `e rec` (= `REC_SOLE`).
  Aucune position n'est imposée (invariant 6).
- **PuTTY sur le port 1** (`Clavicore Log`) : console et journal `[LISTEN]`/`[REC]`.
- **Serial Studio sur le port 2** (`Clavicore REC`, présent seulement si l'option
  est ON) : mode **Quick Plot**, délimiteurs `/*` et `*/`, séparateur `,` — ou
  ouvrir `tools/serial_studio/clavicore_rec.json` (schéma non vérifié contre la
  version installée ; Quick Plot marche sans projet).
- Table de commissioning **valide** obligatoire (sinon `e trace`/`e rec` →
  `[REC] refus REC_NO_TABLE`).

## 1. M1 — FEM à vide (oscilloscope)

Carte Moteur **hors tension** (seul l'oscillo mesure), sonde **différentielle**
sur OUT1–OUT2 d'un canal, appui **doux**, **fort**, relâchement, puis trille.
Noter la tension crête et le sens (l'IPROPI, lui, perd le sens).

Ordre de grandeur attendu : **50 mV de FEM ≈ 6,6 mA ≈ 7 mV sur IPROPI ≈ 9 points
ADC** (K = 1,08 V/A, 3,3 V / 4096 ≈ 0,806 mV/point). Bruit humain : 1–2 points,
3 au maximum. **Noter la valeur mesurée** : elle fixe l'espérance de G1.

## 2. M2 — Inductance

Mesurer L au LCR (ou τ = L/R à l'oscillo). Cette valeur sert à choisir
`split_pct` : plus τ est grand, plus le creux de trille est lent et moins il faut
descendre bas pour séparer deux impulsions.

## 3. M3 — Établissement du mux

Oscillo sur **GPIO45** (sortie du mux), déclenchement sur **S0**, **en REC**
(c'est seulement en écoute que le mux balaie ; hors REC le Core 1 est à l'arrêt).
Les marches entre canaux sont petites (écarts de ligne de base de 10 à 20 points,
soit 8 à 16 mV) : couplage AC, 2 à 5 mV/div. Mesurer le temps pour revenir à
moins de 0,8 mV (1 point) de la valeur finale après chaque commutation, puis
**`settle_us` = 2 × la mesure** (marge). Le pic bref au début de chaque palier
est l'injection de charge du mux ou l'échantillonnage de l'ADC : il fait partie
de ce qu'il faut laisser passer.

Débit : un canal coûte `settle_us + 2 µs × samples_per_dwell + ≈ 1 µs` ;
fréquence par canal = `1 / (12 × ce temps)`.

| `settle_us` | `samples_per_dwell` | Temps/canal | Fréquence/canal |
|---|---|---|---|
| 15 µs | 1 | 18 µs | ≈ 4,6 kHz |
| 15 µs | 2 | 20 µs | ≈ 4,2 kHz |
| 3 µs | 1 | 6 µs | ≈ 13 kHz |

Vérifier la période **réelle** dans le journal :
`[LISTEN] pos=N etat=LISTENING periode=…us debordements=…`.

## 4. G1 — Faisabilité du signal

Entrer en écoute (`e rec` ou REC actif), puis `e trace <touche>` (nom `MI3`,
`RE#3` ou MIDI 36..127). La courbe part sur Serial Studio (port 2) ; PuTTY ne
reçoit que le résumé :
`[TRACE] pos= ch= touche= base= pic= delta= bruit_cc= t_pic=…ms n=4096`.

**Critère** : `pic − base ≥ 3 × bruit crête` (bruit attendu 1–2, max 3 points).

Si échec : **arrêt** et décision **matérielle** humaine. ⚠ Augmenter R_IPROPI
augmente le signal mais abaisse I_TRIP ≈ 3,05 A si VREF = 3,3 V : **question
sécurité**, ne rien changer seul.

Vérifier aussi les corrections de la campagne :
- `e trace` **sans déclenchement** → après `trace_timeout_ms`, `TRACE_TIMEOUT`
  puis retour en LISTEN de **toutes** les cartes ; aucune rafale `PARAM_MISMATCH`.
- `e set` puis `e trace` → aucun `PARAM_MISMATCH` (CRC identique : contrôle de
  mode seul) ; si le CRC a changé, OFF → jeu → LISTEN pour toutes les cartes,
  puis contrôle ciblé.

## 5. Réglage des seuils — 2 événements par note (appui / relâchement)

Entrer en REC (`e rec` ou la touche), puis `e set <champ> <valeur>` à chaud.
En REC, `e set` **arrête puis redémarre** l'écoute (le Moteur refuse
`LISTEN_PARAM` hors OFF) : c'est normal. `e get <champ>` vérifie, `e show` liste
tout.

Ordre conseillé et effet de chaque champ :

| Ordre | Champ | Effet | Sens du réglage |
|---|---|---|---|
| 1 | `rise_thr` | seuil de début | baisser si appuis manqués, monter si faux départs |
| 2 | `return_thr` | seuil de retour, **< rise_thr** | doit rester au-dessus du bruit (3 max) |
| 3 | `return_hold_us` | maintien du retour avant fin | baisser pour les trilles rapides, monter contre les doubles |
| 4 | `split_pct` | creux de trille (% de la crête) | monter = coupe plus facilement (creux peu profond suffit) ; baisser si une seule note se coupe en deux |
| 5 | `min_pulse_us` | impulsion minimale rapportée | baisser pour ne pas perdre les appuis très courts |
| 6 | `baseline_tau_ms` | suivi lent de la base | monter = base plus stable, plus lente à suivre |
| 7 | `samples_per_dwell` | moyenne par canal | monter = moins de bruit, moins de débit |

- `e stream <touche>` (option ON) : flux direct
  `/*brut,base,seuil_haut,seuil_retour,evt*/` sur le port 2. Pendant REC, le mode
  change **sans renvoyer les paramètres** ; relancer `e stream` après un `e set`
  si l'écoute a redémarré.
- Triller de plus en plus vite : on doit obtenir **une paire ON/OFF par
  aller-retour**, jamais deux ON ou deux OFF.
- Si l'alternance s'inverse (impulsion manquée), `e raz` remet toutes les
  touches relevées (prochaine impulsion = ON), sans émettre de Note Off.

## 6. Bruit des voisins

`e bruit` bascule l'envoi automatique des requêtes après chaque impulsion
(**refusé si `detail = 0`** : `necessite detail=1`). Après chaque note, lire
`[BRUIT] k-1 …` et `[BRUIT] k+1 …` (position, canal, touche, min, max, base).
Un voisin non attribué s'affiche `[BRUIT] k±1 : non attribuée`. Fenêtre > 256 ms
→ `TRONQUE`.

## 7. Test MIDI final

- **`REC_SOLE`** : solénoïdes muets, MIDI de la matrice **conservé**.
- **`REC_MUTE`** : solénoïdes muets **et** MIDI de la matrice muet (seules les
  notes captées sortent).
- Vérifier **une Note On et une Note Off par appui**, à vitesse lente puis en
  trille ; sortir de REC (l'une ou l'autre touche, ou `e rec`) sans note
  bloquée.
- `m stats` pendant **10 min** : aucun débordement, période de balayage stable,
  pas de `PARAM_MISMATCH`.
- Option **OFF** : `e stream` répond `indisponible (CLAVIER_SERIAL_STUDIO=OFF)`
  et **aucun port COM supplémentaire** n'apparaît.

## 8. Fin de campagne

`e diff` liste les seuls champs différents du fichier, au format
`.champ = valeur,`. Les recopier dans `firmware/clavier/src/listen_config.h`,
puis recompiler et flasher avec **`CLAVIER_SERIAL_STUDIO=OFF`**.

## Constantes à adapter

| Champ | Défaut | Où (via `e set` / `e diff`) | Test qui le fixe | Sens du réglage |
|---|---|---|---|---|
| `settle_us` | 15 | `listen_config.h` | M3 | 2 × temps d'établissement mesuré |
| `samples_per_dwell` | 2 | idem | M3 / G1 | plus = moins de bruit, moins de débit |
| `baseline_init_ms` | 200 | idem | entrée en écoute | laisser la base se poser |
| `baseline_tau_ms` | 500 | idem | dérive | plus = base plus stable et plus lente |
| `rise_thr` | 5 | idem | G1 / seuils | baisser = plus sensible |
| `return_thr` | 3 | idem | seuils | < `rise_thr`, > bruit max |
| `return_hold_us` | 1000 | idem | trilles | baisser = notes rapides |
| `split_pct` | 30 | idem | M2 / trilles | monter = coupe plus facilement (τ grand → monter) |
| `min_pulse_us` | 500 | idem | seuils | baisser = garde les impulsions courtes |
| `drive_blank_ms` | 50 | idem | hors REC | masquage après pilotage |
| `trace_period_us` | 20 | idem | G1 | 20 µs = 50 kHz |
| `trace_trig` | 5 | idem | G1 | seuil de déclenchement de la trace |
| `trace_timeout_ms` | 10000 | idem | G1 | délai sans déclenchement |
| `stream_decim` | 1 | idem | réglage live | monter = moins de bus |
| `vel_fixed` | 100 | idem | MIDI | vélocité fixe 1..127 |
| `vel_min_delta` / `vel_max_delta` | 0 / 0 | idem | MIDI | (0,0) = fixe ; sinon vélocité linéaire du delta |
| `detail` | 1 | idem | bruit | 1 = durées + `e bruit` |

Toute valeur reste en RAM Clavier : perdue au reboot tant qu'elle n'est pas
recopiée dans `listen_config.h` et recompilée.
