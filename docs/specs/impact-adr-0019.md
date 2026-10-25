# Impact ADR-0019 — phase IMPACT avant PULL (profil v2)

> **Note (2026-10-25).** Ce document décrit la mise en œuvre initiale (v2). Le
> format courant est la **v3** (54 octets, 9 chunks, version flash 3) : les
> champs `impact_max_us`, `pull_max_us` et `probe_max_us` mentionnés ci-dessous
> ont été supprimés (amendement ADR-0014 du 2026-10-25). Référence à jour :
> [`profil-solenoides.md`](./profil-solenoides.md) §2.

Note de référence pour le **Codeur** et le **Validateur**. Elle liste, fichier
par fichier, ce qu'il faut revoir parce que
[ADR-0019](../adr/adr-0019-phase-impact-impulsion-courte-avant-pull.md)
(`Accepted`, 2026-10-22) insère un état **IMPACT** avant PULL dans la machine à
états du Moteur et ajoute **trois champs** au profil d'activation, ce qui passe
le format partagé en **v2** (66 octets, 11 chunks).

> **État (2026-10-22) : exécutée.** Tous les points ci-dessous sont implémentés ;
> la compilation des deux cibles et les tests hôte passent (356 + 19
> vérifications, 0 échec), et le layout binaire v2 a été vérifié indépendamment.
> Seule la **validation matérielle** de la phase IMPACT reste à faire
> (`tests/phase3b_profil_tuning.md` §2.4). Détail de l'état validé :
> [`profil-solenoides.md`](./profil-solenoides.md) §9.

Complément de [`impact-adr-0013.md`](./impact-adr-0013.md) (qui décrit le
profil v1) : cette note ne couvre que le **delta** d'ADR-0019.

Les chemins de ligne cités sont ceux de l'état du 2026-10-22 : point d'entrée,
pas référence stable. Les fichiers sous `.kilo/worktrees/` sont des copies de
travail : à exclure.

## 0. Règle à implémenter (rappel normatif)

| | Avant (v1) | Après (v2, ADR-0019) |
|---|---|---|
| Enchaînement du jeu | `IDLE → PULL → HOLD → RELEASE` | `IDLE → **IMPACT** → PULL → HOLD → RELEASE` |
| Champs de profil | 24 (payload 57 o) | **27** (payload **66 o**) |
| `format_version` | 1 | **2** |
| Chunks de transfert | 10 | **11** (66 = 11×6) |
| Version flash Moteur | 1 | **2** |
| `CAN_PROTO_VERSION` | 1 | **inchangé** (trames additives, aucun nouvel ID) |
| Enveloppe de sécurité | aucune (ADR-0014) | **aucune** (pas de nouvelle dérogation) |
| Repli Moteur IMPACT | — | **désactivé** (`impact_duration_us = 0`) |

Nouveaux champs, insérés **après `pwm_freq_hz`** dans l'ordre canonique :

| Champ | Type | Défaut fichier Clavier | Défaut repli Moteur | Contrôle |
|---|---|---|---|---|
| `impact_duration_us` | u32 µs | 800 | 0 (désactivé) | `<= impact_max_us` ; 0 désactive |
| `impact_duty_pct` | u8 % | 100 | 100 | `<= 100`, `<= duty_max_pct` |
| `impact_max_us` | u32 µs | 2 000 | 2 000 | plafond de cohérence |

## 1. `shared/protocol/` — contrat de format

| Fichier | Revue requise |
|---|---|
| `solenoid_profile.h` | **modifié** : ajouter les 3 champs à `solenoid_profile_t` après `pwm_freq_hz` ; `SOLENOID_PROFILE_FORMAT_VERSION` 1→2 (`:30`) ; `SOLENOID_PROFILE_WIRE_SIZE` 57→66 (`:33`) ; documenter le nouvel octet-layout (`:62-87`). `CAN_PROFILE_PAYLOAD_MAX` (128) reste suffisant. |
| `solenoid_profile.c` | **modifié** : `encode` (`:41-86`) et `decode` (`:88-136`) sérialisent les 3 champs dans l'ordre ; `validate` (`:138-180`) ajoute `impact_duty_pct <= duty_max_pct` (règle DUTY), `impact_duration_us <= impact_max_us` (règle CONSISTENCY). Le contrôle `data[0] != SOLENOID_PROFILE_FORMAT_VERSION` (`:94`) et la longueur suivent les constantes : aucun littéral à changer. |
| `can_protocol.[ch]` | **inchangé** : les codecs `0x420`–`0x423` ne dépendent pas de la taille du payload (`payload_len` est libre de 1 à 128, `chunk_size` reste 6). |

## 2. `firmware/moteur/` — cœur du changement

| Fichier | Revue requise |
|---|---|
| `src/solenoid_driver.h` | **modifié** : ajouter `SOLENOID_STATE_IMPACT` à l'énumération (`:22-33`) et le mettre à jour dans le diagramme d'en-tête (`:5-8`). |
| `src/solenoid_driver.c` | **modifié** : `note_on` (`:174-202`) entre en `IMPACT` (duty `impact_duty_pct`) si `impact_duration_us > 0`, sinon va directement en PULL ; nouveau cas dans `tick` (`:313-322`) : à l'expiration d'`impact_duration_us`, passe en PULL (timer PULL redémarré) ou va en RELEASE si `note_off_pending` ; `note_off` (`:204-218`) traite IMPACT comme PULL. Helper `impact_duration_us()` = `min(impact_duration_us, impact_max_us)` sur le modèle de `pull_duration_us()` (`:23-27`). `elapsed()` inchangé. |
| `src/solenoid_profile_defaults.h` | **modifié** : ajouter les 3 champs, `impact_duration_us = 0` **phase désactivée** (commentaire explicite). |
| `src/solenoid_profile_store.[ch]` | **modifié** : `SOLENOID_PROFILE_FLASH_VERSION` 1→2 (`store.h:49`). `slot_parse` (`store.c:74,76,81,84`) et `build_sector` (`:151-159`) utilisent déjà `SOLENOID_PROFILE_WIRE_SIZE` / `_FLASH_VERSION` : aucun littéral à changer. Un slot v1 est ignoré → repli. |
| `src/can_link.c` | **vérifié** : contrôle de version au COMMIT (`:396-400`) utilise déjà `SOLENOID_PROFILE_FORMAT_VERSION` et `SOLENOID_PROFILE_WIRE_SIZE` : aucun littéral. Un Moteur v1 rejette un profil v2 avec `UNSUPPORTED_VERSION` (7). |
| `src/ipropi_selftest.c` | **inchangé** fonctionnellement : lit le profil actif pour PROBE ; `ipropi_selftest_profile_is_compatible` (`:48`) ne dépend pas des champs IMPACT. Recompiler (structure modifiée). |
| `src/main.c` | **inchangé** fonctionnellement : journalise `profile_tag32`/`source` (`:106-107`). Le tag change (CRC sur 66 o) : normal. |

## 3. `firmware/clavier/` — réglage et transfert

| Fichier | Revue requise |
|---|---|
| `src/solenoid_profile_config.h` | **modifié** : ajouter `impact_duration_us = 800u`, `impact_duty_pct = 100u`, `impact_max_us = 2000u` (commentaire « impulsion courte avant PULL »). Modèle : `docs/procedure-profil-solenoides.md` §2. |
| `src/profile_tuning.c` | **modifié** : 3 entrées `FIELD(...)` **sans verrou** dans `g_fields[]` (`:27-52`), dans l'ordre de la structure (après `pwm_freq_hz`). `read_field`/`write_field` gèrent u8 et u32 sans changement. Table écrite à la main : ne pas l'oublier, sinon `p set impact_* ...` → « champ inconnu ». |
| `src/bringup_console.c` | **inchangé** : itère la table (`:56`) ; `p show` affiche 27 lignes au lieu de 24. |
| `src/can_link.c` | **vérifié** : `begin_profile_sync` (`:901`) dimensionne sur `SOLENOID_PROFILE_WIRE_SIZE` (`:903`) et boucle sur `length` ; `.format_version = SOLENOID_PROFILE_FORMAT_VERSION` (`:921,952`). Aucun littéral de taille. |
| `src/can_link.h` | **inchangé**. |

## 4. Tests et outils

| Fichier | Revue requise |
|---|---|
| `tests/host/test_phase3_helpers.c` | **modifié** : `CHECK(SOLENOID_PROFILE_WIRE_SIZE == 57u)` (`:671`) et les littéraux `57u` de `PROFILE_BEGIN` (`:711,723,725`) passent à 66 ; ajouter les cas `validate` IMPACT (duration > max, duty > duty_max, 0 désactive) ; mettre à jour la liste `wanted[]` des noms de champs (`:827`). |
| `tests/host/test_console_tuning.c` | **modifié** : la fixture et les comptes (`:80-85`, `:159`) reflètent les 3 champs supplémentaires ; ajouter `p set impact_*` / `p diff`. |
| `tests/tools/gen_profile_inject.c` | **à vérifier** : dimensionne via `SOLENOID_PROFILE_WIRE_SIZE` (`:27`) et boucle sur `length` (`:65`) → suit la constante. La valeur injectée (`-InjectPullUs`) reste PULL ; vérifier que le CRC et le nombre de chunks restent cohérents. |
| `tests/tools/moteur_swd_profile.ps1` | **à vérifier** : n'impose pas de taille ; lit `g_profile_commit_trace`/`stats`. Rejouer après reflash Moteur. |
| `tests/host/run_host_tests.ps1` | **inchangé** (mêmes sources). |
| Campagne manuelle | **à créer/étendre** : `tests/phase3b_profil_tuning.md` §2 (réglage IMPACT à chaud) + mesure oscilloscope/IPROPI (§ Nouveau). |

## 5. Documentation

| Fichier | Revue requise |
|---|---|
| `docs/specs/profil-solenoides.md` | **fait** : payload v2, offsets, règles, flash v2, segmentation §1.b. |
| `docs/specs/CAN.md` | **fait** : `format_version` 2, `payload_len` 66, `seq 0..10`, 11 chunks, en-tête flash v2. |
| `docs/procedure-profil-solenoides.md` | **fait** : snippet `k_solenoid_profile`, reflash unique Moteur, contrôles IMPACT. |
| `docs/specs/Detail_architecture.md` | **fait** : paragraphe profil complété (IMPACT, v2). |
| `docs/specs/Roadmap.md` | **fait** : diagramme Phase 2 et suivi Phase 3b/6. |
| `tests/phase3b_profil_tuning.md` | **fait** : 24 → 27 lignes, essais IMPACT. |

## 6. Points d'attention d'implémentation

1. **Sémantique des durées** : `pull_duration_us` reste la durée du **seul**
   segment PULL ; son timer repart à la fin d'IMPACT. Ne pas additionner
   implicitement les deux dans le code.
2. **Note Off pendant IMPACT** : aller en RELEASE (avec délai de sécurité), sans
   passer par PULL ; `note_off_pending` doit être traité comme pendant PULL.
3. **Note On pendant IMPACT** : refus `BUSY` (comportement inchangé des états
   non IDLE).
4. **`impact_duration_us == 0`** : aller directement en PULL (aucun état IMPACT,
   aucune écriture de duty supplémentaire).
5. **Repli Moteur désactivé** : après reflash, avant la première synchronisation,
   IMPACT ne doit pas s'exécuter ; le journal doit montrer `source = DEFAULTS`
   (`PROFILE_FALLBACK_IN_USE`), puis la phase s'active après un transfert v2.
6. **Atomicité** : les 3 champs voyagent dans le même payload ; un profil où
   `impact_duration_us > impact_max_us` est rejeté en bloc (ancien profil
   conservé), jamais écrêté.
7. **Pas de nouvelle trame ni de nouveau code de résultat** : le désaccord de
   version se lit avec `result = 7` et se journalise explicitement (message
   « version de profil non supportée — reflasher la carte », pas un échec
   générique).

## 7. Build et tests (procédure)

```powershell
powershell -File tests\host\run_host_tests.ps1   # attendu : 260+ verifications, 0 echec
cmake --build "build/clavier"                    # -> build/clavier/clavier.uf2 (BOOTSEL)
cmake --build "build/moteur"                     # -> build/moteur/moteur.elf (SWD, une fois)
```

**Forcer la recompilation** des unités touchées (Ninja affiche `no work to do`
sinon) : supprimer les `.obj` correspondants dans `build/.../CMakeFiles/`.

**Contrôle des avertissements** : le CMake Moteur n'active pas `-Wall` ; rejouer
la ligne de compilation de `build/moteur/build.ninja` avec `-Wall -Wextra
-fsyntax-only` sur les fichiers modifiés.

## 8. Ce qui **n'est pas** touché

- **Trames existantes** : `0x100`, `0x400`–`0x402`, `0x410`–`0x412`, `0x600`,
  `0x601` gardent format et DLC. `CAN_PROTO_VERSION` reste `1`.
- **Codecs `0x420`–`0x423`** : aucune modification (le payload est opaque).
- **MAINTENANCE (ADR-0012)** et **PROBE (ADR-0009)** : la phase IMPACT ne
  s'applique qu'au jeu normal.
- **Table de commissioning** (invariant 3) et **anonymat des cartes**
  (invariant 2) : inchangés.
- **ADR-0015 D2** (transfert volatil) : non implémentée, non concernée.
