/*
 * term_render.h — Scrollback buffer et rendu DRM
 */
#pragma once

#include "drm_display.h"

/* Accès à l'état du scrollback depuis d'autres modules */
extern int scroll_offset;
extern int sb_count;

/* Initialise le sous-système de rendu (framebuffer + scrollback).
 * Retourne 0 en cas de succès, -1 en cas d'erreur. */
int  term_render_init(void);

/* Libère les ressources du rendu (framebuffer). */
void term_render_shutdown(void);

/* Écrit un caractère dans le scrollback. */
void term_putc(char c);

/* Écrit une chaîne dans le scrollback. */
void term_print(const char *str);

/* Dessine le contenu du terminal à l'écran via DRM. */
void render_term(void);
