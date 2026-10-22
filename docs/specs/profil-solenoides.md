# Profil d'activation des solénoïdes — module

Document de référence pour le **Codeur** et le **Validateur**, issu des
[ADR-0013](../adr/adr-0013-profil-activation-solenoides-centralise-clavier.md)
et [ADR-0014](../adr/adr-0014-limites-activation-pilotees-profil-clavier-derogation-invariant-4.md)
(statut `Accepted`, 2026-10-21) puis de
[ADR-0019](../adr/adr-0019-phase-impact-impulsion-courte-avant-pull.md)
(`Accepted`, 2026-10-22), qui ajoute la phase **IMPACT** et passe le format en
**v2**.

> **Statut de ce document (2026-10-22).** Ce document décrit l'état **implémenté
> et validé** de la v2 (ADR-0019) : compilation des deux cibles, tests hôte et
> vérification indépendante du layout binaire (§9). La **validation matérielle**
> de la v2 reste à faire (campagne `tests/phase3b_profil_tuning.md` §2.4) ; la v1
> (24 champs, 57 octets) était la version en service jusqu'au 2026-10-22.

Il décrit : le fichier unique de réglage, le format du profil, son cycle de vie,
son transfert, sa persistance et les règles de contrôle. Les trames CAN
correspondantes sont dans [`CAN.md`](./CAN.md) ; les listes de fichiers à revoir
sont dans [`impact-adr-0013.md`](./impact-adr-0013.md) et
[`impact-adr-0019.md`](./impact-adr-0019.md).

## 1. Propriété et fichier unique

| Élément | Emplacement | Nature |
|---|---|---|
| **Réglage** | `firmware/clavier/src/solenoid_profile_config.h` | `static const solenoid_profile_t k_solenoid_profile` — **seul fichier à modifier** |
| Format partagé | `shared/protocol/solenoid_profile.[ch]` | structure, sérialisation, CRC32, contrôles structurels |
| Trames | `shared/protocol/can_protocol.[ch]` | `0x420`–`0x423` |
| Repli Moteur | `firmware/moteur/src/solenoid_profile_defaults.h` | valeurs compilées, jamais un point de réglage |
| Persistance Moteur | `firmware/moteur/src/solenoid_profile_store.[ch]` | deux secteurs flash A/B |
| Constantes de mesure | `firmware/moteur/src/solenoid_ohm_constants.h` | `A_IPROPI`, `R_IPROPI`, `VM`, `VREF`, `R_DS(on)` — hors profil |

Le profil est **global** : une valeur pour les 49 solénoïdes des 5 cartes. Les
profils par canal ne sont pas retenus (extension possible par une
`format_version` ultérieure).

**Aucune valeur limite n'est figée dans le firmware Moteur** (ADR-0014,
dérogation à l'invariant 4). Les valeurs initiales sont celles validées au banc,
mais ce ne sont plus des bornes.

### 1.b Segmentation du jeu normal (ADR-0019)

Un Note On accepté enchaîne, dans le driver Moteur :

```
IDLE --(note on)--> IMPACT (impact_duration_us, impact_duty_pct)
                      --(fin du timer)--> PULL (pull_duration_us, pull_duty_pct)
                      --> HOLD (jusqu'au note off, coupure hold_max_ms)
                      --> RELEASE (polarité inversée, release_duration_us)
```

- IMPACT est une impulsion très courte à pleine puissance, destinée à amorcer la
  montée de courant ; elle est suivie du PULL à duty réduit.
- `pull_duration_us` conserve sa sémantique : durée du **seul** segment PULL, dont
  le timer redémarre à la fin d'IMPACT. La durée totale à courant élevé est
  `impact_duration_us + pull_duration_us` (bornée par
  `impact_max_us + pull_max_us`).
- `impact_duration_us == 0` désactive la phase (Note On → PULL directement).
- Un Note Off pendant IMPACT va directement en RELEASE (pas de passage par PULL).
- MAINTENANCE (ADR-0012) et PROBE (ADR-0009) ne passent **pas** par IMPACT.

## 2. Payload canonique v2 (66 octets, little-endian)

Sérialisation champ par champ, sans `memcpy` de structure. Offsets du payload
tel qu'il circule dans les `PROFILE_CHUNK` et tel qu'il est stocké en flash. La
v2 insère les trois champs `impact_*` **après `pwm_freq_hz`**, ce qui décale de
9 octets tous les champs suivants par rapport à la v1.

| Off. | Champ | Type | Unité | Valeur initiale |
|---|---|---|---|---|
| 0 | `format_version` | u8 | — | 2 |
| 1 | `pwm_freq_hz` | u32 | Hz | 20 000 |
| 5 | `impact_duration_us` | u32 | µs | 800 |
| 9 | `impact_duty_pct` | u8 | % | 100 |
| 10 | `impact_max_us` | u32 | µs | 2 000 |
| 14 | `pull_duration_us` | u32 | µs | 15 000 |
| 18 | `pull_duty_pct` | u8 | % | 50 |
| 19 | `pull_max_us` | u32 | µs | 50 000 |
| 23 | `hold_duty_pct` | u8 | % | 25 |
| 24 | `hold_duty_max_pct` | u8 | % | 25 |
| 25 | `hold_max_ms` | u32 | ms | 20 000 |
| 29 | `release_duration_us` | u32 | µs | 5 000 |
| 33 | `release_duty_pct` | u8 | % | 50 |
| 34 | `probe_duration_us` | u32 | µs | 5 000 |
| 38 | `probe_duty_pct` | u8 | % | 20 |
| 39 | `probe_max_us` | u32 | µs | 10 000 |
| 43 | `probe_duty_max_pct` | u8 | % | 20 |
| 44 | `maint_duty_pct` | u8 | % | 50 |
| 45 | `maint_duty_max_pct` | u8 | % | 100 |
| 46 | `maint_max_ms` | u32 | ms | 20 000 |
| 50 | `maint_min_rest_ms` | u32 | ms | 2 000 |
| 54 | `min_rest_ms` | u32 | ms | 10 |
| 58 | `dir_safety_delay_us` | u16 | µs | 200 |
| 60 | `wake_delay_us` | u16 | µs | 2 000 |
| 62 | `dir_level_pull` | u8 | 0/1 | 0 |
| 63 | `dir_level_release` | u8 | 0/1 | 1 |
| 64 | `dir_level_maint` | u8 | 0/1 | 1 (`= dir_level_release`) |
| 65 | `duty_max_pct` | u8 | % | 100 |

Identité du profil : `profile_tag32` = CRC32 du payload (66 octets), polynôme
`0xEDB88320`, init `0xFFFFFFFF`, xorout `0xFFFFFFFF`.

> **Valeurs initiales** : ce tableau donne les valeurs du **fichier Clavier**
> (opérationnel). Les valeurs de **repli Moteur**
> (`solenoid_profile_defaults.h`) leur sont alignées à une exception près,
> `impact_duration_us` (repli = **0**, phase désactivée jusqu'à la première
> synchronisation) ; la divergence préexistante `pull_duty_pct`
> (fichier = 50, ADR-0013/ADR-0014 et repli = 100) est signalée au §10.

## 3. Contrôles structurels (jamais de sécurité)

`solenoid_profile_validate()` applique **uniquement** ces règles. Un échec
rejette le profil **en bloc** ; l'ancien profil reste actif ; jamais d'écrêtage.

| Règle | Code d'erreur |
|---|---|
| duties `0..100 %` ; `hold_duty_pct <= hold_duty_max_pct` ; `maint_duty_pct <= maint_duty_max_pct` ; `probe_duty_pct <= probe_duty_max_pct` ; `duty_max_pct >=` chaque duty de phase (y compris `impact_duty_pct`) | `SOLENOID_PROFILE_ERR_DUTY` |
| `pull_duration_us <= pull_max_us` ; `probe_duration_us <= probe_max_us` ; `impact_duration_us <= impact_max_us` | `SOLENOID_PROFILE_ERR_CONSISTENCY` |
| `dir_level_pull != dir_level_release` ; niveaux ∈ {0,1} | `SOLENOID_PROFILE_ERR_DIR` |
| `pwm_freq_hz ∈ [1, 100 000]` | `SOLENOID_PROFILE_ERR_PWM` |
| durées en ms `<= 4 000 000` (conversion µs `u32`) | `SOLENOID_PROFILE_ERR_DURATION` |
| version connue et longueur exacte (66) | `SOLENOID_PROFILE_ERR_VERSION` / `ERR_LENGTH` |

`impact_duration_us == 0` est **valide** : il désactive la phase. Il n'y a pas de
plafond de duty de phase propre à IMPACT (le plafond global `duty_max_pct`
s'applique, comme pour PULL et RELEASE). `impact_max_us` est un plafond de
**cohérence**, pas une limite de sécurité (ADR-0014).

Deux contrôles **fonctionnels de mesure** restent côté Moteur, car ils dépendent
de la fenêtre d'échantillonnage ADC (`firmware/moteur/src/ipropi_selftest.h`) :

1. la fenêtre d'échantillonnage couvre au moins une période PWM
   (l'espacement des échantillons est recalculé à partir de `pwm_freq_hz`) ;
2. `probe_duration_us > fenêtre ADC + marge` (558 µs avec les constantes
   actuelles : 8 échantillons espacés de 8 µs + 2 µs, marge 500 µs).

Un profil qui échoue l'un de ces deux contrôles est refusé avec
`CAN_PROFILE_RESULT_INVALID_FIELD`, comme les contrôles structurels.

## 4. Cycle de vie

```
[1] Clavier : compilation du firmware
    firmware/clavier/src/solenoid_profile_config.h -> k_solenoid_profile
        |
        | flash Clavier (BOOTSEL, clavier.uf2)
        v
[2] Clavier au boot
    - envoie UID_REQUEST (0x400)
    - chaque Moteur répond A (0x410) + B (0x411) + PROFILE_STATUS (0x423, transfer_id=0)
    - le Clavier compare profile_tag32 et source de chaque carte au CRC32 local :
        * identique + source NVM        -> rien
        * différent                     -> [STATUS] PROFILE_DIVERGENT
        * source DEFAULTS               -> [STATUS] PROFILE_FALLBACK_IN_USE
      AUCUNE poussée automatique : le profil ne part qu'à un auto-test demandé.
        |
        | (jeu normal : les cartes utilisent leur profil NVM, ou les défauts)
        v
[3] Auto-test IPROPI demandé (bouton Moteur 3 s -> 0x402, ou commande locale)
    start_selftest_request() :
      a. encode(k_solenoid_profile) -> 66 octets + CRC32 ; transfer_id++ (1..255)
      b. diffuse PROFILE_BEGIN (0x420), 11 x PROFILE_CHUNK (0x421), PROFILE_COMMIT (0x422)
      c. attend PROFILE_STATUS de chaque carte présente (timeout ~1000 ms)
         - toutes OK            -> émet IPROPI_SELFTEST_REQUEST (0x401)
         - manquante / erreur   -> abandonne, [STATUS] PROFILE_SYNC_FAILED, pas de 0x401
        |
        v
[4] Moteur, réception du profil
    BEGIN  -> staging réinitialisé (RAM)
    CHUNK  -> remplissage, seq strictement croissant
    COMMIT -> longueur + CRC + version + contrôles structurels + contrôles de mesure
              - refus  -> PROFILE_STATUS(result=erreur), ancien profil conservé
              - accepté:
                  * tag identique au profil durable (source NVM) -> pas d'écriture flash
                  * sinon (y compris source DEFAULTS à tag égal) -> écriture flash
                    (slot au seq le plus faible, seq+1), relecture + vérification,
                    PUIS application en RAM
                  * PROFILE_STATUS(result=OK, source=NVM, profile_tag32)
        |
        v
[5] Moteur : 0x401 -> sonde PROBE par canal, trames 0x412/0x600/0x601 (inchangé)
```

**Transition v1 → v2 (ADR-0019).** Un Moteur reflashé en v2 ne relit pas les
enregistrements flash v1 (version et longueur) : au premier boot après reflash,
il joue avec les **valeurs de repli** (`source = DEFAULTS`, IMPACT désactivé) et
le Clavier le signale `PROFILE_FALLBACK_IN_USE`. Le premier transfert explicite
(auto-test ou `p send`) écrit un enregistrement v2 et active la phase IMPACT. Un
Moteur resté en v1 rejette un profil v2 avec `UNSUPPORTED_VERSION` (7).

## 5. Persistance flash (Moteur)

* **Deux secteurs de 4 Kio** en fin de flash :
  `slot A = PICO_FLASH_SIZE_BYTES - 2 * 4096`,
  `slot B = PICO_FLASH_SIZE_BYTES - 4096`.
* En-tête de 32 octets :

  | Off. | Champ | Type | Remarque |
  |---|---|---|---|
  | 0..3 | `magic` | u32 | `'CVPF'` (`0x46505643`) |
  | 4..5 | `format_version` | u16 | 2 (ADR-0019 ; 1 avant) |
  | 6..7 | `header_size` | u16 | 32 |
  | 8..9 | `payload_len` | u16 | 66 |
  | 10..11 | `reserved` | u16 | 0 |
  | 12..15 | `seq` | u32 | monotone croissant |
  | 16..19 | `crc32` | u32 | CRC32 du payload seul |
  | 20..31 | `reserved` | — | 0 |

* Le payload (66 octets) suit l'en-tête. Au boot, le Moteur lit les deux slots,
  ignore ceux dont le magic, la version, la longueur ou le CRC sont invalides,
  et applique le `seq` le plus élevé. Aucun slot valide -> valeurs de repli
  (`source = DEFAULTS`).
* Écriture : toujours **hors IRQ**, depuis la boucle principale, tous canaux
  IDLE. Effacer le slot au `seq` le plus faible, programmer `seq + 1`, relire et
  vérifier, puis appliquer en RAM. Le slot au `seq` le plus élevé reste intact
  pendant l'opération (coupure d'alimentation sans perte).
* Contrôle de link `__flash_binary_end <= slot A` : **exigé par ADR-0013
  (IMP-006), non implémenté** à ce jour. Sans risque mesuré (fin de binaire
  ≈ 33,6 Ko vs slot A ≈ 1,99 Mo) mais à ajouter.
* `hardware_flash` et `pico_flash` sont liés au CMake Moteur.
* Un UF2/flash SWD normal ne touche pas les secteurs réservés ; seul un
  effacement complet (`flash erase_sector ... last`) les efface, ce qui ramène
  aux valeurs de repli.

## 6. Divergence et invariant 1

Le Clavier est la référence : son `profile_tag32` est calculé au boot depuis
`k_solenoid_profile`. Une carte qui annonce un autre tag, ou `source = DEFAULTS`,
est signalée en CDC (`[STATUS] PROFILE_DIVERGENT` / `PROFILE_FALLBACK_IN_USE`),
avec l'UID et les deux tags. **Jamais de correction silencieuse** : le profil
n'est jamais poussé hors auto-test explicite. Conséquence assumée : après un
changement du fichier Clavier, il faut flasher le Clavier **puis** lancer un
auto-test (ou un `p send`, ADR-0015).

## 7. Sécurité (ADR-0014)

Le firmware Moteur n'impose plus aucune fenêtre thermique : les coupures HOLD et
MAINTENANCE, les plafonds de duty et les repos utilisent les valeurs du profil.
Un profil cohérent mais dangereux est obéi. Depuis ADR-0019, la fenêtre à pleine
puissance du jeu normal s'allonge de `impact_max_us` : la même règle s'applique,
aucune borne fixe n'est ajoutée. La sécurité repose sur la relecture humaine du
fichier de réglage, les essais au banc et la trace du profil actif
(`profile_tag32`, journal CDC). Une protection matérielle de dernier recours
(fusible, limitation de courant du DRV8874) est recommandée mais hors périmètre
d'ADR-0014.

## 8. Points non démontrés

Sans matériel :

* Survie réelle du profil à une coupure d'alimentation pendant l'écriture flash.
* Persistance à travers un flash SWD de la carte Moteur (secteurs réservés).
* Valeur exacte de `PICO_FLASH_SIZE_BYTES` sur le RP2354B (2 Mio déclaré, non
  vérifié) et donc la position des slots.
* Latence de l'écriture flash (stall XIP) et son effet sur la réception CAN.
* Valeurs de profil optimales au banc (PULL, HOLD, PROBE, polarités).
* **Phase IMPACT (ADR-0019)** : effet mécanique et acoustique réel, montée de
  courant mesurée (IPROPI/oscilloscope), valeur utile de `impact_duration_us`,
  et intérêt de `impact_max_us` au-delà de 2 000 µs. Aucune validation
  matérielle.

Dette et compléments identifiés par la validation (2026-10-21) :

* **Store flash non couvert par les tests hôte** (dépend du SDK) : sélection
  A/B, magic/`seq`/CRC, repli, chemin d'erreur d'écriture.
* **Garde de link absente** (`__flash_binary_end <= slot A`, §5).
* Le CMake Moteur n'active pas `-Wall`/`-Wextra` : dette préexistante ; seules
  les unités touchées ont été contrôlées en strict.

## 9. État validé

* **v1 (ADR-0013/ADR-0014) — validée le 2026-10-21**, compilation + tests hôte
  (260 vérifications, 0 échec), aucune validation matérielle :

  ```powershell
  powershell -File tests\host\run_host_tests.ps1     # 260 verifications, 0 echec (exit 0)
  cmake --build "build/moteur"                        # -> build/moteur/moteur.elf, exit 0
  cmake --build "build/clavier"                       # -> build/clavier/clavier.uf2, exit 0
  ```

  Empreinte : Moteur text 37 800 o / bss 6 612 o ; Clavier text 72 200 o /
  bss 11 088 o ; `__flash_binary_end` = `0x100083A8` (≈ 33,6 Ko).

* **v2 (ADR-0019) — implémentée et validée le 2026-10-22** (compilation + tests
  hôte, aucune validation matérielle) :

  ```powershell
  powershell -File tests\host\run_host_tests.ps1   # 356 + 19 verifications, 0 echec (exit 0)
  cmake --build "build/moteur"                     # -> build/moteur/moteur.elf, exit 0
  cmake --build "build/clavier"                    # -> build/clavier/clavier.uf2, exit 0
  ```

  Contrôles complémentaires : recompilation **forcée** des unités modifiées
  (suppression des `.obj`) → 0 erreur / 0 avertissement ; passe stricte
  `-Wall -Wextra -Wpedantic` sur `shared/protocol/solenoid_profile.c`,
  `firmware/clavier/src/profile_tuning.c` et
  `firmware/moteur/src/solenoid_driver.c` → 0 avertissement ; vérification
  **indépendante** du layout binaire v2 (programme hôte jetable) :
  `WIRE_SIZE=66`, `FORMAT_VERSION=2`, `FLASH_VERSION=2`, ordre
  `version → pwm → impact_duration → impact_duty → impact_max → pull_duration`
  conforme octet par octet (offsets 0..17).

  **Non couvert** (validation matérielle à faire) : machine à états
  `solenoid_driver.c` (IMPACT→PULL, Note Off pendant IMPACT, durée 0 → PULL),
  store flash Moteur (rejet d'un slot v1), transfert `0x420`–`0x423` sur
  11 chunks, et effet physique d'IMPACT.

Pour forcer la recompilation des unités touchées (Ninja affiche sinon
`no work to do`), supprimer leurs `.obj` sous `build/<cible>/CMakeFiles/` avant
le build. Passe d'avertissements stricts sur les unités modifiées : rejouer la
commande de `build/moteur/build.ninja` avec `-Wall -Wextra -Wpedantic -fsyntax-only`
(retirer `-o`, `-MF`, `-MT`). Un défaut réel y a été trouvé et corrigé
(`can_protocol.c` : comparaison toujours vraie sur une énumération non signée,
masquée par le gcc hôte).

## 10. Questions ouvertes pour l'Architecte

1. **Attente d'acquittement du profil** : ADR-0013 §4 dit « chaque carte
   présente ». L'implémentation n'attend que les cartes **en ligne au dernier
   scan UID** ; sans table valide, elle n'attend aucun acquittement. Faut-il
   confirmer cette lecture (préserver le traitement des cartes manquantes,
   ADR-0009 §2) ou exiger un acquittement de toutes les positions amorcées, au
   risque de bloquer l'auto-test quand une carte est absente ?
2. **Valeurs de repli** : elles sont aujourd'hui une copie manuelle du fichier
   Clavier (`solenoid_profile_defaults.h`). La génération automatique au build a
   été demandée par l'utilisateur mais n'est couverte par aucune ADR : à
   formaliser (portée, couplage CMake, artefact).
3. **`pull_duty_pct` : 50 ou 100 ?** Le fichier Clavier vaut 50, tandis
   qu'ADR-0013/ADR-0014 et les valeurs de repli Moteur documentent 100. Cette
   divergence préexistante (non introduite par ADR-0019) n'est pas tranchée :
   aligner le repli sur 50, ou corriger la documentation sur 100 ?
4. **Protection matérielle de dernier recours** (fusible, limitation de courant
   DRV8874) recommandée par ADR-0014 IMP-010 : aucune décision prise.
5. **Plage d'IMPACT au banc** : ADR-0019 fixe `impact_max_us = 2000`. Si la
   mesure de `τ` montre qu'une impulsion plus longue est nécessaire, le plafond
   est réglable à chaud ; faut-il néanmoins une valeur par défaut plus large
   (3 000–5 000 µs) une fois `τ` connu ?
