/*
 * terminal.h — Configuration partagée du terminal Turingine
 *
 * Ce header est inclus par tous les modules du terminal.
 * Il définit les constantes, les variables globales partagées
 * et les déclarations forward des fonctions inter-modules.
 */
#pragma once

#include <stdint.h>

/* ══════════════════════════════════════════════════════════════
 * Constantes de configuration
 * ══════════════════════════════════════════════════════════════ */

#define COLS 60                /* Nombre de colonnes (caractères par ligne) */
#define ROWS 20                /* Nombre de lignes visibles                */
#define SCROLLBACK_LINES 1000  /* Nombre total de lignes conservées        */
#define HISTORY_MAX 500        /* Nombre max d'entrées dans l'historique   */
#define COMPLETE_THRESHOLD 30  /* Au-delà, demander confirmation           */
#define INPUT_MAX 128          /* Taille max de la ligne de saisie         */

/* ══════════════════════════════════════════════════════════════
 * Variables globales partagées entre modules
 * ══════════════════════════════════════════════════════════════ */

/* État de l'utilisateur et du shell (définis dans term_shell.c) */
extern char user_name[64];
extern char user_home[128];
extern char user_cwd[256];
extern char prompt_str[128];

/* Pointeurs vers la keymap active (définis dans term_shell.c) */
extern const char *active_keymap;
extern const char *active_keymap_shift;

/* Contrôle de la boucle principale (défini dans main.c) */
extern volatile int keep_running;
