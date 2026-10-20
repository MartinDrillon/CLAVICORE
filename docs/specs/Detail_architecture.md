
Moitié “Clavier”

Simple matrice de 80 touches géré par un pico 2. Les évènements “touches pressées” sont communiqué CAN avec la partie “moteur du système” (via ce repo git : https://github.com/KevinOConnor/can2040/blob/master/docs/Tools.md). Elles peuvent également être transmise en simultané en midi, si un périphérique usb relié à un appareil adapté est branché au pico 2. 
8 switch de la matrice permettent de gérer la transposition et d’autres actions de contôle. 30 leds RGB indiquent diverses informations relative à ce qui se passe. 
Possibilité d’envoyer du midi (usb et midi din), et d’en recevoir (à interpréter puis à transférer à la moitié "moteur"). Le Clavier expose aussi un journal diagnostic texte sur le port série virtuel USB CDC du périphérique composite, à lire avec PuTTY.

Moitié “Moteur” 

24 Volt IN - 20 A max avec le HLG-480 Routé aux 49 solénoïdes, 2A à 3A (à définir avec les prototypes), 24V par unité, bobine confectionnée sur mesure et aimant 
Routé à 5 cartes de contrôle des solénoïde, chaque carte ayant la capacité de gérer 12 solénoïde (4 cartes en contrôleront 10, et 1 carte 9), liées entre elles en CAN. Toutes les cartes exécutent le même firmware et sont identifiées au démarrage par l'UID matériel 64 bits de leur RP2350. La carte clavier conserve de façon persistante une table de commissioning modifiable (UID -> canal -> touche) : les cartes moteur restent interchangeables et ne connaissent pas leur rôle musical.
En entrée, buck converter (TPS54302) pour passer de 24 à 5 volt 
LDO tlv1117-33IDCYR pour passer de 5 à 3,3 volt stable 
RP2354B en cœur de système
Un driver DRV8874PWPR et 680uF par solénoïde (push ou pull grâce à l’aimant et au pont en H du driver). La sortie IPROPI de chaque driver sert à un auto-test **sur demande explicite seulement** (bouton Moteur maintenu 3 s, orchestration par le Clavier) : une faible impulsion caractérisée sur prototype produit un bitmap d'occupation des 12 canaux, comparé par la carte clavier à la table de commissioning persistante. Il n’y a **pas** d’auto-test au boot. Toute divergence doit être signalée explicitement, jamais compensée par un remapping automatique.