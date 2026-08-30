# BOM — Alimentation & isolation, carte Clavier (Pico 2)

Références fabricant vérifiées ; codes LCSC "C" et statut Basic/Extended à
reconfirmer sur jlcpcb.com/parts avant commande (non interrogeable en direct
depuis cet outil).

## A. Alimentation dédiée du transceiver CAN (et LDO carte moteur)

Même choix de composants pour `IC3` (carte clavier) et le LDO 3,3V des
cartes moteur, pour rester cohérent et limiter la variété de références.

| Réf | Fonction | Référence | Boîtier | JLCPCB | Notes |
|---|---|---|---|---|---|
| IC3 | LDO 3,3V | AMS1117-3.3 | SOT-223 | Basic (courant) | Remplace TLV1117-33IDCY (même pinout/footprint SOT-223) |
| C_in | Découplage entrée LDO | 10 µF X7R céramique | 0805 | Basic | Céramique conservée : filtre le bruit HF du buck en amont (TPS54302), l'ESR faible est ici un avantage |
| C_out | Découplage sortie LDO | 2× 10 µF tantale (≈20 µF total) | Case B (ou équiv.), 16V | Basic (courant) | Tantale plutôt que céramique : l'AMS1117 (dérivé LM1117) a besoin d'une ESR de sortie plus élevée pour la stabilité de boucle. Polarisé — vérifier l'orientation. Déclassement en tension : 16V pour un rail à 3,3V. Footprint différent de la céramique 0805 → modif de layout |
| C_hf | Découplage HF sortie | 100 nF X7R | 0603 | Basic | Déjà présent dans le schéma (C78) |

## B. Transceiver CAN

| Réf | Fonction | Référence | Boîtier | JLCPCB |
|---|---|---|---|---|
| IC12 | Transceiver CAN | TCAN332DR (déjà dans le schéma) | SOT-23-8 | Extended (probable) |

## C. Protection à l'entrée `5VIN` (connecteur venant de la carte moteur)

| Réf | Fonction | Référence type | Boîtier | JLCPCB | Notes |
|---|---|---|---|---|---|
| F1 | Fusible réarmable (PTC) | PTC SMD, ~1 A hold | 1206 ou 0603 | Basic (courant) | Dimensionner selon charge réelle (Pico + CAN + LED éventuelles sur le 5V) |
| D_tvs | Protection ESD/transitoires 5VIN | TVS unidirectionnel, standoff 5V (ex. PESD5V0S1BA / SMF5.0A) | SOD-323 / SOT-23 | Basic (probable) | Protège contre surtensions du câble inter-cartes |
| C_bulk | Découplage bulk entrée | 10-22 µF X7R | 0805/1206 | Basic | — |
| C_hf2 | Découplage HF entrée | 100 nF X7R | 0603 | Basic | — |

## D. Isolation USB — hors BOM JLCPCB

Module autonome prêt à l'emploi, placé dans le câble USB entre le Pico et
l'ordinateur/hôte MIDI (pas soudé sur la carte, pas dans l'assemblage
JLCPCB). Basé sur puce Analog Devices ADUM3160 ou ADUM4160 (isolateur
USB 2.0 full-speed, 2,5 kV) — achat séparé (Digikey/Mouser/Adafruit ou
modules génériques équivalents).

## Décisions d'architecture (rappel)

- Pico 2 alimenté via `VSYS` depuis `5VIN` (pas via `VBUS`) — utilise le
  régulateur interne du module pour son propre 3,3V.
- LDO dédié (IC3) réservé au transceiver CAN uniquement, pour isoler ce
  domaine d'alimentation du rail interne du Pico (pas de connexion en
  parallèle des deux sorties 3,3V).
- Masse commune GND obligatoire entre carte clavier et carte moteur
  (même câble que le 5V/CAN) ; isolation nécessaire uniquement côté USB
  (lien avec un référentiel de masse indépendant), pas côté CAN.
