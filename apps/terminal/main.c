#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

#include "drm_display.h"
#include "evdev_input.h"
#include "font8x16.h"

static volatile int keep_running = 1;

static void handle_sigint(int sig) {
  (void)sig;
  keep_running = 0;
}

#define COLS 60
#define ROWS 20

static char term_text[ROWS][COLS];
static int cursor_x = 0;
static int cursor_y = 0;

static struct drm_display drm;
static uint32_t scr_w, scr_h, stride;
static uint32_t *fb = NULL;

static void term_scroll(void) {
  for (int y = 0; y < ROWS - 1; y += 1) {
    memcpy(term_text[y], term_text[y + 1], COLS);
  }
  memset(term_text[ROWS - 1], 0, COLS);
  cursor_y = ROWS - 1;
  cursor_x = 0;
}

static void term_putc(char c) {
  if (c == '\n') {
    cursor_x = 0;
    cursor_y += 1;
  } else if (c == '\r') {
    cursor_x = 0;
  } else if (c == '\b') {
    if (cursor_x > 0) {
      cursor_x -= 1;
      term_text[cursor_y][cursor_x] = '\0';
    } else if (cursor_y > 0) {
      cursor_y -= 1;
      cursor_x = COLS - 1;
      term_text[cursor_y][cursor_x] = '\0';
    }
  } else if (c >= 32 && c <= 126) {
    if (cursor_x >= COLS - 1) {
      cursor_x = 0;
      cursor_y += 1;
      if (cursor_y >= ROWS) {
        term_scroll();
      }
    }
    term_text[cursor_y][cursor_x] = c;
    cursor_x += 1;
  }
  if (cursor_y >= ROWS) {
    term_scroll();
  }
}

static void term_print(const char *str) {
  while (*str) {
    term_putc(*str);
    str += 1;
  }
}

static void render_term(void) {
  /* Effacer l'écran en noir */
  for (uint32_t i = 0; i < stride * scr_h; i += 1) {
    fb[i] = 0xFF000000;
  }

    /* Dessiner le texte */
  for (int y = 0; y < ROWS; y += 1) {
    for (int x = 0; x < COLS; x += 1) {
      char c = term_text[y][x];
      if (c == 0) {
        break;
      }

      const uint8_t *glyph = &font8x16[(int)c * 16];
      for (int r = 0; r < 16; r += 1) {
        uint8_t bits = glyph[r];
        for (int c_bit = 0; c_bit < 8; c_bit += 1) {
          if (bits & (0x80 >> c_bit)) {
            int px = x * 8 + c_bit;
            int py = y * 16 + r;
            if (px < (int)scr_w && py < (int)scr_h) {
              fb[py * stride + px] = 0xFFFFFFFF; // Blanc
            }
          }
        }
      }
    }
  }

    /* Dessiner le curseur */
    int cx = cursor_x * 8;
    int cy = cursor_y * 16;
  for (int r = 0; r < 16; r += 1) {
    for (int c_bit = 0; c_bit < 8; c_bit += 1) {
      int px = cx + c_bit;
      int py = cy + r;
      if (px < (int)scr_w && py < (int)scr_h) {
        fb[py * stride + px] ^= 0xFFFFFFFF; // Inverser les couleurs
      }
    }
  }

  drm_display_blit_argb32(&drm, fb, stride);
  drm_display_present(&drm);
}

static void run_command(const char *cmd) {
  char full_cmd[256];
  snprintf(full_cmd, sizeof(full_cmd), "%s 2>&1", cmd);
  FILE *f = popen(full_cmd, "r");
  if (!f) {
    term_print("Erreur: impossible d'exécuter la commande\n");
    return;
  }
  char buf[128];
  while (fgets(buf, sizeof(buf), f)) {
    term_print(buf);
    render_term(); // Rendu progressif pendant que la commande tourne
  }
  pclose(f);
}

// Map qwerty simplifiée
static const char keymap[128] = {
    [KEY_1] = '1', [KEY_2] = '2', [KEY_3] = '3', [KEY_4] = '4', [KEY_5] = '5',
    [KEY_6] = '6', [KEY_7] = '7', [KEY_8] = '8', [KEY_9] = '9', [KEY_0] = '0',
    [KEY_MINUS] = '-', [KEY_EQUAL] = '=',
    [KEY_Q] = 'q', [KEY_W] = 'w', [KEY_E] = 'e', [KEY_R] = 'r', [KEY_T] = 't', [KEY_Y] = 'y', [KEY_U] = 'u', [KEY_I] = 'i', [KEY_O] = 'o', [KEY_P] = 'p', [KEY_LEFTBRACE] = '[', [KEY_RIGHTBRACE] = ']',
    [KEY_A] = 'a', [KEY_S] = 's', [KEY_D] = 'd', [KEY_F] = 'f', [KEY_G] = 'g', [KEY_H] = 'h', [KEY_J] = 'j', [KEY_K] = 'k', [KEY_L] = 'l', [KEY_SEMICOLON] = ';', [KEY_APOSTROPHE] = '\'', [KEY_BACKSLASH] = '\\',
    [KEY_Z] = 'z', [KEY_X] = 'x', [KEY_C] = 'c', [KEY_V] = 'v', [KEY_B] = 'b', [KEY_N] = 'n', [KEY_M] = 'm', [KEY_COMMA] = ',', [KEY_DOT] = '.', [KEY_SLASH] = '/',
    [KEY_SPACE] = ' '
};

static const char keymap_shift[128] = {
    [KEY_1] = '!', [KEY_2] = '@', [KEY_3] = '#', [KEY_4] = '$', [KEY_5] = '%',
    [KEY_6] = '^', [KEY_7] = '&', [KEY_8] = '*', [KEY_9] = '(', [KEY_0] = ')',
    [KEY_MINUS] = '_', [KEY_EQUAL] = '+',
    [KEY_Q] = 'Q', [KEY_W] = 'W', [KEY_E] = 'E', [KEY_R] = 'R', [KEY_T] = 'T', [KEY_Y] = 'Y', [KEY_U] = 'U', [KEY_I] = 'I', [KEY_O] = 'O', [KEY_P] = 'P', [KEY_LEFTBRACE] = '{', [KEY_RIGHTBRACE] = '}',
    [KEY_A] = 'A', [KEY_S] = 'S', [KEY_D] = 'D', [KEY_F] = 'F', [KEY_G] = 'G', [KEY_H] = 'H', [KEY_J] = 'J', [KEY_K] = 'K', [KEY_L] = 'L', [KEY_SEMICOLON] = ':', [KEY_APOSTROPHE] = '"', [KEY_BACKSLASH] = '|',
    [KEY_Z] = 'Z', [KEY_X] = 'X', [KEY_C] = 'C', [KEY_V] = 'V', [KEY_B] = 'B', [KEY_N] = 'N', [KEY_M] = 'M', [KEY_COMMA] = '<', [KEY_DOT] = '>', [KEY_SLASH] = '?',
    [KEY_SPACE] = ' '
};

int main(void) {
  signal(SIGINT, handle_sigint);
  signal(SIGTERM, handle_sigint);

  if (drm_display_init(&drm, &scr_w, &scr_h, &stride) != 0) {
    fprintf(stderr, "Terminal: Erreur init DRM\n");
    return 1;
  }

  struct input_devices in_devs;
  input_devices_detect(&in_devs);

  fb = calloc(stride * scr_h, sizeof(uint32_t));
  if (!fb) {
    drm_display_shutdown(&drm);
    return 1;
  }

  memset(term_text, 0, sizeof(term_text));

  term_print("==================================\n");
  term_print(" Turingine Native Shell (Root)\n");
  term_print("==================================\n\n");
  term_print("root@turingine:~# ");

  char input_cmd[128] = {0};
  int input_pos = 0;
  int shift_held = 0;

  while (keep_running) {
    render_term();

    struct input_event ev;
    while (in_devs.kb) {
      int rc = libevdev_next_event(in_devs.kb, LIBEVDEV_READ_FLAG_NORMAL, &ev);
      if (rc == LIBEVDEV_READ_STATUS_SYNC) {
        while (rc == LIBEVDEV_READ_STATUS_SYNC) {
          rc = libevdev_next_event(in_devs.kb, LIBEVDEV_READ_FLAG_SYNC, &ev);
        }
        continue;
      }
      if (rc != LIBEVDEV_READ_STATUS_SUCCESS)
        break;

      if (ev.type == EV_KEY) {
        if (ev.code == KEY_LEFTSHIFT || ev.code == KEY_RIGHTSHIFT) {
          shift_held = (ev.value != 0); // 1 = pressed, 0 = released
        } else if (ev.value == 1 || ev.value == 2) { // Pressed or Auto-repeat
          if (ev.code == KEY_ESC) {
            keep_running = 0;
          } else if (ev.code == KEY_ENTER) {
            term_putc('\n');
            if (input_pos > 0) {
              if (strcmp(input_cmd, "exit") == 0) {
                keep_running = 0;
              } else {
                run_command(input_cmd);
              }
            }
            input_pos = 0;
            input_cmd[0] = '\0';
            if (keep_running)
              term_print("root@turingine:~# ");
          } else if (ev.code == KEY_BACKSPACE) {
            if (input_pos > 0) {
              input_pos -= 1;
              input_cmd[input_pos] = '\0';
              term_putc('\b');
            }
          } else if (ev.code < 128) {
            char c = shift_held ? keymap_shift[ev.code] : keymap[ev.code];
            if (c != 0 && input_pos < (int)sizeof(input_cmd) - 1) {
              input_cmd[input_pos] = c;
              input_pos += 1;
              input_cmd[input_pos] = '\0';
              term_putc(c);
            }
          }
        }
      }
    }

    usleep(33000); // ~30 FPS
  }

  input_devices_release(&in_devs);
  free(fb);
  drm_display_shutdown(&drm);
  return 0;
}
