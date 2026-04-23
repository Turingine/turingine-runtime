# Turingine Platform

Plateforme interne pour les applications TURINGINE (asso), orientee rendu natif DRM/KMS, gestion d'entrees evdev et mode kiosk hybride avec pont X11.

## Structure

- `core/display`: rendu bas niveau (`drm_display`)
- `core/input`: detection/gestion souris + clavier (`evdev_input`)
- `core/common`: ressources communes (ex: `font8x16.h`)
- `apps/homescreen`: launcher kiosk principal
- `apps/x11_bridge`: pont entre Xvfb et ecran DRM physique
- `apps/terminal`: terminal natif minimal
- `config`: configuration runtime (`homescreen.conf`)
- `packaging/systemd`: service systemd
- `packaging/openbox`: config Openbox
- `scripts`: scripts de lancement

## Build

```bash
make
```

Les binaires sont produits dans `.out/bin/`.

## Run (exemple)

```bash
./.out/bin/homescreen
```
