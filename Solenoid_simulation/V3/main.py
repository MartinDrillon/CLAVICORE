#Importing libraries

import numpy as np
import magpylib as magpy

from Solenoid import create_solenoid, cost_function, visualize_3d
from magnet import create_magnet
from analysis import sweep_axial_force, plot_force_curve
from electrical import coil_resistance


#Solenoid parameters

R=4e-3            #in m
L=30e-3
layers=3
diameter=0.4e-3

#turns (par couche) n'est PAS un parametre libre : il est impose par la
#geometrie (L, diametre du fil) et par le packing factor du bobinage (jamais
#100% en pratique, surtout a la main). ~75% est une estimation raisonnable
#pour un bobinage manuel -- ajuste si tu mesures autre chose. Meme formule
#que dans coil_configs.xlsx (colonne "turns" de l'onglet Config_Coil).
packing_factor=0.75
turns=int(L/diameter*packing_factor)

#Diametre du cuivre nu : meme convention par defaut que Config_Coil (diametre
#total - 0.03mm d'email typique). Surcharge directement si tu as mesure le tien.
copper_diameter = diameter - 0.03e-3

#Le courant n'est PAS non plus un parametre libre : une fois la bobine
#bobinee (donc sa resistance fixee) et l'alimentation choisie (12V ou 24V),
#le courant est impose par la loi d'Ohm (I=V/R), pas choisi. On calcule donc
#d'abord la resistance, puis le courant qui en resulte a pleine tension
#(regime "impulsion" -- voir l'onglet Electrique de coil_configs.xlsx pour le
#regime de maintien en PWM).
voltage=12          #in V -- change a 24 pour comparer
resistance = coil_resistance(R, L, layers, diameter, turns, copper_diameter)
current_magnitude = voltage / resistance

#sens_courant : "attire" (comportement historique, force negative = attraction
#vers le centre de la bobine) ou "repousse" (courant inverse -> force inversee).
#Meme convention que la colonne sens_courant de Config_Coil.
sens_courant = "attire"
current = current_magnitude if sens_courant == "attire" else -current_magnitude

print(f'Resistance du bobinage : {resistance:.4f} Ohm\n')
print(f'Courant a pleine tension ({voltage}V, sens={sens_courant}) : {current:+.3f} A\n')


#Magnet parameters (aimant S-06-13-N48 par defaut -- change shape/grade/dimensions
#pour tester un autre aimant, meme logique que l'onglet Config_Magnet)

magnet_shape = "cylindre"          # "cylindre" ou "carre" (voir magnet.py)
magnet_dim1 = 6e-3                 # diametre (cylindre) ou 1er cote (carre), en m
magnet_dim2 = None                 # ignore pour "cylindre" ; 2e cote si "carre"
magnet_height = 13e-3              # en m

#Br (remanence, en T) : milieu de plage du grade N48 (1.38-1.42T, voir la table
#de reference dans Config_Magnet) -- remplace par la valeur exacte de ta fiche
#technique si tu l'as.
magnet_Br = 1.40                   # in T

#Position de depart de l'aimant (x0, y0, z0), en m, centre de la bobine = origine.
#z0 = L/2 + magnet_height/2 place le bas de l'aimant a fleur du sommet de la
#bobine (gap=0) -- augmente z0 pour simuler un ecart (gap) au repos.
magnet_position = (0.0, 0.0, L / 2 + magnet_height / 2)


#Building the two objects

solenoid, coordinates = create_solenoid(R, L, layers, diameter, turns, current, plotting=False)
magnet = create_magnet(magnet_shape, magnet_dim1, magnet_dim2, magnet_height, magnet_Br,
                        position=magnet_position, meshing=20, plotting=False)


#Force/torque sweep : de la position de depart jusqu'au centre du coil (z_end=0).
#L'aimant se deplace le long de l'axe du solenoide (note : "horizontal" ou
#"vertical" depend juste de l'orientation physique du mecanisme reel, le calcul
#ne depend que de la distance parcourue le long de cet axe).

z_values, F = sweep_axial_force(solenoid, magnet, start_position=magnet_position,
                                 z_end=0.0, step=0.5e-3)

fig = plot_force_curve(z_values, F, z_rest=magnet_position[2], zoom_mm=10,
                        title="Clavecin — Force sur l'aimant lors de l'attraction")


#Résultats clés

displacement_mm = (magnet_position[2] - z_values) * 1000
mask_1cm = displacement_mm <= 10

print('Position de repos (deplacement=0mm) : Fz =', F[0, 2], 'N\n')

#Le pic le plus attractif est le minimum de Fz si "attire" (force negative), le
#maximum si "repousse" (force positive) -- meme logique que batch_compare.py.
peak_fn = np.argmin if sens_courant == "attire" else np.argmax

idx_peak = peak_fn(F[:, 2])   # position ou l'effet du courant est le plus marque
print('Force max sur la course complete : Fz =', F[idx_peak, 2], 'N, a',
      displacement_mm[idx_peak], 'mm de deplacement\n')

idx_peak_1cm = peak_fn(np.where(mask_1cm, F[:, 2], 0))
print('Force max sur le premier centimetre : Fz =', F[idx_peak_1cm, 2], 'N, a',
      displacement_mm[idx_peak_1cm], 'mm de deplacement\n')

fig.show(renderer="browser")


#(Optionnel) Vue 3D interactive du systeme, a la position de repos.
#Pas indispensable au resultat principal (la courbe ci-dessus) -> desactivee
#par defaut pour aller plus vite. Mettre a True pour la reactiver.

show_3d_view = False

if show_3d_view:

    system = magpy.Collection(solenoid, magnet)

    roi=0.1
    eta, mean_B, sphere_points = cost_function(system, roi, R)

    z_top = magnet_position[2] + magnet_height/2 + 2e-3
    grid_3D=np.mgrid[-R*1.5:R*1.5:10j, -R*1.5:R*1.5:10j, -L/2:z_top:30j].T
    visualize_3d(grid_3D, system)

    print('For Solenoid + Magnet (position de repos)', '\n')
    print(f'homogeneity ',eta,'\n')
    print(f'field (mT) ',mean_B,'\n')
