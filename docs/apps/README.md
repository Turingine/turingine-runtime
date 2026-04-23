# Apps existantes

Cette section documente les applications actuellement presentes dans la plateforme, leur role et leurs interactions.

## Liste rapide

- `homescreen`: launcher kiosk principal, rendu natif DRM, gestion des boutons via `config/homescreen.conf`.
- `x11_bridge`: pont entre un display X11 virtuel (Xvfb) et l'ecran physique DRM.
- `terminal`: terminal natif minimal, rendu texte direct dans framebuffer DRM.

## Flux global

1. Le service systemd lance `scripts/screen.sh`.
2. Le script demarre `./.out/bin/homescreen`.
3. `homescreen` reste en DRM natif.
4. Sur une app `STD`, `homescreen` lance Xvfb/Openbox + `x11_bridge`.
5. Sur une app `NATIVE`, `homescreen` execute la commande directement.

## Docs detaillees

- [Homescreen](homescreen.md)
- [X11 Bridge](x11-bridge.md)
- [Terminal](terminal.md)
