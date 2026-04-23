#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/XTest.h>
#include <errno.h>
#include <fcntl.h>
#include <libevdev/libevdev.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>

#include "drm_display.h"
#include "evdev_input.h"

static volatile int keep_running = 1;

void handle_sigint(int sig) {
  (void)sig;
  keep_running = 0;
}

// Périphériques d'entrée
static struct input_devices in_devs;

int main(void) {
  // 1. Initialiser le DRM physique (Turingine Screen)
  struct drm_display drm_disp;
  uint32_t w_drm, h_drm, stride_drm;

  if (drm_display_init(&drm_disp, &w_drm, &h_drm, &stride_drm) != 0) {
    fprintf(stderr, "Erreur initialisation DRM (Es-tu bien root ou membre du "
                    "groupe video ?)\n");
    return 1;
  }
  printf("DRM Ok: %ux%u (stride en px: %u)\n", w_drm, h_drm, stride_drm);

  // 2. Connexion à X11 (l'écran virtuel Xvfb :99)
  // XOpenDisplay utilisera par défaut la variable d'environnement DISPLAY.
  Display *dpy = XOpenDisplay(NULL);
  if (!dpy) {
    fprintf(stderr, "Impossible d'ouvrir l'affichage X11 (DISPLAY=:99 manquant "
                    "ou Xvfb éteint ?)\n");
    drm_display_shutdown(&drm_disp);
    return 1;
  }

  int screen = DefaultScreen(dpy);
  Window root = RootWindow(dpy, screen);

  // On assume que le Display virtuel a la même taille que l'écran (ex:
  // 1920x1080)
  uint32_t w_x11 = DisplayWidth(dpy, screen);
  uint32_t h_x11 = DisplayHeight(dpy, screen);
  printf("X11 Ok: %ux%u\n", w_x11, h_x11);

  int depth = DefaultDepth(dpy, screen);
  if (depth != 24) {
    fprintf(stderr,
            "Attention: la profondeur X11 n'est pas 24 bits (depth=%d). ARGB32 "
            "requis.\n",
            depth);
  }

  // 3. Préparer la mémoire partagée X11 et le Noyau Linux (XShm)
  XShmSegmentInfo shminfo;
  XImage *image = XShmCreateImage(dpy, DefaultVisual(dpy, screen), depth,
                                  ZPixmap, NULL, &shminfo, w_x11, h_x11);
  if (!image) {
    fprintf(stderr, "Erreur XShmCreateImage\n");
    XCloseDisplay(dpy);
    drm_display_shutdown(&drm_disp);
    return 1;
  }

  // Allocation du segment de mémoire partagée noyau (IPC)
  shminfo.shmid = shmget(IPC_PRIVATE, image->bytes_per_line * image->height,
                         IPC_CREAT | 0777);
  if (shminfo.shmid < 0) {
    perror("shmget");
    image->data = NULL;
    XDestroyImage(image);
    XCloseDisplay(dpy);
    drm_display_shutdown(&drm_disp);
    return 1;
  }

  // On attache la mémoire à notre processus C
  shminfo.shmaddr = image->data = shmat(shminfo.shmid, 0, 0);
  shminfo.readOnly = False;

  if (image->data == (char *)-1) {
    perror("shmat");
    XDestroyImage(image);
    XCloseDisplay(dpy);
    drm_display_shutdown(&drm_disp);
    return 1;
  }

  // On dit au Serveur X11 (Xvfb) de pouvoir écrire dans cette zone
  if (!XShmAttach(dpy, &shminfo)) {
    fprintf(stderr, "Erreur XShmAttach\n");
    return 1;
  }
  XSync(dpy, False);

  // On marque le segment pour destruction automatique une fois détaché
  shmctl(shminfo.shmid, IPC_RMID, 0);

  // Permettre l'arrêt propre avec Ctrl+C
  signal(SIGINT, handle_sigint);
  signal(SIGTERM, handle_sigint);

  // 4. Boucle principale Turingine (Capture -> DRM)
  printf("Démarrage de la boucle de rendu Turingine (Appuies sur Ctrl+C pour "
         "quitter)...\n");
  input_devices_detect(&in_devs);

  // Position absolue de la souris (centre écran au démarrage)
  int mouse_x = w_x11 / 2;
  int mouse_y = h_x11 / 2;

  while (keep_running) {
    // A. Capture instantanée de l'image du serveur Xvfb via mémoire partagée
    XShmGetImage(dpy, root, image, 0, 0, AllPlanes);
    struct input_event ev;
    int rc;

    // --- SOURIS ---
    while (in_devs.mouse) {
      rc = libevdev_next_event(in_devs.mouse, LIBEVDEV_READ_FLAG_NORMAL, &ev);

      if (rc == LIBEVDEV_READ_STATUS_SYNC) {
        // Vider la file de sync avant de reprendre
        while (rc == LIBEVDEV_READ_STATUS_SYNC)
          rc = libevdev_next_event(in_devs.mouse, LIBEVDEV_READ_FLAG_SYNC, &ev);
        continue;
      }
      if (rc != LIBEVDEV_READ_STATUS_SUCCESS)
        break;

      if (ev.type == EV_REL) {
        if (ev.code == REL_X) {
          mouse_x += ev.value;
        }
        if (ev.code == REL_Y) {
          mouse_y += ev.value;
        }
        if (ev.code == REL_WHEEL) {
          // Sous X11, la molette correspond aux clics "hardware" 4 (Haut) et 5
          // (Bas).
          int button = (ev.value > 0) ? 4 : 5;
          int abs_val = (ev.value > 0) ? ev.value : -ev.value;

          for (int i = 0; i < abs_val; i += 1) {
            XTestFakeButtonEvent(dpy, button, True, CurrentTime);
            XTestFakeButtonEvent(dpy, button, False, CurrentTime);
          }
        }
        // Clamp pour ne pas sortir de l'écran
        if (mouse_x < 0) {
          mouse_x = 0;
        }
        if (mouse_x >= (int)w_x11) {
          mouse_x = w_x11 - 1;
        }
        if (mouse_y < 0) {
          mouse_y = 0;
        }
        if (mouse_y >= (int)h_x11) {
          mouse_y = h_x11 - 1;
        }

        XTestFakeMotionEvent(dpy, screen, mouse_x, mouse_y, CurrentTime);
      } else if (ev.type == EV_KEY) {
        if (ev.code == BTN_LEFT)
          XTestFakeButtonEvent(dpy, 1, ev.value == 1, CurrentTime);
        else if (ev.code == BTN_RIGHT)
          XTestFakeButtonEvent(dpy, 3, ev.value == 1, CurrentTime);
        else if (ev.code == BTN_MIDDLE)
          XTestFakeButtonEvent(dpy, 2, ev.value == 1, CurrentTime);
      }
    }

    // --- CLAVIER ---
    while (in_devs.kb) {
      rc = libevdev_next_event(in_devs.kb, LIBEVDEV_READ_FLAG_NORMAL, &ev);

      if (rc == LIBEVDEV_READ_STATUS_SYNC) {
        while (rc == LIBEVDEV_READ_STATUS_SYNC)
          rc = libevdev_next_event(in_devs.kb, LIBEVDEV_READ_FLAG_SYNC, &ev);
        continue;
      }
      if (rc != LIBEVDEV_READ_STATUS_SUCCESS)
        break;

      if (ev.type == EV_KEY)
        XTestFakeKeyEvent(dpy, ev.code + 8, ev.value != 0, CurrentTime);
    }

    XFlush(dpy);

    // B. Copie (blit) vers le DRM physique
    uint32_t stride_pixels_x11 = image->bytes_per_line / 4;

    // --- DESSIN DU CURSEUR TURINGINE (Croisée rouge + Centre blanc) ---
    uint32_t *pixels = (uint32_t *)image->data;
    uint32_t cursor_red = 0xFFFF0000;
    uint32_t cursor_white = 0xFFFFFFFF;

    for (int i = -10; i <= 10; i += 1) {
      int hx = mouse_x + i;
      if (hx >= 0 && hx < (int)w_x11)
        pixels[mouse_y * stride_pixels_x11 + hx] = cursor_red;
      int vy = mouse_y + i;
      if (vy >= 0 && vy < (int)h_x11)
        pixels[vy * stride_pixels_x11 + mouse_x] = cursor_red;
    }
    if (mouse_x >= 0 && mouse_x < (int)w_x11 && mouse_y >= 0 &&
        mouse_y < (int)h_x11)
      pixels[mouse_y * stride_pixels_x11 + mouse_x] = cursor_white;

    drm_display_blit_argb32(&drm_disp, pixels, stride_pixels_x11);

    // C. Présentation à l'écran HDMI physique
    drm_display_present(&drm_disp);

    // D. ~60 FPS
    usleep(16666);
  }

  printf("\nArrêt propre...\n");

  // Code de nettoyage strict pour éviter les leaks de mémoire IPC !
  XShmDetach(dpy, &shminfo);
  XDestroyImage(image);
  shmdt(shminfo.shmaddr);
  XCloseDisplay(dpy);
  drm_display_shutdown(&drm_disp);

  return 0;
}