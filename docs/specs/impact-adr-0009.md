# Impact ADR-0009 — interfaces de `shared/protocol/` touchées

Note de référence pour le **Codeur** et le **Validateur**. Elle liste, fichier
par fichier, les points à revoir parce que
[ADR-0009](../adr/adr-0009-commissioning-provisoire-bring-up-et-estimation-resistance-bobine.md)
modifie deux interfaces partagées (invariant 5) :

1. le **format de la table de commissioning** (`commissioning_table.[ch]`) :
   en-tête v1 12 octets → **v2 16 octets** avec `state` et `flags` ;
2. le **protocole CAN** (`can_protocol.[ch]`) : nouvelle trame
   `IPROPI_OHM` (`0x601`, DLC 8).

Les chemins de ligne cités sont ceux **avant** modification (état du
2026-10-20) : ils servent de point d'entrée, pas de référence stable.

## 1. `commissioning_table.[ch]` — format flash v2

| Fichier | Point d'entrée | Revue requise |
|---|---|---|
| `shared/protocol/commissioning_table.h` | `:13` `COMMISSIONING_FLASH_FORMAT_VERSION 1u` | passer à `2u`, `COMMISSIONING_FLASH_HEADER_SIZE` 12 → 16, ajouter l'enum d'état et les `#define` de motifs, faire porter `state`/`flags` par l'API encode/decode |
| `shared/protocol/commissioning_table.c` | `:220` `commissioning_flash_encode`, `:251` `commissioning_flash_decode` | écrire systématiquement en v2 ; **accepter la lecture d'une v1** et la rendre `state = FINAL`, `flags = 0` ; les enregistrements démarrent à l'octet 16 ; CRC sur les enregistrements seulement |
| `firmware/clavier/src/commissioning.c` | `:47` `commissioning_state_load`, `:89` `commissioning_state_store`, `:107` appel d'encode | propager `state`/`flags` (lecture → état en mémoire, écriture → paramètres) |
| `firmware/clavier/src/commissioning.h` | `:11` `commissioning_state_t`, `:19-21` API | exposer l'état et les motifs à `can_link.c` |
| `firmware/clavier/src/can_link.c` | `:969` `new_entries[]`, `:1012` `commissioning_build_runtime`, `:1030-1032` diff, `:1076` `commissioning_state_store` | **cœur du sujet** : remplacer la condition « toutes les cartes de `uid_position.h` ont répondu » par la politique ADR-0009 §2, calculer `flags` **avant** l'écriture, marquer `bitmap = 0` une carte `test_status != OK`, exclure une carte hors `uid_position.h` |
| `firmware/moteur/CMakeLists.txt` | `:48` compile `commissioning_table.c` | le Moteur **lie** ce module sans l'utiliser : toute dépendance nouvelle (inclure `can_protocol.h`, lib) doit rester compatible avec le build Moteur |
| `tests/host/test_phase3_helpers.c` | `:201-228` tests encode/decode | ajouter : aller-retour v2 avec états/motifs, **lecture d'une v1** interprétée `FINAL`, refus d'une version inconnue, CRC inchangé |

## 2. `can_protocol.[ch]` — trame `IPROPI_OHM` 0x601

| Fichier | Point d'entrée | Revue requise |
|---|---|---|
| `shared/protocol/can_protocol.h` | `:38` `CAN_ID_IPROPI_RAW 0x600`, `:54` `CAN_DLC_IPROPI_RAW 8`, `:134-140` `can_ipropi_raw_t` | ajouter `CAN_ID_IPROPI_OHM 0x601`, `CAN_DLC_IPROPI_OHM 8`, `can_ipropi_ohm_t` (`uid_tag`, `reply_to`, `channel`, `ohm_milliohm`, `i100_mA`) et les prototypes encode/decode. **Ne pas toucher** `0x600` |
| `shared/protocol/can_protocol.c` | `:321` encode RAW, `:340` decode RAW | ajouter l'encodeur/décodeur `0x601`, DLC strict, `0xFFFF` = invalide transmis tel quel |
| `firmware/moteur/src/ipropi_selftest.[ch]` | `.h:20` `raw_reports[]`, `.c:197` remplissage, `.c:260-279` `take_result` | ajouter la collecte des valeurs `ohm`/`i100` par canal, calculées dans `solenoid_profile`-respectant le duty appliqué |
| `firmware/moteur/src/can_link.c` | `:191-209` émission des 12 `IPROPI_RAW` | ajouter l'émission des 12 `IPROPI_OHM` **après** les RAW, même espacement, et **uniquement** si le flag calibration est présent |
| `firmware/clavier/src/can_link.c` | `:1187` `on_ipropi_raw`, `:1304` dispatch RX | ajouter la réception `0x601` et l'alimentation de la ligne de synthèse `[SELFTEST_OHM]` |
| `firmware/clavier/src/diag_log.[ch]` | `.c:195` ligne `idle/probe`, `.c:212` ligne `delta` | étendre la ligne `delta` avec `ohm_est=` et `i100=` ; ajouter `diag_format_selftest_ohm_line()` ; adapter les tests d'attente |
| `tests/host/test_phase3_helpers.c` | `:79-96` codecs | ajouter le codec `0x601` : aller-retour, DLC faux, `0xFFFF` |

## 3. Constantes Moteur concernées (non partagées avec le Clavier)

| Fichier | Point d'entrée | Revue requise |
|---|---|---|
| `firmware/moteur/src/solenoid_profile.h` | `:33` `SOLENOID_PROBE_DURATION_MS 3u`, `:55` `SOLENOID_PROBE_MAX_MS 10u` | PROBE → **5u** ; ajouter `SOLENOID_VM_MILLIVOLT`, `SOLENOID_ADC_VREF_MILLIVOLT`, `SOLENOID_IPROPI_UA_PER_A` (450), `SOLENOID_IPROPI_RESISTOR_OHM` (2400), `SOLENOID_RDSON_MILLIOHM` (240). Le plafond `SOLENOID_PROBE_MAX_MS` reste à **10u** : la valeur de 5 ms y reste soumise (invariant 4) |
| `firmware/moteur/src/ipropi_selftest.h` | `:12` `IPROPI_SELFTEST_ADC_SAMPLE_DELAY_US 1800u` | remplacé par `IPROPI_SELFTEST_ADC_TAIL_MARGIN_US` ; fenêtre d'échantillonnage **en fin** d'impulsion |
| `firmware/moteur/src/ipropi_selftest.c` | `:11-14` assertions statiques, `:210-231` état `SELFTEST_PROBING` | réécrire l'assertion (`N × t_adc + marge ≤ durée PROBE`, fenêtre ≥ 50 µs) et prélever les `N` derniers échantillons |
| `firmware/moteur/src/solenoid_driver.c` | `:41-42` écrêtage de la durée PROBE | **aucune nouvelle phase, aucun nouveau plafond** ; le calcul ohmique doit lire le duty **appliqué**, pas la constante du profil |

## 4. Divers Clavier

| Fichier | Point d'entrée | Revue requise |
|---|---|---|
| `firmware/clavier/CMakeLists.txt` | `:15` `option(CLAVIER_SELFTEST_CALIBRATION ... OFF)` | défaut **`ON`** pendant le bring-up (ADR-0009 §7) ; prévoir le retour à `OFF` en configuration finale |
| `firmware/clavier/src/diag_log.c` | `:261-295` lignes `[TABLE]` | ajouter `state=`/`flags=` sur la ligne `loaded`, et les lignes `[MAP]` |

## 5. Ce qui **n'est pas** touché

- Aucune trame existante ne change de format ni de DLC : `0x100`, `0x400`,
  `0x401`, `0x402`, `0x410`, `0x411`, `0x412`, `0x600` restent identiques.
- `uid_position_entry_t` et `uid_position.h` restent inchangés.
- Les bornes de la couche pilote (invariant 4) restent inchangées ; le profil
  PROBE ne fait que passer sous un plafond déjà existant.
