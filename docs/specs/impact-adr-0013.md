# Impact ADR-0013 / ADR-0014 — profil d'activation des solénoïdes centralisé sur le Clavier

Note de référence pour le **Codeur** et le **Validateur**. Elle liste, fichier
par fichier, ce qu'il faut revoir parce que
[ADR-0013](../adr/adr-0013-profil-activation-solenoides-centralise-clavier.md)
(statut `Accepted`, 2026-10-21) déplace **tous** les paramètres d'activation des
solénoïdes dans un fichier unique côté Clavier, les transmet aux cartes Moteur au
lancement de l'auto-test IPROPI (`0x420`–`0x423`) et les fait **persister en
flash** côté Moteur ; et parce que
[ADR-0014](../adr/adr-0014-limites-activation-pilotees-profil-clavier-derogation-invariant-4.md)
(`Accepted`) **dérogé à l'invariant 4** : plus aucune enveloppe thermique n'est
figée dans le firmware Moteur.

Les chemins de ligne cités sont ceux **avant** modification (état du 2026-10-21) :
ils servent de point d'entrée, pas de référence stable. Les fichiers sous
`.kilo/worktrees/` sont des copies de travail : à exclure.

> **État : exécutée (2026-10-21).** L'implémentation est en place et validée en
> compilation/tests hôte (260 vérifications, 0 échec ; `moteur.elf` et
> `clavier.elf` liés). Les tableaux ci-dessous restent la référence de ce qui a
> été fait. Deux compléments restent exigés/utiles : la **garde de link**
> `__flash_binary_end <= slot A` (§4.3, non implémentée) et des **tests
> automatisés du store flash** (§5). Suivi des écarts et questions ouvertes :
> [`profil-solenoides.md`](./profil-solenoides.md) §8 et §10.

## 0. Règle à implémenter (rappel normatif)

| | Avant | Après |
|---|---|---|
| Où se règle le profil | `firmware/moteur/src/solenoid_profile.h` (macros compilées) | `firmware/clavier/src/solenoid_profile_config.h` (`k_solenoid_profile`) |
| Transport | aucun | `PROFILE_BEGIN`/`CHUNK`/`COMMIT`/`STATUS` (`0x420`–`0x423`) |
| Persistance Moteur | aucune | deux secteurs flash A/B, `seq` + CRC32 |
| Enveloppe thermique | bornes compilées + `_Static_assert` | **supprimée** ; seuls des contrôles structurels (ADR-0014) |
| Budget MAINTENANCE | `durée × duty ≤ 1 000 000` | supprimé ; `maint_max_ms` et `maint_duty_pct` font foi |
| Repli Moteur | — | `solenoid_profile_defaults.h` (valeurs de repli compilées) |
| Constantes ohmiques | dans `solenoid_profile.h` | `solenoid_ohm_constants.h`, hors profil |
| `CAN_PROTO_VERSION` | 1 | **inchangé** (trames additives) |

## 1. `shared/protocol/` — contrat de format

| Fichier | Revue requise |
|---|---|
| `solenoid_profile.h` | **créé** : `solenoid_profile_t`, constantes de format, `SOLENOID_PROFILE_ERR_*`, prototypes `encode`/`decode`/`validate`/`tag`/`crc32`. En-tête et layout déjà écrits. |
| `solenoid_profile.c` | **à écrire** : sérialisation champ par champ little-endian (57 octets), CRC32 (réutiliser/extraire l'algorithme de `commissioning_table.c:60-70`), `solenoid_profile_validate` (règles structurelles du §3 de `profil-solenoides.md`). |
| `can_protocol.h` | **modifié** : IDs `0x420`–`0x423`, DLC 8, `CAN_PROFILE_CHUNK_SIZE`, enums `can_profile_result_t`/`can_profile_source_t`, structs `can_profile_begin_t`/`_chunk_t`/`_commit_t`/`_status_t`, prototypes des 8 codecs. Déjà écrit. |
| `can_protocol.c` | **à écrire** : les 8 codecs (DLC strict, LE, octet 3 de STATUS = `result | source<<3`). |
| `commissioning_table.[ch]` | **inchangé** (modèle de CRC32 et de flash, mais aucun chevauchement). |

## 2. `firmware/moteur/` — cœur du changement

| Fichier | Revue requise |
|---|---|
| `src/solenoid_profile.h` | **supprimé** (ou vidé) : plus aucune macro de profil. Son contenu se répartit entre `solenoid_profile_defaults.h` (valeurs de repli), `solenoid_ohm_constants.h` (constantes de mesure) et le profil Clavier. |
| `src/solenoid_profile_defaults.h` | **créé** : `k_solenoid_profile_defaults` (mêmes valeurs que le fichier Clavier au moment de la migration), commenté « repli, jamais un point de réglage ». |
| `src/solenoid_ohm_constants.h` | **créé** : `A_IPROPI`, `R_IPROPI`, `VM`, `VREF`, `R_DS(on)` — valeurs inchangées. |
| `src/solenoid_profile_store.[ch]` | **créés** : lecture/écriture des slots A/B, magic `'CVPF'`, `seq`, CRC32, sélection du slot valide, écriture via `flash_safe_execute`, contrôle `__flash_binary_end`. |
| `src/solenoid_driver.c` | **modifié** : ne lit plus les macros. Reçoit un `const solenoid_profile_t *` (accesseur vers le profil actif). Les `_Static_assert` `:11-33` deviennent des asserts structurels + des contrôles d'exécution. Les clamps `:36-81` utilisent les champs du profil. `SOLENOID_WAKE_DELAY_US` `:99`, `SOLENOID_DIR_LEVEL_*` `:168,289`, `SOLENOID_PWM_FREQ_HZ` `:175` → champs. |
| `src/solenoid_driver.h` | **modifié** : signatures prenant le profil (ou accesseur `solenoid_driver_set_profile`), commentaires de contrat. |
| `src/ipropi_selftest.c` | **modifié** : `:18-19` et `:22` (constantes PROBE/PWM) et `:96-100` (constantes ohmiques) sortent du profil ; `solenoid_driver_probe_duration_us()`/`_duty_permil()` restent la seule source. Les `_Static_assert` `:22-26` deviennent des **contrôles de validation** (fenêtre ADC vs période PWM, fenêtre + marge vs durée PROBE). |
| `src/ipropi_selftest.h` | **modifié** : `IPROPI_SELFTEST_ADC_*` inchangés, mais utilisés par la validation du profil. |
| `src/ipropi_ohm.h` | commentaires `:21-28` à mettre à jour (constantes déplacées). |
| `src/can_link.c` | **modifié** : réception `0x420`–`0x423` dans `handle_frame` (`:274`, switch `:285-325`), machine d'états de staging, planification de l'écriture flash dans `can_link_poll` (`:373`) — **jamais** dans l'ISR `can_rx_cb` (`:55`). Émission de `PROFILE_STATUS` en complément de A/B (`:173,186,193`) et au boot. |
| `src/can_link.h` | **modifié** : prototypes/état du profil actif. |
| `src/main.c` | **modifié** : applique le profil persistant (ou les défauts) au boot, appelle le service d'écriture flash dans la boucle (`:101`), journalise le profil actif. |
| `CMakeLists.txt` | **modifié** : ajouter `solenoid_profile.c`, `solenoid_profile_store.c`, `solenoid_ohm_constants.h` ; lier `hardware_flash` et `pico_flash` (`:85`). |
| `boards/moteur_rp2354b.h` | **à vérifier** : `PICO_FLASH_SIZE_BYTES` (`:57`) conditionne la position des slots A/B. |

## 3. `firmware/clavier/` — réglage, transfert, diagnostic

| Fichier | Revue requise |
|---|---|
| `src/solenoid_profile_config.h` | **créé** : `k_solenoid_profile`, **seul fichier de réglage**. Modèle dans `docs/procedure-profil-solenoides.md` §2. |
| `src/can_link.c` | **modifié** : `start_selftest_request` (`:738`) diffuse le profil (BEGIN + 10 CHUNK + COMMIT) puis attend les `PROFILE_STATUS` **avant** d'émettre `0x401` (`:747`) ; abandon + `PROFILE_SYNC_FAILED` sinon. Traitement de `0x423` dans `handle_frame` (`:1537`) : mémorise tag/source par carte. Au boot (`can_link_init` `:1618`), compare les tags annoncés. |
| `src/can_link.h` | **modifié** : état par carte (tag, source), helpers. |
| `src/main.c` | **modifié** : boucle (`:283`) ; éventuelle commande locale de test ; affichage des avertissements de divergence. |
| `src/diag_log.c` / `.h` | **modifié** : lignes `[PROFILE]`/`[STATUS]` `PROFILE_DIVERGENT` (`0x33`), `PROFILE_FALLBACK_IN_USE` (`0x34`), `PROFILE_SYNC_FAILED` (`0x32`). |
| `src/commissioning.c` | **inchangé** : sert de **modèle** pour l'écriture flash Moteur (`flash_safe_execute` `:126`, offsets `:11`). |
| `CMakeLists.txt` | **modifié** : ajouter `solenoid_profile.c`. |

## 4. Points d'attention d'implémentation

1. **Atomicité** : le profil n'est appliqué en RAM qu'après écriture flash
   vérifiée (ou tag identique). L'échange se fait en section critique courte,
   aucun canal actif, aucune commande de note en cours.
2. **Écriture flash hors IRQ** : planifiée dans la boucle principale. Le
   `flash_safe_execute` provoque un stall XIP : ne pas l'appeler depuis
   `can_rx_cb` ni depuis `ipropi_selftest`.
3. **Repli durable** : si aucun slot valide n'existe et que le tag reçu est
   identique aux valeurs de repli, écrire quand même le slot (recommandé), pour
   que `source` passe de `DEFAULTS` à `NVM`.
4. **Contrôles de mesure** : la fenêtre ADC doit couvrir une période PWM et
   `probe_duration_us > fenêtre + marge` (558 µs avec les constantes actuelles).
   Ces deux contrôles dépendent du Moteur, pas du format partagé.
5. **Pas de rétro-compatibilité** : un Moteur ancien ne répond pas aux
   `PROFILE_*` ; le Clavier doit abandonner l'auto-test avec un message
   « profil non confirmé » plutôt que de sonder.
6. **Divergence** : jamais de poussée automatique hors auto-test ; le boot ne
   fait que comparer et journaliser (invariant 1).

## 5. Tests

| Fichier | Revue requise |
|---|---|
| `tests/host/run_host_tests.ps1` | ajouter `shared/protocol/solenoid_profile.c` à la ligne de compilation (`:13-16`). |
| `tests/host/test_phase3_helpers.c` | ajouter une suite profil : encode/decode v1, longueur, version inconnue, CRC32, `validate` (chaque règle structurelle), `tag`, enregistrement flash (magic/seq/CRC, sélection du slot), codecs `0x420`–`0x423`. |
| `docs/procedure-profil-solenoides.md` | procédure de réglage/build/test (créée). |
| Campagne manuelle au banc | à créer (`tests/`) : persistance après coupure, survie au flash SWD, sens des polarités, latence d'écriture. Non couvrable en hôte. |

## 6. Build et tests (procédure)

```powershell
powershell -File tests\host\run_host_tests.ps1   # attendu : 221+ vérifications, 0 échec
cmake --build "build/clavier"                    # -> build/clavier/clavier.uf2 (BOOTSEL)
cmake --build "build/moteur"                     # -> build/moteur/moteur.elf (SWD, OpenOCD)
```

Rediriger les sorties (`*> "build\...\build.log"`) et n'extraire que les erreurs
et le résumé. Détail : [`../tests/build_clavier_windows.md`](../../tests/build_clavier_windows.md)
et [`../tests/build_moteur_windows.md`](../../tests/build_moteur_windows.md).

**Forcer la recompilation** des unités touchées (Ninja affiche `no work to do`
sinon) : supprimer les `.obj` correspondants dans `build/.../CMakeFiles/`.

**Contrôle des avertissements** : les options Moteur n'incluent pas `-Wall` ;
rejouer la ligne de compilation de `build/moteur/build.ninja` avec
`-Wall -Wextra -fsyntax-only` sur les fichiers modifiés.

## 7. Ce qui **n'est pas** touché

- **Trames existantes** : `0x100`, `0x400`–`0x402`, `0x410`–`0x412`, `0x600`,
  `0x601` gardent format et DLC. `CAN_PROTO_VERSION` reste `1`.
- **Table de commissioning** (invariant 3) : format, écriture et règle
  d'assignation inchangés.
- **Moteur anonyme** (invariant 2) : le profil est une donnée de carte, pas un
  rôle musical.
- **Mode MAINTENANCE** (ADR-0012) : logique inchangée ; seules les valeurs de
  durée/duty/repos viennent désormais du profil.
- **Constantes ohmiques** : valeurs inchangées, seulement déplacées.
