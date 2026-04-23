/*
 * Homescreen Turingine — Rendu DRM direct + entrées libevdev.
 * Plus aucune dépendance X11 : le stack Xvfb/Openbox/display n'est
 * lancé qu'à la demande quand l'utilisateur choisit une app STD.
 *
 * Compiler avec -DVIRTUAL_DISPLAY pour le mode test PC (fenêtre Wayland
 * via la lib virtuelle Zig, pas de libevdev).
 */

#define _DEFAULT_SOURCE /* usleep() en mode C99 strict */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "drm_display.h"
#include "evdev_input.h"
#include "font8x16.h"

/* ── Constantes d'affichage ── */
#define BTN_WIDTH 400
#define BTN_HEIGHT 80
#define BTN_SPACING 20
#define MAX_BUTTONS 10
#define FONT_SCALE 2 /* Facteur d'agrandissement de la police 8x16 */

/* ── Couleurs ── */
#define COL_BG       0xFF111111
#define COL_TITLE    0xFF888888
#define COL_OUTLINE  0xFFFFFFFF
#define COL_TEXT     0xFFFFFFFF
#define COL_CURSOR   0xFFFF0000
#define COL_CENTER   0xFFFFFFFF
#define COL_HOVER    0xFF333333

static volatile int keep_running = 1;

/* ── Structure bouton ── */
struct Button {
  int is_native;
  uint32_t color;
  char label[64];
  char cmd[128];
};

static struct Button buttons[MAX_BUTTONS];
static int btn_count = 0;

/* ══════════════════════════════════════════════════════════════
 * Utilitaires chaînes & config (identique à l'ancien code)
 * ══════════════════════════════════════════════════════════════ */

static void trim_spaces(char *str) {
  int len = strlen(str);
  while (len > 0 &&
         (str[len - 1] == ' ' || str[len - 1] == '\r' || str[len - 1] == '\n'))
    len--;
  str[len] = '\0';
  char *start = str;
  while (*start == ' ')
    start++;
  if (start != str)
    memmove(str, start, strlen(start) + 1);
}

static void load_config(const char *filename) {
  FILE *f = fopen(filename, "r");
  if (!f)
    return;
  char line[256];
  while (fgets(line, sizeof(line), f)) {
    trim_spaces(line);
    if (line[0] == '#' || line[0] == '\0')
      continue;

    char type_str[32], color_str[32], lbl[128], cmd[128];
    if (sscanf(line, "%31[^|]|%31[^|]|%127[^|]|%127[^\n]", type_str,
               color_str, lbl, cmd) == 4) {
      trim_spaces(type_str);
      trim_spaces(color_str);
      trim_spaces(lbl);
      trim_spaces(cmd);

      buttons[btn_count].is_native = (strstr(type_str, "NATIVE") != NULL);
      buttons[btn_count].color = (uint32_t)strtol(color_str, NULL, 16);
      strncpy(buttons[btn_count].label, lbl, 63);
      strncpy(buttons[btn_count].cmd, cmd, 127);

      btn_count++;
      if (btn_count >= MAX_BUTTONS)
        break;
    }
  }
  fclose(f);
}

/* ══════════════════════════════════════════════════════════════
 * Rendu logiciel : dessin de primitives dans le framebuffer
 * ══════════════════════════════════════════════════════════════ */

/* Éclaircit une couleur ARGB sans overflow entre canaux */
static uint32_t lighten_color(uint32_t color, uint32_t amount) {
  uint32_t a = (color >> 24) & 0xFF;
  uint32_t r = (color >> 16) & 0xFF;
  uint32_t g = (color >>  8) & 0xFF;
  uint32_t b =  color        & 0xFF;
  r = (r + amount > 255) ? 255 : r + amount;
  g = (g + amount > 255) ? 255 : g + amount;
  b = (b + amount > 255) ? 255 : b + amount;
  return (a << 24) | (r << 16) | (g << 8) | b;
}

static void fill_rect(uint32_t *fb, uint32_t stride, uint32_t scr_w,
                      uint32_t scr_h, int x, int y, int w, int h,
                      uint32_t color) {
  for (int row = y; row < y + h; row++) {
    if (row < 0 || row >= (int)scr_h)
      continue;
    for (int col = x; col < x + w; col++) {
      if (col < 0 || col >= (int)scr_w)
        continue;
      fb[row * stride + col] = color;
    }
  }
}

static void draw_rect(uint32_t *fb, uint32_t stride, uint32_t scr_w,
                      uint32_t scr_h, int x, int y, int w, int h,
                      uint32_t color) {
  for (int col = x; col < x + w; col++) {
    if (col >= 0 && col < (int)scr_w) {
      if (y >= 0 && y < (int)scr_h)
        fb[y * stride + col] = color;
      if (y + h - 1 >= 0 && y + h - 1 < (int)scr_h)
        fb[(y + h - 1) * stride + col] = color;
    }
  }
  for (int row = y; row < y + h; row++) {
    if (row >= 0 && row < (int)scr_h) {
      if (x >= 0 && x < (int)scr_w)
        fb[row * stride + x] = color;
      if (x + w - 1 >= 0 && x + w - 1 < (int)scr_w)
        fb[row * stride + x + w - 1] = color;
    }
  }
}

/* Dessine un caractère 8x16 agrandi par FONT_SCALE */
static void draw_char(uint32_t *fb, uint32_t stride, uint32_t scr_w,
                      uint32_t scr_h, int x, int y, unsigned char c,
                      uint32_t color) {
  if (c > 127)
    c = '?';
  const uint8_t *glyph = &font8x16[(int)c * 16];
  for (int row = 0; row < 16; row++) {
    uint8_t bits = glyph[row];
    for (int col = 0; col < 8; col++) {
      if (bits & (0x80 >> col)) {
        for (int sy = 0; sy < FONT_SCALE; sy++) {
          int py = y + row * FONT_SCALE + sy;
          if (py < 0 || py >= (int)scr_h)
            continue;
          for (int sx = 0; sx < FONT_SCALE; sx++) {
            int px = x + col * FONT_SCALE + sx;
            if (px < 0 || px >= (int)scr_w)
              continue;
            fb[py * stride + px] = color;
          }
        }
      }
    }
  }
}

static void draw_string(uint32_t *fb, uint32_t stride, uint32_t scr_w,
                        uint32_t scr_h, int x, int y, const char *str,
                        uint32_t color) {
  int char_w = 8 * FONT_SCALE;
  for (int i = 0; str[i]; i++)
    draw_char(fb, stride, scr_w, scr_h, x + i * char_w, y, str[i], color);
}

static void draw_string_centered(uint32_t *fb, uint32_t stride, uint32_t scr_w,
                                 uint32_t scr_h, int y, const char *str,
                                 uint32_t color) {
  int char_w = 8 * FONT_SCALE;
  int text_w = (int)strlen(str) * char_w;
  int x = ((int)scr_w - text_w) / 2;
  draw_string(fb, stride, scr_w, scr_h, x, y, str, color);
}

static void draw_cursor(uint32_t *fb, uint32_t stride, uint32_t scr_w,
                        uint32_t scr_h, int mx, int my) {
  for (int i = -10; i <= 10; i++) {
    int hx = mx + i;
    if (hx >= 0 && hx < (int)scr_w && my >= 0 && my < (int)scr_h)
      fb[my * stride + hx] = COL_CURSOR;
    int vy = my + i;
    if (vy >= 0 && vy < (int)scr_h && mx >= 0 && mx < (int)scr_w)
      fb[vy * stride + mx] = COL_CURSOR;
  }
  if (mx >= 0 && mx < (int)scr_w && my >= 0 && my < (int)scr_h)
    fb[my * stride + mx] = COL_CENTER;
}

/* ══════════════════════════════════════════════════════════════
 * Gestion des périphériques d'entrée (init / release)
 * ══════════════════════════════════════════════════════════════ */

static struct input_devices in_devs;

static void detect_input_devices(void) {
  input_devices_detect(&in_devs);
}

static void release_input_devices(void) {
  input_devices_release(&in_devs);
}

/* ══════════════════════════════════════════════════════════════
 * Lancement des applications
 * ══════════════════════════════════════════════════════════════ */

#ifdef VIRTUAL_DISPLAY
static void launch_std_app(const char *cmd, uint32_t scr_w, uint32_t scr_h) {
  (void)scr_w;
  (void)scr_h;
  printf("[VIRTUAL] Lancement STD simulé: %s\n", cmd);
}

static void launch_native_app(const char *cmd) {
  printf("[VIRTUAL] Lancement NATIVE simulé: %s\n", cmd);
}
#else
static void launch_std_app(const char *cmd, uint32_t scr_w, uint32_t scr_h) {
  char script[1024];
  snprintf(script, sizeof(script),
           "Xvfb :99 -screen 0 %ux%ux24 & XVFB_PID=$!; "
           "export DISPLAY=:99; sleep 0.2; "
           "mkdir -p /root/.config/openbox; "
           "cp packaging/openbox/rc.xml /root/.config/openbox/rc.xml; "
           "openbox & OB_PID=$!; sleep 0.2; "
           "%s & APP_PID=$!; "
           "./.out/bin/x11_bridge & DISP_PID=$!; "
           "wait $APP_PID 2>/dev/null; "
           "kill $DISP_PID $OB_PID $XVFB_PID 2>/dev/null; "
           "wait 2>/dev/null",
           scr_w, scr_h, cmd);

  printf("Lancement STD: %s\n", cmd);
  system(script);
  printf("Retour au homescreen.\n");
}

static void launch_native_app(const char *cmd) {
  printf("Lancement NATIVE: %s\n", cmd);
  system(cmd);
  printf("Retour au homescreen.\n");
}
#endif

/* ══════════════════════════════════════════════════════════════
 * Signal handler
 * ══════════════════════════════════════════════════════════════ */

static void handle_sigint(int sig) {
  (void)sig;
  keep_running = 0;
}

/* ══════════════════════════════════════════════════════════════
 * Point d'entrée
 * ══════════════════════════════════════════════════════════════ */

int main(void) {
  signal(SIGINT, handle_sigint);
  signal(SIGTERM, handle_sigint);

  /* Charger la configuration des boutons */
  load_config("config/homescreen.conf");
  if (btn_count == 0) {
    load_config("homescreen.conf");
  }
  if (btn_count == 0) {
    strcpy(buttons[0].label, "ERROR CONFIG");
    buttons[0].color = 0xAA0000;
    strcpy(buttons[0].cmd, "");
    btn_count = 1;
  }

  /* Initialiser DRM */
  struct drm_display drm;
  uint32_t scr_w, scr_h, stride;

  if (drm_display_init(&drm, &scr_w, &scr_h, &stride) != 0) {
    fprintf(stderr, "Erreur DRM (root ou groupe video requis)\n");
    return 1;
  }
  printf("DRM Ok: %ux%u (stride: %u)\n", scr_w, scr_h, stride);

  /* Allouer le framebuffer logiciel */
  uint32_t *fb = calloc(stride * scr_h, sizeof(uint32_t));
  if (!fb) {
    fprintf(stderr, "Erreur allocation framebuffer\n");
    drm_display_shutdown(&drm);
    return 1;
  }

  /* Détecter souris et clavier */
  detect_input_devices();

  /* Position de la souris (centré au départ) */
  int mouse_x = (int)scr_w / 2;
  int mouse_y = (int)scr_h / 2;

  /* Pré-calcul des positions des boutons */
  int total_h = btn_count * (BTN_HEIGHT + BTN_SPACING) - BTN_SPACING;
  int start_y = (int)scr_h / 2 - total_h / 2;
  int char_h = 16 * FONT_SCALE;

  printf("Homescreen Turingine DRM direct démarré.\n");

  /* ── Boucle principale ── */
  int clicked_btn = -1;

  while (keep_running) {
    /* 1. Effacer le framebuffer (fond sombre) */
    for (uint32_t i = 0; i < stride * scr_h; i++)
      fb[i] = COL_BG;

    /* 2. Titre */
    draw_string_centered(fb, stride, scr_w, scr_h, start_y - char_h - 20,
                         "=== TURINGINE CONFIGURABLE KIOSK ===", COL_TITLE);

    /* 3. Boutons */
    for (int i = 0; i < btn_count; i++) {
      int bx = (int)scr_w / 2 - BTN_WIDTH / 2;
      int by = start_y + i * (BTN_HEIGHT + BTN_SPACING);

      /* Couleur de fond (hover si la souris est dessus) */
      int hovered = (mouse_x >= bx && mouse_x < bx + BTN_WIDTH &&
                     mouse_y >= by && mouse_y < by + BTN_HEIGHT);
      uint32_t bg = hovered ? lighten_color(buttons[i].color, 0x22) : buttons[i].color;

      fill_rect(fb, stride, scr_w, scr_h, bx, by, BTN_WIDTH, BTN_HEIGHT, bg);
      draw_rect(fb, stride, scr_w, scr_h, bx, by, BTN_WIDTH, BTN_HEIGHT,
                COL_OUTLINE);
      draw_rect(fb, stride, scr_w, scr_h, bx + 1, by + 1, BTN_WIDTH - 2,
                BTN_HEIGHT - 2, COL_OUTLINE);

      /* Label centré dans le bouton */
      int char_w = 8 * FONT_SCALE;
      int text_w = (int)strlen(buttons[i].label) * char_w;
      int tx = bx + (BTN_WIDTH - text_w) / 2;
      int ty = by + (BTN_HEIGHT - char_h) / 2;
      draw_string(fb, stride, scr_w, scr_h, tx, ty, buttons[i].label,
                  COL_TEXT);
    }

    /* 4. Curseur */
    draw_cursor(fb, stride, scr_w, scr_h, mouse_x, mouse_y);

    /* 5. Blit + présentation */
    if (drm_display_blit_argb32(&drm, fb, stride) < 0) {
      /* En mode virtuel, -1 = fenêtre fermée */
      keep_running = 0;
      break;
    }
    drm_display_present(&drm);

    /* 6. Lecture des entrées */
    int mouse_clicked = 0;

#ifdef VIRTUAL_DISPLAY
    poll_virtual_input(&mouse_x, &mouse_y, &mouse_clicked);
#else
    {
      struct input_event ev;
      int rc;

      /* Souris */
      while (in_devs.mouse) {
        rc = libevdev_next_event(in_devs.mouse, LIBEVDEV_READ_FLAG_NORMAL, &ev);
        if (rc == LIBEVDEV_READ_STATUS_SYNC) {
          while (rc == LIBEVDEV_READ_STATUS_SYNC) {
            rc = libevdev_next_event(in_devs.mouse, LIBEVDEV_READ_FLAG_SYNC, &ev);
          }
          continue;
        }
        if (rc != LIBEVDEV_READ_STATUS_SUCCESS)
          break;

        if (ev.type == EV_REL) {
          if (ev.code == REL_X)
            mouse_x += ev.value;
          if (ev.code == REL_Y)
            mouse_y += ev.value;
          if (mouse_x < 0) mouse_x = 0;
          if (mouse_x >= (int)scr_w) mouse_x = scr_w - 1;
          if (mouse_y < 0) mouse_y = 0;
          if (mouse_y >= (int)scr_h) mouse_y = scr_h - 1;
        } else if (ev.type == EV_KEY && ev.code == BTN_LEFT && ev.value == 1) {
          mouse_clicked = 1;
        }
      }

      /* Clavier */
      while (in_devs.kb) {
        rc = libevdev_next_event(in_devs.kb, LIBEVDEV_READ_FLAG_NORMAL, &ev);
        if (rc == LIBEVDEV_READ_STATUS_SYNC) {
          while (rc == LIBEVDEV_READ_STATUS_SYNC) {
            rc = libevdev_next_event(in_devs.kb, LIBEVDEV_READ_FLAG_SYNC, &ev);
          }
          continue;
        }
        if (rc != LIBEVDEV_READ_STATUS_SUCCESS)
          break;

        if (ev.type == EV_KEY && ev.code == KEY_ESC && ev.value == 1)
          keep_running = 0;
      }
    }
#endif

    /* 7. Détection du clic sur un bouton */
    if (mouse_clicked) {
      clicked_btn = -1;
      for (int i = 0; i < btn_count; i++) {
        int bx = (int)scr_w / 2 - BTN_WIDTH / 2;
        int by = start_y + i * (BTN_HEIGHT + BTN_SPACING);
        if (mouse_x >= bx && mouse_x < bx + BTN_WIDTH && mouse_y >= by &&
            mouse_y < by + BTN_HEIGHT) {
          clicked_btn = i;
          break;
        }
      }

      if (clicked_btn >= 0 && buttons[clicked_btn].cmd[0] != '\0') {
        /* Libérer les ressources avant de lancer l'app */
        release_input_devices();
        drm_display_shutdown(&drm);

        if (buttons[clicked_btn].is_native)
          launch_native_app(buttons[clicked_btn].cmd);
        else
          launch_std_app(buttons[clicked_btn].cmd, scr_w, scr_h);

        /* Réinitialiser après retour */
        if (drm_display_init(&drm, &scr_w, &scr_h, &stride) != 0) {
          fprintf(stderr, "Erreur: impossible de réinitialiser DRM\n");
          free(fb);
          return 1;
        }
        detect_input_devices();
        mouse_x = (int)scr_w / 2;
        mouse_y = (int)scr_h / 2;
      }
    }

    /* 8. ~20 FPS */
    usleep(50000);
  }

  /* Nettoyage */
  printf("\nArrêt propre du homescreen...\n");
  release_input_devices();
  free(fb);
  drm_display_shutdown(&drm);

  return 0;
}
