#Importing libraries

import magpylib as magpy
import numpy as np
import matplotlib.pyplot as plt
import plotly.graph_objects as go


#Initial parameters

resolution=1000    #number of points
R=4e-3            #in m
L=30e-3
layers=3
diameter=0.4e-3      #diameter of individual wire
turns=75           #number of turns
plotting=1
current=3          #in A

fig = go.Figure()

#Defining Solenoid

def create_solenoid(R,L,layers,diameter,turns,current,plotting,resolution=resolution):
    # resolution=resolution : garde le comportement d'origine (1000 points,
    # la valeur globale definie plus haut) si on ne precise rien, mais
    # permet maintenant de le faire varier par appel -- utile quand on
    # compare des configs avec des nombres de tours tres differents.
    coils=[]
    coordinates=[]
    # Couches en serie (un seul fil continu) -> le meme courant total traverse
    # chaque couche.
    layer_current=current

    # Couches imbriquees dans les creux (bobinage manuel, orthocyclique) :
    # chaque couche a UN TOUR DE MOINS que celle du dessous (elle demarre et
    # finit un demi-pas plus en retrait de chaque cote), et son rayon
    # n'augmente que de diametre*racine(3)/2 par rapport a la couche du
    # dessous (pas un diametre plein) -- geometrie standard de l'empilement
    # hexagonal de cercles. "turns" ici = nombre de tours de la couche la
    # PLUS INTERNE (couche 0) ; les couches suivantes en ont progressivement
    # moins. Meme convention utilisee dans electrical.py (coil_resistance)
    # et dans les formules de coil_configs.xlsx -- les trois DOIVENT rester
    # coherents entre eux si l'un est modifie.
    NESTING_FACTOR = 3**0.5 / 2   # ~0.866
    turn_pitch = L / turns        # pas axial entre deux tours consecutifs d'une meme couche

    for i in range (layers):

        turns_i = turns - i
        if turns_i < 1:
            raise ValueError(
                f"create_solenoid : pas assez de tours pour {layers} couches imbriquees "
                f"(la couche {i} n'aurait que {turns_i} tour(s)). Reduis 'layers' ou augmente "
                f"'turns' (via L/wire_diameter/packing_factor)."
            )

        radius_i = R + i * diameter * NESTING_FACTOR
        inset_i = (i + 1) * turn_pitch / 2

        theta=np.linspace(0,2*np.pi*turns_i,resolution)
        z=np.linspace(-L/2+inset_i,L/2-inset_i,resolution)
        
        x=radius_i*np.cos(theta)
        y=radius_i*np.sin(theta)
        z=z
        
        
        layer_coordinates=np.stack((x,y,z),axis=1)
        
        coil_layer = magpy.current.Polyline(
            current=layer_current,
            vertices=layer_coordinates
        )
        
        coils.append(coil_layer)
        coordinates=np.vstack((layer_coordinates))

        if plotting==True:
            fig.add_trace(go.Scatter3d(
                x=layer_coordinates[:, 0], y=layer_coordinates[:, 1], z=layer_coordinates[:, 2],
                mode='lines', name=f'Layer {i+1}'))
    
    Solenoid=magpy.Collection(coils)
        
    #Solenoid.show()
    
    
    if plotting==True:
        fig.show(renderer="browser")

    return Solenoid, coordinates


#Cost function
 
#Where:
 
# — max and min field magnitudes over the spherical ROI
# — mean field magnitude over the ROI
#The ROI is a sphere of radius 
#, sampled on a cubic grid, where 
# is the fractional ROI radius and 
# is the coil radius


def cost_function(mags,roi,R): #  of radius in decimal form, R = coil radius in m (maintenant un paramètre explicite, plus une variable globale)

    roi_grid=np.mgrid[-roi*R:roi*R:10j, -roi*R:roi*R:10j, -roi*R:roi*R:10j].T
    

    r = np.linalg.norm(roi_grid, axis=-1)   # shape is (N,N,N,3)
    mask = r <= roi*R

    sphere_points = roi_grid[mask]
    
    B_roi=mags.getB(sphere_points)
    
    magnitude_B=np.linalg.norm(B_roi,axis=-1)
    
    mean_B=np.mean(magnitude_B)
    
    B_max=magnitude_B.max()
    B_min=magnitude_B.min()
    eta=((B_max-B_min)/mean_B)*1e6
    
    return eta, mean_B*1000, sphere_points

#Visualize in 3D

def visualize_3d(grid,mags):    #grid to be (N,N,N,3)

    sens = magpy.Sensor(pixel=grid)
    
    # hide axes arrows
    sens.style.arrows.x.show = False
    sens.style.arrows.y.show = False
    sens.style.arrows.z.show = False
    
    # control size
    sens.style.size = 2
    
    # pixel field settings
    sens.style.pixel.field.source = "B"
    sens.style.pixel.field.sizescaling = "linear"
    sens.style.pixel.field.colormap = "Inferno"
    sens.style.pixel.field.colorscaling = "linear"
    sens.style.pixel.field.symbol = "arrow3d"
    
    
    fig = magpy.show([sens, mags], backend="plotly", return_fig=True)
    fig.show(renderer="browser")


# Tout ce qui suit ne s'exécute QUE si on lance ce fichier directement
# (ex: python Solenoid.py), et PAS quand on fait
# "from Solenoid import create_solenoid" depuis un autre fichier (ex: main.py).
# C'est ce garde-fou qui permet de réutiliser les fonctions ci-dessus
# ailleurs sans déclencher la démo (calcul + affichage 3D) à chaque import.

if __name__ == "__main__":

    #Creating a coil

    solenoid,coordinates=create_solenoid(R,L,layers,diameter,turns,current,plotting)  #coils is a mags object
    roi=0.1
    grid_3D=np.mgrid[-R*1.5:R*1.5:10j, -R*1.5:R*1.5:10j, -L/2:L/2:20j].T
    eta,mean_B,sphere_points=cost_function(solenoid,roi,R)

    #Results

    visualize_3d(grid_3D,solenoid)

    print('For Solenoid','\n')
    print(f'homogeneity ',eta,'\n')
    print(f'field (mT) ',mean_B,'\n')
