---
title: "ADR-0013: Profil d'activation des solénoïdes centralisé sur le Clavier, transféré à l'auto-test IPROPI et persisté côté Moteur"
status: "Accepted"
date: "2026-10-21"
authors: "Agent Architecte"
tags: ["architecture", "firmware", "protocol", "hardware"]
supersedes: ""
superseded_by: ""
---

### Status
**Accepted**

**Dérogation à un invariant portée par ADR-0014** : l'invariant 4 est dérogé.
Les limites électriques restent appliquées par le pilote Moteur, mais leurs
valeurs proviennent intégralement du profil Clavier, sans borne fixe.
L'invariant 1 est **renforcé** (divergence de profil signalée explicitement).
L'invariant 5 est respecté : cette ADR précède toute modification de
`shared/protocol/`.

### Context

Aujourd'hui, **toutes** les constantes d'activation des solénoïdes vivent dans
`firmware/moteur/src/solenoid_profile.h` : elles sont exclusivement
`compile-time`, sans lecture runtime ni persistance (`firmware/moteur/` ne
contient aucune écriture flash). Le driver les écrête et des `_Static_assert`
vérifient l'enveloppe thermique (ADR-0007, ADR-0012). Conséquence : le moindre
ajustement de durée, de duty, de temps de repos ou de polarité impose de
recompiler et reflasher **jusqu'à cinq cartes Moteur**, alors que les cartes
sont anonymes, interchangeables et parfois difficiles d'accès.

La demande est de faire du firmware Clavier l'unique point de réglage :
un seul fichier à modifier et un seul firmware à flasher, les valeurs étant
transmises aux cartes Moteur au lancement de l'auto-test IPROPI, puis
conservées par elles d'une mise sous tension à l'autre. Les cartes Moteur
doivent néanmoins rester capables d'actionner un solénoïde entre un flash et
le premier auto-test : elles conservent donc des valeurs de repli compilées.

Hypothèse de lecture de la demande : « les valeurs existantes sont conservées
dans le firmware du clavier » est interprétée comme « conservées dans le
firmware **Moteur** comme valeurs de repli ». L'inverse serait circulaire (le
Clavier écraserait ses propres valeurs par celles qu'il vient d'envoyer).
Voir NEG-005 si l'intention était de supprimer tout repli côté Moteur.

Contraintes structurantes :

* **CAN classique, 8 octets de payload maximum** (`CAN_BITRATE_HZ` 500 kbit/s,
  `shared/protocol/can_protocol.h:23`). Il n'existe aujourd'hui ni
  fragmentation, ni multi-trames, ni CRC de trame, ni version de configuration
  (`0x500`/`0x501` sont réservés sans codec). Un profil complet ne tient pas
  dans une trame.
* **Cinq cartes Moteur anonymes** (invariant 2) : le profil est une donnée de
  carte, pas un rôle musical. Il doit pouvoir être diffusé et acquitté par
  chaque carte, sans identité autre que son `uid_tag` CRC16.
* **Invariant 4** : les limites électriques sont appliquées dans la couche
  pilote. ADR-0014 déroge à la partie qui figeait ces limites : le pilote
  applique les valeurs du profil, sans enveloppe prédéfinie.
* **Invariant 1** : toute divergence entre l'état d'une carte et la référence
  du Clavier est signalée, jamais corrigée silencieusement. Un profil différent
  entre le Clavier et une carte relève du même principe.
* **Flash** : effacement par secteurs de 4 KiB, écriture avec stall du XIP,
  `flash_safe_execute` obligatoire. `PICO_FLASH_SIZE_BYTES` (2 MiB) est déclaré
  mais non vérifié (`firmware/moteur/src/moteur_rp2354b.h:12-13`) et aucun
  linker script n'est versionné.
* **Distinction nécessaire** entre trois familles de constantes, aujourd'hui
  mélangées dans le même fichier :
  1. les **paramètres d'activation** (réglables, objet de cette ADR) ;
  2. les **valeurs de sécurité** validées au banc (ADR-0007, ADR-0012), qui
     deviennent des valeurs initiales du profil (ADR-0014), plus des bornes ;
  3. les **constantes de mesure** de la chaîne IPROPI (A_IPROPI, R_IPROPI, VM,
     VREF, R_DS(on)), qui caractérisent la carte et n'interviennent pas dans
     l'activation.

### Decision

#### 1. Propriété du profil : un seul fichier de réglage, côté Clavier

Le fichier unique de réglage devient
**`firmware/clavier/src/solenoid_profile_config.h`**. Il contient une unique
constante `k_solenoid_profile` (initialiseur désigné, commentaires en français)
avec **tous** les paramètres d'activation. C'est le seul fichier à modifier
pour ajuster le comportement des solénoïdes ; il est compilé dans le firmware
Clavier.

Le profil est **global** : une seule valeur pour les 49 solénoïdes des 5 cartes.
Les profils par canal ne sont pas retenus (ALT-007), mais le format est
versionné pour permettre leur ajout sans nouveau plan d'identifiants.

Le profil est une structure partagée, sérialisée explicitement en
little-endian, définie dans **`shared/protocol/solenoid_profile.[ch]`** (source
unique, invariant 5) :

| Champ | Type | Unité | Valeur initiale |
|---|---|---|---|
| `format_version` | u8 | — | 1 |
| `pwm_freq_hz` | u32 | Hz | 20 000 |
| `pull_duration_us` | u32 | µs | 15 000 |
| `pull_duty_pct` | u8 | % | 100 |
| `pull_max_us` | u32 | µs | 50 000 |
| `hold_duty_pct` | u8 | % | 25 |
| `hold_duty_max_pct` | u8 | % | 25 |
| `hold_max_ms` | u32 | ms | 20 000 |
| `release_duration_us` | u32 | µs | 5 000 |
| `release_duty_pct` | u8 | % | 50 |
| `probe_duration_us` | u32 | µs | 5 000 |
| `probe_duty_pct` | u8 | % | 20 |
| `probe_max_us` | u32 | µs | 10 000 |
| `probe_duty_max_pct` | u8 | % | 20 |
| `maint_duty_pct` | u8 | % | 50 |
| `maint_duty_max_pct` | u8 | % | 100 |
| `maint_max_ms` | u32 | ms | 20 000 |
| `maint_min_rest_ms` | u32 | ms | 2 000 |
| `min_rest_ms` | u32 | ms | 10 |
| `dir_safety_delay_us` | u16 | µs | 200 |
| `wake_delay_us` | u16 | µs | 2 000 |
| `dir_level_pull` | u8 | 0/1 | 0 |
| `dir_level_release` | u8 | 0/1 | 1 |
| `dir_level_maint` | u8 | 0/1 | 1 |
| `duty_max_pct` | u8 | % | 100 |

Taille canonique du payload v1 : **57 octets**, sans padding (sérialisation
champ par champ, jamais un `memcpy` de structure C). Les durées sont en µs
pour PULL/RELEASE/PROBE et en ms pour HOLD/MAINTENANCE/repos ; leur largeur
`u32` supprime les plafonds arbitraires de l'ancien `u16` (ADR-0014). Les
valeurs initiales restent celles validées au banc ; ce ne sont plus des bornes.
Seule contrainte : une valeur en ms doit rester convertible en µs `u32`
(`<= 4 000 000 ms`).

#### 2. Ce qui reste dans le firmware Moteur, et pourquoi

| Élément | Emplacement | Justification |
|---|---|---|
| Valeurs de repli | `firmware/moteur/src/solenoid_profile_defaults.h` | Permet d'actionner les solénoïdes entre un flash et le premier auto-test, ou après effacement complet de la flash. Jamais un point de réglage. |
| `A_IPROPI`, `R_IPROPI`, `VM`, `VREF`, `R_DS(on)` | `firmware/moteur/src/solenoid_ohm_constants.h` | Constantes de la chaîne de mesure (estimation ohmique ADR-0009), sans effet sur l'activation. Un profil de format v2 pourra les transporter si le besoin d'un réglage distant apparaît. |
| Contrôles structurels | `shared/protocol/solenoid_profile.c` | Rejet en bloc des profils incohérents ou non représentables ; aucune limite thermique (ADR-0014). |

**Aucune enveloppe fixe (ADR-0014).** Le profil peut modifier chaque valeur
dans les deux sens ; le firmware Moteur ne conserve aucun seuil thermique. Les
contrôles ci-dessous sont des contrôles de **représentabilité et de cohérence**,
jamais des limites de sécurité. Un profil qui les échoue est rejeté en bloc ;
l'ancien profil reste actif.

* duties `0..100 %` ; `hold_duty_pct <= hold_duty_max_pct`,
  `maint_duty_pct <= maint_duty_max_pct`,
  `probe_duty_pct <= probe_duty_max_pct`, et `duty_max_pct >=` chaque duty de
  phase ;
* `pull_duration_us <= pull_max_us`, `probe_duration_us <= probe_max_us` ;
* durées PULL/RELEASE : toute valeur `>= 0` (0 ignore la phase) ; PROBE
  strictement positive et conforme au contrôle de fenêtre ci-dessous ;
* `dir_level_pull != dir_level_release`, niveaux ∈ {0,1} ;
* `hold_max_ms`, `maint_max_ms`, `maint_min_rest_ms`, `min_rest_ms` <=
  4 000 000 ms (conversion en µs `u32`) ;
* `pwm_freq_hz ∈ [1, 100 000]` et la fenêtre d'échantillonnage ADC doit
  couvrir au moins une période PWM ; `probe_duration_us` doit dépasser
  `fenêtre ADC + marge` (558 µs avec les constantes actuelles de
  `ipropi_selftest.h`). Ces deux contrôles étaient des `_Static_assert` ; ils
  deviennent des contrôles d'exécution, l'espacement des échantillons étant
  recalculé à partir de la fréquence PWM appliquée.
* longueur, version et CRC du payload.

Aucune borne supérieure de durée, de duty (au-delà de 100 %), de budget
thermique ou de repos minimal n'est appliquée. `maint_max_ms` et
`maint_duty_pct` font foi directement ; le budget `maint_max x duty` est
supprimé (ADR-0014).

#### 3. Transport : quatre trames additives dans la plage `0x42x`

Aucune trame existante n'est modifiée : `0x400`, `0x401`, `0x402`, `0x410`,
`0x411`, `0x412`, `0x600`, `0x601` et `0x100` gardent leur format et leur DLC.
`CAN_PROTO_VERSION` reste 1. Les nouvelles trames sont diffusées ; le Moteur
les accepte sans filtrage d'UID (le profil est global).

**`PROFILE_BEGIN` — ID `0x420`, DLC 8 (Clavier → diffusion)**

| Octet | Champ | Valeurs |
|---|---|---|
| 0 | `proto_version` | 1 |
| 1 | `format_version` | 1 |
| 2 | `transfer_id` | 1..255, incrémenté par le Clavier ; 0 réservé à l'annonce de boot |
| 3 | `flags` | bit 0 = 1 (profil global) ; bits 1..7 = 0 |
| 4..5 | `payload_len` | u16 LE, 57 pour v1 |
| 6 | `chunk_size` | 6 |
| 7 | `reserved` | 0 |

**`PROFILE_CHUNK` — ID `0x421`, DLC 8 (Clavier → diffusion)**

| Octet | Champ | Valeurs |
|---|---|---|
| 0 | `transfer_id` | recopie du BEGIN |
| 1 | `seq` | 0..6, croissant, sans trou |
| 2..7 | `data[6]` | 6 octets du payload canonique |

Payload de 57 octets ⇒ 10 chunks (9×6 + 3). Un chunk orphelin (pas de BEGIN
actif, `transfer_id` inconnu, `seq` inattendu) est ignoré et compté, jamais
appliqué partiellement.

**`PROFILE_COMMIT` — ID `0x422`, DLC 8 (Clavier → diffusion)**

| Octet | Champ | Valeurs |
|---|---|---|
| 0 | `proto_version` | 1 |
| 1 | `format_version` | 1 |
| 2 | `transfer_id` | recopie du BEGIN |
| 3 | `reserved` | 0 |
| 4..7 | `crc32` | CRC32 du payload canonique, LE |

Le Moteur vérifie : transfert actif, tous les chunks 0..N-1 reçus une seule
fois, longueur exacte, CRC, version supportée, puis les contrôles structurels
(§2). En cas d'échec, il conserve le profil actif et répond avec le code
d'erreur correspondant : aucun écrêtage, aucune application partielle.

**`PROFILE_STATUS` — ID `0x423`, DLC 8 (Moteur → Clavier)**

| Octet | Champ | Valeurs |
|---|---|---|
| 0..1 | `uid_tag` | CRC16 LE |
| 2 | `transfer_id` | 0 = annonce de boot ; sinon recopie |
| 3 | `result` (bits 0..2) + `source` (bits 3..4) | voir ci-dessous ; bits 5..7 = 0 |
| 4..7 | `profile_tag32` | CRC32 du profil actif, LE |

`result` : 0 OK, 1 pas de transfert actif, 2 séquence/longueur invalide,
3 CRC invalide, 4 champ invalide ou incohérent, 5 occupé (canal actif), 6 erreur flash,
7 version non supportée.

`source` : 0 valeurs de repli compilées, 1 enregistrement flash.

La trame est émise **en complément de A+B à chaque `UID_REQUEST`** (et une fois
au boot de la carte, avec `transfer_id = 0`), avec le même délai dérivé de
l'UID que les trames d'annonce. Elle n'est pas une annonce spontanée hors boot.

#### 4. Séquence de l'auto-test IPROPI

À chaque `0x401` émis par le Clavier, **quelle qu'en soit l'origine** (bouton
Moteur via `0x402` ou commande locale), l'ordre devient :

1. Le Clavier calcule le payload canonique et son CRC32, puis diffuse
   `PROFILE_BEGIN`, les 10 `PROFILE_CHUNK`, `PROFILE_COMMIT`.
2. Chaque Moteur valide, écrit la flash si le CRC diffère de son profil actif,
   applique le profil en RAM, puis émet `PROFILE_STATUS`.
3. Le Clavier attend un `PROFILE_STATUS` de **chaque carte présente** (mêmes
   règles de présence que l'auto-test : `uid_position.h` + table de
   commissioning) pendant `CAN_PROFILE_COMMIT_TIMEOUT_MS` (proposé : 1 000 ms,
   couvrant effacement + programmation + vérification).
4. Si toutes les cartes répondent OK, le Clavier émet `0x401` comme aujourd'hui.
   Si une carte manque, répond un code d'erreur ou reste en source « repli »,
   le Clavier **n'émet pas** `0x401` : l'auto-test est abandonné et signalé
   (CDC + code STATUS). Aucune sonde ne doit être lancée avec des paramètres
   PROBE divergents d'une carte à l'autre.

Le Moteur refuse un `PROFILE_COMMIT` si un canal est actif (`BUSY`) : l'écriture
flash ne doit jamais interrompre une activation en cours.

#### 5. Persistance côté Moteur : deux secteurs, séquence, CRC

Deux secteurs de 4 KiB sont réservés à la fin de la flash du Moteur :

```
slot A = PICO_FLASH_SIZE_BYTES - 2 * FLASH_SECTOR_SIZE
slot B = PICO_FLASH_SIZE_BYTES - 1 * FLASH_SECTOR_SIZE
```

Chaque slot contient un enregistrement : `magic` (`'CVPF'`), `format_version`,
`payload_len`, `seq` u32 monotone, CRC32 du payload, puis le payload canonique.
Au boot, le Moteur lit les deux slots, ignore ceux dont le magic, la version, la
longueur ou le CRC sont invalides, et applique celui du `seq` le plus élevé.
Si aucun slot n'est valide, il applique les valeurs de repli et annonce
`source = 0`.

Écriture (toujours hors IRQ, depuis la boucle principale, sorties coupées) :
effacer le slot au `seq` le plus faible, programmer l'enregistrement avec
`seq + 1`, relire et vérifier le CRC, puis seulement appliquer le profil en RAM
et émettre `PROFILE_STATUS OK`. Le slot au `seq` le plus élevé reste intact
pendant toute l'opération : une coupure d'alimentation ne peut pas laisser le
Moteur sans profil valide.

Si le CRC reçu est identique à celui du profil actif, **aucune écriture flash**
n'est faite : le Moteur applique (déjà identique) et acquitte OK. L'endurance
n'est consommée que lors d'un vrai changement de profil.

L'accès flash ajoute `hardware_flash` et `pico_flash` au CMake du Moteur. Un
garde de link vérifie que `__flash_binary_end` reste sous `slot A` ; si
`PICO_FLASH_SIZE_BYTES` est faux, le build échoue au lieu de corrompre le
firmware.

#### 6. Divergence de profil : signalement explicite (invariant 1)

Au boot du Clavier, chaque carte présente renvoie son `profile_tag32` et sa
`source` en réponse à `UID_REQUEST`. Le Clavier compare avec le CRC32 de son
propre fichier de réglage :

* tags identiques : rien à signaler ;
* tags différents : avertissement CDC `PROFILE_DIVERGENT` (UID, tag carte,
  tag Clavier, source), **sans aucune poussée automatique** ;
* carte en source « repli » : avertissement renforcé
  `PROFILE_FALLBACK_IN_USE` ; la carte joue avec des valeurs de repli
  conservatrices tant qu'aucun auto-test n'a été demandé.

Le profil n'est **jamais** transmis au boot ni à la première note. Il ne l'est
qu'à un auto-test explicitement demandé, conformément à l'invariant 1 (pas de
correction silencieuse) et à la demande. Conséquence assumée : après un
changement du fichier Clavier, il faut flasher le Clavier **puis** lancer un
auto-test pour que les cartes adoptent les nouvelles valeurs.

### Consequences

#### Positive
* **POS-001**: Un seul firmware à flasher pour régler les solénoïdes ; les cinq
  cartes Moteur ne sont plus reflashées lors d'un ajustement de profil.
* **POS-002**: Les valeurs survivent aux coupures d'alimentation et aux mises à
  jour UF2 du Moteur (secteurs réservés hors image), sans reconfiguration au
  boot.
* **POS-003**: Transmission atomique : le profil n'est appliqué qu'après
  validation complète (CRC + contrôles structurels) sur `PROFILE_COMMIT` ; un
  chunk perdu ou une trame corrompue laisse l'ancien profil intact.
* **POS-004**: Le pilote applique les limites du profil et coupe aux échéances
  configurées ; une valeur incohérente est rejetée, mais une valeur dangereuse
  mais cohérente est obéie (ADR-0014).
* **POS-005**: Toute divergence Clavier/Moteur est visible au boot (tag + source)
  et avant l'auto-test, jamais corrigée en silence (invariant 1).
* **POS-006**: Persistance A/B avec `seq` et CRC : une coupure pendant
  l'écriture flash ne peut pas rendre le profil indéchiffrable.

#### Negative
* **NEG-001**: Complexité protocolaire : 4 nouvelles trames, un transfert
  fragmenté, une machine d'états de réception sur le Moteur et une attente
  d'acquittements côté Clavier.
* **NEG-002**: Écriture flash sur le Moteur : stall du XIP, IRQ CAN différées
  pendant l'effacement/programmation. Fenêtre aveugle d'environ 100 ms,
  acceptable seulement parce que les sorties sont coupées et qu'aucun test
  n'est en cours.
* **NEG-003**: Les valeurs de repli du Moteur dupliquent le profil du Clavier et
  peuvent dériver. La divergence est signalée, mais elle existe entre deux
  auto-tests.
* **NEG-004**: Le nouveau profil n'est actif qu'après un auto-test explicite ;
  un ajustement suivi d'un simple redémarrage ne change pas le comportement des
  cartes.
* **NEG-005**: Sans valeurs de repli côté Moteur (si l'intention de la demande
  était de les supprimer), une carte effacée ne pourrait plus actionner de
  solénoïde avant un auto-test ; le Clavier devrait refuser de jouer, ce qui
  contredit l'objectif « activables entre un flash et un auto-test ».
* **NEG-006**: Le CRC de profil est calculé sur la représentation canonique ;
  toute évolution du format impose une nouvelle `format_version` et une
  procédure de compatibilité, sinon les anciens profils sont rejetés (rejet
  explicite, pas d'interprétation).
* **NEG-007**: Aucune borne fixe ne protège plus contre un profil
  structurellement valide mais thermiquement dangereux ; la sécurité devient
  procédurale (ADR-0014).

### Alternatives Considered

#### Profil maintenu côté Moteur, réglage à distance en RAM uniquement
* **ALT-001**: **Description**: garder `solenoid_profile.h` dans le firmware
  Moteur et ajouter une trame de configuration volatile, sans flash.
* **ALT-002**: **Rejection Reason**: ne répond pas à l'objectif (deux firmwares
  à maintenir, réglage perdu à chaque coupure) et laisse le profil dépendre
  d'un envoi au boot, donc d'une correction silencieuse.

#### Profil uniquement en RAM, renvoyé à chaque boot
* **ALT-003**: **Description**: le Clavier diffuse le profil au démarrage et
  après chaque changement, sans persistance côté Moteur.
* **ALT-004**: **Rejection Reason**: la demande exige la conservation d'une
  mise sous tension à l'autre ; un Clavier absent ou en retard laisserait les
  cartes avec un profil obsolète ou des valeurs de repli non signalées.

#### Trame unique indexée (écriture champ par champ)
* **ALT-005**: **Description**: une trame par champ ou un index + valeur,
  répétée pour chaque paramètre.
* **ALT-006**: **Rejection Reason**: application partielle et non atomique,
  aucun CRC global, état intermédiaire incohérent pendant le transfert,
  multiplication des trames et des courses avec le jeu.

#### Profils par canal
* **ALT-007**: **Description**: un profil par solénoïde (49 entrées) ou par
  carte (5 entrées), pour compenser les dispersions de bobines.
* **ALT-008**: **Rejection Reason**: non demandé, payload et protocole beaucoup
  plus lourds, et aucune donnée de dispersion ne justifie aujourd'hui un réglage
  individuel. Le format versionné permet d'ajouter un bloc par canal en v2 sans
  nouveau plan d'identifiants.

#### Conserver une enveloppe de sécurité fixe côté Moteur
* **ALT-009**: **Description**: garder les maxima validés (HOLD 20 s / 25 %,
  PULL 50 ms, PROBE 10 ms / 20 %, budget MAINTENANCE) comme bornes non
  contournables ; le fichier Clavier ne pourrait que les réduire.
* **ALT-010**: **Rejection Reason**: contredit la demande (« toutes les
  valeurs », sans limite directionnelle) et empêcherait tout essai au-delà des
  valeurs actuelles sans reflasher les cartes. Le risque est accepté et encadré
  par ADR-0014 (dérogation à l'invariant 4).

#### Stockage dans une EEPROM/FRAM externe
* **ALT-011**: **Description**: ajouter une mémoire non volatile dédiée sur
  chaque carte Moteur.
* **ALT-012**: **Rejection Reason**: composant et routage supplémentaires sur
  cinq cartes, pour un profil écrit quelques fois par séance ; la flash interne
  du RP2354B suffit largement en endurance (quelques dizaines d'écritures par
  secteur et par an).

#### Envoi du profil à chaque `SOLENOID_COMMAND`
* **ALT-013**: **Description**: joindre les paramètres utiles à chaque commande
  de note, sans profil persistant.
* **ALT-014**: **Rejection Reason**: alourdit la trame temps réel de jeu,
  duplique les paramètres à chaque note, ne couvre pas HOLD/RELEASE/MAINTENANCE
  de façon cohérente et empêche toute validation globale.

### Implementation Notes

* **IMP-001**: Le driver ne doit plus lire de macros de profil. Il reçoit un
  `const solenoid_profile_t *` (profil actif en RAM) et applique les plafonds
  de ce profil. Les `_Static_assert` de `solenoid_driver.c:11-33` deviennent :
  (a) des asserts de représentabilité et de cohérence structurelle, (b) des
  contrôles d'exécution sur le profil reçu. Aucune enveloppe fixe (ADR-0014).
* **IMP-002**: `solenoid_driver_probe_duration_us()` et
  `solenoid_driver_probe_duty_permil()` restent les seules sources des
  paramètres PROBE réellement appliqués ; `ipropi_selftest.c` ne doit jamais
  relire les champs bruts. Les deux `_Static_assert` de
  `ipropi_selftest.c:22-26` deviennent des contrôles de validation du profil
  (fenêtre ADC vs période PWM, fenêtre + marge vs durée PROBE).
* **IMP-003**: Déplacer les constantes ohmiques vers
  `solenoid_ohm_constants.h` sans changer leurs valeurs ; ne pas les inclure
  dans le payload v1.
* **IMP-004**: La réception du profil doit être une machine d'états distincte
  de la machine d'auto-test : `BEGIN` réinitialise le staging, `CHUNK` remplit,
  `COMMIT` valide et planifie l'écriture. Un timeout de staging (proposé :
  500 ms) libère le buffer si le transfert reste incomplet. L'échange du profil
  actif en RAM se fait en section critique courte, après vérification qu'aucun
  canal n'est actif et qu'aucune commande de note n'est en cours de traitement.
* **IMP-005**: L'écriture flash est planifiée dans la boucle principale, jamais
  dans le callback CAN ni dans une IRQ. Vérifier que tous les canaux sont IDLE
  avant l'effacement. Utiliser `flash_safe_execute` et relire le slot après
  programmation.
* **IMP-006**: Ajouter `hardware_flash` et `pico_flash` à
  `firmware/moteur/CMakeLists.txt` et un contrôle de link
  `__flash_binary_end <= slot A`. Vérifier la taille réelle de la flash du
  RP2354B sur le prototype avant de figer les offsets.
* **IMP-007**: Côté Clavier, la séquence de `start_selftest_request()` devient :
  calcul du payload + CRC, diffusion du profil, attente des `PROFILE_STATUS`,
  puis `0x401`. Le `transfer_id` doit être non nul et incrémenté à chaque
  tentative ; un timeout ou un code d'erreur abandonne le test et produit un
  message CDC explicite. Les 12 trames doivent respecter la file TX de can2040
  (ne pas les émettre plus vite qu'elle ne se vide). Pas de retry automatique
  dans cette version : l'utilisateur relance l'auto-test, ce qui produit un
  nouveau `transfer_id`.
* **IMP-008**: Le Clavier doit conserver le dernier `profile_tag32` et la
  `source` de chaque carte pour le diagnostic de boot. Ne jamais écrire ce tag
  en flash côté Clavier : la référence est le fichier compilé.
* **IMP-009**: Tests hôte à ajouter : encodage/décodage v1, CRC32, chunk perdu,
  chunk dupliqué, `transfer_id` périmé, CRC faux, version inconnue, chaque
  règle structurelle, sélection A/B (séquences, slot corrompu), repli sur
  défauts. Test manuel documenté dans `tests/` : deux cartes, modification d'un
  seul paramètre, flash du Clavier seul, auto-test, vérification du tag,
  coupure d'alimentation, reflash UF2 du Moteur, contrôle de la persistance.
* **IMP-010**: Mettre à jour `docs/specs/CAN.md` (trames 0x420–0x423 et codes
  STATUS `PROFILE_SYNC_FAILED`, `PROFILE_DIVERGENT`,
  `PROFILE_FALLBACK_IN_USE`), `docs/specs/Detail_architecture.md` (propriété du
  profil) et la documentation du module Moteur. Le Documentaliste fige les
  offsets exacts du payload et du format flash.
* **IMP-011**: Ne pas bumper `CAN_PROTO_VERSION` : les trames existantes sont
  inchangées. Un Moteur ancien ne répond pas aux nouvelles trames ; le Clavier
  doit alors abandonner l'auto-test avec un message « profil non confirmé »
  plutôt que de sonder avec des paramètres divergents.

### References
* **REF-001**: [ADR-0001 — HAL solénoïde DRV8874](./adr-0001-hal-solenoide-drv8874-phen.md)
* **REF-002**: [ADR-0004 — Auto-test IPROPI, table d'assignation et SOLENOID_COMMAND](./adr-0004-auto-test-ipropi-table-assignation-solenoide-command.md) (§1 : localisation du profil PROBE ; §4 : annonce de boot complétée par PROFILE_STATUS)
* **REF-003**: [ADR-0006 — Mode maintenance push-only](./adr-0006-mode-maintenance-push-only.md) (§4 : budget thermique, supprimé comme borne par ADR-0014)
* **REF-004**: [ADR-0007 — Limite HOLD à 20 s](./adr-0007-hold-limite-20s.md) (valeur devenue défaut, ADR-0014)
* **REF-005**: [ADR-0009 — Commissioning provisoire et estimation de résistance](./adr-0009-commissioning-provisoire-bring-up-et-estimation-resistance-bobine.md)
* **REF-006**: [ADR-0012 — Maintenance en polarité inverse](./adr-0012-mode-maintenance-force-inverse-duree-20s.md)
* **REF-007**: `docs/specs/CAN.md` — plan d'identifiants et trames Phase 3b
* **REF-008**: Datasheet DRV8874 (PWM, tWAKE, R_DS(on), IPROPI)
* **REF-009**: Raspberry Pi Pico SDK — `pico_flash`, `flash_safe_execute`, `FLASH_SECTOR_SIZE`
* **REF-010**: [ADR-0014 — Limites d'activation entièrement pilotées par le profil Clavier](./adr-0014-limites-activation-pilotees-profil-clavier-derogation-invariant-4.md)
