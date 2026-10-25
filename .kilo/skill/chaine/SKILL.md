---
name: chaine
description: Procédure d'orchestration de la chaîne automatique doc-amont → codeur → validateur → doc-aval par sous-agents. À charger par l'Architecte ou le Maître dès qu'une décision (ADR acceptée ou fiche de tâche) doit être réalisée.
---

# Chaîne automatique de réalisation

Source unique de la chaîne. Utilisée par `architecte` et `maitre` (même procédure,
seul le modèle diffère). Les sous-agents sont lancés avec l'outil `task` ;
ils ne peuvent **pas** poser de question à l'humain.

## 1. Quand lancer la chaîne

- **Avec ADR** : l'humain a passé l'ADR en `Accepted` (ou dit explicitement
  « go » sur l'ADR). Étapes : doc-amont → codeur → validateur → doc-aval.
- **Sans ADR** (constante, log, retouche de comportement, correction) : une
  **fiche de tâche** suffit. Étapes : codeur → validateur → doc-aval.
  `doc-amont` est sauté.
- Avant tout lancement : l'humain a validé (ADR acceptée ou « go » sur la
  fiche). Une dérogation à un invariant de `AGENTS.md` exige une validation
  humaine explicite, sans exception.

## 2. Fiche de tâche (à écrire avant de lancer)

Dans le prompt du premier sous-agent, pas dans un fichier :

```
OBJECTIF   : <1-2 phrases, comportement attendu pour l'humain>
PÉRIMÈTRE  : <fichiers / module ; clavier | moteur | shared>
ADR        : <chemin ou « aucune »>
NIVEAU MIN : N0 | N1 | N2   (grille : docs/harness/niveaux-validation.md)
HORS PÉRIMÈTRE : <ce qu'on ne touche pas>
TEST HUMAIN ATTENDU : <le plus petit test banc utile, ou « aucun »>
```

## 3. Séquence d'appels

Chaque sous-agent repart d'un contexte vierge : son prompt doit être
**autosuffisant** (fiche + chemins à lire + rapport du précédent), sans recopier
l'historique de la conversation.

1. `doc-amont` (si ADR) : produit la fiche de lot pour le Codeur.
2. `codeur` : implémente, rend la liste des fichiers modifiés.
3. `validateur` : applique le niveau (plancher de la fiche, relevé par la
   grille), rend OK ou ÉCHEC.
4. Si ÉCHEC de code : retour à `codeur` avec le rapport du validateur, puis
   `validateur` à nouveau. **Deux allers-retours au plus.**
5. Si OK : `doc-aval` avec le rapport du validateur.

## 4. Format de retour des sous-agents (obligatoire, 30 lignes max)

Première ligne : `STATUT: OK | ECHEC | BLOQUE`. Taille : 30 lignes au plus (40
pour la fiche de lot de `doc-amont`). Puis : résultat, fichiers
concernés, détails dans un fichier si besoin (pas dans la réponse).
`BLOQUE` = question que le sous-agent ne peut pas trancher (interface
manquante, décision d'architecture, doute de sécurité, information
indisponible) ; le sous-agent écrit la question en une phrase.

## 5. Traitement des retours

- `BLOQUE` : arrête la chaîne, pose la question à l'humain en termes concrets.
  Ne la tranche pas à sa place.
- Interface partagée manquante ou à changer : nouvelle ADR (validation
  humaine), jamais de contournement.
- Doute de sécurité remonté par un sous-agent : **« Question sécurité »** à
  l'humain avant toute poursuite.
- Même échec après 2 tours codeur↔validateur : arrête-toi. Architecte : demande
  à l'humain de lancer `/maitre` avec un dossier d'incident (symptôme, extrait
  de log utile, fichiers, ce qui a été tenté). Maître : applique son budget
  (3 tentatives) puis rapporte à l'humain.
- Une limite matérielle non prévue (RAM/Flash, timing, charge) remonte comme
  question à l'humain, avec les chiffres.

## 6. Rapport final à l'humain

En termes concrets, pas en jargon d'ADR : **ce qui change pour toi** (comportement,
PuTTY, câblage), **ce qui a été validé** (niveau, commandes, résultats), **ce
qu'il te reste à tester sur banc** (liste courte, réversible), **fichiers
modifiés**. Rien n'est commité : l'humain décide. Pour annuler : `git restore`
des fichiers listés.

## 7. Interdits

- Pas d'invention d'interface partagée, pas de modification de
  `shared/protocol/` sans ADR référencée.
- **Aucun bridage électrique** (plafond, coupure, budget thermique) ajouté de sa
  propre initiative : invariant 4 de `AGENTS.md`.
- **Aucune attribution à une version de firmware non à jour**, sauf preuve par
  l'identité du firmware dans le journal de boot (ADR-0017).
- Pas de commit, pas de flash de carte, pas de modification de l'environnement
  de la machine.
