#Importing libraries

import numpy as np
import magpylib as magpy
import plotly.graph_objects as go


#Sweeping the magnet's axial position

def sweep_axial_force(solenoid, magnet, start_position, z_end=0.0, step=0.5e-3):
    """
    Balaie la position de l'aimant depuis start_position jusqu'a z_end le long de
    l'axe Z (X et Y restent fixes a leur valeur de depart), et calcule la force
    s'exercant sur lui a chaque position via magpy.getFT (source=solenoid,
    target=magnet).

    Genéralisation : contrairement a l'ancienne version (qui ne balayait qu'un
    aimant demarrant sur l'axe, X=Y=0), start_position est desormais un triplet
    (x0, y0, z0) complet -- voir Config_Magnet (x0_mm, y0_mm, z0_mm). Ca permet de
    simuler un aimant decale lateralement par rapport a l'axe de la bobine, tout en
    gardant un deplacement purement axial (le cas d'usage principal, un aimant qui
    coulisse le long de l'axe du solenoide).

    Note : magpy.getFT retourne aussi un couple, mais on ne le garde pas ici — dans
    ce projet, la trajectoire de l'aimant est quasi verticale/axiale (negligeable en
    pratique), donc le couple n'apporte pas d'information utile et on simplifie en
    ne calculant/tracant que la force.

    Note d'implementation : magpylib permet en principe de donner a magnet.position
    tout un "chemin" (array de positions) pour un calcul vectorise en un seul appel.
    En pratique, pour cette geometrie (coil a resolution~1000 points/couche), cette
    approche consomme enormement de memoire (~3.5 Go pour seulement 10 points,
    plantage au-dela) sans gain de vitesse. On utilise donc une simple boucle
    Python : ca reste rapide (~0.5s par point apres un premier appel de ~2s
    d'echauffement) et ne consomme pas de memoire excessive.

    Parametres
    ----------
    solenoid : magpy.Collection
        Source de champ (le solenoide). Non modifie.
    magnet : magpy.magnet.Cylinder ou magpy.magnet.Cuboid
        Aimant cible. Doit avoir un parametre "meshing" defini (necessaire pour
        magpy.getFT). Sa position est modifiee pendant le balayage, puis restauree
        a sa valeur d'origine a la fin.
    start_position : tuple(float, float, float)
        Position de depart (x0, y0, z0) de l'aimant, en m. x0 et y0 restent fixes
        pendant tout le balayage ; seul z varie, de z0 vers z_end.
    z_end : float
        Position Z d'arrivee du balayage, en m (defaut 0.0 = centre de la bobine,
        puisque le solenoide est centre sur l'origine et s'etend de -L/2 a +L/2).
    step : float
        Pas d'echantillonnage, en m (defaut 0.5mm). Plus petit = plus precis mais
        plus lent (~0.5s par point supplementaire).

    Retourne
    --------
    z_values : ndarray (n,)
        Positions Z echantillonnees, en m (X et Y restent constants = x0, y0).
    F : ndarray (n,3)
        Force sur l'aimant a chaque position, en N.
    """

    x0, y0, z0 = start_position
    n_points = max(2, int(round(abs(z0 - z_end) / step)) + 1)
    z_values = np.linspace(z0, z_end, n_points)

    F = np.zeros((n_points, 3))

    original_position = magnet.position

    for i, z in enumerate(z_values):
        magnet.position = (x0, y0, z)
        f, _ = magpy.getFT(solenoid, magnet)   # couple (2e valeur) ignore volontairement
        F[i] = f

    magnet.position = original_position   # on remet l'aimant a sa position d'origine

    return z_values, F


#Plotting the force curve

def plot_force_curve(z_values, F, z_rest, zoom_mm=10, title="Force sur l'aimant"):
    """
    Trace la force axiale (Fz) en fonction du deplacement de l'aimant depuis sa
    position de repos. La zone des "zoom_mm" premiers millimetres (la course utile
    d'une touche de clavecin, typiquement ~1cm) est mise en evidence.

    Le signe de Fz depend a la fois du sens du deplacement (rapprochement ou
    eloignement du centre de la bobine) ET du sens du courant (sens_courant =
    "Attire" ou "Repousse" dans Config_Coil) : une valeur negative attire l'aimant
    vers le centre, une valeur positive le repousse.

    Parametres
    ----------
    z_values : ndarray (n,)
        Positions z (m), issues de sweep_axial_force.
    F : ndarray (n,3)
        Force (N) a chaque position.
    z_rest : float
        Position de repos de l'aimant selon Z (m) -> sert de reference
        (deplacement = 0). C'est le z0 utilise dans sweep_axial_force.
    zoom_mm : float
        Largeur (mm) de la zone d'interet a mettre en evidence depuis le depart de
        la course.
    title : str
        Titre du graphique.

    Retourne
    --------
    fig : plotly.graph_objects.Figure
    """

    displacement_mm = (z_rest - z_values) * 1000   # 0 = position de repos, augmente vers le centre
    Fz = F[:, 2]

    fig = go.Figure()
    fig.add_trace(go.Scatter(x=displacement_mm, y=Fz, mode="lines+markers", name="Fz (N)",
                              line=dict(color="crimson")))

    fig.add_vrect(x0=0, x1=zoom_mm, fillcolor="lightgreen", opacity=0.15, line_width=0,
                  annotation_text="course utile (~1cm)", annotation_position="top left")

    fig.update_xaxes(title_text="Deplacement depuis la position de repos (mm)")
    fig.update_yaxes(title_text="Fz (N)  [signe dependant de sens_courant, voir Config_Coil]")
    fig.update_layout(title=title, height=500)

    return fig


#Overlaying several configurations on the same chart

def plot_force_overlay(results, zoom_mm=10, title="Comparaison des configurations"):
    """
    Superpose la force axiale de plusieurs configurations sur un meme graphique
    (une courbe par config), pour comparaison directe.

    Parametres
    ----------
    results : list[dict]
        Une entree par configuration, chacune avec les cles :
        "name" (str), "z_values" (ndarray m), "F" (ndarray n×3, N),
        "z_rest" (float, m).
        Typiquement construit en appelant sweep_axial_force() une fois par config
        et en empaquetant le resultat dans un dict.
    zoom_mm : float
        Largeur (mm) de la zone d'interet mise en evidence.
    title : str
        Titre du graphique.

    Retourne
    --------
    fig : plotly.graph_objects.Figure
    """

    fig = go.Figure()

    palette = ["crimson", "royalblue", "seagreen", "darkorange", "purple",
               "teal", "brown", "magenta", "gray", "gold"]

    for i, res in enumerate(results):
        color = palette[i % len(palette)]
        displacement_mm = (res["z_rest"] - res["z_values"]) * 1000
        Fz = res["F"][:, 2]

        fig.add_trace(go.Scatter(x=displacement_mm, y=Fz, mode="lines", name=res["name"],
                                  line=dict(color=color)))

    fig.add_vrect(x0=0, x1=zoom_mm, fillcolor="lightgreen", opacity=0.12, line_width=0,
                  annotation_text="course utile (~1cm)", annotation_position="top left")

    fig.update_xaxes(title_text="Deplacement depuis la position de repos (mm)")
    fig.update_yaxes(title_text="Fz (N)  [signe dependant de sens_courant, voir Config_Coil]")
    fig.update_layout(title=title, height=600, legend_title_text="Configuration")

    return fig
