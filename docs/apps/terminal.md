# Terminal

## Role

`apps/terminal/main.c` est un terminal natif minimal, sans X11.

- rendu texte direct en framebuffer DRM,
- lecture clavier via evdev,
- execution de commandes shell avec affichage progressif de la sortie.

## Fonctionnalites

- buffer texte fixe (`ROWS x COLS`),
- gestion simple du curseur, backspace, retour ligne,
- mini keymap QWERTY (normal + shift),
- commande speciale `exit` pour quitter.

## Pipeline

1. Init DRM.
2. Detecte les peripheriques input.
3. Boucle de rendu:
   - dessine texte + curseur,
   - lit les touches,
   - met a jour la ligne de commande,
   - execute via `popen()` a `Enter`.

## Limitations connues

- keymap simplifiee (pas de layout international complet).
- shell non interactif complet (pas de pseudo-terminal).
- buffer texte a taille fixe.

## Usage typique

- depuis le homescreen avec un bouton `NATIVE` pointant vers `./.out/bin/terminal`.
