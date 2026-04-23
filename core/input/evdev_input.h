#pragma once

#include <libevdev/libevdev.h>

struct input_devices {
    struct libevdev *mouse;
    int mouse_fd;
    struct libevdev *kb;
    int kb_fd;
};

/* Détecte et ouvre le meilleur clavier et la meilleure souris.
 * Retourne 0 en cas de succès (au moins un device trouvé), -1 sinon.
 * Remplit la structure fournie. */
int input_devices_detect(struct input_devices *devs);

/* Libère les périphériques et ferme les descripteurs de fichiers. */
void input_devices_release(struct input_devices *devs);
