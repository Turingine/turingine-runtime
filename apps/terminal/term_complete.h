/*
 * term_complete.h — Auto-complétion par Tab
 */
#pragma once

/* Accesseurs d'état pour la boucle principale */
extern int last_tab_was_tab;
extern int awaiting_confirm;

/* Lance la complétion sur la saisie courante.
 * Modifie input_cmd et input_pos en place. */
void complete_input(char *input_cmd, int *input_pos);

/* Affiche toutes les complétions (après confirmation Y). */
void show_completions(char *input_cmd, int *input_pos);
