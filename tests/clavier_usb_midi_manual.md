# Validation manuelle USB-MIDI Clavicore

## Prerequis

- Compiler et flasher `firmware/clavier` sur un Pico 2.
- Relier le port USB natif du Pico a un ordinateur.
- Ouvrir un moniteur MIDI, par exemple MIDIView sous Windows ou `aseqdump` sous Linux.

## Procedure

1. Verifier que le peripherique apparait comme `Clavicore Keyboard`.
2. Appuyer puis relacher une touche musicale. Le moniteur doit afficher un Note On puis un Note Off, canal 1, pour la meme note; la velocite du Note On est 100.
3. Appuyer sur une touche de transposition, ou sur un modificateur d'alteration seul (74 = A_b, 75 = A_d). Aucun evenement MIDI ne doit etre emis : le modificateur n'agit que maintenu, a l'appui d'une touche note.
4. Maintenir un modificateur d'alteration (74 = A_b, 75 = A_d), puis appuyer sur une touche note. Verifier que l'alteration (A_b : -1 demi-ton, A_d : +1 demi-ton) est appliquee avant la transposition et que la note recue est bornee entre 0 et 127. Voir `clavier_alteration_manual.md`.
5. Maintenir une touche note, puis appuyer sur un modificateur d'alteration ou une touche de transposition. La note tenue ne doit pas etre modifiee ; a son relachement, son Note Off doit utiliser exactement la note du Note On initial.
6. Demarrer le clavier sans hote USB, puis appuyer sur plusieurs touches. Le scan doit rester reactif; les sorties serie indiquent que MIDI USB est indisponible, sans blocage.