# Validation manuelle — modificateurs d'altération A_b / A_d (ADR-0026)

Procedure courte et reversible, a mener sur le Clavier flashe (voir
`clavier_usb_midi_manual.md` pour le prerequis USB-MIDI et
`clavier_matrix_scan_manual.md` pour le scan). Aucun commit ni flash n'est fait
par un agent.

Positions : 74 = `A_b` (bemol), 75 = `A_d` (diese). Sur la base layer, la position
57 joue RE4 (MIDI 62).

1. **Modificateur seul.** Appuyer puis relacher 74, puis 75. Aucun evenement
   MIDI ne doit etre emis ; la trace serie montre `behavior=A_b` puis
   `behavior=A_d` (sans champ d'etat).
2. **Bemol (74).** Maintenir 74, appuyer sur la position 57 (RE4, MIDI 62),
   relacher ; la note emise est 61 (RE -> RE bemol, -1 demi-ton). Relacher 74.
3. **Diese (75).** Maintenir 75, appuyer sur la position 57 (RE4, MIDI 62),
   relacher ; la note emise est 63 (RE -> RE diese, +1 demi-ton). Relacher 75.
4. **Retour a la normale.** Repeter le meme geste (modificateur + position 57) :
   la bascule est a deux etats, un second appui sur la meme cible revient a la
   base. Apres `74 + 57` puis `74 + 57`, la position 57 rejoue 62. Idem pour 75.
5. **Note tenue non modifiee.** Maintenir la position 57 (Note On 62), puis
   appuyer sur 74 (ou 75) : aucun nouveau Note On. Relacher la position 57 : le
   Note Off doit etre exactement 62 (la note memorisee), pas 61 ni 63.
6. **Touche noire** (en partant de l'etat de base, aucune bascule en cours).
   Maintenir 75 et jouer la position 48 (DO#4, MIDI 61) : la racine est DO, donc
   la position 56 (DO4, MIDI 60) joue 61 et la position 48 joue 60 (echange
   {DO, DO#}). Maintenir 74 et jouer la position 48 : la racine est RE, donc la
   position 48 joue 62 (echange {RE, RE bemol}).
7. **Retour a la normale global.** Appuyer sur 73 (`UNTRANSPOSE`) : toutes les
   alterations et transpositions memorisees sont effacees ; les notes rejouees
   reprennent le keymap de base.

Resultat attendu : chaque etape est reversible et aucune note tenue n'est
alteree retroactivement.
