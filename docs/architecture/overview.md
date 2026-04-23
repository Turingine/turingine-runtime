# Architecture Overview

## Runtime flow

1. `homescreen` demarre en DRM direct.
2. Les entrees `evdev` pilotent le curseur et les interactions.
3. Si app native: execution directe en DRM.
4. Si app STD/X11: lancement Xvfb/Openbox + `x11_bridge`.
5. `x11_bridge` capture XShm et blit vers DRM.

## Core modules

- `core/display/drm_display.*`: init DRM/KMS, blit, present, shutdown.
- `core/input/evdev_input.*`: detection des meilleurs peripheriques et lecture evenementielle.
- `core/common/font8x16.h`: police bitmap 8x16 pour les UIs natives.
