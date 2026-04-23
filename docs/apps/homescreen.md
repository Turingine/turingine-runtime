# Homescreen

## Role

`apps/homescreen/main.c` est le launcher principal.

- Rend l'UI directement via DRM/KMS.
- Lit la configuration des boutons.
- Gere souris/clavier via `libevdev`.
- Lance les apps `NATIVE` ou `STD` selon le type de bouton.

## Dependances principales

- `core/display/drm_display.*`
- `core/input/evdev_input.*`
- `core/common/font8x16.h`
- Binaire externe pour mode STD: `Xvfb`, `openbox`, apps lancees par commande shell.

## Configuration

Fichier principal: `config/homescreen.conf`  
Fallback: `homescreen.conf` (compatibilite)

Format d'une ligne:

```txt
TYPE | COULEUR_HEX | LABEL | COMMANDE
```

- `TYPE`: `NATIVE` ou `STD`
- `COULEUR_HEX`: ex. `0x004400`
- `LABEL`: texte du bouton
- `COMMANDE`: commande shell executee au clic

## Comportement au clic

- `NATIVE`: execution directe de la commande.
- `STD`: lance une pile temporaire:
  - `Xvfb :99`
  - `openbox`
  - app cible
  - `./.out/bin/x11_bridge`

Puis nettoyage et retour au homescreen.

## Points d'attention

- Lancement en root attendu pour acces a `/dev/dri/*` et `/dev/input/*`.
- Les commandes de `homescreen.conf` sont executees via `system()`.
- Le script de lancement STD copie `packaging/openbox/rc.xml` vers `/root/.config/openbox/rc.xml`.
