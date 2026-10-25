# Niveaux de validation (N0 / N1 / N2)

Source unique de la profondeur de validation. Lue par l'Architecte et le Maître
(qui fixent un plancher dans la fiche de tâche) et par le Validateur (qui
recalcule le niveau depuis le diff).

## Grille

| Niveau | Déclencheur (contenu du diff) | Ce que fait le Validateur |
|---|---|---|
| **N0** | Constantes, textes de log, commentaires, noms, doc embarquée. Aucune logique modifiée. | Build de la **carte concernée** + `tests/host/run_host_tests.ps1`. |
| **N1** | Logique firmware **sans** `shared/protocol/`, sans pilote solénoïde, sans CAN, sans ISR/PIO/DMA, sans persistance flash. | Build **Clavier et Moteur** + `tests/host/run_host_tests.ps1` complet. |
| **N2** | Tout diff touchant `shared/protocol/`, le pilote solénoïde (activation, profil, PWM, IPROPI), le CAN (`can2040`, trames), une ISR/PIO/DMA, la persistance flash, ou le partage de données entre Core 0 et Core 1. | N1 + résumé de taille des régions mémoire (RAM/Flash) + **liste courte de tests banc** à faire par l'humain (quoi, où, résultat attendu). |

## Règles

1. L'Architecte (ou le Maître) fixe un **plancher** dans la fiche de tâche.
2. Le Validateur **recalcule** le niveau depuis `git diff --stat` / `git diff`
   avec cette grille et retient **le plus élevé** des deux. Il peut monter,
   jamais descendre.
3. En cas d'hésitation entre deux niveaux, prendre le plus élevé.
4. Le niveau réellement appliqué est écrit sur la première ligne du rapport.
5. Les tests banc de N2 sont **minimaux, lisibles par un humain et
   réversibles** (un flag à retirer, aucun état flash modifié durablement).
6. Le Validateur ne conclut jamais « à mettre à jour vos cartes » sans preuve :
   l'identité du firmware au boot figure dans le journal CDC (ADR-0017), c'est
   la seule preuve admise.
