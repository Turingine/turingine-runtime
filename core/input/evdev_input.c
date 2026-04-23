#include "evdev_input.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int is_primary_interface(struct libevdev *dev) {
  const char *phys = libevdev_get_phys(dev);
  if (!phys)
    return 1;
  const char *p = strstr(phys, "/input");
  if (!p)
    return 1;
  int iface_num = atoi(p + 6);
  return (iface_num == 0) ? 1 : 0;
}

static int score_keyboard(struct libevdev *dev) {
  if (!libevdev_has_event_type(dev, EV_REP))
    return 0;
  int score = 0;
  const int alpha[] = {KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U,
                       KEY_I, KEY_O, KEY_P, KEY_A, KEY_S, KEY_D, KEY_F,
                       KEY_G, KEY_H, KEY_J, KEY_K, KEY_L, KEY_Z, KEY_X,
                       KEY_C, KEY_V, KEY_B, KEY_N, KEY_M};
  for (int i = 0; i < 26; i += 1)
    if (libevdev_has_event_code(dev, EV_KEY, alpha[i]))
      score += 1;
  if (libevdev_has_event_code(dev, EV_KEY, KEY_SPACE))
    score += 2;
  if (libevdev_has_event_code(dev, EV_KEY, KEY_ENTER))
    score += 2;
  if (libevdev_has_event_code(dev, EV_KEY, KEY_BACKSPACE))
    score += 2;
  if (libevdev_has_event_code(dev, EV_KEY, KEY_LEFTSHIFT))
    score += 2;
  if (libevdev_has_event_code(dev, EV_KEY, KEY_LEFTCTRL))
    score += 1;
  if (libevdev_has_event_code(dev, EV_LED, LED_CAPSL))
    score += 10;
  if (libevdev_has_event_code(dev, EV_LED, LED_NUML))
    score += 5;
  if (libevdev_has_event_code(dev, EV_LED, LED_SCROLLL))
    score += 5;
  if (!is_primary_interface(dev))
    score -= 40;
  if (libevdev_has_event_code(dev, EV_KEY, BTN_LEFT) &&
      libevdev_has_event_type(dev, EV_REL))
    score -= 20;
  return score;
}

static int score_mouse(struct libevdev *dev) {
  if (!libevdev_has_event_type(dev, EV_REL))
    return 0;
  if (!libevdev_has_event_code(dev, EV_REL, REL_X))
    return 0;
  if (!libevdev_has_event_code(dev, EV_REL, REL_Y))
    return 0;
  if (!libevdev_has_event_code(dev, EV_KEY, BTN_LEFT))
    return 0;
  if (libevdev_has_event_type(dev, EV_REP))
    return 0;
  if (libevdev_has_event_type(dev, EV_LED))
    return 0;
  int score = 10;
  if (libevdev_has_event_code(dev, EV_KEY, BTN_RIGHT))
    score += 3;
  if (libevdev_has_event_code(dev, EV_KEY, BTN_MIDDLE))
    score += 2;
  if (libevdev_has_event_code(dev, EV_REL, REL_WHEEL))
    score += 3;
  if (libevdev_has_event_code(dev, EV_REL, REL_HWHEEL))
    score += 1;
  if (libevdev_has_event_code(dev, EV_REL, REL_WHEEL_HI_RES))
    score += 1;
  if (is_primary_interface(dev))
    score += 5;
  if (libevdev_has_event_code(dev, EV_KEY, KEY_SPACE))
    score -= 15;
  return score;
}

int input_devices_detect(struct input_devices *devs) {
  if (!devs)
    return -1;
  memset(devs, 0, sizeof(*devs));
  devs->mouse_fd = -1;
  devs->kb_fd = -1;

  char dev_path[64];
  int best_mouse_score = 0;
  int best_kb_score = 0;

  printf("Recherche automatique de Clavier et Souris dans /dev/input...\n");
  for (int i = 0; i < 64; i += 1) {
    snprintf(dev_path, sizeof(dev_path), "/dev/input/event%d", i);
    int temp_fd = open(dev_path, O_RDONLY | O_NONBLOCK);
    if (temp_fd < 0)
      continue;

    struct libevdev *temp_dev;
    if (libevdev_new_from_fd(temp_fd, &temp_dev) != 0) {
      close(temp_fd);
      continue;
    }

    int ms = score_mouse(temp_dev);
    int ks = score_keyboard(temp_dev);

    if (ms > 0 || ks > 0) {
      printf("  [%s] %-40s | souris: %2d | clavier: %2d\n", dev_path,
             libevdev_get_name(temp_dev), ms, ks);
    }

    if (ms > best_mouse_score) {
      if (devs->mouse) {
        libevdev_free(devs->mouse);
        close(devs->mouse_fd);
      }
      devs->mouse = temp_dev;
      devs->mouse_fd = temp_fd;
      best_mouse_score = ms;
    }
    if (ks > best_kb_score) {
      if (devs->kb) {
        libevdev_free(devs->kb);
        close(devs->kb_fd);
      }
      devs->kb = temp_dev;
      devs->kb_fd = temp_fd;
      best_kb_score = ks;
    }
    if (temp_dev != devs->mouse && temp_dev != devs->kb) {
      libevdev_free(temp_dev);
      close(temp_fd);
    }
  }

  if (devs->mouse) {
    printf("Souris sélectionnée : %s\n", libevdev_get_name(devs->mouse));
    libevdev_grab(devs->mouse, LIBEVDEV_GRAB);
  } else {
    fprintf(stderr, "AUCUNE souris détectée !\n");
  }
  
  if (devs->kb) {
    printf("Clavier sélectionné : %s\n", libevdev_get_name(devs->kb));
    libevdev_grab(devs->kb, LIBEVDEV_GRAB);
  } else {
    fprintf(stderr, "AUCUN clavier détecté !\n");
  }

  return (devs->mouse || devs->kb) ? 0 : -1;
}

void input_devices_release(struct input_devices *devs) {
  if (!devs)
    return;
  if (devs->mouse) {
    libevdev_grab(devs->mouse, LIBEVDEV_UNGRAB);
    libevdev_free(devs->mouse);
    close(devs->mouse_fd);
    devs->mouse = NULL;
    devs->mouse_fd = -1;
  }
  if (devs->kb) {
    libevdev_grab(devs->kb, LIBEVDEV_UNGRAB);
    libevdev_free(devs->kb);
    close(devs->kb_fd);
    devs->kb = NULL;
    devs->kb_fd = -1;
  }
}
