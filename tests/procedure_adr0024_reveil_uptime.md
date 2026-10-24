# Procédure de validation — ADR-0024 (réveil permanent et refus par cause)

Procédure du **Validateur**, issue d'[ADR-0024](../docs/adr/adr-0024-reveil-solenoides-permanent-diagnostic-refus.md)
(`Accepted`) et de
[`docs/specs/impact-adr-0024.md`](../docs/specs/impact-adr-0024.md).

> **État au 2026-10-24** : ADR-0024 est implémentée et validée hôte (harnais
> `test_solenoid_wake` et tests compteurs). Les campagnes §3 et §4 restent à
> exécuter sur matériel.

> **Deux problèmes distincts.** La campagne **uptime** prouve le verrou de
> réveil. La campagne **24 h** cherche une éventuelle autre cause de mutisme
> après inactivité. Un succès de la première ne vaut **pas** validation de la
> seconde.

## 0. Pré-requis

* Chaîne de build et flash SWD opérationnels :
  [`build_moteur_windows.md`](./build_moteur_windows.md) et
  [`build_clavier_windows.md`](./build_clavier_windows.md) §6.
* Builds générés dans un dossier **propre au worktree** ; ne pas réutiliser tel
  quel un cache `build/` contenant des chemins absolus d'un autre worktree.
* Clavier et Moteur flashés avec le lot ADR-0024. Les deux côtés sont nécessaires
  pour les nouveaux noms de compteurs ; le format de trame reste inchangé.
* Deux cartes Moteur alimentées, une touche commissionnée par carte, table de
  commissioning valide.
* Alimentation 24 V stable, sans coupure automatique pendant 24 h.
* PuTTY sur le CDC du Clavier, journal vers `build/logs/adr0024_putty.log`.
* Un chronomètre ou l'horodatage `[T+… ms]` du Clavier pour les relevés ;
  l'uptime de référence reste celui de la carte Moteur (§3.1).
* Facultatif mais requis en cas d'échec : oscilloscope, sonde de courant,
  multimètre 24 V, accès visuel à nSLEEP/NFAULT sans démontage.

## 1. Build et tests hôte

Si `build/moteur` ou `build/clavier` n'existe pas, exécuter d'abord la
configuration initiale de [`build_moteur_windows.md`](./build_moteur_windows.md)
§2 et de [`build_clavier_windows.md`](./build_clavier_windows.md) (§2–§3), dans
un dossier propre au worktree.

Depuis la racine du repo :

```powershell
New-Item -ItemType Directory -Force "build\logs" | Out-Null
powershell -File tests\host\run_host_tests.ps1 *> "build\logs\adr0024_host.log"
cmake --build "build/moteur" *> "build\logs\adr0024_moteur.log"
cmake --build "build/clavier" *> "build\logs\adr0024_clavier.log"
```

Puis :

```powershell
Select-String -Path "build\logs\adr0024_host.log" -Pattern "FAIL|ECHEC|echec|verifications|error|warning" | Select-Object -Last 20
Select-String -Path "build\logs\adr0024_moteur.log","build\logs\adr0024_clavier.log" -Pattern "error|warning|FAILED" | Select-Object -Last 20
```

Critères :

* code de sortie 0 pour les tests et les deux builds ;
* cas W-01 à W-09 et C-01 à C-10 exécutés ;
* W-09 est couvert par le harnais hôte sur la version corrigée ; l'échec du
  mutant sans verrou reste à démontrer localement ;
* aucun warning nouveau sur `solenoid_driver.c`, `can_link.c` et
  `can_protocol.c` (contrôle `-Wall -Wextra -Wpedantic -fsyntax-only`, méthode
  décrite dans [`impact-adr-0019.md`](../docs/specs/impact-adr-0019.md) et
  `profil-solenoides.md` §9) ;
* aucun changement de trame ni de `CAN_PROTO_VERSION`.

Les logs de build ne sont pas recopiés dans la conversation : seules les erreurs
et le résumé final sont extraits.

## 2. Vérification statique de la ventilation

Avant le banc, relire `handle_solenoid_command()` :

1. `SOLENOID_OK` incrémente `solenoid_commands_ok` et aucun refus ;
2. chaque résultat non OK incrémente `solenoid_rejected` **et exactement un**
   compteur 13–17 ;
3. `PARAM`, `PWM`, `FAULT_PERSISTS` et tout code non listé tombent dans
   `REJECT_OTHER` ;
4. aucun `CARD_EVENT` supplémentaire n'est émis ;
5. `card_counter_value(RX_OVERRUN)` retourne `isr_rx_overrun`.

Un double comptage ou un refus sans cause spécialisée est un échec de revue.

## 3. Campagne uptime — preuve du verrou (150 min)

### 3.1 Mise en place

1. Mettre sous tension, flasher si nécessaire. Noter **T0** = instant de boot
   de chaque carte Moteur, tel que reçu par l'annonce de boot dans le journal du
   Clavier. Consigner l'écart avec le `[T+… ms]` du Clavier : les fenêtres de
   panne se mesurent sur `time_us_32()` de la carte Moteur, pas sur l'horloge du
   Clavier.
2. Vérifier que les cartes ne redémarrent pas : aucune ligne `CARD_REBOOTED`,
   aucun nouveau boot UID pendant la campagne.
3. Choisir une touche commissionnée par carte, jeu bref à faible duty selon le
   profil en service. Ne pas modifier le profil pendant la campagne.

### 3.2 Référence avant correction (si un firmware ancien est disponible)

Cette étape est informative ; elle ne doit pas être rejouée sur le firmware
corrigé.

1. Avec l'ancien firmware, envoyer un ON/OFF toutes les 10 s.
2. Attendu : premier refus à la première commande après **2 147,486 s**
   (± 10 s d'échantillonnage).
3. Sur l'ancien firmware, le refus est visible comme `COMMAND_IGNORED` raison
   PILOTE, sans cause fine ; il n'existe pas encore de `reject_sleep`.
4. Noter le temps, l'uptime et la ligne exacte. Si l'ancien firmware n'est pas
   disponible, conserver les journaux historiques et la démonstration hôte W-09.

### 3.3 Campagne corrigée

1. Depuis T0, envoyer un cycle toutes les 10 s : **ON à t, OFF à t + 1 s**,
   pendant au moins 150 min, soit au moins 900 cycles. Ne pas envoyer d'autre
   note, ne pas réarmer le réveil, ne pas faire de reset ni d'auto-test.
2. Toutes les 5 min, relever `m stats` au repos et consigner :
   `solenoid_reject_sleep`, `reject_rest`, `reject_busy`, `reject_fault`,
   `reject_other`, `solenoid_commands_ok`, `solenoid_rejected`, `rx_overrun`.
3. Vérifier que chaque note produit le mouvement attendu à chaque cycle.
4. La campagne couvre deux anciennes fenêtres de panne :
   `[2 147,486 s ; 4 294,972 s]` et `[6 442,458 s ; 8 589,944 s]` après T0.
5. Ne pas redémarrer les cartes. Un redémarrage invalide la mesure d'uptime et
   doit être noté.

Critères :

* **zéro** `solenoid_reject_sleep` sur les 900 cycles ;
* **zéro** note muette constatée ;
* aucune augmentation inexpliquée de `reject_*`, `rejected`, `rx_overrun` ou
  `can_errors` ;
* aucune ligne `CARD_REBOOTED`.

Une seule occurrence de `reject_sleep` après le délai initial est un échec :
conserver les compteurs, l'état du canal et l'heure, puis ne pas relancer avant
analyse.

## 4. Campagne 24 h — recherche d'une cause distincte

### 4.1 Mise en place

1. Partir d'un système sain et d'un profil déjà propagé.
2. Noter T0 et relever une première fois `m stats`.
3. Vérifier l'absence de `PROFILE_SYNC_FAILED`, de défaut de canal et de
   divergence de profil.
4. Laisser sous tension **24 h sans trafic CAN applicatif ni auto-test** : pas de
   `p send`, pas de jeu, pas de requête `m stats` pendant l'attente, pas de reset.
5. Utiliser une alimentation ou un enregistreur permettant d'affirmer qu'aucune
   coupure d'alimentation n'a eu lieu. À défaut, la limite de détection doit être
   écrite dans le compte rendu.

### 4.2 Au terme des 24 h

1. Relever `m stats` **avant** de jouer.
2. Jouer toutes les touches commissionnées, une par une, avec un repos suffisant ;
   vérifier le mouvement de chaque solénoïde.
3. Relever `m stats` après le jeu.
4. Comparer :

| Compteur | Interprétation |
|---|---|
| `solenoid_reject_sleep` | réveil non verrouillé ou état global incohérent |
| `solenoid_reject_fault` | défaut DRV8874 / NFAULT |
| `solenoid_reject_busy` | canal encore actif ou état local incohérent |
| `solenoid_reject_rest` | repos non écoulé |
| `solenoid_reject_other` | PARAM/PWM/FAULT_PERSISTS, à qualifier |
| `rx_overrun`, `can_errors` | perte de trames CAN |
| `solenoid_commands_ok` | commandes réellement exécutées |

5. Vérifier qu'aucune ligne `CARD_REBOOTED` n'est apparue et que les cartes sont
   toujours les mêmes UID.

Critères :

* aucune note muette ;
* entre le relevé avant jeu et le relevé après jeu : Δ`solenoid_rejected` = 0
  et Δ de chacun des compteurs 13 à 17 = 0 ; `solenoid_reject_sleep` = 0 en
  valeur absolue ;
* aucun redémarrage de carte ;
* le résultat doit être comparé à la campagne uptime : un échec 24 h malgré un
  uptime vert est un **autre** problème, pas une régression du verrou.

### 4.3 Si la campagne 24 h échoue

Ne pas relancer plusieurs auto-tests « jusqu'à ce que ça reparte ». Relever
d'abord, au moment d'une commande :

1. les compteurs 13–17 avant/après ;
2. l'état logique du canal et le bitmap local ;
3. la tension 24 V au driver ;
4. le niveau nSLEEP ;
5. NFAULT ;
6. la présence et la forme de l'impulsion de courant (sonde de courant ou
   oscilloscope).

Une cause matérielle n'est affirmée qu'avec ces chiffres. Documenter le point de
mesure et la valeur, sans démontage non nécessaire.

## 5. Vérification du mélange de firmwares

Si une carte ancienne est disponible :

1. garder son firmware antérieur (IDs 0–12 seulement), flasher les autres et le
   Clavier avec ADR-0024 ;
2. lancer `m stats` ;
3. attendu : les IDs 13–17 de la carte ancienne sont `n/a` ou omis, **sans**
   `[STATUS] code=NO_RESPONSE pos=N` ni carte marquée absente ;
4. un compteur historique de cette carte doit rester lisible.

Si la carte ne répond à aucun compteur, `NO_RESPONSE` reste correct et doit être
émis une fois.

## 6. Résultats à consigner

Créer `build/logs/adr0024_banc.md` :

| Élément | Valeur |
|---|---|
| Date, opérateur, UIDs, versions firmware | |
| T0, heure de boot, absence de redémarrage | |
| Uptime : cycles, première anomalie, `reject_sleep` | |
| Deux fenêtres de panne traversées | oui/non |
| 24 h : début/fin, alimentation, `CARD_REBOOTED` | |
| `m stats` avant/après 24 h | |
| Touches muettes | |
| Mesures 24 V / nSLEEP / NFAULT / courant | |
| Mélange ancienne/nouvelle carte | |
| Conclusion séparée uptime / 24 h | |

Le compte rendu doit distinguer :

1. ce qui est **prouvé** par les tests hôte ;
2. ce qui est **prouvé** au banc ;
3. ce qui reste **non mesuré** (cause physique du mutisme après inactivité,
   défauts NFAULT réels, comportement thermique).

## 7. Critères de sortie

ADR-0024 est validée lorsque :

* W-01 à W-09 et C-01 à C-10 passent ;
* les deux firmwares compilent sans warning nouveau ;
* la campagne de 150 min ne montre aucun refus SLEEP, y compris après
  2³¹ µs et 2³² µs d'uptime ;
* la campagne de 24 h ne montre aucune note muette et aucun refus non expliqué ;
* le mélange de firmwares affiche correctement les compteurs indisponibles ;
* les compteurs `RX_OVERRUN` et les causes 13–17 sont corrects ;
* aucun reset automatique, aucune reprise de note et aucune limite du pilote
  n'ont été ajoutés.

Si seule la campagne 24 h échoue, ADR-0024 peut être validée pour le verrou mais
le second problème reste **ouvert** et doit être orienté vers l'Architecte avec
les compteurs et mesures recueillis.
