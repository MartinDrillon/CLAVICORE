# Test manuel — réglage à chaud du profil par la console CDC (ADR-0015, D1)

Vérifie la console PuTTY du Clavier : réception des commandes, modification du
profil d'activation sans reflash, propagation aux cartes Moteur, compte rendu.

Couvert par les **tests hôte** (`tests\host\run_host_tests.ps1`) : table des champs,
analyse, atomicité, refus, console (`bringup_console.c` avec doublures). Non
couvrable sans matériel : tout ce qui suit.

## Pré-requis

- Clavier flashé (`build/clavier/clavier.uf2`, `CLAVIER_CDC_CONSOLE=ON`, défaut).
- **Aucun reflash des cartes Moteur** : le firmware Moteur n'est pas modifié (D1).
  Les cartes doivent toutefois avoir un firmware ADR-0013 (gestionnaire `PROFILE_*`).
- PuTTY sur le port COM du CDC du Clavier. Terminal → **Local echo : Force on**,
  **Local line editing : Force on** (le Clavier ne renvoie pas les caractères).
- Au moins une carte Moteur alimentée, scan UID de boot terminé (journal `[CARD]`).

## 0. L'entrée CDC répond (régression corrigée)

1. Taper `?` + Entrée → ligne `[CONSOLE] t=auto-test ... p=profil (p ?)`.
   Si rien ne s'affiche : l'entrée CDC ne fonctionne pas (voir ADR-0015, constat 1).
2. Taper `m` + Entrée → `[CONSOLE] m -> etat`.

## 1. Lecture

1. `p show` → 24 lignes `.champ = Vu,` et le tag. 2. `p fields` → champs réglables
et verrouillés (raison). 3. `p diff` → « aucune difference ».

## 2. Réglage et propagation

### 2.1 Où vit un réglage (à lire avant d'essayer)

| Où | Ce qui s'y passe | Durée de vie |
|---|---|---|
| **RAM du Clavier** (« profil actif ») | `p set` y écrit, mais **seulement après** que toutes les cartes attendues ont acquitté | jusqu'au reboot du Clavier (retour au fichier compilé) |
| **Cartes Moteur** | reçoivent le profil par CAN, l'appliquent tout de suite **et l'écrivent en flash** (limite D1, ADR-0015) | survit à leur mise hors tension |
| **`solenoid_profile_config.h`** | **jamais modifié par le firmware** : le Clavier ne peut pas écrire dans un fichier source | permanent, c'est vous qui le mettez à jour |

Conséquences :

- **La propagation Clavier → cartes est automatique** : un `p set` valide est
  diffusé et appliqué sans autre action (pas de reflash, pas d'auto-test).
- **La rétropropagation vers le fichier n'est PAS automatique.** C'est une
  recopie manuelle, en 3 temps :
  1. `p diff` affiche les seules lignes qui diffèrent du fichier compilé, déjà au
     format du `.h` (`.pull_duration_us = 12000u,`) ;
  2. vous remplacez les lignes correspondantes dans `solenoid_profile_config.h`
     (même nom de champ, nouvelle valeur) ;
  3. vous reconstruisez et reflashez le Clavier (`clavier.uf2`). Au boot, profil
     actif = fichier = ce que les cartes ont déjà : aucune divergence signalée si
     les valeurs sont identiques.
- Les réglages **s'accumulent** : chaque `p set` part du profil actif, pas du
  fichier. Un champ non cité garde sa valeur. `p reset` revient au fichier.
- Un **auto-test** ou `p reset` renvoie le profil du **fichier** et écrase vos
  réglages (le journal le signale). Faites `p diff` et notez/recopiez avant.

### 2.2 Comment envoyer une commande quand on ne voit pas ce qu'on tape

Le Clavier **ne renvoie jamais les caractères saisis** (il n'y a pas d'écho côté
firmware). Sans réglage PuTTY, la ligne est donc invisible. Réglages à faire une
fois, puis à enregistrer dans la session PuTTY (Session → Save) :

- Terminal → **Local echo : Force on** : PuTTY affiche ce que vous tapez.
- Terminal → **Local line editing : Force on** : PuTTY garde la ligne chez lui,
  vous laisse corriger (Retour arrière, flèches) et **n'envoie qu'à Entrée**.
  Rien n'est donc interprété par le Clavier avant que vous ayez relu la ligne.
- Terminal → *Implicit LF in every CR* : indifférent (le Clavier accepte CR, LF
  et CR+LF, et ignore les lignes vides).
- Connection → Serial : le débit n'a aucune importance (USB CDC) ; seul le
  numéro de port COM compte. DTR doit rester actif (défaut PuTTY), sinon le
  journal n'est pas envoyé.

**Méthode la plus pratique : préparer la ligne dans un éditeur de texte, puis la
coller** (clic droit dans la fenêtre PuTTY = coller, + Entrée si le texte collé
n'en contient pas). Avantages : la ligne est relue avant envoi, pas de faute de
frappe, et un historique de vos essais reste dans le fichier. Deux formes
équivalentes :

- `p set pull_duration_us 12000 hold_duty_max_pct 40 hold_duty_pct 30`
- `.pull_duration_us = 12000u, .hold_duty_max_pct = 40u, .hold_duty_pct = 30u,`
  (même format que le `.h` : on peut donc copier une ligne du fichier, changer la
  valeur, et la coller ; un commentaire `// ...` en fin de ligne est ignoré).

Règles pratiques :

- **Une seule commande par envoi.** Pour changer plusieurs champs, les mettre sur
  la **même ligne** : elle est appliquée en bloc (tout ou rien) et diffusée une
  seule fois. Coller plusieurs lignes d'un coup est déconseillé : la première
  lance la diffusion, les suivantes sont refusées tant qu'elle dure
  (`operation CAN en cours`) et sont perdues.
- **Mettre ensemble les champs liés** : `hold_duty_pct` ne peut pas dépasser
  `hold_duty_max_pct`, ni `pull_duration_us` dépasser `pull_max_us`. Pour monter
  l'un, envoyer l'autre dans la même ligne, sinon la ligne est refusée.
- **Ne rien envoyer en jouant** : le réglage est refusé si une touche est tenue
  ou relâchée depuis moins de 0,5 s, et une carte dont un canal n'est pas au repos
  répond `BUSY`.
- **Le retour fait foi, pas la saisie.** Après Entrée, vérifier les lignes
  `[PROFILE]   champ ancien -> nouveau` (ce qui change vraiment) puis
  `reglage: OK acquittements=N/N`. Une faute de frappe sur un nom de champ donne
  `champ inconnu`, sur une valeur `valeur invalide`, et **rien n'est envoyé**.
  En cas de doute, `p get <champ>` ou `p show`.

### 2.3 Essais

1. `p set pull_duration_us 12000` → liste `pull_duration_us 15000 -> 12000`, puis
   `[PROFILE] sync debut but=reglage ... attendus=N`, puis
   `[PROFILE] reglage: OK acquittements=N/N tag=0x...`.
   `attendus=N` doit être le nombre de cartes en ligne ; `attendus=0` donne un
   avertissement « application NON confirmee » (table invalide ou aucune carte en
   ligne au dernier scan).
2. Jouer une note : PULL à 12 ms (oscilloscope sur `PWM_CHANNEL`, durée de la
   phase pleine).
3. Coller `.pull_duty_pct = 70u, .hold_duty_max_pct = 40u, .hold_duty_pct = 30u,`
   → **une seule** diffusion ; trois lignes de changement.
4. `p diff` → exactement les lignes à recopier dans `solenoid_profile_config.h`
   (ici les 4 champs modifiés, `pull_duration_us` compris).
5. Recopier ces lignes dans le fichier, reconstruire, reflasher le Clavier :
   `p diff` → « aucune difference » et pas de `PROFILE_DIVERGENT` au boot.

## 3. Refus (rien n'est envoyé, aucune trame CAN)

- `p set hold_duty_pct 90` (au-dessus de `hold_duty_max_pct`) → « profil incoherent ».
- `p set pwm_freq_hz 25000` → « non reglable a chaud » + raison.
- `p set pull_duty_pct 300` → hors type ; `p set nimporte 1` → champ inconnu.
- Maintenir une touche puis `p set ...` → `reglage refuse: jeu en cours`.
- Lancer un auto-test puis `p set ...` avant la fin → `operation CAN en cours`.

## 4. Échec d'acquittement (diagnostic)

Couper une carte **après** le scan UID, puis `p set min_rest_ms 12` :
`[STATUS] PROFILE_SYNC_FAILED`, `[PROFILE] sync echec ... acquittements=k/N`,
une ligne `sync carte pos=… MANQUANT` par carte muette, et
`reglage: ECHEC`. Le profil actif ne change pas ; `p send` resynchronise.

## 5. Persistance côté Moteur (limite D1)

1. `p set hold_duty_pct 30` (après `p set hold_duty_max_pct 40`) → OK.
2. Couper/remettre **les cartes seulement** : elles gardent 30 % (NVM), pas de
   `PROFILE_DIVERGENT` (le Clavier suit le profil actif).
3. Redémarrer le **Clavier** : `[STATUS] PROFILE_DIVERGENT` **attendu**
   (Clavier au fichier compilé, cartes au profil réglé). `p reset` ou un auto-test
   resynchronise.

## 6. Auto-test après réglage

`p set pull_duration_us 12000` puis auto-test (bouton Moteur 3 s ou `t`) : la ligne
`auto-test: le profil du fichier compile remplace le profil regle a chaud` apparaît ;
`p diff` indique ensuite « aucune difference ».

## 7. À relever si un échec persiste

Sortie complète de `p set ...` jusqu'à `reglage: ...`, lignes `[PROFILE] refus` et
`sync carte`, plus `m`. Un `acquittements=0/N` avec carte « MANQUANT » alors que la
force a changé au banc = acquittement perdu côté Moteur (voir ADR-0015 NEG-002) ;
D2 (transfert volatil) supprimerait l'écriture flash de ce chemin.
