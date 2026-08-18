# Clavicore 
## Le projet
> Piloter un clavecin à distance grâce à un clavier ergonomique

Tout comme pour les claviers d’ordinateurs, le clavier de piano tire sa forme de contraintes physiques et d’usages très spécifiques que la tradition a figé. En s’en libérant, il est possible de concevoir une disposition plus simple à apprendre et à jouer. 

## L’instrument
La base est une copie d’une épinette italienne ancienne, vendue dans les années 70 en kit et monté par un amateur. L’intrument est en cours de révisition (modification des sautereaux, changement des cordes, réharmonisation à la plume). 

Le système électronique sera intégré au corps de l’instrument.
 
![Épinette](Images/Epinette.jpg)

![3D Clavecin](Images/3DClavecin.png)

## L’automatisation

### Moité "Clavier"
*PCB en cours de conception*

Simple matrice de touche géré par un Pico 2. Les évènements “touches pressées” sont communiqué en CAN avec la partie “moteur" du système. Ils pourront également être transmis en MIDI à un ordinateur.

Ce projet utilisera une base de clavier ergonomique (https://github.com/MartinDrillon/IteMX3-) adaptée pour recevoir une disposition de type "Wiki-Haydn". Une présentation plus détaillée du clavier est à venir. 

## Moitié "Moteur"
*PCB en cours de conception*

La carte de contrôle est basé sur un RP2354B avec un DRV8874 par solénoïdes pour un contrôle bidirectionnel. Cinq cartes permettent de gérer les 49 touches du clavier. Les cartes communiquent en CAN.

![PCB](Images/PCB_STP.png)

![Schéma](Images/Schéma_moteur.png)

## Les solénoïdes
Les solénoïdes sont les éléments centraux du montage. Ils doivent être réalisé sur-mesure. Ceux disponibles sur le marché sont :
- Trop bruyants
- Trop encombrants
- Trop puissants ou trop faibles
- Trop chers (~5€ pièce pour la référence la proche du besoin)
### Le cahier des charges
- Bobine de 30mm x 15 mm Ø max, intégrés au corps du clavecin, pour être invisible et conserver la posibilité de jouer normalement.   
- Noyau constitué d’un aimant néodyme de 13mm x 6mm Ø N48 fixé sous la touche.
- Alimentation en 24 volt, courant de 1,5 à 3 ampères
    - Au delà, la chauffe de la bobine et le wattage nécessaire à un accord deviennent trop importants
    - En deça, la force n’est pas pas suffisante ou le nombre de spire devient trop élevé
- Fil d’un diamètres de 0,3 à 0,4mm. 
    - Au dela, la quantité de cuivre par bobine devient trop importante (coût, encombrement, bobinage complexe à réaliser à cause de l’augmentation du nombre de spire).
    - En deça, la force est trop faible ou la chauffe est trop élevée. 

## L’état d’avancement 
- [X] Génération d’un script Magpylib pour réaliser un premier dimmensionnement des solénoïdes 
- [X] Modélisation 3D de l’instument
- [X] Conception et impression 3D du corps de la bobine intégré sous les touches
- [X] Premier test concluant avec un fil de 0.4mm - 2 ampères - 24 volts
- [ ] Conception des cartes de pilotage pour l’ensemble du clacevin *en cours*
- [ ] Conception du clavier ergonomique *en cours*
- [ ] Programmation de l’ensemble
- [ ] Ajustement de la bobine en fonction de la carte de pilotage
- [ ] Réalisation artisanale de 50 bobines homogènes ! 


![Évatuation de la force](Images/03_04_15_25.png)
![3D Bobine Clavecin](Images/3DBobineClavecin.png)