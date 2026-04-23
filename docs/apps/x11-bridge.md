# X11 Bridge

## Role

`apps/x11_bridge/main.c` fait le pont entre:

- un display X11 virtuel (souvent `:99`, typiquement Xvfb),
- et l'ecran physique pilote par DRM/KMS.

## Fonctionnement

1. Initialise le display DRM (`drm_display_init`).
2. Ouvre le serveur X11 (`XOpenDisplay`).
3. Cree une image partagee via XShm.
4. Dans la boucle:
   - capture l'image X11 (`XShmGetImage`),
   - traduit les events evdev en events X11 (`XTest*`),
   - dessine un curseur overlay,
   - blit vers DRM (`drm_display_blit_argb32`),
   - presente a l'ecran (`drm_display_present`).

## Dependances

- `libX11`, `libXext`, `libXtst`
- `core/display/drm_display.*`
- `core/input/evdev_input.*`

## Entrees

- Souris: mouvement, boutons, molette -> injectes dans X11.
- Clavier: key events -> `XTestFakeKeyEvent`.

## Points d'attention

- Suppose un serveur X disponible (ex: `DISPLAY=:99`).
- Le nettoyage IPC/XShm est critique pour eviter les fuites.
- En production, est generalement lance par `homescreen` en mode app `STD`.
