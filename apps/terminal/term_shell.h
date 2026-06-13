/*
 * term_shell.h — Gestion utilisateur, prompt, commandes, keymaps
 */
#pragma once

#include <sys/types.h>

/* Détecte le premier utilisateur non-root dans /etc/passwd. */
void detect_default_user(void);

/* Reconstruit le prompt après un changement de répertoire. */
void prompt_refresh(void);

/* Gestion de la commande interne "cd". */
void handle_cd(const char *arg);

/* Lance une commande dans un PTY (non-bloquant). */
void run_command(const char *cmd);

/* Détecte le layout clavier depuis /etc/default/keyboard. */
void detect_keyboard_layout(void);

/* fd master du PTY en cours (-1 si aucune commande active). */
extern int   pty_fd;
extern pid_t pty_child;
