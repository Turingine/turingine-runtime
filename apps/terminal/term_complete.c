#define _DEFAULT_SOURCE

#include "term_complete.h"
#include "terminal.h"
#include "term_render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

/* ══════════════════════════════════════════════════════════════
 * État interne de la complétion
 * ══════════════════════════════════════════════════════════════ */

#define COMPLETE_MAX 256
static char completions[COMPLETE_MAX][INPUT_MAX];
static int  complete_count = 0;
int  last_tab_was_tab = 0;
int  awaiting_confirm = 0;

/* ══════════════════════════════════════════════════════════════
 * Recherche de commandes dans PATH
 * ══════════════════════════════════════════════════════════════ */

static void complete_command(const char *prefix) {
  complete_count = 0;
  int plen = strlen(prefix);
  if (plen == 0) return;

  const char *path_dirs[] = {"/usr/bin", "/bin", "/usr/sbin", "/sbin", NULL};
  for (int d = 0; path_dirs[d]; d += 1) {
    DIR *dir = opendir(path_dirs[d]);
    if (!dir) continue;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && complete_count < COMPLETE_MAX) {
      if (strncmp(ent->d_name, prefix, plen) == 0) {
        /* Vérifier qu'on n'a pas de doublon */
        int dup = 0;
        for (int i = 0; i < complete_count; i += 1) {
          if (strcmp(completions[i], ent->d_name) == 0) { dup = 1; break; }
        }
        if (!dup) {
          snprintf(completions[complete_count],
                   sizeof(completions[0]), "%s", ent->d_name);
          complete_count += 1;
        }
      }
    }
    closedir(dir);
  }
}

/* ══════════════════════════════════════════════════════════════
 * Recherche de fichiers/dossiers
 * ══════════════════════════════════════════════════════════════ */

static void complete_path(const char *prefix) {
  complete_count = 0;
  int plen = strlen(prefix);

  /* Séparer le répertoire du préfixe du nom */
  char dir_path[256] = ".";
  const char *name_prefix = prefix;

  const char *last_slash = strrchr(prefix, '/');
  if (last_slash) {
    int dlen = (int)(last_slash - prefix);
    if (dlen == 0) {
      strcpy(dir_path, "/");
    } else {
      if (dlen > (int)sizeof(dir_path) - 1) dlen = sizeof(dir_path) - 1;
      memcpy(dir_path, prefix, dlen);
      dir_path[dlen] = '\0';
    }
    name_prefix = last_slash + 1;
    plen = strlen(name_prefix);
  }

  /* Résoudre le chemin dans user_cwd */
  char full_dir[512];
  if (dir_path[0] == '/') {
    snprintf(full_dir, sizeof(full_dir), "%s", dir_path);
  } else {
    snprintf(full_dir, sizeof(full_dir), "%s/%s", user_cwd, dir_path);
  }

  DIR *dir = opendir(full_dir);
  if (!dir) return;
  struct dirent *ent;
  while ((ent = readdir(dir)) != NULL && complete_count < COMPLETE_MAX) {
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
      continue;
    if (plen == 0 || strncmp(ent->d_name, name_prefix, plen) == 0) {
      /* Ajouter un / si c'est un dossier */
      char entry[512];
      if (last_slash) {
        char dir_part[256];
        int dlen = (int)(last_slash - prefix) + 1;
        if (dlen > (int)sizeof(dir_part) - 1) dlen = sizeof(dir_part) - 1;
        memcpy(dir_part, prefix, dlen);
        dir_part[dlen] = '\0';
        snprintf(entry, sizeof(entry), "%s%s", dir_part, ent->d_name);
      } else {
        snprintf(entry, sizeof(entry), "%s", ent->d_name);
      }

      /* Vérifier si c'est un répertoire */
      char check_path[768];
      snprintf(check_path, sizeof(check_path), "%s/%s", full_dir, ent->d_name);
      struct stat st;
      if (stat(check_path, &st) == 0 && S_ISDIR(st.st_mode)) {
        int elen = strlen(entry);
        if (elen < (int)sizeof(entry) - 2) {
          entry[elen] = '/';
          entry[elen + 1] = '\0';
        }
      }

      snprintf(completions[complete_count],
               sizeof(completions[0]), "%s", entry);
      complete_count += 1;
    }
  }
  closedir(dir);
}

/* ══════════════════════════════════════════════════════════════
 * Logique de complétion
 * ══════════════════════════════════════════════════════════════ */

/* Trouve le plus long préfixe commun parmi les résultats */
static int common_prefix_len(void) {
  if (complete_count <= 1) return 0;
  int len = strlen(completions[0]);
  for (int i = 1; i < complete_count; i += 1) {
    int j = 0;
    while (j < len && completions[i][j] == completions[0][j]) j += 1;
    len = j;
  }
  return len;
}

void complete_input(char *input_cmd, int *input_pos) {
  if (*input_pos == 0) return;

  /* Extraire le dernier mot */
  int word_start = *input_pos;
  while (word_start > 0 && input_cmd[word_start - 1] != ' ')
    word_start -= 1;

  char word[INPUT_MAX] = {0};
  int wlen = *input_pos - word_start;
  if (wlen >= (int)sizeof(word)) wlen = sizeof(word) - 1;
  memcpy(word, input_cmd + word_start, wlen);
  word[wlen] = '\0';

  /* Premier mot → commande, sinon → fichier */
  int is_first_word = (word_start == 0);
  if (is_first_word) {
    complete_command(word);
  } else {
    complete_path(word);
  }

  if (complete_count == 0) return;

  if (complete_count == 1) {
    /* Complétion unique : remplacer le mot */
    const char *result = completions[0];
    int rlen = strlen(result);

    /* Effacer le mot courant */
    for (int i = 0; i < wlen; i += 1) term_putc('\b');

    /* Écrire le résultat */
    for (int i = 0; i < rlen && word_start + i < INPUT_MAX - 2; i += 1) {
      input_cmd[word_start + i] = result[i];
      term_putc(result[i]);
    }

    /* Ajouter un espace si ce n'est pas un dossier */
    int total = word_start + rlen;
    if (total < INPUT_MAX - 2 && result[rlen - 1] != '/') {
      input_cmd[total] = ' ';
      total += 1;
      term_putc(' ');
    }
    input_cmd[total] = '\0';
    *input_pos = total;
    last_tab_was_tab = 0;
  } else {
    /* Plusieurs résultats : compléter le préfixe commun */
    int cplen = common_prefix_len();
    if (cplen > wlen) {
      /* Il y a du préfixe commun à ajouter */
      for (int i = 0; i < wlen; i += 1) term_putc('\b');
      for (int i = 0; i < cplen && word_start + i < INPUT_MAX - 2; i += 1) {
        input_cmd[word_start + i] = completions[0][i];
        term_putc(completions[0][i]);
      }
      int total = word_start + cplen;
      input_cmd[total] = '\0';
      *input_pos = total;
      last_tab_was_tab = 0;
    } else if (last_tab_was_tab) {
      /* Double-Tab : afficher la liste */
      if (complete_count > COMPLETE_THRESHOLD) {
        /* Demander confirmation */
        char msg[64];
        snprintf(msg, sizeof(msg),
                 "\nAfficher les %d possibilites ? [y/N] ",
                 complete_count);
        term_print(msg);
        render_term();
        awaiting_confirm = 1;
      } else {
        term_putc('\n');
        for (int i = 0; i < complete_count; i += 1) {
          term_print(completions[i]);
          term_print("  ");
        }
        term_putc('\n');
        term_print(prompt_str);
        /* Ré-afficher la saisie courante */
        for (int i = 0; i < *input_pos; i += 1) {
          term_putc(input_cmd[i]);
        }
      }
      last_tab_was_tab = 0;
    } else {
      last_tab_was_tab = 1;
    }
  }
}

/* Affiche toutes les complétions (après confirmation Y) */
void show_completions(char *input_cmd, int *input_pos) {
  term_putc('\n');
  for (int i = 0; i < complete_count; i += 1) {
    term_print(completions[i]);
    term_print("  ");
  }
  term_putc('\n');
  term_print(prompt_str);
  for (int i = 0; i < *input_pos; i += 1) {
    term_putc(input_cmd[i]);
  }
}
