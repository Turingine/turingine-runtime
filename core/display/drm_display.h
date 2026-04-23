/*
 * Petite lib pour afficher directement dans le buffer graphique via DRM/KMS.
 *
 * API simple :
 *
 *   struct drm_display d;
 *   uint32_t w, h, stride;
 *
 *   if (drm_display_init(&d, &w, &h, &stride) == 0) {
 *       // pixels ARGB8888, taille h x stride
 *       uint32_t *buf = malloc(stride * h * sizeof(uint32_t));
 *       // ... remplir buf ...
 *       drm_display_blit_argb32(&d, buf, stride);
 *       drm_display_present(&d);     // envoie sur l'écran
 *       drm_display_shutdown(&d);
 *   }
 */

#pragma once

#include <stdint.h>

struct drm_display {
    int fd;
    uint32_t conn_id;
    uint32_t crtc_id;

    uint32_t fb_id;
    void *map;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;   /* en octets */
    uint32_t format;  /* DRM_FORMAT_* */
};

/* Initialise l'affichage, crée un buffer et le mappe.
 * Retourne 0 en cas de succès.
 * - out_w / out_h : résolution logique de l'écran
 * - out_stride    : nombre de pixels par ligne dans le buffer fourni à blit
 */
int drm_display_init(struct drm_display *d,
                     uint32_t *out_w,
                     uint32_t *out_h,
                     uint32_t *out_stride);

/* Copie une image ARGB8888 (32 bits) dans le buffer interne.
 * - pixels : matrice [h][stride] de pixels ARGB8888
 * - stride : nombre de pixels par ligne dans la matrice source
 */
int drm_display_blit_argb32(struct drm_display *d,
                            const uint32_t *pixels,
                            uint32_t stride);

/* Affiche le contenu actuel du buffer (DRM/KMS set/crtc). */
int drm_display_present(struct drm_display *d);

/* Libère toutes les ressources. Toujours appelable, même en cas d'échec partiel. */
void drm_display_shutdown(struct drm_display *d);

