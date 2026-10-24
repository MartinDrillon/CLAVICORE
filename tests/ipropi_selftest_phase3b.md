# Phase 3b — Livraison 1 : auto-test IPROPI (procédure manuelle)

Décisions de référence :
- [ADR-0004](../docs/adr/adr-0004-auto-test-ipropi-table-assignation-solenoide-command.md)
- ADR-0005
- [docs/specs/CAN.md](../docs/specs/CAN.md)

> **Journal (ADR-0025 implémentée, validée hôte 2026-10-24)** : après
> implémentation, taper `l 2` avant les étapes qui attendent `[SELFTEST_RAW]`.
> À `l 0`, seuls les verdicts, échecs et warnings restent visibles ;
> `[SELFTEST_OHM] warn` et `ohm_manquants` ne sont jamais filtrés. Sur le
> firmware actuel, les RAW sont émis sans commande `l`.

Périmètre **Livraison 1 uniquement** :
- Moteur : `PROBE`, lecture mux/ADC, bitmap d'occupation, `fault_bits`,
  `IPROPI_SELFTEST_TRIGGER` (`0x402`), `BOARD_ANNOUNCE_STATUS` (`0x412`),
  `IPROPI_RAW` (`0x600`, mode calibration)
- Clavier : réception A+B+C, refus pendant le jeu, lignes `[STATUS]`
- **Hors périmètre** : écriture flash de la table, assignation persistante,
  `SOLENOID_COMMAND` (`0x100`)

## 1. Objectif

Vérifier sur un Clavier et une ou plusieurs cartes Moteur que :
1. aucun auto-test ne part au boot ;
2. un appui long 3 s sur le bouton local d'une carte Moteur déclenche bien une
   demande d'auto-test via le Clavier ;
3. le Clavier diffuse `IPROPI_SELFTEST_REQUEST` (`0x401`) ;
4. chaque carte répond par `BOARD_ANNOUNCE_UID` (`0x410`),
   `BOARD_ANNOUNCE_INFO` (`0x411`) et `BOARD_ANNOUNCE_STATUS` (`0x412`) ;
5. le Clavier remonte les lignes `[STATUS]` et `[SELFTEST]` attendues sur le
   journal CDC ;
6. un refus pendant le jeu n'émet pas de `0x401`.

## 2. Pré-requis

1. Clavier flashé avec le firmware concerné.
2. Carte(s) Moteur flashée(s) avec le même protocole.
3. CANH/CANL câblés, masses communes.
4. `R24` soudée uniquement aux deux extrémités physiques du bus.
5. Alimentation 24 V active côté Moteur.
6. PC connecté au Clavier en USB.
7. **PuTTY** prêt sur le port COM CDC du Clavier.
8. Si possible, analyseur CAN ou journal permettant d'observer les IDs
   `0x401`, `0x402`, `0x410`, `0x411`, `0x412`.

## 3. Trames CAN attendues

| ID | Nom | Sens | Attendu |
|---|---|---|---|
| `0x402` | `IPROPI_SELFTEST_TRIGGER` | Moteur -> Clavier | après appui long 3 s |
| `0x401` | `IPROPI_SELFTEST_REQUEST` | Clavier -> diffusion | si le Clavier accepte le test |
| `0x410` | `BOARD_ANNOUNCE_UID` | Moteur -> Clavier | réponse à la requête |
| `0x411` | `BOARD_ANNOUNCE_INFO` | Moteur -> Clavier | réponse à la requête |
| `0x412` | `BOARD_ANNOUNCE_STATUS` | Moteur -> Clavier | réponse à la requête |
| `0x600` | `IPROPI_RAW` | Moteur -> Clavier | seulement si `request_id bit7 = 1` |
| `0x601` | `IPROPI_OHM` | Moteur -> Clavier | seulement si `request_id bit7 = 1`, après les 12 `0x600` |

## 4. Lignes CDC attendues

### STATUS

```text
[STATUS] code=SELFTEST_STARTED
[STATUS] code=SELFTEST_REFUSED_PLAYING pos=127
[STATUS] code=BOARD_MISSING pos=2
[STATUS] code=BOARD_REFUSED_BUSY pos=4 bitmap=0x007
[STATUS] code=FAULT_DURING_TEST pos=4 bitmap=0x004
```

Note : `SELFTEST_UNCALIBRATED` reste un code protocolaire réservé, mais il
n'est pas attendu avec le firmware Moteur courant (seuil delta figé à 400).

### SELFTEST

```text
[SELFTEST] req=5 pos=4 status=OK bitmap=0x007 faults=0x000 tag=29B1
[SELFTEST_RAW] req=5 pos=4 ch=2 idle=18 probe=79
[SELFTEST_RAW] req=5 pos=4 ch=2 delta=61 ohm_est=5.12 i100=4.48
[SELFTEST_OHM] pos=4 n=3 ohm_min=5.02 ohm_max=6.11 spread=10% i100_min=3.77 i100_max=4.55
```

Notes (ADR-0009) :
- `probe_avg` est la moyenne des **N derniers échantillons** d'une impulsion
  PROBE de **5 ms** à 20 % : à courant établi, donc `delta` est **plus élevé**
  qu'avec le firmware 3 ms / délai fixe du 2026-10-18. Les valeurs ci-dessus
  sont des ordres de grandeur, pas des attendus stricts ;
- `ohm_est` et `i100` sont des valeurs **estimées** ; `--` si invalides ;
- `[SELFTEST_OHM]` est une synthèse par carte sur les canaux occupés.

## 5. Préparation matérielle

1. Brancher le Clavier en USB.
2. Alimenter le Clavier puis la ou les cartes Moteur.
3. Ouvrir PuTTY sur le port COM CDC du Clavier.
4. Ouvrir, si disponible, le moniteur CAN.

## 6. Test A — Aucun auto-test au boot

1. Couper puis remettre sous tension la carte Moteur.
2. Observer les échanges juste après le boot.

**Attendu :**
- présence possible des trames UID de Phase 3a (`0x410`, `0x411`) ;
- absence de `0x402`, `0x401` et `0x412` au boot ;
- absence de ligne `SELFTEST_STARTED` sans action utilisateur.

## 7. Test B — Déclenchement local par bouton Moteur

1. Sur une carte Moteur au repos, maintenir le bouton de test pendant 3 s.
2. Observer d'abord le déclencheur puis la diffusion orchestrée.

**Attendu :**
1. la carte émet `0x402` ;
2. le Clavier répond par `0x401` ;
3. le Clavier écrit `[STATUS] code=SELFTEST_STARTED` ;
4. chaque carte présente renvoie `0x410`, `0x411`, puis `0x412` ;
5. le Clavier écrit une ligne `[SELFTEST]` par carte.

## 8. Test C — Carte unique, exemple simple

Précondition : une seule carte Moteur connectée, avec 3 canaux réellement
occupés correspondant à l'exemple ADR-0004.

**Attendu :**
- la ligne `[SELFTEST]` contient `bitmap=0x007` si les canaux 0..2 sont occupés ;
- aucun `SOLENOID_COMMAND` n'est requis pour ce test.

## 9. Test D — Refus pendant le jeu

1. Mettre le système dans un état où le Clavier considère qu'il « joue ».
2. Maintenir le bouton Moteur 3 s.

**Attendu :**
- la carte peut émettre `0x402` ;
- le Clavier écrit `[STATUS] code=SELFTEST_REFUSED_PLAYING pos=127` ;
- le Clavier n'émet pas de `0x401`.

## 10. Test E — Carte occupée ou défaut

### E1 — Carte occupée

**Attendu :**
- ligne `[SELFTEST] ... status=REFUSED_BUSY ...`
- ligne `[STATUS] code=BOARD_REFUSED_BUSY pos=N bitmap=0xXYZ`

### E2 — Défaut pendant test

**Attendu :**
- `faults=0xXYZ` dans la ligne `[SELFTEST]`
- ligne `[STATUS] code=FAULT_DURING_TEST pos=N bitmap=0xXYZ`

## 11. Critères de réussite Livraison 1

- [ ] Aucun auto-test automatique au boot
- [ ] Déclenchement local fonctionnel par bouton 3 s
- [ ] Diffusion `0x401` faite uniquement par le Clavier
- [ ] Réception complète des trames A+B+C
- [ ] `reply_to` cohérent avec le `request_id`
- [ ] Lignes `[STATUS]` visibles sur USB CDC
- [ ] Ligne `[SELFTEST]` visible sur USB CDC
- [ ] Refus pendant le jeu confirmé
- [ ] Les cas « occupé » et « défaut » remontent explicitement

## 12. Calibration du seuil IPROPI

But : relever les valeurs ADC brutes pour **vérifier sur banc** le seuil figé
`IPROPI_SELFTEST_THRESHOLD_DELTA_ADC = 400` (même firmware pour toutes les
cartes), sans changer le protocole.

### 12.1 Préparation

1. Le mode diagnostic est **actif par défaut** depuis ADR-0009 §7. Pour vérifier
   explicitement qu'il est bien là, ou le forcer :

   ```powershell
   cmake -S "firmware/clavier" -B "build/clavier" -DCLAVIER_SELFTEST_CALIBRATION=ON
   cmake --build "build/clavier"
   ```

2. Flasher ce build Clavier.
3. Laisser le firmware Moteur inchangé : il émet les RAW (et les OHM) dès que le
   bit 7 est présent dans `IPROPI_SELFTEST_REQUEST`.

### 12.2 Relevé

1. Déclencher un auto-test au repos.
2. Relever, pour plusieurs canaux **branchés** et **débranchés**, les lignes :
   - `[SELFTEST_RAW] ... idle=I probe=P`
   - `[SELFTEST_RAW] ... delta=D ohm_est=<Ω|--> i100=<A|-->`
   - `[SELFTEST_OHM] ...` (synthèse par carte)
3. Comparer les `delta` :
   - canaux occupés / branchés : `delta` attendu nettement plus grand ;
   - canaux non occupés / débranchés : `delta` attendu plus faible, et
     `ohm_est`/`i100` à `--`.
4. Vérifier que la décision officielle utilise `max(delta, 0)` puis
   `delta >= 400`.
5. Recouper `ohm_est` d'une bobine avec la mesure au multimètre (voir
   [`bringup_commissioning_provisoire.md`](bringup_commissioning_provisoire.md),
   §6.5) : c'est le contrôle de cohérence de la chaîne ADC/`VM`/`VREF`.

### 12.3 Seuil retenu

Le firmware de référence applique désormais directement
`IPROPI_SELFTEST_THRESHOLD_DELTA_ADC = 400`. Le mode calibration reste utile
pour confirmer au banc que les cartes branchées restent au-dessus de ce seuil
et que les cartes débranchées restent en dessous.

### 12.4 Statut « non calibré »

Le code protocolaire `UNCALIBRATED` reste défini pour compatibilité, mais il
n'est **plus attendu** avec le firmware courant puisque le seuil delta 400 est
défini explicitement dans le Moteur.

## 13. Point non couvert

Cette procédure reste manuelle : la confirmation du seuil 400 sur toutes les
cartes et les cas de `FAULT` contrôlé dépendent encore du banc matériel.
