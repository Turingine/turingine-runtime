#define _DEFAULT_SOURCE

#include "term_render.h"
#include "terminal.h"
#include "font8x16.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ══════════════════════════════════════════════════════════════
 * Scrollback buffer — état interne
 * ══════════════════════════════════════════════════════════════ */

static char scrollback[SCROLLBACK_LINES][COLS];
static int  sb_write = 0;     /* Prochaine ligne d'écriture (index linéaire) */
int  sb_count = 0;            /* Nombre total de lignes écrites              */
int  scroll_offset = 0;       /* 0 = live, >0 = remonté de N lignes          */

static int cursor_x = 0;
static int cursor_y = 0;      /* Position relative dans la fenêtre visible   */

static struct drm_display drm;
static uint32_t scr_w, scr_h, stride;
static uint32_t *fb = NULL;

/* ══════════════════════════════════════════════════════════════
 * Initialisation / Shutdown
 * ══════════════════════════════════════════════════════════════ */

int term_render_init(void) {
  if (drm_display_init(&drm, &scr_w, &scr_h, &stride) != 0) {
    fprintf(stderr, "Terminal: Erreur init DRM\n");
    return -1;
  }

  fb = calloc(stride * scr_h, sizeof(uint32_t));
  if (!fb) {
    drm_display_shutdown(&drm);
    return -1;
  }

  memset(scrollback, 0, sizeof(scrollback));
  return 0;
}

void term_render_shutdown(void) {
  free(fb);
  fb = NULL;
  drm_display_shutdown(&drm);
}

/* ══════════════════════════════════════════════════════════════
 * Fonctions de texte
 * ══════════════════════════════════════════════════════════════ */

/* Retourne un pointeur vers la ligne du scrollback correspondant à
 * la ligne visible y (0 = haut de l'écran visible). */
static char *sb_line(int visible_y) {
  int idx = sb_write - ROWS + visible_y;
  if (idx < 0) idx = 0;
  if (idx >= SCROLLBACK_LINES) idx = SCROLLBACK_LINES - 1;
  return scrollback[idx];
}

static void term_scroll(void) {
  /* Si le buffer est plein, décaler tout d'une ligne vers le haut */
  if (sb_write >= SCROLLBACK_LINES) {
    memmove(scrollback[0], scrollback[1],
            (SCROLLBACK_LINES - 1) * COLS);
    sb_write = SCROLLBACK_LINES - 1;
  }
  /* Nettoyer la nouvelle ligne */
  memset(scrollback[sb_write], 0, COLS);
  cursor_y = ROWS - 1;
  cursor_x = 0;
  if (sb_count < SCROLLBACK_LINES) sb_count = sb_write + 1;
}

void term_putc(char c) {
  if (c == '\n') {
    cursor_x = 0;
    cursor_y += 1;
    /* Avancer sb_write quand on passe à une nouvelle ligne */
    if (cursor_y >= ROWS) {
      sb_write += 1;
      term_scroll();
    } else {
      /* S'assurer que la prochaine ligne est initialisée */
      int idx = sb_write - ROWS + 1 + cursor_y;
      if (idx >= 0 && idx < SCROLLBACK_LINES) {
        if (idx >= sb_write) {
          sb_write = idx + 1;
          if (sb_count < sb_write) sb_count = sb_write;
        }
        memset(scrollback[idx], 0, COLS);
      }
    }
  } else if (c == '\r') {
    cursor_x = 0;
  } else if (c == '\b') {
    if (cursor_x > 0) {
      cursor_x -= 1;
      char *line = sb_line(cursor_y);
      line[cursor_x] = '\0';
    } else if (cursor_y > 0) {
      cursor_y -= 1;
      cursor_x = COLS - 1;
      char *line = sb_line(cursor_y);
      line[cursor_x] = '\0';
    }
  } else if (c >= 32 && c <= 126) {
    if (cursor_x >= COLS - 1) {
      cursor_x = 0;
      cursor_y += 1;
      if (cursor_y >= ROWS) {
        sb_write += 1;
        term_scroll();
      }
    }
    char *line = sb_line(cursor_y);
    line[cursor_x] = c;
    cursor_x += 1;
  }
  if (cursor_y >= ROWS) {
    sb_write += 1;
    term_scroll();
  }
}

void term_print(const char *str) {
  while (*str) {
    term_putc(*str);
    str += 1;
  }
}

/* ══════════════════════════════════════════════════════════════
 * Rendu DRM
 * ══════════════════════════════════════════════════════════════ */

/* Dessine un caractère de la police 8x16 à la position pixel (px, py) */
static void render_glyph(char c, int px_base, int py_base, uint32_t color) {
  const uint8_t *glyph = &font8x16[(int)c * 16];
  for (int r = 0; r < 16; r += 1) {
    uint8_t bits = glyph[r];
    for (int cb = 0; cb < 8; cb += 1) {
      if (bits & (0x80 >> cb)) {
        int px = px_base + cb;
        int py = py_base + r;
        if (px < (int)scr_w && py < (int)scr_h) {
          fb[py * stride + px] = color;
        }
      }
    }
  }
}

void render_term(void) {
  /* Effacer l'écran en noir */
  for (uint32_t i = 0; i < stride * scr_h; i += 1) {
    fb[i] = 0xFF000000;
  }

  /* Déterminer les lignes à afficher en fonction du scroll_offset */
  int base_line = sb_write - ROWS;
  if (base_line < 0) base_line = 0;
  int view_start = base_line - scroll_offset;
  if (view_start < 0) view_start = 0;

  /* Dessiner le texte */
  for (int y = 0; y < ROWS; y += 1) {
    int sb_idx = view_start + y;
    if (sb_idx < 0 || sb_idx >= SCROLLBACK_LINES) continue;
    for (int x = 0; x < COLS; x += 1) {
      char c = scrollback[sb_idx][x];
      if (c == 0) break;
      render_glyph(c, x * 8, y * 16, 0xFFFFFFFF);
    }
  }

  /* Dessiner le curseur (seulement en mode live) */
  if (scroll_offset == 0) {
    int cx = cursor_x * 8;
    int cy = cursor_y * 16;
    for (int r = 0; r < 16; r += 1) {
      for (int cb = 0; cb < 8; cb += 1) {
        int px = cx + cb;
        int py = cy + r;
        if (px < (int)scr_w && py < (int)scr_h) {
          fb[py * stride + px] ^= 0xFFFFFFFF;
        }
      }
    }
  }

  /* Indicateur de scroll (en haut à droite, en jaune) */
  if (scroll_offset > 0) {
    char indicator[32];
    snprintf(indicator, sizeof(indicator), "[+%d]", scroll_offset);
    int len = strlen(indicator);
    int start_x = (COLS - len - 1) * 8;
    for (int i = 0; i < len; i += 1) {
      render_glyph(indicator[i], start_x + i * 8, 0, 0xFFFFCC00);
    }
  }

  drm_display_blit_argb32(&drm, fb, stride);
  drm_display_present(&drm);
}
