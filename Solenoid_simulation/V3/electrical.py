#Importing libraries

import numpy as np


#Constante physique

COPPER_RESISTIVITY = 1.68e-8   # Ohm.m, cuivre recuit a 20C (valeur standard)
NESTING_FACTOR = 3 ** 0.5 / 2   # ~0.866, meme convention que Solenoid.py (couches imbriquees)


#Resistance d'un bobinage en serie (un seul fil continu, couches imbriquees
#dans les creux -- meme geometrie que create_solenoid dans Solenoid.py, les
#deux DOIVENT rester coherents si l'un des deux est modifie)

def coil_resistance(R, L, layers, wire_diameter, turns_per_layer0, copper_diameter=None):
    """
    Resistance (Ohm) d'un bobinage en serie, couches imbriquees dans les
    creux (bobinage manuel, orthocyclique) : chaque couche a un tour de
    moins que la couche du dessous, et son rayon croit de
    wire_diameter*sqrt(3)/2 (pas un diametre plein) par rapport a la
    couche precedente. Toutes les longueurs en metres.

    Parametres
    ----------
    R : float
        Rayon interieur du bobinage (rayon de la couche 0), en m.
    L : float
        Longueur du solenoide, en m.
    layers : int
        Nombre de couches.
    wire_diameter : float
        Diametre TOTAL du fil (cuivre + email), en m -- determine
        l'encombrement (tours/couche, pas radial entre couches).
    turns_per_layer0 : int
        Nombre de tours de la couche la PLUS INTERNE (couche 0). Les
        couches suivantes en ont turns_per_layer0 - i (couche i).
    copper_diameter : float ou None
        Diametre du CUIVRE NU (sans email), en m -- determine la section
        utile pour la resistance. Toujours <= wire_diameter (l'email prend
        typiquement 0.02-0.04mm sur le diametre total ; Config_Coil calcule
        ca automatiquement par defaut : wire_copper_diameter_mm =
        wire_diameter_mm - 0.03mm, surchargeable a la main). Si None, on
        suppose copper_diameter = wire_diameter (comportement le plus
        optimiste -- sous-estime la resistance reelle).

    Retourne
    --------
    resistance : float
        Resistance totale du bobinage, en Ohm.
    """

    if copper_diameter is None:
        copper_diameter = wire_diameter

    wire_section = np.pi * (copper_diameter / 2) ** 2
    wire_length = 0.0

    for i in range(layers):
        turns_i = turns_per_layer0 - i
        if turns_i < 1:
            raise ValueError(
                f"coil_resistance : pas assez de tours pour {layers} couches imbriquees "
                f"(la couche {i} n'aurait que {turns_i} tour(s))."
            )
        radius_i = R + i * wire_diameter * NESTING_FACTOR
        wire_length += turns_i * 2 * np.pi * radius_i

    return COPPER_RESISTIVITY * wire_length / wire_section
