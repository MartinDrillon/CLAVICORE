# Impact ADR-0021 — conserver le bitmap sur une mesure non concluante

Note de référence pour le **Codeur** et le **Validateur**.
[ADR-0021](../adr/adr-0021-conservation-bitmap-mesure-non-concluante.md)
(`Accepted`, 2026-10-23) corrige une conséquence d'ADR-0009 : une mesure
d'occupation **non concluante** ne doit plus effacer une assignation déjà valide.

> **État (2026-10-24) : exécutée et validée en compilation + tests hôte.**
> `commissioning_previous_bitmap()`, `diag_format_table_bitmap_kept_line()` et la
> conservation dans `finish_selftest_session` sont en place ; les 4 cas (même
> UID, UID différent, position absente, arguments invalides) sont couverts par
> les tests hôte. Sans ce correctif, un auto-test avorté rend le clavier muet
> jusqu'au test suivant (incident du 2026-10-23, `keys=0`,
> `missing_keys=1-49`). Validation matérielle à faire.

## 0. Règle à implémenter

| Situation | Avant (ADR-0009 IMP-003) | Après (ADR-0021) |
|---|---|---|
| Carte répond `test_status = OK` | `occupation_bitmap = occupation_bits` | inchangé |
| Carte répond `ABORTED` / `REFUSED_BUSY` / `UNCALIBRATED`, entrée précédente **même position + même UID** | **bitmap 0** | **bitmap précédent conservé** |
| Idem, mais aucune entrée précédente ou UID différent | bitmap 0 | bitmap 0 (inchangé) |
| Carte sans réponse (`BOARD_MISSING`) | absente de la table | **inchangée** (ADR-0009 §2) |

Un bitmap conservé est une valeur **mesurée antérieurement**, jamais un bitmap
partiel deviné : la règle de sécurité d'ADR-0009 §5b reste intacte. Aucun canal
n'est activé qui ne l'était pas déjà ; aucun remappage.

## 1. Règle de conservation (normative)

Dans `finish_selftest_session` (`firmware/clavier/src/can_link.c:1650-1669`),
remplacer la construction de l'entrée :

```c
// AVANT
new_entries[new_count].occupation_bitmap =
    (status[position].test_status == CAN_SELFTEST_STATUS_OK)
        ? status[position].occupation_bits
        : 0u;
```

par :

1. si `test_status == OK` → `occupation_bits` (inchangé) ;
2. sinon → chercher dans la **table courante valide** (`commissioning.valid`)
   l'entrée dont `identity.position == position` **et** dont
   `identity.uid.id` est **identique octet pour octet** à
   `selftest_session.uid[position]` :
   - trouvée → reprendre `occupation_bitmap`, et **émettre la ligne du §2** ;
   - non trouvée, table invalide, ou UID différent → `0u` (inchangé).
3. Ne pas modifier `flags` : `COMMISSIONING_FLAG_BOARD_NOT_OK` est déjà posé par
   `selftest_session.any_non_ok_status` (`:1638-1640`).

## 2. Ligne de journal (obligatoire dès qu'une conservation a lieu)

```text
[TABLE] pos=N bitmap conserve=0xNNN (mesure non concluante: ABORTED)
```

- `NNN` en hexadécimal sur 3 chiffres, **valeur conservée** ;
- le libellé du statut est celui du protocole (`ABORTED`, `REFUSED_BUSY`,
  `UNCALIBRATED`) ;
- la ligne est émise **une fois par entrée conservée**, avant la ligne
  `[TABLE] loaded` de fin de session ;
- aucune conservation n'est silencieuse (invariant 1).

Implémentation : ajouter `diag_format_table_bitmap_kept_line()` dans
`firmware/clavier/src/diag_log.c` (fonction **pure**, testable hôte) plutôt que
d'imprimer à la volée dans `can_link.c`.

## 3. Helper testable (recommandé, non obligatoire)

Pour que la règle soit couverte par un test hôte, extraire la recherche dans
`shared/protocol/commissioning_table.[ch]` (fonction pure, aucune allocation) :

```c
// Renvoie true et écrit *bitmap si une entrée de même position et même UID
// existe dans la table ; false sinon.
bool commissioning_previous_bitmap(const commissioning_runtime_t *table,
                                   uint8_t position, const uint8_t uid[8],
                                   uint16_t *bitmap);
```

C'est une **fonction interne** : ni un format de message, ni un format de table
(invariant 5 non concerné). `can_link.c` se contente alors d'appeler le helper et
d'émettre la ligne.

## 4. Ce qui ne change pas

- `BOARD_MISSING` : une carte sans réponse reste absente de la table (ADR-0009 §2).
  Une carte retirée ne doit pas être re-déclarée présente sur la foi d'une mesure
  ancienne ; le cas « carte qui redémarre pendant le test » produit donc une
  perte d'assignation, signalée (`CARD_REBOOTED`, ADR-0017).
- La décision d'écrire ou non la table, les refus `DUPLICATE_TAG` / position
  dupliquée, et `commissioning_build_runtime` : inchangés.
- Les trames CAN : aucune modification.

## 5. Fichiers à revoir

| Fichier | Revue requise |
|---|---|
| `firmware/clavier/src/can_link.c` | **modifié** : `finish_selftest_session` (§1) + appel du formateur (§2). |
| `firmware/clavier/src/diag_log.h/.c` | **modifié** : `diag_format_table_bitmap_kept_line()` + nom de statut si absent. |
| `shared/protocol/commissioning_table.h/.c` | **modifié (si helper retenu)** : `commissioning_previous_bitmap()`. |
| `tests/host/test_phase3_helpers.c` | **modifié** : 4 cas (conservation même UID ; UID différent ; pas d'entrée ; carte absente) + format de la ligne. |
| `tests/phase3b_bout_en_bout.md` | **modifié** : cas « auto-test avorté : le clavier continue de jouer les touches déjà validées ». |
| `docs/specs/CAN.md` | **modifié** : ligne `[TABLE] … bitmap conserve=…` (fait dans ce lot). |

## 6. Critères d'acceptation

- Une carte qui répond `ABORTED` alors que la table courante contient son entrée
  (même UID, même position) : la table **conserve** son bitmap, `keys` reste
  inchangé pour cette carte, `BOARD_NOT_OK` et `PROVISIONAL` sont posés, la ligne
  `bitmap conserve=` apparaît.
- Une carte remplacée (UID différent) : bitmap 0, **pas** de ligne
  `bitmap conserve=`.
- Une carte retirée : absente de la table (comportement ADR-0009 inchangé).
- Les tests hôte passent ; aucune modification de trame.
