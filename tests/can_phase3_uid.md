# CAN Phase 3 — Relevé de l'UID d'une carte Moteur (test manuel)

Couvre `shared/protocol/can_protocol.*`, `firmware/clavier/src/{can_link,midi,diag_log,usb_log}.*`,
`firmware/moteur/src/{can_link,board_identity}.*` (convention AGENTS.md : test manuel
documenté pour tout chemin touchant au CAN et au MIDI). Décision d'affichage :
ADR-0005.

Légende : **[À VÉRIFIER]** = non vérifié, à confirmer sur le matériel / dans le logiciel.

## 0. Ce que fait ce test

Chaque carte Moteur a un UID matériel 64 bits (16 hex majuscules, par ex.
`A3FA4C99CA526F4B`). Le Clavier reçoit cet UID par CAN puis l'écrit en texte
sur le journal **USB CDC**. L'utilisateur lit la ligne dans **PuTTY** pour
reporter l'UID dans `firmware/clavier/src/uid_position.h`.

Deux déclencheurs :
- **au démarrage du Moteur** : annonce de boot ;
- **à la demande** : appui sur la touche `GET_UID` du Clavier.

## 1. Pré-requis

1. Sous-module `can2040` présent : `git submodule update --init --recursive`.
2. `shared/protocol/can_protocol.h`, `can_protocol.c`, `uid_position_format.h`
   présents.
3. Une touche `GET_UID` assignée dans `firmware/clavier/src/keymap.c`.
4. `R24` (120 Ω) soudée sur le Clavier et sur la carte Moteur testée.
5. CANH/CANL reliés, masses communes.
6. Une seule carte Moteur à la fois pour ce relevé.
7. PC : gcc (MinGW-w64/WinLibs) dans le PATH pour les tests hôte.
8. PC : **PuTTY** installé, et port COM du Clavier repéré dans le
   **Gestionnaire de périphériques** Windows.

## 2. Tests hôte (sans carte, sans CAN)

```powershell
cd "D:\Documents\MUSIQUE\EPINETTE\Kit Heugel_Bedard"
powershell -File tests\host\run_host_tests.ps1
```

Attendu : `N verifications, 0 echec(s)`. Couvre : CRC-16, `uid_tag`, jitter,
little-endian, DLC stricts, request_id, trames A/B/C, table `uid_position` et
formatage des lignes `[UID]`, `[STATUS]`, `[SELFTEST]`.

## 3. Compiler et flasher

**Clavier** : si `CMakeLists.txt` a changé, reconfigurer si nécessaire, puis :

```powershell
cmake --build build\clavier
```

Copier ensuite `build\clavier\clavier.uf2` sur le Pico 2.

**Moteur** : suivre `tests/moteur_phase2a.md` pour le flash SWD, puis :

```powershell
cmake --build build\moteur
```

## 4. Régler PuTTY

1. Brancher le Clavier en USB.
2. Dans le **Gestionnaire de périphériques**, relever le **port COM** du
   périphérique composite Clavier.
3. Ouvrir **PuTTY**.
4. Choisir **Serial**.
5. Renseigner le **port COM** relevé.
6. Le champ **Speed** peut rester à n'importe quelle valeur raisonnable :
   **sans importance fonctionnelle** pour le CDC.
7. Ouvrir la session ; garder PuTTY seul sur ce port COM.

## 5. Procédure de relevé

1. Alimenter le Clavier (USB) et la carte Moteur testée.
2. Observer la ligne d'annonce de boot dans PuTTY. Si elle a été manquée,
   appuyer sur la touche `GET_UID`.
3. Repérer une ligne de ce type :

   ```text
   [UID] pos=127 uid=A3FA4C99CA526F4B fw=0.1.0 proto=1 reply_to=0
   ```

   Interprétation :
   - `pos=127` : UID non présent dans `uid_position.h` ;
   - `uid=` : UID matériel ;
   - `fw=` : version firmware annoncée ;
   - `proto=` : version de protocole ;
   - `reply_to=0` : annonce de boot ; `1..127` = réponse à requête.
4. Relancer la demande une seconde fois avec `GET_UID` et comparer l'UID : il
   doit être identique.
5. Noter l'UID et le repère physique de la carte.
6. Répéter pour les 5 cartes, une à la fois.
7. Reporter les UID dans `firmware/clavier/src/uid_position.h`.

## 6. Ce que ce test valide

La liaison CAN, la terminaison, le débit 500 kbit/s (`CAN_BITRATE_HZ`), le
câblage RX/TX (Clavier GPIO0/GPIO1 ; Moteur GPIO46/GPIO47) et la stabilité de
l'UID relevé. Le diagnostic lisible passe par le **CDC texte**, pas par le
canal MIDI.

## 7. Lignes `[STATUS]` et cas d'erreur

Formats attendus :

```text
[STATUS] code=NO_RESPONSE
[STATUS] code=PROTO_MISMATCH
[STATUS] code=INCOMPLETE
[STATUS] code=TABLE_INVALID
[STATUS] code=UID_UNKNOWN
```

| Code | Signification / action |
|---|---|
| `NO_RESPONSE` | Aucune réponse en 500 ms. Vérifier alimentation, flash, CANH/CANL, R24, masses et GPIO. |
| `PROTO_MISMATCH` | Le Moteur annonce une autre version de protocole. Reflasher les deux cartes avec la même version. |
| `INCOMPLETE` | Trame A reçue sans B, ou B sans A. Vérifier la qualité du bus. |
| `TABLE_INVALID` | `uid_position.h` contient une entrée invalide ou un doublon. Corriger puis reflasher le Clavier. |
| `UID_UNKNOWN` | L'UID relevé n'est pas dans une table non vide. Ajouter l'entrée voulue dans `uid_position.h`. |

## 8. Exemple de session

```text
position 12 GET_UID result 0 (0=sent 1=busy 2=tx_fail)
[UID] pos=127 uid=A3FA4C99CA526F4B fw=0.1.0 proto=1 reply_to=5
```

## 9. Point non couvert

Cette procédure ne prouve pas l'activation correcte des solénoïdes ; elle ne
vérifie que le bring-up CAN et le relevé d'UID.
