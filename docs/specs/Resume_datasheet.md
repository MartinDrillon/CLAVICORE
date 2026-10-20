# Spécifications Techniques Firmware : Système de Contrôle de Clavecin Automatisé

1. Architecture Système et Topologie Matérielle


Le système exploite les capacités avancées du RP2350, notamment sa flexibilité entre les architectures ARM Cortex-M33 (avec extensions de sécurité TrustZone) et Hazard3 RISC-V, bien que l'implémentation standard privilégie ici le mode M33 pour la compatibilité SDK.

Caractéristique	Carte Clavier (Pico 2)	Carte Moteur (Nœud Esclave)
Microcontrôleur	                                RP2350A	RP2354B (QFN-80)
Cœurs	Dual Cortex-M33 @ 150MHz	            Dual Cortex-M33 @ 150MHz
Mémoire Flash	4 Mo Externe (QSPI)	            2 Mo Intégrée (SiP)
Ressources Mémoire	520 Ko SRAM	                520 Ko SRAM
Blocs PIO	3 blocs (12 State Machines)	        3 blocs (12 State Machines)
Rôle Spécifique	Scan matrice, filtrage MIDI	    Pilotage DRV8874, Acquisition ADC

1.2 Adressage et Identification

L'identification des nœuds sur le bus CAN s'affranchit des commutateurs physiques (DIP switches). Le firmware utilise la fonction pico_get_unique_board_id pour extraire l'OTP Unique ID (identifiant unique 64-bit) de chaque puce. Cet identifiant est utilisé lors de la phase d'énumération pour assigner dynamiquement les ID de message CAN en fonction de la position de la carte dans l'instrument, simplifiant drastiquement le déploiement et le remplacement d'un nœud défaillant.

2. Implémentation du Bus CAN via CAN2040 (PIO)

L'utilisation de la bibliothèque logicielle CAN2040 exploitant les blocs PIO est impérative. Cette approche délègue le bit-stuffing, le calcul de CRC et l'arbitrage aux machines d'état PIO, garantissant un débit de 1Mbit/s sans surchargeant le CPU.

2.1 Configuration du Hardware PIO et SDK

Le développement doit obligatoirement s'appuyer sur le Pico SDK v2.0.0 (ou supérieur) avec le flag de compilation -DPICO_RP2350.

* Initialisation : La fonction can2040_setup doit viser le bloc PIO2 sur RP2350 pour isoler le trafic CAN des autres fonctions PIO.
* Mappage GPIO : Les pins CAN RX et CAN TX doivent être configurées dans les plages autorisées par le RP2350 (typiquement GPIO 0-31 ou 16-47 pour un routage cohérent).

2.2 Paramétrage de la Couche Physique et Latence

La précision temporelle est critique pour la synchronisation musicale :

* Vitesse : Bitrate fixé à 1Mbit/s.
* Horloge : sys_clock configurée à 150MHz.
* Gestion de la Latence : Le Source Context avertit qu'une latence supérieure à 3-7 temps de bit dégrade les performances. En conséquence, le firmware doit mapper le code PIO et la fonction de callback can2040_rx_cb en SRAM via la directive copy_to_ram (ou via le linker script) pour éliminer les délais d'accès à la mémoire Flash.

2.3 Gestion des Flux de Données

La fonction can2040_callback_config gère les notifications suivantes :

* CAN2040_NOTIFY_RX : Réception d'une commande de vélocité. Le message doit être copié immédiatement dans une file d'attente (FIFO) pour traitement hors contexte IRQ.
* CAN2040_NOTIFY_TX : Confirmation d'envoi des rapports d'état moteur.
* CAN2040_NOTIFY_ERROR : Alerte de saturation des buffers PIO, déclenchant une procédure de réinitialisation de la pile CAN.

3. Pilotage des Solénoïdes via Driver DRV8874

Le driver DRV8874 est central pour la modulation du courant, permettant de varier la force de frappe du sautereau et d'obtenir des nuances musicales précises.

3.1 Modes de Contrôle Logiciel

Le firmware implémente le mode PH-EN (Phase-Enable). Ce mode permet un contrôle bidirectionnel nécessaire au désengagement rapide du solénoïde, tout en offrant une réduction du bruit de commutation par rapport au mode PWM binaire classique, préservant ainsi la pureté acoustique du clavecin.

3.2 Monitoring de Courant et Protection

La lecture de la broche IPROPI via l'ADC interne du RP2350 permet un feedback temps réel :

* Stall Detection : Si le courant dépasse un seuil défini pendant t > t_limit, le firmware détecte un blocage mécanique.
* Open-Load : Une absence de courant lors d'une commande active signale une défaillance de la bobine.

3.3 Configuration des Paramètres Critiques

Le firmware doit initialiser rigoureusement les interfaces suivantes :

* nSLEEP : Sortie numérique maintenue à l'état haut pour activer le pont en H.
* VREF : fixée à 3.3V
* nFAULT : Configuré en entrée d'interruption prioritaire pour couper immédiatement les commandes en cas de surchauffe ou de court-circuit.

4. Acquisition de Données et Multiplexage (74HC4051D)

Pour surveiller les 8 solénoïdes d'une carte moteur avec un seul canal ADC, un multiplexeur analogique 74HC4051D est utilisé.

4.1 Séquencement des Lectures ADC

Le firmware pilote les broches de sélection S0, S1, S2 pour balayer les retours de courant IPROPI. L’objectif est de pouvoir suivre en direct le jeu sur le clavecin, et donc le mouvement des solénoïdes. La fonction est expérimentale et doit être testée sur le hardware.

S2 (GPIO)	S1 (GPIO)	S0 (GPIO)	Canal Sélectionné (Solénoïde)
0	0	0	Canal 0
0	0	1	Canal 1
0	1	0	Canal 2
0	1	1	Canal 3
1	0	0	Canal 4
1	0	1	Canal 5
1	1	0	Canal 6
1	1	1	Canal 7

4.2 Analyse de l'Écoute Acoustique

La fréquence d'échantillonnage doit être suffisamment élevée pour capturer la signature de courant lors du "pincement" de la corde.

5. Gestion de l'Énergie, Horloges et Sécurité

5.1 Chaîne de Conversion et Filtrage

Le filtrage du rail de puissance est critique pour la stabilité des drivers.

* Régulation : Cascade TPS54302 (5V) vers TLV1117 (3.3V) pour isoler le bruit numérique.