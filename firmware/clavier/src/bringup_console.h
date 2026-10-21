// Console de bring-up sur CDC (ADR-0009 §9, livraison B).
//
// Commandes acceptées (une lettre, terminée par Entrée) :
//   t : demander un auto-test (mêmes refus et même chemin que le bouton Moteur)
//   c : basculer le mode diagnostic (IPROPI_RAW + IPROPI_OHM) pour la session
//   d : journaliser la table persistée et la correspondance canal -> touche
//   m : état (validité de table, état/motifs, auto-test en cours, diagnostic)
//   ? : aide
//
// Aucune commande n'écrit ni n'efface la table : seul un auto-test explicitement
// demandé écrit la flash (invariant 1, ADR-0009 §1).
#ifndef BRINGUP_CONSOLE_H
#define BRINGUP_CONSOLE_H

void bringup_console_init(void);
// Consommation non bloquante, à appeler à chaque tour de boucle principale.
void bringup_console_poll(void);

#endif  // BRINGUP_CONSOLE_H
