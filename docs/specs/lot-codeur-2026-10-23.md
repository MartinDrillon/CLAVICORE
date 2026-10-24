# Lot Codeur du 2026-10-23 — corrections et instrumentation

Point d'entrée unique du **Codeur** pour ce lot. À lire avec les trois notes
d'impact citées ; aucune autre lecture n'est nécessaire. L'ordre est **imposé**.

## Tâche 0 — double saut de ligne (aucune ADR, à faire en premier)

`diag_out_printf` (`firmware/clavier/src/diag_out.c`) écrit la ligne via
`diag_out_line`, qui ajoute `\n`, alors que les formats convertis depuis
`printf("…\n")` en contiennent déjà un : **chaque ligne est suivie d'une ligne
vide** (défaut relevé par le Validateur, visible dans les journaux des 22 et
23 octobre).

Correctif : dans `diag_out_printf`, retirer les `\r`/`\n` de fin du tampon avant
d'appeler `diag_out_line`. Ne pas toucher aux appelants. Vérifier qu'aucun
exemple de `tests/` ne comporte plus de ligne vide.

## Tâche 1 — ADR-0021 : conserver le bitmap sur mesure non concluante

Note : [`impact-adr-0021.md`](./impact-adr-0021.md).

Petit lot, sans trame nouvelle, **le plus urgent** : c'est lui qui empêche un
auto-test avorté de rendre le clavier muet (`keys=0`, incident du 2026-10-23).
Modifie `finish_selftest_session` + un formateur pur + 4 tests hôte.

## Tâche 2 — ADR-0018 : trame `CARD_EVENT` (`0x424`)

Note : [`impact-adr-0018.md`](./impact-adr-0018.md).

Migre `shared/protocol/` **une seule fois** : l'énumération complète des
8 événements (y compris `COMMAND_IGNORED` et `SELFTEST_ABORTED`, émis par les
tâches 3 et 4). Émission Moteur aux transitions du bouton, réception Clavier,
codes `0x38`/`0x39`, attribution UID → position.

## Tâche 3 — ADR-0020 décision 1 : acquittement de profil répété

Note : [`impact-adr-0020.md`](./impact-adr-0020.md) §1.
3 tentatives, 20 ms, arrêt au premier succès, `profile_status_lost` au-delà.
Aucune trame modifiée. Test hôte : double acquittement compté une seule fois.

## Tâche 4 — ADR-0020 décision 2 : compteurs lisibles

Note : [`impact-adr-0020.md`](./impact-adr-0020.md) §2.
`0x403 CARD_STATS_REQUEST` / `0x425 CARD_STATS`, 13 compteurs, console `m stats`,
ligne `[STATS] note_sent=… cartes_ok=… ecart=…`, bascule `n` du journal des notes
(§4). Requêtes sérialisées, une en vol.

## Tâche 5 — ADR-0020 décision 3 : commande ignorée signalée

Note : [`impact-adr-0020.md`](./impact-adr-0020.md) §3. Dépend de la tâche 2.
Émission `COMMAND_IGNORED` pour **toutes** les raisons de refus, limiteur 1/s par
raison. C'est ce qui transforme « les cartes ont gelé » en une ligne nommée.

## Règles communes

- Aucune modification de `CAN_PROTO_VERSION` (trames additives).
- Aucun comportement de solénoïde modifié ; aucune limite, aucun délai, aucun
  mapping touché (invariants 1 à 6).
- `diag_log.c` reste **pur** (aucune horloge, aucun accès matériel) : les
  formateurs ajoutés doivent être testables sur hôte.
- Ne pas mélanger avec **ADR-0019 IMPACT** (profil v2), déjà exécutée : lot
  distinct, même code de profil.
- Un `printf` de diagnostic non horodaté est interdit : passer par `diag_out_*`
  (ADR-0017).

## Vérification avant relais

```powershell
powershell -File tests\host\run_host_tests.ps1
cmake --build "build/clavier"
cmake --build "build/moteur"
```

Rediriger les sorties vers un fichier et n'en extraire que les erreurs et le
résumé (voir `tests/build_clavier_windows.md` §5).

## Critère de sortie du lot

- Les deux cibles compilent, les tests hôte passent.
- `p send` sur un profil identique : un seul acquittement suffit.
- `m stats` répond pour les cartes présentes et affiche la comparaison
  `note_sent` / `commands_ok`.
- Presser une touche pendant qu'une carte refuse produit une ligne
  `[CARD] … event=COMMAND_IGNORED` avec la raison, en moins d'une seconde.
- Un auto-test avorté ne change plus `keys` pour une carte déjà mesurée.
