// Mode REC côté Clavier (ADR-0032 §7, §8, §11) — logique pure, testable sur
// hôte : aucun accès SDK, aucune horloge, aucun envoi CAN ou MIDI.
//
// Le Clavier est maître du mode : il choisit la carte et le canal par la table
// de commissioning (invariant 3), les cartes Moteur restent anonymes
// (invariant 2). Une impulsion IPROPI ne porte pas le sens du mouvement : le
// Clavier alterne appui/relâchement par (UID, canal), donc par touche.
//
// Ce module ne décide d'aucune limite électrique ; il ne fait que convertir des
// impulsions en notes MIDI et fusionner deux sources (matrice et écoute) par
// note, avec un compteur.
#ifndef CLAVIER_REC_MODE_H
#define CLAVIER_REC_MODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "commissioning_table.h"
#include "listen_params.h"

typedef enum {
    REC_MODE_OFF = 0,
    REC_MODE_SOLE = 1,  // solénoïdes muets, MIDI de la matrice conservé
    REC_MODE_MUTE = 2,  // solénoïdes muets et MIDI de la matrice muet
} rec_mode_t;

// Refus d'entrée, nommés : jamais de mode REC silencieusement partiel.
typedef enum {
    REC_OK = 0,
    REC_NO_TABLE,        // table de commissioning absente/invalide
    REC_SELFTEST,        // auto-test en cours
    REC_PROFILE_SYNC,    // synchronisation de profil en cours
    REC_MAINTENANCE,     // maintenance active
} rec_refusal_t;

// Liste bornée des Note Off à émettre. 128 notes MIDI au maximum : la liste ne
// peut pas déborder si chaque note n'y est écrite qu'une fois.
#define REC_OFF_LIST_MAX 128u
typedef struct {
    uint8_t count;
    uint8_t note[REC_OFF_LIST_MAX];
} rec_off_list_t;

typedef struct {
    bool emit_on;    // Note On à émettre
    bool emit_off;   // Note Off à émettre
    uint8_t midi_note;
    uint8_t velocity;
} rec_action_t;

// Pré-déclenchement de LISTEN_TRACE : 25 % des 4096 échantillons (ADR-0032 §3).
#define REC_TRACE_PRE_TRIGGER 1024u

typedef struct {
    // Compteur par note des positions MATRICE qui la tiennent. Sature à 255 :
    // un compteur ne doit jamais mentir par débordement.
    uint8_t matrix_ref[128];
    // Note captée active (0/1), une seule impulsion à la fois par touche.
    uint8_t captured_ref[128];
    // Alternance par touche 1..49 : 0 = prochain appui, 1 = prochain
    // relâchement. L'index 0 n'est jamais utilisé.
    uint8_t key_down[COMMISSIONING_KEY_COUNT + 1u];
    bool noise_enabled;  // `e bruit` : requêtes de voisins après chaque timing
    // Résumé de trace, alimenté par les trames LISTEN_TRACE.
    uint16_t trace_min;   // minimum du pré-déclenchement
    uint16_t trace_max;   // maximum du pré-déclenchement
    uint16_t trace_first; // premier échantillon global
    uint16_t trace_last;  // dernier échantillon reçu
    uint16_t trace_pic;   // maximum global
    uint32_t trace_count;
    uint32_t trace_peak_index;  // index global du maximum
    // Paramètres d'écoute utilisés pour la vélocité. Jamais possédés : le
    // propriétaire est `listen_params` du Clavier (ou le test hôte).
    const listen_params_t *params;
} rec_state_t;

typedef struct {
    uint16_t base;      // minimum du pré-déclenchement
    uint16_t pic;       // maximum global
    uint16_t delta;     // pic - base
    uint16_t bruit_cc;  // max - min du pré-déclenchement
    uint32_t t_pic_ms;  // index du maximum * trace_period_us / 1000
    uint32_t count;     // nombre d'échantillons reçus (4096 attendu)
} rec_trace_summary_t;

const char *rec_mode_name(rec_mode_t mode);
const char *rec_refusal_name(rec_refusal_t refusal);

// Le Clavier doit fournir un jeu de paramètres valide avant d'appeler la
// capture (vélocité). Pointeur non possédé ; NULL retombe sur une vélocité
// fixe de secours.
void rec_mode_set_params(rec_state_t *state, const listen_params_t *params);

// Entrée en REC : vérifie les refus dans l'ordre table, auto-test, synchro
// profil, maintenance. En cas de succès, remplit `offs` des notes matrice
// actives UNIQUEMENT en REC_MUTE, puis remet leur compteur à zéro. Dans tous
// les cas, `captured_ref` et l'alternance repartent à zéro. Les Note Off de
// `offs` sont émis par l'appelant (coupure des solénoïdes).
rec_refusal_t rec_mode_enter(rec_state_t *state, rec_mode_t mode,
                             bool table_valid, bool selftest_active,
                             bool profile_sync_active, bool maintenance,
                             rec_off_list_t *offs);

// Sortie : `offs` reçoit toutes les notes captées encore actives (jamais les
// notes matrice, qui ne sont pas réémises). Le compteur matrice est vidé : hors
// REC il n'est plus consulté, et la prochaine entrée le réamorce depuis l'état
// physique courant.
void rec_mode_exit(rec_state_t *state, rec_off_list_t *offs);

// `e raz` : alternance seule, aucun Note Off.
void rec_mode_raz(rec_state_t *state);

// Réamorce le compteur matrice d'une note tenue au moment de l'entrée en REC
// (état physique lu par l'appelant avant rec_mode_enter).
void rec_mode_seed_matrix_note(rec_state_t *state, uint8_t midi_note);

// Événement matrice : met à jour le compteur par note et retourne l'action MIDI
// à émettre. En REC_MUTE, aucune action n'est retournée (matrice muette) mais
// les compteurs restent justes. En REC_SOLE et OFF, l'émission est retournée ;
// l'appelant supprime la trame CAN 0x100 en REC.
rec_action_t rec_mode_matrix_event(rec_state_t *state, rec_mode_t mode,
                                   uint8_t midi_note, bool pressed);

// Impulsion captée sur la touche `key` (1..49) : bascule l'alternance, applique
// la fusion par compteur et retourne l'action MIDI (vélocité §11).
rec_action_t rec_mode_captured_event(rec_state_t *state, rec_mode_t mode,
                                     uint8_t key, uint16_t delta);

// (UID, canal) -> touche, par la table de commissioning (toutes cartes).
bool rec_mode_lookup_uid_channel(const commissioning_runtime_t *runtime,
                                 uint16_t uid_tag, uint8_t channel, uint8_t *key);

// Voisin `key + delta` (delta = ±1) dans la table : false si hors 1..49 ou non
// attribué. `out` n'est écrit qu'en cas de succès.
bool rec_mode_lookup_neighbor(const commissioning_runtime_t *runtime, uint8_t key,
                              int8_t delta, commissioning_note_target_t *out);

// Vélocité MIDI §11 : `vel_fixed` si vel_min_delta == 0, sinon linéaire entre
// vel_min_delta (-> 1) et vel_max_delta (-> 127), bornée 1..127.
uint8_t rec_mode_velocity(const listen_params_t *params, uint16_t delta);

// "MI3", "RE#3", "DO2", "52" -> note MIDI. Refuse hors 0..127 ou texte inconnu.
bool rec_mode_parse_note(const char *text, uint8_t *midi_note);
// Nom français compact ("DO2", "RE#3", "MI3"), sans numéro MIDI.
bool rec_mode_note_name(uint8_t midi_note, char *out, size_t capacity);

// Touche de console (`e trace`/`e stream`) : nom ou numéro MIDI, puis table.
bool rec_mode_resolve_touch(const commissioning_runtime_t *runtime, const char *text,
                            uint8_t *midi_note, commissioning_note_target_t *out);

// `e bruit` : bascule ; refusé si detail == 0 (aucun changement).
bool rec_mode_noise_toggle(rec_state_t *state, bool detail_enabled, bool *enabled_out);

// Résumé de trace : valide seulement quand les 4096 échantillons sont arrivés.
void rec_mode_trace_reset(rec_state_t *state);
void rec_mode_trace_feed(rec_state_t *state, const can_listen_trace_t *frame);
bool rec_mode_trace_summary(const rec_state_t *state, uint32_t trace_period_us,
                            rec_trace_summary_t *out);

// Console `e` : accès aux 18 paramètres par nom (délèguent à listen_params).
bool rec_mode_param_get(const listen_params_t *params, const char *name, uint32_t *value);
bool rec_mode_param_set(listen_params_t *params, const char *name, uint32_t value);
// Valide le jeu complet ; *bad_name reçoit le premier champ fautif (NULL si OK).
listen_params_result_t rec_mode_params_validate(const listen_params_t *params,
                                                const char **bad_name);
// Écrit dans `out` les lignes `.champ = valeur,` différentes de k_listen_config
// (une par champ, terminées par '\n', buffer toujours terminé par '\0').
// Retourne le nombre de lignes écrites (0 possible).
size_t rec_mode_params_diff(const listen_params_t *params, char *out, size_t capacity);

#endif  // CLAVIER_REC_MODE_H
