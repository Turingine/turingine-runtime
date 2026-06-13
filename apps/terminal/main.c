#define _DEFAULT_SOURCE

#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

#include "terminal.h"
#include "term_render.h"
#include "term_history.h"
#include "term_complete.h"
#include "term_shell.h"
#include "evdev_input.h"

/* ══════════════════════════════════════════════════════════════
 * Signal handling
 * ══════════════════════════════════════════════════════════════ */

volatile int keep_running = 1;

static void handle_sigint(int sig) {
  (void)sig;
  keep_running = 0;
}

/* ══════════════════════════════════════════════════════════════
 * Conversion evdev → séquence VT100 pour le PTY
 * ══════════════════════════════════════════════════════════════ */

/* Traduit un événement clavier en octets à envoyer au PTY.
 * Retourne le nombre d'octets écrits dans buf (0 si ignoré). */
static int ev_to_pty(const struct input_event *ev, int shift_held,
                     char *buf, int bufsz) {
  (void)bufsz; /* toujours >= 8, vérification par construction */

  if (ev->code == KEY_ENTER) { buf[0] = '\r'; return 1; }
  if (ev->code == KEY_BACKSPACE) { buf[0] = 127; return 1; }
  if (ev->code == KEY_TAB)  { buf[0] = '\t'; return 1; }
  if (ev->code == KEY_ESC)  { buf[0] = 0x1b; return 1; }

  /* Touches de direction → séquences VT100 */
  if (ev->code == KEY_UP)    { memcpy(buf, "\x1b[A", 3); return 3; }
  if (ev->code == KEY_DOWN)  { memcpy(buf, "\x1b[B", 3); return 3; }
  if (ev->code == KEY_RIGHT) { memcpy(buf, "\x1b[C", 3); return 3; }
  if (ev->code == KEY_LEFT)  { memcpy(buf, "\x1b[D", 3); return 3; }

  /* Ctrl+C → SIGINT via PTY */
  if (ev->code == KEY_C && shift_held == 0) {
    /* Détecter Ctrl : pas de mapping dans active_keymap */
    /* On le gère dans la boucle avec une variable ctrl_held */
  }

  /* Caractère normal */
  if (ev->code < 128) {
    char c = shift_held ? active_keymap_shift[ev->code]
                        : active_keymap[ev->code];
    if (c != 0) { buf[0] = c; return 1; }
  }
  return 0;
}

/* ══════════════════════════════════════════════════════════════
 * Point d'entrée
 * ══════════════════════════════════════════════════════════════ */

int main(void) {
  signal(SIGINT,  handle_sigint);
  signal(SIGTERM, handle_sigint);
  signal(SIGCHLD, SIG_DFL); /* waitpid() géré manuellement */

  /* Initialisation de l'affichage DRM + scrollback */
  if (term_render_init() != 0)
    return 1;

  /* Détection des périphériques d'entrée */
  struct input_devices in_devs;
  input_devices_detect(&in_devs);

  /* Initialisation du shell */
  detect_default_user();
  detect_keyboard_layout();
  history_load();

  /* Bannière de bienvenue */
  term_print("==================================\n");
  term_print(" Turingine Native Shell\n");
  term_print("==================================\n\n");
  term_print(prompt_str);

  char input_cmd[INPUT_MAX] = {0};
  int input_pos = 0;
  int shift_held = 0;
  int ctrl_held = 0;

  /* ══════════════════════════════════════════════════════════════
   * Boucle principale (~30 FPS)
   * ══════════════════════════════════════════════════════════════ */

  while (keep_running) {
    render_term();

    /* ── PTY actif : lire la sortie de la commande ── */
    if (pty_fd >= 0) {
      char buf[256];
      ssize_t n;
      while ((n = read(pty_fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; i += 1) {
          /* Filtrage minimal des séquences ANSI :
           * on ignore les ESC[ ... m (couleurs) et ESC[ ... H (curseur)
           * pour ne garder que le texte brut et les sauts de ligne. */
          char c = buf[i];
          if (c == '\x1b') {
            /* Consommer jusqu'à la lettre finale de la séquence */
            i += 1;
            if (i < n && buf[i] == '[') {
              i += 1;
              while (i < n && !(buf[i] >= 'A' && buf[i] <= 'Z') &&
                     !(buf[i] >= 'a' && buf[i] <= 'z'))
                i += 1;
            }
            /* Séquence consommée, continuer */
            continue;
          }
          if (c == '\r') continue; /* \r\n → on garde seulement \n */
          term_putc(c);
        }
      }

      /* Vérifier si la commande est terminée */
      int status;
      pid_t r = waitpid(pty_child, &status, WNOHANG);
      if (r == pty_child) {
        /* Vider le dernier flush du PTY */
        while ((n = read(pty_fd, buf, sizeof(buf))) > 0) {
          for (ssize_t i = 0; i < n; i += 1) {
            char c = buf[i];
            if (c == '\x1b') {
              i += 1;
              if (i < n && buf[i] == '[') {
                i += 1;
                while (i < n && !(buf[i] >= 'A' && buf[i] <= 'Z') &&
                       !(buf[i] >= 'a' && buf[i] <= 'z'))
                  i += 1;
              }
              continue;
            }
            if (c == '\r') continue;
            term_putc(c);
          }
        }
        close(pty_fd);
        pty_fd = -1;
        pty_child = -1;
        /* Afficher le prompt */
        term_print(prompt_str);
      }
    }

    /* ── Lecture des événements souris (molette → scrollback) ── */
    struct input_event mev;
    while (in_devs.mouse) {
      int rc = libevdev_next_event(in_devs.mouse,
                                   LIBEVDEV_READ_FLAG_NORMAL, &mev);
      if (rc == LIBEVDEV_READ_STATUS_SYNC) {
        while (rc == LIBEVDEV_READ_STATUS_SYNC)
          rc = libevdev_next_event(in_devs.mouse,
                                   LIBEVDEV_READ_FLAG_SYNC, &mev);
        continue;
      }
      if (rc != LIBEVDEV_READ_STATUS_SUCCESS) break;

      if (mev.type == EV_REL && mev.code == REL_WHEEL) {
        scroll_offset += mev.value * 3;
        if (scroll_offset < 0) scroll_offset = 0;
        int max_scroll = sb_count - ROWS;
        if (max_scroll < 0) max_scroll = 0;
        if (scroll_offset > max_scroll) scroll_offset = max_scroll;
      }
    }

    /* ── Lecture des événements clavier ── */
    struct input_event ev;
    while (in_devs.kb) {
      int rc = libevdev_next_event(in_devs.kb, LIBEVDEV_READ_FLAG_NORMAL, &ev);
      if (rc == LIBEVDEV_READ_STATUS_SYNC) {
        while (rc == LIBEVDEV_READ_STATUS_SYNC)
          rc = libevdev_next_event(in_devs.kb, LIBEVDEV_READ_FLAG_SYNC, &ev);
        continue;
      }
      if (rc != LIBEVDEV_READ_STATUS_SUCCESS)
        break;

      if (ev.type != EV_KEY) continue;

      /* Suivi des modificateurs */
      if (ev.code == KEY_LEFTSHIFT || ev.code == KEY_RIGHTSHIFT) {
        shift_held = (ev.value != 0);
        continue;
      }
      if (ev.code == KEY_LEFTCTRL || ev.code == KEY_RIGHTCTRL) {
        ctrl_held = (ev.value != 0);
        continue;
      }

      if (ev.value != 1 && ev.value != 2) continue; /* pressed ou repeat */

      /* ── Mode PTY actif : rediriger le clavier vers la commande ── */
      if (pty_fd >= 0) {
        scroll_offset = 0;
        char seq[8];
        int slen = 0;

        /* Ctrl+C → envoyer ETX (0x03) */
        if (ctrl_held && ev.code == KEY_C) {
          seq[0] = 0x03;
          slen = 1;
        } else if (ctrl_held && ev.code == KEY_D) {
          /* Ctrl+D → EOF */
          seq[0] = 0x04;
          slen = 1;
        } else {
          slen = ev_to_pty(&ev, shift_held, seq, sizeof(seq));
        }

        if (slen > 0)
          write(pty_fd, seq, slen);

        continue; /* Ne pas traiter la touche dans le shell */
      }

      /* ── Mode shell normal ── */
      scroll_offset = 0;

      /* Mode confirmation Y/n pour la complétion */
      if (awaiting_confirm) {
        awaiting_confirm = 0;
        char c = shift_held ? active_keymap_shift[ev.code]
                            : active_keymap[ev.code];
        if (c == 'y' || c == 'Y') {
          show_completions(input_cmd, &input_pos);
        } else {
          term_putc('\n');
          term_print(prompt_str);
          for (int i = 0; i < input_pos; i += 1)
            term_putc(input_cmd[i]);
        }
        continue;
      }

      /* ── Dispatch des touches ── */

      if (ev.code == KEY_ESC) {
        keep_running = 0;

      } else if (ev.code == KEY_ENTER) {
        term_putc('\n');
        if (input_pos > 0) {
          history_append(input_cmd);
          if (strcmp(input_cmd, "exit") == 0) {
            keep_running = 0;
          } else if (strncmp(input_cmd, "cd", 2) == 0 &&
                     (input_cmd[2] == ' ' || input_cmd[2] == '\0')) {
            handle_cd(input_cmd[2] == ' ' ? input_cmd + 3 : "");
            term_print(prompt_str);
          } else {
            run_command(input_cmd); /* Lance dans le PTY, non-bloquant */
            /* Le prompt sera affiché quand le processus se termine */
          }
        } else {
          term_print(prompt_str);
        }
        input_pos = 0;
        input_cmd[0] = '\0';
        hist_browsing = 0;
        hist_cursor = hist_count;
        last_tab_was_tab = 0;

      } else if (ev.code == KEY_BACKSPACE) {
        if (input_pos > 0) {
          input_pos -= 1;
          input_cmd[input_pos] = '\0';
          term_putc('\b');
          last_tab_was_tab = 0;
        }

      } else if (ev.code == KEY_UP) {
        history_navigate(-1, input_cmd, &input_pos);
        last_tab_was_tab = 0;

      } else if (ev.code == KEY_DOWN) {
        history_navigate(+1, input_cmd, &input_pos);
        last_tab_was_tab = 0;

      } else if (ev.code == KEY_TAB) {
        complete_input(input_cmd, &input_pos);

      } else if (ev.code < 128) {
        char c = shift_held ? active_keymap_shift[ev.code]
                            : active_keymap[ev.code];
        if (c != 0 && input_pos < INPUT_MAX - 1) {
          input_cmd[input_pos] = c;
          input_pos += 1;
          input_cmd[input_pos] = '\0';
          term_putc(c);
          last_tab_was_tab = 0;
        }
      }
    }

    usleep(33000); /* ~30 FPS */
  }

  /* Cleanup : tuer le processus enfant s'il tourne encore */
  if (pty_child > 0) {
    kill(pty_child, SIGTERM);
    waitpid(pty_child, NULL, 0);
    close(pty_fd);
  }

  input_devices_release(&in_devs);
  term_render_shutdown();
  return 0;
}
