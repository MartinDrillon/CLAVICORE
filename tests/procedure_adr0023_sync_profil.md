# Procédure de validation — ADR-0023 (synchronisation du profil)

Procédure destinée au **Validateur** (et au Codeur pour l'auto-vérification).
Elle couvre les tests hôte **et** la campagne de banc reproductible de
[ADR-0023](../docs/adr/adr-0023-synchronisation-profil-reprise-verification-etat.md)
(`Accepted`) et de
[`docs/specs/impact-adr-0023.md`](../docs/specs/impact-adr-0023.md).

> **État au 2026-10-24** : l'implémentation est présente dans
> `firmware/clavier/src/can_link.c` et le harnais `test_profile_sync` est intégré
> aux tests hôte. La campagne de banc reste à exécuter ; les tests hôte doivent
> rester verts avant de lancer §2.

## 0. Pré-requis

### Hôte et build

* Pico SDK, toolchain ARM, Ninja, MinGW et générateur installés comme décrit
  dans [`tests/build_clavier_windows.md`](./build_clavier_windows.md) §1–§2.
* Configurations générées dans un dossier de build **propre au worktree** (ne pas
  réutiliser tel quel un `build/` dont le cache contient des chemins absolus
  d'un autre worktree ; voir `AGENTS.md`).
* Le nouveau harnais (par exemple `tests/host/test_profile_sync.c`) est appelé
  par `tests/host/run_host_tests.ps1` ; toute compilation ou tout test en échec
  fait échouer le script.

### Banc

* Clavier flashé avec le firmware contenant ADR-0023.
* **Aucun reflash Moteur n'est requis** par ADR-0023 : le firmware Moteur et
  `shared/protocol/` sont inchangés.
* Deux cartes Moteur alimentées, au moins une touche commissionnée par carte,
  scan UID de boot terminé.
* PuTTY sur le CDC du Clavier avec **Local echo** et **Local line editing**
  forcés (`tests/phase3b_profil_tuning.md` §2.2).
* Analyseur CAN facultatif mais requis pour caractériser la perte physique.
* Journal PuTTY activé vers un fichier, par exemple
  `build/logs/adr0023_putty.log`.

## 1. Tests hôte et compilation

Depuis la racine du repo, rediriger chaque sortie vers un fichier :

```powershell
New-Item -ItemType Directory -Force "build\logs" | Out-Null
powershell -File tests\host\run_host_tests.ps1 *> "build\logs\adr0023_host.log"
cmake --build "build/clavier" *> "build\logs\adr0023_clavier.log"
cmake --build "build/moteur" *> "build\logs\adr0023_moteur.log"
```

Contrôles :

```powershell
Select-String -Path "build\logs\adr0023_host.log" -Pattern "FAIL|ECHEC|echec|verifications|error|warning" | Select-Object -Last 20
Select-String -Path "build\logs\adr0023_clavier.log","build\logs\adr0023_moteur.log" -Pattern "error|warning|FAILED" | Select-Object -Last 20
```

Critères :

* `run_host_tests.ps1` sort avec le code 0 et tous les cas passent ;
* les cas H-01 à H-20 de `impact-adr-0023.md` §6 sont présents et exécutés ;
* les deux firmwares compilent sans erreur ni avertissement nouveau ;
* aucun fichier de `shared/protocol/` ni aucun fichier Moteur n'a été modifié
  par ce lot.

Si le harnais n'est pas encore branché au script, **arrêter** : ADR-0023 exige
des tests hôte, et une compilation seule ne démontre pas les reprises.

## 2. Campagne de banc — avant/après reproductible

Consigner dans un fichier de résultats :
`build/logs/adr0023_banc.md` (ne pas recopier tout le journal dans la
conversation).

### T0 — Référence nominale à tag identique

1. `p send` sans changer le profil.
2. Attendu après implémentation :

```text
[PROFILE] sync ok but=reglage duree=... ms tentatives=1/3 acquittements=2/2 reconciliees=0 tardifs=0 source=console tag=0x...
```

3. Aucun `PROFILE_SYNC_FAILED`, aucun `0x401` si le but est `reglage`.
4. Rejouer 20 fois : aucune reprise ne doit apparaître, aucune commande manuelle
   supplémentaire n'est nécessaire.

### T1 — 200 alternances avec écriture flash

Chaque alternance doit changer le tag et forcer une écriture flash Moteur.

1. Envoyer, une commande par ligne, en alternant :
   `.pull_duration_us = 15000u,` puis `.pull_duration_us = 15001u,`
   (ou `p set pull_duration_us 15000` / `15001`).
2. Après chaque commande, vérifier que la diffusion se termine par **un seul**
   verdict `sync ok` ou, en cas de perte, une reprise visible suivie d'un
   `sync ok`.
3. Compter sur **200 alternances** :
   * nombre de `PROFILE_SYNC_FAILED` finaux ;
   * nombre de `sync reprise` ;
   * nombre de `reconciliees` ;
   * nombre de commandes `p send` humaines supplémentaires.
4. Restaurer la valeur de départ par une dernière commande et vérifier
   `p diff` → aucune différence.

Critères d'acceptation :

* **0** échec final ;
* **0** commande manuelle de resynchronisation ;
* chaque reprise ou réconciliation est tracée, jamais silencieuse ;
* aucune écriture flash n'est déclenchée pour `p send` à tag identique.

> La variation `pull_duration_us` de 1 µs ne doit pas être utilisée pour jouer
> pendant la campagne. Elle sert seulement à changer l'identité du profil.

### T2 — Perte d'une trame ou d'un acquittement

Les pertes sont injectées de façon déterministe par le harnais hôte (cas H-09 à
H-14). Si un analyseur CAN permet de supprimer une trame sur le bus, rejouer les
mêmes scénarios au banc :

| Injection | Résultat attendu |
|---|---|
| un `PROFILE_STATUS` après COMMIT réussi | reprise ou réconciliation, une seule commande, `0x401` seulement si tout est confirmé |
| un `PROFILE_BEGIN` | transfert complet suivant, nouveau `transfer_id`, même CRC |
| un `PROFILE_CHUNK` | transfert complet suivant, aucune application partielle |
| le `PROFILE_COMMIT` | transfert complet suivant, l'ancien profil reste actif tant que le nouveau n'est pas validé |
| tous les STATUS de transfert, sonde intacte | `reconciliees` > 0 si les cartes détiennent le bon tag NVM |

Sur les logs, vérifier qu'un refus réessayable ne produit **pas** de
`PROFILE_SYNC_FAILED` immédiat et que le tag d'un refus n'est jamais comparé au
tag attendu.

### T3 — Divergence et sources non réconciliables

1. Provoquer une divergence avec un ancien profil injecté côté Moteur, si
   l'outillage SWD existant le permet (voir
   `tests/tools/gen_profile_inject.c`), ou en gardant une carte sur un profil
   différent.
2. Lancer `p send` ou un auto-test.
3. Attendu :
   * `PROFILE_DIVERGENT` si une réponse **OK** porte un autre tag ;
   * `PROFILE_FALLBACK_IN_USE` si la source est `DEFAULTS`, même à tag égal ;
   * aucun `0x401` tant que la carte n'est pas confirmée ;
   * aucun remapping ni écrasement silencieux de la divergence.

### T4 — Auto-test après réconciliation

1. Provoquer la perte de tous les acquittements de transfert mais laisser la
   sonde UID fonctionner (harnais hôte H-10 ; au banc, uniquement si
   l'analyseur permet cette injection).
2. Lancer un auto-test.
3. Attendu : `tentatives=3/3`, `reconciliees=N`, puis `0x401` **seulement** si
   toutes les cartes attendues sont confirmées. Sinon, `PROFILE_SYNC_FAILED` et
   aucun `0x401`.

### T5 — Régressions fonctionnelles

1. Jouer chaque touche commissionnée : aucune note muette, aucun changement de
   mapping.
2. `m stats` reste disponible et ne bloque pas une synchronisation en cours.
3. Un `p send` pendant un jeu récent ou une opération en cours reste refusé par
   les gardes existantes.
4. Un auto-test nominal réussit après la campagne.

## 3. Mesures complémentaires pour la cause physique

Ces mesures ne conditionnent pas le succès fonctionnel d'ADR-0023 mais sont
exigées pour ne pas confondre récupération et cause racine :

1. Avec l'analyseur CAN, capturer les 20 premiers transferts après flash et
   relever :
   * présence du `0x423` manquant sur le fil ;
   * trames d'erreur CAN au moment des réponses `0x423` ;
   * écart COMMIT → STATUS ;
   * simultanéité de plusieurs `0x423`.
2. Mesurer la durée COMMIT → fin d'écriture → STATUS et la comparer à la fenêtre
   de 1 s de l'auto-test. Un dépassement est un résultat à documenter, pas à
   masquer.
3. Relever les compteurs `m stats` avant/après : `can_errors`, `tx_fail`,
   `profile_status_lost`, `profile_commits_ok/rejected`.
4. Si une perte se reproduit à chaque tentative, rouvrir la piste de collision
   de réponses de même identifiant ; ADR-0023 ne la corrige pas.

## 4. Résultats à consigner

Créer `build/logs/adr0023_banc.md` avec :

| Élément | Valeur |
|---|---|
| Date, opérateur, UIDs des cartes | |
| Firmware Clavier / Moteur (build) | |
| T0 : nombre de `p send`, échecs, reprises | |
| T1 : 200 alternances, échecs, reprises, réconciliations | |
| T2 : injections et résultats | |
| T3/T4 : divergences, refus, `0x401` émis ou non | |
| T5 : régressions | |
| Mesures analyseur / latences | |
| Conclusion : succès fonctionnel oui/non | |
| Ce qui reste non démontré | |

Ne pas conclure « cause matérielle » sans les chiffres de §3. Ne pas déclarer le
second problème « solénoïdes muets après inactivité » résolu par cette campagne :
il relève d'ADR-0024, indépendante.

## 5. Critères de sortie

La validation ADR-0023 est acquise lorsque :

* le harnais hôte est intégré et tous ses cas passent ;
* les deux firmwares compilent sans avertissement nouveau ;
* T0 et T1 réussissent sans intervention humaine ;
* T2/T4 montrent une reprise ou une réconciliation visible, jamais un échec
  terminal sur une perte transitoire ;
* T3 interdit toujours `0x401` sur divergence, source `DEFAULTS`, carte absente
  ou refus terminal ;
* les journaux et mesures sont archivés dans `build/logs/`.

Le compte rendu final doit indiquer séparément :

1. ce qui est **prouvé** par les tests hôte ;
2. ce qui est **prouvé** au banc ;
3. ce qui reste **non mesuré** (point de perte CAN, collision, marge flash).
