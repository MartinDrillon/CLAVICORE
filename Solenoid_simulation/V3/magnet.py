#Importing libraries

import magpylib as magpy
import plotly.graph_objects as go


#Defining the magnet
#
# Deux formes supportees, correspondant aux colonnes 'shape' de l'onglet
# Config_Magnet : "cylindre" (magpy.magnet.Cylinder) ou "carre" (magpy.magnet.Cuboid,
# qui accepte en fait n'importe quel rectangle, pas seulement un carre parfait).
# Si tu as besoin d'une autre forme (spherique, anneau...), ajoute une branche ici
# et un shape correspondant dans Config_Magnet -- la suite du fichier (position,
# polarisation, meshing) ne change pas.

VALID_SHAPES = ("cylindre", "carre")


def create_magnet(shape, dim1, dim2, height, polarization_T, position=(0, 0, 0),
                   meshing=50, color="green", plotting=False):
    """
    Cree un aimant permanent avec magpylib, cylindrique ou rectangulaire.

    Parametres
    ----------
    shape : str
        "cylindre" -> magpy.magnet.Cylinder(dimension=(dim1, height)), dim1 = DIAMETRE.
        "carre" -> magpy.magnet.Cuboid(dimension=(dim1, dim2, height)), dim1/dim2 = les
        deux cotes de la section (peut donc etre rectangulaire, pas seulement carre).
        Insensible a la casse/aux espaces.
    dim1 : float
        Diametre (cylindre) ou premier cote (carre), en m.
    dim2 : float ou None
        Ignore si shape="cylindre". Deuxieme cote si shape="carre".
    height : float
        Hauteur (epaisseur) selon l'axe d'aimantation, en m, pour les deux formes.
    polarization_T : float
        Magnitude du vecteur de polarisation J (= Br, remanence), en Tesla. L'aimant
        est toujours aimante selon +Z dans son propre repere (magpylib gere ensuite la
        position) -- c'est cette convention (+Z) qui determine si sens_courant="Attire"
        rapproche ou eloigne l'aimant, voir batch_compare.py.
    position : tuple(float, float, float)
        Position du CENTRE de l'aimant dans le repere global, en m. Le centre de la
        bobine est l'origine (0,0,0) -- voir Config_Magnet (x0_mm, y0_mm, z0_mm).
    meshing : int
        Finesse du maillage utilisee UNIQUEMENT pour le calcul de force (magpy.getFT).
        Sans effet sur le calcul de champ (getB). 50 est un bon compromis
        precision/vitesse pour des aimants de quelques mm a 1-2cm -- augmente si
        l'aimant est nettement plus gros.
    color : str ou None
        Couleur d'affichage 3D. None = couleur par defaut de magpylib.
    plotting : bool
        Si True, affiche l'aimant seul en 3D pour verification rapide.

    Retourne
    --------
    magnet : magpy.magnet.Cylinder ou magpy.magnet.Cuboid
        Objet source magpylib, utilisable seul ou dans une magpy.Collection.
    """

    shape_norm = str(shape).strip().lower()
    polarization = (0, 0, polarization_T)

    if shape_norm == "cylindre":
        magnet = magpy.magnet.Cylinder(
            dimension=(dim1, height),   # magpylib attend (diametre, hauteur), pas le rayon
            polarization=polarization,
            position=position,
            meshing=meshing,
        )
    elif shape_norm == "carre":
        magnet = magpy.magnet.Cuboid(
            dimension=(dim1, dim2, height),
            polarization=polarization,
            position=position,
            meshing=meshing,
        )
    else:
        raise ValueError(
            f"create_magnet : shape={shape!r} inconnu, attendu l'un de {VALID_SHAPES} "
            f"(voir la colonne 'shape' de Config_Magnet)."
        )

    if color is not None:
        magnet.style.color = color

    if plotting == True:
        fig = go.Figure()
        magpy.show(magnet, canvas=fig, backend="plotly")
        fig.show(renderer="browser")

    return magnet


# Meme logique de garde-fou que dans Solenoid.py : ce bloc ne s'execute
# que si on lance "python magnet.py" directement, pas lors d'un import.
if __name__ == "__main__":

    magnet = create_magnet(
        shape="cylindre",
        dim1=6e-3,
        dim2=None,
        height=13e-3,
        polarization_T=1.4,
        position=(0, 0, 0),
        plotting=True,
    )

    print('For Magnet alone', '\n')
    print('B at origin (T):', magnet.getB((0, 0, 0)), '\n')
