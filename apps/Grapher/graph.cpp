#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

struct Image {
  int width, height;
  std::vector<uint32_t> pixels; // ARGB : 0xAARRGGBB

  Image(int w, int h) : width(w), height(h), pixels(w * h, 0xFF000000) {}

  // TODO: Implémenter set_pixel
  void set_pixel(int x, int y, uint32_t color) {
    // Vérifier les bornes, puis écrire dans pixels[y * width + x]
    if (x >= 0 && x < width && y >= 0 && y < height) {
      pixels[y * width + x] = color;
    }
  }

  // TODO: Implémenter clear
  void clear(uint32_t color = 0xFF000000) {
    // Remplir tout le vecteur avec la couleur
    std::fill(pixels.begin(), pixels.end(), color);
  }

  // Mélange un pixel coloré avec le fond existant
  // alpha : 0 = totalement transparent, 255 = totalement opaque
  void blend_pixel(int x, int y, uint32_t color, uint8_t alpha) {
    if (x < 0 || x >= width || y < 0 || y >= height)
      return;

    uint32_t bg = pixels[y * width + x];

    // Décompose la couleur de premier plan (fg) et le fond (bg)
    uint32_t fg_r = (color >> 16) & 0xFF;
    uint32_t fg_g = (color >> 8) & 0xFF;
    uint32_t fg_b = color & 0xFF;
    uint32_t bg_r = (bg >> 16) & 0xFF;
    uint32_t bg_g = (bg >> 8) & 0xFF;
    uint32_t bg_b = bg & 0xFF;

    // TODO: Calcule le mélange pour chaque canal
    // Formule : result = (fg * alpha + bg * (255 - alpha)) / 255
    // Puis recombine en 0xFF000000 | (r << 16) | (g << 8) | b

    uint32_t r = (fg_r * alpha + bg_r * (255 - alpha)) / 255;
    uint32_t g = (fg_g * alpha + bg_g * (255 - alpha)) / 255;
    uint32_t b = (fg_b * alpha + bg_b * (255 - alpha)) / 255;
    pixels[y * width + x] = 0xFF000000 | (r << 16) | (g << 8) | b;
  }

  void fill_rect(int x, int y, int w, int h, uint32_t color) {
    for (int row = y; row < y + h; row++)
      for (int col = x; col < x + w; col++)
        set_pixel(col, row, color);
  }

  void draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = x1 - x0;
    int dy = y1 - y0;
    int steps = std::max(std::abs(dx), std::abs(dy));
    if (steps == 0) {
      set_pixel(x0, y0, color);
      return;
    }
    double x_inc = (double)dx / steps;
    double y_inc = (double)dy / steps;

    double x = x0;
    double y = y0;
    for (int i = 0; i <= steps; i++) {
      set_pixel((int)x, (int)y, color);
      x += x_inc;
      y += y_inc;
    }
  }

  // Rectangle contour (juste les 4 bords)
  void draw_rect(int x, int y, int w, int h, uint32_t color) {
    draw_line(x, y, x + w - 1, y, color);
    draw_line(x, y + h - 1, x + w - 1, y + h - 1, color);
    draw_line(x, y, x, y + h - 1, color);
    draw_line(x + w - 1, y, x + w - 1, y + h - 1, color);
  }

  void draw_line_wu(int x0, int y0, int x1, int y1, uint32_t color) {
    // On travaille en double pour la précision
    auto ipart = [](double x) -> int { return (int)std::floor(x); };
    auto fpart = [](double x) -> double { return x - std::floor(x); };
    auto rfpart = [](double x) -> double { return 1.0 - (x - std::floor(x)); };

    bool steep = std::abs(y1 - y0) > std::abs(x1 - x0);

    // Si la pente est forte, on transpose (on parcourt selon y)
    if (steep) {
      std::swap(x0, y0);
      std::swap(x1, y1);
    }
    // On s'assure de parcourir de gauche à droite
    if (x0 > x1) {
      std::swap(x0, x1);
      std::swap(y0, y1);
    }

    int dx = x1 - x0;
    int dy = y1 - y0;
    double gradient = (dx == 0) ? 1.0 : (double)dy / dx;

    // --- Premier point ---
    double y_inter = (double)y0;

    // --- Boucle principale ---
    // TODO: Pour chaque x de x0 à x1 :
    //   1. Calcule ipart(y_inter) → le pixel "bas"
    //   2. Calcule fpart(y_inter) → la partie fractionnaire
    //   3. Si steep : blend_pixel(ipart(y_inter), x, ..., rfpart * 255)
    //                 blend_pixel(ipart(y_inter)+1, x, ..., fpart * 255)
    //      Sinon :    blend_pixel(x, ipart(y_inter), ..., rfpart * 255)
    //                 blend_pixel(x, ipart(y_inter)+1, ..., fpart * 255)
    //   4. y_inter += gradient

    for (int x = x0; x <= x1; x++) {
      if (steep) {
        blend_pixel(ipart(y_inter), x, color, (uint8_t)(rfpart(y_inter) * 255));
        blend_pixel(ipart(y_inter) + 1, x, color, (uint8_t)(fpart(y_inter) * 255));
      } else {
        blend_pixel(x, ipart(y_inter), color, (uint8_t)(rfpart(y_inter) * 255));
        blend_pixel(x, ipart(y_inter) + 1, color, (uint8_t)(fpart(y_inter) * 255));
      }
      y_inter += gradient;
    }
  }

  // Sauvegarde au format PPM (déjà fait pour toi)
  void save_ppm(const char *filename) const {
    FILE *f = fopen(filename, "wb");
    if (!f)
      return;
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    for (int i = 0; i < width * height; i++) {
      uint8_t r = (pixels[i] >> 16) & 0xFF;
      uint8_t g = (pixels[i] >> 8) & 0xFF;
      uint8_t b = pixels[i] & 0xFF;
      fwrite(&r, 1, 1, f);
      fwrite(&g, 1, 1, f);
      fwrite(&b, 1, 1, f);
    }
    fclose(f);
  }
};

struct Viewport {
  // Fenêtre mathématique (ce qu'on voit du plan mathématique)
  double x_min, x_max;
  double y_min, y_max;

  // Dimensions de l'image en pixels
  int pixel_width, pixel_height;

  Viewport(double xmin, double xmax, double ymin, double ymax, int pw, int ph)
      : x_min(xmin), x_max(xmax), y_min(ymin), y_max(ymax), pixel_width(pw), pixel_height(ph) {}

  // Convertit une coordonnée mathématique → pixel
  int math_to_pixel_x(double x) const { return (int)std::round((x - x_min) / (x_max - x_min) * (pixel_width - 1)); }
  int math_to_pixel_y(double y) const { return (int)std::round((y_max - y) / (y_max - y_min) * (pixel_height - 1)); }

  // Convertit un pixel → coordonnée mathématique
  double pixel_to_math_x(int px) const { return x_min + (double)px / (pixel_width - 1) * (x_max - x_min); }
  double pixel_to_math_y(int py) const { return y_max - (double)py / (pixel_height - 1) * (y_max - y_min); }
};

void draw_axes(Image &img, const Viewport &vp, uint32_t color) {
  // Axe X : la droite y = 0 (si elle est visible)
  if (vp.y_min <= 0.0 && 0.0 <= vp.y_max) {
    int py = vp.math_to_pixel_y(0.0);
    // TODO: Dessine une ligne horizontale sur toute la largeur à la hauteur py
    img.draw_line(vp.math_to_pixel_x(vp.x_min), py, vp.math_to_pixel_x(vp.x_max), py, color);
  }

  // Axe Y : la droite x = 0 (si elle est visible)
  if (vp.x_min <= 0.0 && 0.0 <= vp.x_max) {
    int px = vp.math_to_pixel_x(0.0);
    // TODO: Dessine une ligne verticale sur toute la hauteur à la colonne px
    img.draw_line(px, vp.math_to_pixel_y(vp.y_min), px, vp.math_to_pixel_y(vp.y_max), color);
  }
}

void draw_grid(Image &img, const Viewport &vp, double step, uint32_t color) {
  // Lignes verticales (x = ..., -2, -1, 0, 1, 2, ...)
  for (double x = std::ceil(vp.x_min / step) * step; x <= vp.x_max; x += step) {
    int px = vp.math_to_pixel_x(x);
    for (int py = 0; py < img.height; py++)
      img.blend_pixel(px, py, color, 40); // très transparent
  }

  // TODO: Lignes horizontales (même principe avec y)
  for (double y = std::ceil(vp.y_min / step) * step; y <= vp.y_max; y += step) {
    int py = vp.math_to_pixel_y(y);
    for (int px = 0; px < img.width; px++)
      img.blend_pixel(px, py, color, 40);
  }
}

void plot_function_points(Image &img, const Viewport &vp, std::function<double(double)> f, uint32_t color) {
  for (int px = 0; px < img.width; px++) {
    double x = vp.pixel_to_math_x(px);
    double y = f(x);

    // Vérifier que y est un nombre fini (pas NaN, pas infini)
    if (!std::isfinite(y))
      continue;

    int py = vp.math_to_pixel_y(y);
    img.set_pixel(px, py, color);
  }
}

int main() {
  Image img(800, 600);
  img.set_pixel(400, 300, 0xFFFF0000);
  img.draw_rect(100, 100, 200, 150, 0xFF0000FF);
  img.fill_rect(125, 125, 175, 125, 0xFF00FF00);
  img.draw_line(0, 0, 800, 600, 0xFFFFFF00);
  img.draw_line_wu(10, 10, 790, 600, 0xFFFFFF00);
  img.save_ppm("test.ppm");
  return 0;
}