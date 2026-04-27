#define _DEFAULT_SOURCE

#include <signal.h>
#include <string.h>
#include <unistd.h>

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
 * Point d'entrée
 * ══════════════════════════════════════════════════════════════ */

int main(void) {
  signal(SIGINT, handle_sigint);
  signal(SIGTERM, handle_sigint);

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

  /* ══════════════════════════════════════════════════════════════
   * Boucle principale (~30 FPS)
   * ══════════════════════════════════════════════════════════════ */

  while (keep_running) {
    render_term();

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

      if (ev.code == KEY_LEFTSHIFT || ev.code == KEY_RIGHTSHIFT) {
        shift_held = (ev.value != 0);
        continue;
      }

      if (ev.value != 1 && ev.value != 2) continue; /* Pressed or repeat */

      /* Toute touche pressée ramène au live */
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
          } else {
            run_command(input_cmd);
          }
        }
        input_pos = 0;
        input_cmd[0] = '\0';
        hist_browsing = 0;
        hist_cursor = hist_count;
        last_tab_was_tab = 0;
        if (keep_running)
          term_print(prompt_str);

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

  /* Cleanup */
  input_devices_release(&in_devs);
  term_render_shutdown();
  return 0;
}
