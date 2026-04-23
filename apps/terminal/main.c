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

/* ══════════════════════════════════════════════════════════════
 * Détection du premier utilisateur non-root (UID >= 1000)
 * ══════════════════════════════════════════════════════════════ */

static char user_name[64] = "root";
static char user_home[128] = "/root";
static char user_cwd[256] = "/root";
static char prompt_str[128] = "root@turingine:~# ";

static void detect_default_user(void) {
  FILE *f = fopen("/etc/passwd", "r");
  if (!f) {
    printf("detect_default_user: impossible d'ouvrir /etc/passwd\n");
    return;
  }

  char line[512];
  while (fgets(line, sizeof(line), f)) {
    /* Découpage manuel par ':' (strtok fusionne les champs vides) */
    char *fields[7] = {0};
    char *cur = line;
    for (int i = 0; i < 7 && cur; i += 1) {
      fields[i] = cur;
      char *sep = strchr(cur, ':');
      if (sep) {
        *sep = '\0';
        cur = sep + 1;
      } else {
        /* Dernier champ : retirer le \n */
        char *nl = strchr(cur, '\n');
        if (nl) *nl = '\0';
        cur = NULL;
      }
    }

    /* fields: 0=name 1=pass 2=uid 3=gid 4=gecos 5=home 6=shell */
    if (!fields[0] || !fields[2] || !fields[5])
      continue;

    int uid = atoi(fields[2]);
    if (uid >= 1000 && uid < 65534) {
      strncpy(user_name, fields[0], sizeof(user_name) - 1);
      strncpy(user_home, fields[5], sizeof(user_home) - 1);
      strncpy(user_cwd, fields[5], sizeof(user_cwd) - 1);
      snprintf(prompt_str, sizeof(prompt_str), "%s@turingine:~$ ", user_name);
      printf("Utilisateur détecté : %s (home: %s)\n", user_name, user_home);
      break;
    }
  }
  fclose(f);

  /* Déplacer le processus dans le répertoire de l'utilisateur */
  if (chdir(user_home) != 0)
    printf("Attention: impossible de chdir vers %s\n", user_home);
}

/* Reconstruit le prompt après un changement de répertoire */
static void prompt_refresh(void) {
  int home_len = strlen(user_home);
  const char *display_path = user_cwd;
  static char short_path[256];

  if (strcmp(user_cwd, user_home) == 0) {
    display_path = "~";
  } else if (strncmp(user_cwd, user_home, home_len) == 0 &&
             user_cwd[home_len] == '/') {
    snprintf(short_path, sizeof(short_path), "~%s", user_cwd + home_len);
    display_path = short_path;
  }

  const char *sym = (strcmp(user_name, "root") == 0) ? "#" : "$";
  snprintf(prompt_str, sizeof(prompt_str), "%s@turingine:%s%s ",
           user_name, display_path, sym);
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

/* Gestion de la commande cd (changement de répertoire) */
static void handle_cd(const char *arg) {
  /* cd sans argument ou cd ~ → retour au home */
  if (arg[0] == '\0' || strcmp(arg, "~") == 0) {
    strncpy(user_cwd, user_home, sizeof(user_cwd) - 1);
    prompt_refresh();
    return;
  }

  /* Résoudre le chemin via le shell */
  char full_cmd[512];
  if (strcmp(user_name, "root") == 0) {
    snprintf(full_cmd, sizeof(full_cmd),
             "cd %s && cd %s && pwd 2>&1", user_cwd, arg);
  } else {
    snprintf(full_cmd, sizeof(full_cmd),
             "su -l %s -c 'cd %s && cd %s && pwd' 2>&1",
             user_name, user_cwd, arg);
  }

  FILE *f = popen(full_cmd, "r");
  if (!f) {
    term_print("cd: erreur interne\n");
    return;
  }

  char result[256] = {0};
  if (fgets(result, sizeof(result), f)) {
    int len = strlen(result);
    if (len > 0 && result[len - 1] == '\n')
      result[len - 1] = '\0';

    if (result[0] == '/') {
      strncpy(user_cwd, result, sizeof(user_cwd) - 1);
      prompt_refresh();
    } else {
      /* Le shell a renvoyé une erreur */
      term_print(result);
      term_print("\n");
    }
  }
  pclose(f);
}

static void run_command(const char *cmd) {
  char full_cmd[512];
  if (strcmp(user_name, "root") == 0) {
    snprintf(full_cmd, sizeof(full_cmd),
             "cd %s && %s 2>&1", user_cwd, cmd);
  } else {
    snprintf(full_cmd, sizeof(full_cmd),
             "su -l %s -c 'cd %s && %s' 2>&1",
             user_name, user_cwd, cmd);
  }
  FILE *f = popen(full_cmd, "r");
  if (!f) {
    term_print("Erreur: impossible d'exécuter la commande\n");
    return;
  }
  char buf[128];
  while (fgets(buf, sizeof(buf), f)) {
    term_print(buf);
    render_term(); /* Rendu progressif pendant que la commande tourne */
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
  detect_default_user();

  term_print("==================================\n");
  term_print(" Turingine Native Shell\n");
  term_print("==================================\n\n");
  term_print(prompt_str);

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
              } else if (strncmp(input_cmd, "cd", 2) == 0 &&
                         (input_cmd[2] == ' ' || input_cmd[2] == '\0')) {
                handle_cd(input_cmd[2] == ' ' ? input_cmd + 3 : "");
              } else {
                run_command(input_cmd);
              }
            }
            input_pos = 0;
            input_cmd[0] = '\0';
            if (keep_running)
              term_print(prompt_str);
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
