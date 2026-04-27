/*
 * term_history.h — Historique de commandes (~/.bash_history)
 */
#pragma once

/* Accesseurs d'état pour la boucle principale */
extern int hist_count;
extern int hist_cursor;
extern int hist_browsing;

/* Charge l'historique depuis ~/.bash_history. */
void history_load(void);

/* Ajoute une commande à l'historique (et l'écrit dans le fichier). */
void history_append(const char *cmd);

/* Navigue dans l'historique.
 * direction: -1 = plus ancien (haut), +1 = plus récent (bas). */
void history_navigate(int direction, char *input_cmd, int *input_pos);
