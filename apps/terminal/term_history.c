#define _DEFAULT_SOURCE

#include "term_history.h"
#include "terminal.h"
#include "term_render.h"

#include <stdio.h>
#include <string.h>

/* ══════════════════════════════════════════════════════════════
 * État interne de l'historique
 * ══════════════════════════════════════════════════════════════ */

static char history[HISTORY_MAX][INPUT_MAX];
int  hist_count = 0;
int  hist_cursor = 0;
static char hist_saved_input[INPUT_MAX];
int  hist_browsing = 0;
static char hist_file_path[256] = "";

/* ══════════════════════════════════════════════════════════════
 * Chargement / Sauvegarde
 * ══════════════════════════════════════════════════════════════ */

void history_load(void) {
  snprintf(hist_file_path, sizeof(hist_file_path), "%s/.bash_history", user_home);
  FILE *f = fopen(hist_file_path, "r");
  if (!f) return;

  char line[INPUT_MAX];
  /* Lire toutes les lignes, ne garder que les HISTORY_MAX dernières */
  while (fgets(line, sizeof(line), f)) {
    int len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
    if (line[0] == '\0') continue;

    strncpy(history[hist_count % HISTORY_MAX], line,
            sizeof(history[0]) - 1);
    hist_count += 1;
  }
  fclose(f);

  /* Si on a lu plus que HISTORY_MAX, recaler pour que l'index 0
   * soit la plus ancienne entrée conservée */
  if (hist_count > HISTORY_MAX) hist_count = HISTORY_MAX;

  hist_cursor = hist_count; /* Curseur après la dernière entrée */
  printf("Historique: %d entrées chargées depuis %s\n",
         hist_count, hist_file_path);
}

void history_append(const char *cmd) {
  if (cmd[0] == '\0') return;

  /* Ne pas dupliquer la dernière entrée */
  if (hist_count > 0 &&
      strcmp(history[(hist_count - 1) % HISTORY_MAX], cmd) == 0)
    return;

  strncpy(history[hist_count % HISTORY_MAX], cmd,
          sizeof(history[0]) - 1);
  hist_count += 1;
  if (hist_count > HISTORY_MAX) hist_count = HISTORY_MAX;

  hist_cursor = hist_count;
  hist_browsing = 0;

  /* Écrire en append dans le fichier */
  if (hist_file_path[0]) {
    FILE *f = fopen(hist_file_path, "a");
    if (f) {
      fprintf(f, "%s\n", cmd);
      fclose(f);
    }
  }
}

/* ══════════════════════════════════════════════════════════════
 * Manipulation de la ligne de saisie
 * ══════════════════════════════════════════════════════════════ */

/* Efface la saisie visible à l'écran */
static void input_clear_line(char *input_cmd, int *input_pos) {
  for (int i = 0; i < *input_pos; i += 1) {
    term_putc('\b');
  }
  *input_pos = 0;
  input_cmd[0] = '\0';
}

static void input_set_line(char *input_cmd, int *input_pos,
                           const char *text) {
  input_clear_line(input_cmd, input_pos);
  int len = strlen(text);
  if (len > INPUT_MAX - 2) len = INPUT_MAX - 2;
  memcpy(input_cmd, text, len);
  input_cmd[len] = '\0';
  *input_pos = len;
  for (int i = 0; i < len; i += 1) {
    term_putc(text[i]);
  }
}

/* ══════════════════════════════════════════════════════════════
 * Navigation dans l'historique (flèches haut/bas)
 * ══════════════════════════════════════════════════════════════ */

void history_navigate(int direction, char *input_cmd, int *input_pos) {
  /* direction: -1 = haut (plus ancien), +1 = bas (plus récent) */
  if (hist_count == 0) return;

  /* Sauvegarder la saisie courante au premier appui */
  if (!hist_browsing) {
    strncpy(hist_saved_input, input_cmd, sizeof(hist_saved_input) - 1);
    hist_saved_input[sizeof(hist_saved_input) - 1] = '\0';
    hist_browsing = 1;
  }

  int new_cursor = hist_cursor + direction;
  if (new_cursor < 0) new_cursor = 0;
  if (new_cursor > hist_count) new_cursor = hist_count;
  if (new_cursor == hist_cursor) return;
  hist_cursor = new_cursor;

  if (hist_cursor == hist_count) {
    /* Retour à la saisie courante */
    input_set_line(input_cmd, input_pos, hist_saved_input);
  } else {
    input_set_line(input_cmd, input_pos,
                   history[hist_cursor % HISTORY_MAX]);
  }
}
