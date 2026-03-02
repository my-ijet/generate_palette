// Компиляция: cc generate_palette.c -lm -o generate_palette

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ctype.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// Параметры по умолчанию
#define DEFAULT_WIDTH 9
#define DEFAULT_HUE_ROWS 24
#define MIN_WIDTH 3
#define MIN_HUE_ROWS 1

// Глобальные настройки
int width = DEFAULT_WIDTH;
int hueRows = DEFAULT_HUE_ROWS;
int height;
int use_okhsl = 0;  // по умолчанию HSL

typedef unsigned char u8;
typedef const char* stringz;

typedef struct {
    u8 r, g, b;
} RGB;

typedef struct {
    double r, g, b;
} sRGB;

typedef struct {
    double h, s, l;
} HSL;

inline
double deg_to_rad(double deg) {
    return deg * M_PI / 180.0;
}

inline
double clamp01(double x) {
    return (x < 0.0)? 0.0 : (x > 1.0)? 1.0 : x;
}

u8 srgb_to_rgb256(double v) {
    v = clamp01(v);
    v = (v <= 0.0031308)? 12.92 * v : 1.055 * pow(v, 1.0 / 2.4) - 0.055;
    return (u8)(v * 255.0 + 0.5);
}

inline
RGB rgb_uniform(u8 v) {
    return (RGB){v, v, v};
}

RGB srgb_to_rgb(sRGB color) {
    RGB result;
    result.r = srgb_to_rgb256(color.r);
    result.g = srgb_to_rgb256(color.g);
    result.b = srgb_to_rgb256(color.b);
    return result;
}

// ----------------------------------------
//       HSL: Hue-Saturation-Lightness
// ----------------------------------------

RGB hslToRgb(HSL color) {
    if (color.s == 0.0) {
        return rgb_uniform((u8)(color.l * 255.0 + 0.5));
    }

    double c = (1.0 - fabs(2.0 * color.l - 1.0)) * color.s;
    double hp = fmod(color.h, 360.0) / 60.0;
    double x = c * (1.0 - fabs(fmod(hp, 2.0) - 1.0));
    double m = color.l - c / 2.0;
    double r1, g1, b1;

    if (hp >= 0 && hp < 1) { r1 = c; g1 = x; b1 = 0; }
    else if (hp >= 1 && hp < 2) { r1 = x; g1 = c; b1 = 0; }
    else if (hp >= 2 && hp < 3) { r1 = 0; g1 = c; b1 = x; }
    else if (hp >= 3 && hp < 4) { r1 = 0; g1 = x; b1 = c; }
    else if (hp >= 4 && hp < 5) { r1 = x; g1 = 0; b1 = c; }
    else { r1 = c; g1 = 0; b1 = x; }

    return (RGB){
        .r = (u8)((r1 + m) * 255.0 + 0.5),
        .g = (u8)((g1 + m) * 255.0 + 0.5),
        .b = (u8)((b1 + m) * 255.0 + 0.5),
    };
}

// ----------------------------------------
//      OKHSL: Perceptually Uniform
// ----------------------------------------

sRGB oklab_to_srgb(double L, double a, double b) {
    sRGB result;
    double l_ = L + 0.3963377774*a + 0.2158037573*b;
    double m_ = L - 0.1055613458*a - 0.0638541728*b;
    double s_ = L - 0.0894841775*a - 1.2914855480*b;

    double l = l_ * l_ * l_;
    double m = m_ * m_ * m_;
    double s_val = s_ * s_ * s_;

    result.r = +4.0767416621*l - 3.3077115913*m + 0.2309699292*s_val;
    result.g = -1.2684380046*l + 2.6097574011*m - 0.3413193965*s_val;
    result.b = -0.0041960863*l - 0.7034186147*m + 1.7076147010*s_val;
    return result;
}

#define CACHE_SIZE 1000
double cusp_cache_L[CACHE_SIZE];
double cusp_cache_h[CACHE_SIZE];
double cusp_cache_Cmax[CACHE_SIZE];
int cache_count = 0;

double find_cusp(double L, double aDir, double bDir) {
    double C = 0.0;
    double step = 0.1;
    sRGB srgb;

    for (int i = 0; i < 20; i++) {
        srgb = oklab_to_srgb(L, C * aDir, C * bDir);

        if (srgb.r < -1e-9 || srgb.g < -1e-9 || srgb.b < -1e-9 ||
            srgb.r > 1.0 + 1e-9 || srgb.g > 1.0 + 1e-9 || srgb.b > 1.0 + 1e-9) {
            break;
        }
        C += step;
        step *= 2.0;
    }

    double low = 0.0, high = C;
    for (int i = 0; i < 24; i++) {
        double mid = (low + high) / 2.0;
        srgb = oklab_to_srgb(L, mid * aDir, mid * bDir);

        if (srgb.r >= -1e-12 && srgb.g >= -1e-12 && srgb.b >= -1e-12 &&
            srgb.r <= 1.0 + 1e-12 && srgb.g <= 1.0 + 1e-12 && srgb.b <= 1.0 + 1e-12) {
            low = mid;
        } else {
            high = mid;
        }
    }
    return low;
}

double getCMaxForLH(double L, double hDeg) {
    int h_int = (int)(hDeg + 0.5);
    for (int i = 0; i < cache_count; i++) {
        if (fabs(cusp_cache_L[i] - L) < 1e-6 && cusp_cache_h[i] == h_int) {
            return cusp_cache_Cmax[i];
        }
    }
    
    double hr = deg_to_rad(hDeg);
    double aDir = cos(hr);
    double bDir = sin(hr);

    if (cache_count >= CACHE_SIZE) {
        int idx = cache_count % CACHE_SIZE;
        cusp_cache_L[idx] = L;
        cusp_cache_h[idx] = h_int;
        cusp_cache_Cmax[idx] = find_cusp(L, aDir, bDir);
        return cusp_cache_Cmax[idx];
    }

    double cmax = find_cusp(L, aDir, bDir);
    cusp_cache_L[cache_count] = L;
    cusp_cache_h[cache_count] = h_int;
    cusp_cache_Cmax[cache_count] = cmax;
    cache_count++;

    return cmax;
}

RGB okhslToRgb(HSL color) {
    color.h = fmod(color.h, 360.0);
    color.s = clamp01(color.s);
    color.l = clamp01(color.l);
    double L = color.l;

    if (color.s == 0.0) {
        return srgb_to_rgb(oklab_to_srgb(L, 0.0, 0.0));
    }

    double C_max = getCMaxForLH(L, color.h);
    double C_desired = color.s * C_max;

    double hr = deg_to_rad(color.h);
    double aDir = cos(hr);
    double bDir = sin(hr);

    sRGB srgb = oklab_to_srgb(L, C_desired * aDir, C_desired * bDir);

    if (srgb.r < -1e-12 || srgb.g < -1e-12 || srgb.b < -1e-12 ||
        srgb.r > 1.0 + 1e-12 || srgb.g > 1.0 + 1e-12 || srgb.b > 1.0 + 1e-12) {
        double low = 0.0, high = 1.0;
        double okR = 0.0, okG = 0.0, okB = 0.0;
        for (int i = 0; i < 24; i++) {
            double mid = (low + high) / 2.0;
            double Ctest = C_desired * mid;
            srgb = oklab_to_srgb(L, Ctest * aDir, Ctest * bDir);
            if (srgb.r >= -1e-12 && srgb.g >= -1e-12 && srgb.b >= -1e-12 &&
                srgb.r <= 1.0 + 1e-12 && srgb.g <= 1.0 + 1e-12 && srgb.b <= 1.0 + 1e-12) {
                low = mid;
                okR = srgb.r; okG = srgb.g; okB = srgb.b;
            } else {
                high = mid;
            }
        }
        srgb.r = okR; srgb.g = okG; srgb.b = okB;
    }

    return srgb_to_rgb(srgb);
}

// ----------------------------------------
//        Универсальная функция выбора
// ----------------------------------------

inline
RGB convertColor(HSL c) {
    return (use_okhsl) ? okhslToRgb(c) : hslToRgb(c);
}

// ----------------------------------------
//           Вспомогательные
// ----------------------------------------

double easeEndpoints(double t) {
    double p = pow(t, 3);
    double q = 1.0 - pow(1.0 - t, 3);
    double mix = 0.5 * p + 0.5 * q;
    return mix * mix * (3.0 - 2.0 * mix);
}

double lightnessForColumn(int colIndex, double lMin, double lMax) {
    double t = (width == 1) ? 0.0 : (double)colIndex / (width - 1);
    double tt = easeEndpoints(t);
    return lMin + (lMax - lMin) * tt;
}

double saturationForColumn(int colIndex) {
    double t = (width == 1) ? 0.0 : (double)colIndex / (width - 1);
    double tt = easeEndpoints(t);
    return (tt <= 0.5)?
            1.0 - 0.1 *( tt / 0.5 ):        // от 1.0 до 0.9
            0.9 - 0.2 *((tt - 0.5) / 0.5);  // от 0.9 до 0.7
}

void setPixel(u8* image, int x, int y, RGB color, u8 a) {
    int idx = 4 * (y * width + x);
    image[idx + 0] = color.r;
    image[idx + 1] = color.g;
    image[idx + 2] = color.b;
    image[idx + 3] = a;
}

void printUsage(stringz name) {
    printf("Использование: %s [ширина] [строки_оттенков] [-okhsl|-h]\n", name);
    printf("  ширина         - целое >= 3 (по умолчанию %d)\n", DEFAULT_WIDTH);
    printf("  строки_оттенков - целое >= 1 (по умолчанию %d)\n", DEFAULT_HUE_ROWS);
    printf("  -okhsl         - использовать OKHSL вместо HSL\n");
    printf("  -h, --help     - показать эту справку\n");
}

int toPositiveInt(stringz str, int fallback) {
    if (!str) return fallback;
    char* end;
    long val = strtol(str, &end, 10);
    if (*end != '\0' || val < 1) return fallback;
    return (int)val;
}

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printUsage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-okhsl") == 0) {
            use_okhsl = 1;
        } else if (width == DEFAULT_WIDTH) {
            width = toPositiveInt(argv[i], DEFAULT_WIDTH);
        } else if (hueRows == DEFAULT_HUE_ROWS) {
            hueRows = toPositiveInt(argv[i], DEFAULT_HUE_ROWS);
        } else {
            fprintf(stderr, "Неизвестный аргумент: %s\n", argv[i]);
            printUsage(argv[0]);
            return 1;
        }
    }

    // Валидация
    if (width < MIN_WIDTH) {
        printf("Ширина слишком мала, используем минимум = %d\n", MIN_WIDTH);
        width = MIN_WIDTH;
    }
    if (hueRows < MIN_HUE_ROWS) {
        printf("Количество строк оттенков слишком мало, используем минимум = %d\n", MIN_HUE_ROWS);
        hueRows = MIN_HUE_ROWS;
    }

    const int specialTopRows = 1;
    const int grayRowCount = 1;
    height = specialTopRows + grayRowCount + hueRows;

    u8* image = calloc(width * height, 4 * sizeof(u8));
    if (!image) {
        fprintf(stderr, "Ошибка выделения памяти\n");
        return 1;
    }

    // --- Строка 0: ---
    for (int x = 0; x < width; x++) {
        if (x == 0) {
            setPixel(image, x, 0, (RGB){0, 0, 0}, 255);           // Чёрный
        } else if (x == width - 1) {
            setPixel(image, x, 0, (RGB){255, 255, 255}, 255);     // Белый
        } else {
            setPixel(image, x, 0, (RGB){0, 0, 0}, 0);             // Прозрачный
        }
    }

    // --- Строка 1: серый градиент ---
    double lMin = use_okhsl ? 0.15 : 0.05;
    double lMax = 0.95;
    for (int x = 0; x < width; x++) {
        double l = lightnessForColumn(x, lMin, lMax);
        RGB color = (use_okhsl)?
            srgb_to_rgb(oklab_to_srgb(l, 0.0, 0.0)):
            rgb_uniform((u8)(l * 255.0 + 0.5));
        setPixel(image, x, 1, color, 255);
    }

    // --- Цветовые строки ---
    double hueStep = 360.0 / hueRows;
    for (int row = 0; row < hueRows; row++) {
        int y = specialTopRows + grayRowCount + row;
        double hue = fmod(row * hueStep, 360.0);
        for (int x = 0; x < width; x++) {
            RGB color = convertColor((HSL){
                .h = hue,
                .s = saturationForColumn(x),
                .l = lightnessForColumn(x, lMin, lMax)
            });
            setPixel(image, x, y, color, 255);
        }
    }

    int success = stbi_write_png("palette.png", width, height, 4, image, width * 4);
    // free(image);

    if (!success) {
        fprintf(stderr, "Ошибка записи palette.png\n");
        return 1;
    }

    printf("palette.png записан (%s, ширина=%d, высота=%d)\n",
            (use_okhsl)? "OKHSL" : "HSL", width, height);
    return 0;
}
