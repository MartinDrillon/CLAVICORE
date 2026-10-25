# Commandes PuTTY du Clavier

Référence des commandes reçues sur le port **CDC** du Clavier. Source de vérité
du comportement : `firmware/clavier/src/bringup_console.c`,
`firmware/clavier/src/can_link.c` et `firmware/clavier/src/profile_tuning.c`.
Tenue à jour à chaque évolution de la console (ADR-0009, ADR-0015, ADR-0025).

La console n'est compilée qu'avec `CLAVIER_CDC_CONSOLE=ON`. Dans PuTTY, forcer
**Local echo** et **Local line editing** : le Clavier n'affiche pas les caractères
tapés. Les commandes se terminent par `Entrée` (`\r` ou `\n`). Retour arrière
accepté ; une ligne de plus de 127 caractères est **ignorée en entier**.

Les commandes sont **insensibles à la casse sur la lettre principale**. Les
sous-commandes (`stats`, `set`, `show`…) et les noms de champs sont **en
minuscules**. Une commande inconnue répond `[CONSOLE] commande inconnue (?=aide)`.

## Commandes principales

| Commande | Effet | Réponse typique |
|---|---|---|
| `?` | Affiche l'aide générale | lignes `[CONSOLE]` |
| `t` | Demande un auto-test IPROPI (profil + `0x401`) | `[CONSOLE] t -> auto-test demande`, puis `[TRIGGER]`/`[STATUS]`; refus si auto-test, synchronisation, requête UID/contrôle en cours, ou jeu depuis moins de 0,5 s |
| `c` | Bascule l'**acquisition** de calibration (trames `0x601`, estimation ohmique). Ne change pas le niveau de journal | `[CONSOLE] c -> mode diagnostic ON|OFF` |
| `d` | Affiche la table de commissioning persistée et la correspondance position/canal/touche | lignes `[TABLE]`, `[MAP]`; réponse toujours affichée, même à `l 0` |
| `m` | Affiche l'état synthétique du Clavier | `[CONSOLE] status valid=… state=… flags=… cards=… selftest=… calib=…` |
| `m stats` | Lance la lecture des compteurs de toutes les cartes attendues | `[CONSOLE] m stats -> lecture…`, puis une ligne `[CARD] pos=N stats …` par carte et `[STATS]`; refus si opération en cours, aucune carte attendue, ou émission impossible |
| `l` | Affiche le niveau de journal et l'état de l'horodatage | `[CONSOLE] log niveau=0|1|2 timestamp=on|off trace=on|off` |
| `l 0` | Journal normal : verdicts, défauts, divergences, réponses console | idem |
| `l 1` | Bring-up : niveau 0 + boot, UID, table, mapping, résumés et événements ordinaires | idem |
| `l 2` | Trace : niveau 1 + notes, RAW, matrice, PUSH courants | idem ; refusé si `CLAVIER_DIAG_TRACE=OFF` |
| `l t` | Bascule le préfixe d'horodatage `[T+… ms]` | idem |
| `p …` | Commandes de profil (voir ci-dessous) | lignes `[PROFILE]` |
| `.…` | Raccourci de `p set` : `.champ = valeuru, …` | identique à `p set` |

Le niveau `l` n'est **pas persistant** : au reboot, il repart sur
`CLAVIER_DIAG_DEFAULT_LEVEL` (1 au bring-up, 0 pour un build final).

## Sous-commandes de profil `p`

| Commande | Effet |
|---|---|
| `p` ou `p ?` ou `p help` | Aide des commandes de profil |
| `p show` | Profil actif, version de format, tag CRC32 et nombre de champs différents du fichier compilé ; liste tous les champs |
| `p get <champ>` | Valeur du champ demandé (le `.` initial est toléré) |
| `p diff` | Liste **uniquement** les champs différents du fichier compilé, prêts à recopier dans `solenoid_profile_config.h` ; affiche `aucune difference` sinon |
| `p fields` | Liste tous les champs avec `reglable` ou `verrouille: <raison>` |
| `p send` | Rediffuse le profil **actif** (RAM Clavier) vers les cartes |
| `p reset` | Diffuse le profil **du fichier compilé** (`solenoid_profile_config.h`) |
| `p set <champ> <valeur> …` | Applique une ou plusieurs affectations, puis diffuse le profil si au moins une valeur change ; affiche `champ ancien -> nouveau` |
| `p set .<champ> = <valeur>u, …` | Même chose avec la syntaxe initialiseur C (copier-coller du `.h`) |

Règles de `p set` :

- aucune valeur ne change → `[PROFILE] reglage: aucune valeur ne change` et rien
  n'est diffusé ;
- un champ verrouillé ou une incohérence est refusé **avant tout envoi** :
  `[PROFILE] reglage refuse (rien n'est envoye): <raison>` ;
- le profil actif n'est promu qu'après confirmation des cartes ; un échec de
  synchronisation laisse l'ancien profil actif et émet `[PROFILE] sync echec` ;
- `pwm_freq_hz` et les champs verrouillés restent lisibles par `p get/show/diff`
  mais ne sont pas modifiables à chaud.

## Champs de profil réglables à chaud

| Champ | Rôle |
|---|---|
| `impact_duration_us` | Durée de l'impulsion IMPACT |
| `impact_duty_pct` | Duty de l'impulsion IMPACT |
| `pull_duration_us` | Durée de la phase PULL |
| `pull_duty_pct` | Duty de la phase PULL |
| `hold_duty_pct` | Duty de la phase HOLD |
| `hold_duty_max_pct` | Plafond de duty en HOLD |
| `hold_max_ms` | Durée maximale de HOLD |
| `release_duration_us` | Durée de la phase RELEASE |
| `release_duty_pct` | Duty de la phase RELEASE |
| `maint_duty_pct` | Duty de la poussée maintenance |
| `maint_duty_max_pct` | Plafond de duty en maintenance |
| `maint_max_ms` | Durée maximale de poussée maintenance |
| `maint_min_rest_ms` | Repos minimal entre deux poussées maintenance |
| `min_rest_ms` | Repos minimal entre deux notes |
| `duty_max_pct` | Plafond général de duty |

## Champs verrouillés (lisibles, non réglables à chaud)

| Champ | Raison |
|---|---|
| `pwm_freq_hz` | appliquée à l'init du Moteur seulement (fichier + reboot) |
| `probe_duration_us` | réservée à l'auto-test IPROPI (fichier + auto-test) |
| `probe_duty_pct` | réservée à l'auto-test IPROPI (fichier + auto-test) |
| `probe_duty_max_pct` | réservée à l'auto-test IPROPI (fichier + auto-test) |
| `dir_safety_delay_us` | délai de sécurité de polarité (fichier) |
| `wake_delay_us` | appliquée au réveil des drivers seulement (fichier) |
| `dir_level_pull` | polarité non réécrite à l'arrêt (fichier) |
| `dir_level_release` | polarité non réécrite à l'arrêt (fichier) |
| `dir_level_maint` | polarité non réécrite à l'arrêt (fichier) |

## Diagnostiquer un refus de `t`

`[STATUS] code=SELFTEST_REFUSED_PLAYING` est émis par plusieurs gardes : auto-test
déjà en cours, requête UID/contrôle ou synchronisation de profil en cours, ou
jeu depuis moins de 0,5 s. Le code seul ne dit pas laquelle.

1. `m` — état principal :
   - `valid=oui|non`, `state=FINAL|PROVISIONAL|INVALID|…` (table) ;
   - `selftest=en_cours|repos` ;
   - `calib=ON|OFF`.
   Si `selftest=en_cours`, attendre la fin.
2. `l 1` — affiche les opérations en cours et terminées (`[PROFILE]`, `[UID]`,
   `[TRIGGER]`, `[STATUS]`). Un `[PROFILE] sync ok/echec` ou une séquence UID
   récente explique souvent le refus.
3. Attendre au moins **1 s** après la dernière touche ou le dernier échange, puis
   retaper `t`. Après un reflash, le boot et la synchronisation de profil
   peuvent légitimement refuser pendant quelques centaines de millisecondes.
4. Si le refus persiste, `p send` donne une raison explicite :
   - `operation CAN en cours` → requête UID/contrôle ou synchronisation active ;
   - `jeu en cours` → une touche a été jouée il y a moins de 0,5 s ;
   - `CAN non demarre` → lien CAN non initialisé.
   Ne pas utiliser `p send` juste pour observer si un jeu est en cours : il
   déclenche une diffusion de profil s’il est accepté.

`m` ne montre pas directement `profile_sync_active`, `uid_inflight`,
`control_tx_inflight` ni l’instant du dernier jeu : c’est le `l 1`/`p send`
ci-dessus qui les révèle indirectement.

## Notes d'usage

- `p send` et `p reset` n'écrivent pas la flash Clavier : le réglage chaud vit en
  RAM Clavier et est perdu au reboot Clavier. La persistance Moteur est décrite
  par ADR-0013/ADR-0019.
- `c` commande l'acquisition, `l 2` commande l'affichage : les deux sont
  indépendants. Pour voir les ADC/ohm par canal d'un auto-test, activer `c` puis
  passer `l 2`.
- Les commandes `t`, `p set`, `p send` et `p reset` sont refusées pendant une
  autre opération CAN, une requête UID, un auto-test actif ou un jeu récent
  (< 0,5 s) ; voir `[STATUS]` et les lignes `[PROFILE]`/`[TRIGGER]`.
- Les commandes `t`, `c`, `d` et `?` ignorent le texte qui suit la lettre. `p`,
  `m` et `l` interprètent leurs arguments ; `m` sans `stats` affiche l'état et
  ignore le reste de la ligne.
- Aucune commande de la console n'écrit, n'efface ni ne remappe la table de
  commissioning. `GET_UID` est un **keycode clavier**, pas une commande PuTTY.
