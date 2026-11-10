// AGENDA 3000: agenda gráfica con estética de terminal retro-futurista (fósforo verde).
// Vistas Mes / Semana / Día (por horas) y HORARIO (rutinas semanales automáticas por mañana/tarde/noche),
// ratón + teclado, mascota animada original, alarmas según el reloj del ordenador y sincronización
// entre dispositivos por Bluetooth, cable serie o TCP (tecla S).
//
// Mascota: si junto al programa hay una hoja de fotogramas 'mascota.png' (se detectan solos los fotogramas),
// se anima en verde fósforo según el estado; si no, se dibuja la mascota original con formas.
// Pestaña 6 JUEGOS: hackeo de terminal, ganzúa y defensa de ciudades, para 1 jugador o 2 en coop local.
// Calendario: cuadradito verde = eventos, cuadradito de color (tecla K) = rutinas. SEMANA: clic o TAB despliega el día.
// Pestaña INVENTARIO: objetos con ruta de almacenaje, cantidad, lista "lo que llevo", imagen JPG/PNG
// (tecla I o arrastrando el archivo a la ventana) y avisos de escritorio para los objetos marcados.
//
// Archivos: agenda_pipboy.cpp + agenda_core.hpp + agenda_notify.hpp (misma carpeta). Dependencias: SDL2 y SDL2_image.
//   Linux/macOS:  g++ -std=c++17 -O2 -pthread agenda_pipboy.cpp -o agenda_pipboy $(sdl2-config --cflags --libs) -lSDL2_image
//   Windows (MinGW): g++ -std=c++17 -O2 agenda_pipboy.cpp -o agenda_pipboy.exe -I<SDL2>\include -I<SDL2_image>\include
//                    -L<SDL2>\lib -L<SDL2_image>\lib -lmingw32 -lSDL2main -lSDL2 -lSDL2_image -lws2_32 -lshell32 -mwindows
//                    (-mwindows = sin ventana de consola)
//   Avisos en Linux: sudo apt install libnotify-bin (notify-send).
// Uso opcional: agenda_pipboy [destino_sync]   (p. ej. COM5, /dev/rfcomm0, 192.168.1.20:5050, :5050)

#include "agenda_core.hpp"  // debe ir antes que SDL (winsock)
#include "agenda_notify.hpp"
#include "agenda_games.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
using namespace std;

static const float PI = 3.14159265f;

// ------------------------------------------------------------------ Nombres
static const char *MONTHS[] = {"enero", "febrero", "marzo", "abril", "mayo", "junio", "julio",
                               "agosto", "septiembre", "octubre", "noviembre", "diciembre"};
static const char *DAYS_FULL[] = {"Lunes", "Martes", "Miércoles", "Jueves", "Viernes", "Sábado", "Domingo"};
static const char *DAYS_SHORT[] = {"LU", "MA", "MI", "JU", "VI", "SA", "DO"};

// -------------------------------------------------------------- Primitivas
static SDL_Renderer *R = nullptr;
static float g_flick = 1.f;
struct Col { int r, g, b; };
static const Col BRIGHT{110, 255, 140}, MID{44, 170, 76}, DIMC{12, 58, 26}, BGC{3, 13, 6};

static void setCol(Col c, int a = 255) {
    SDL_SetRenderDrawColor(R, (Uint8)(c.r * g_flick), (Uint8)(c.g * g_flick), (Uint8)(c.b * g_flick), (Uint8)a);
}
static void fillRect(int x, int y, int w, int h) { SDL_Rect r{x, y, w, h}; SDL_RenderFillRect(R, &r); }
static void rectLineSq(int x, int y, int w, int h, int t = 2) {
    fillRect(x, y, w, t); fillRect(x, y + h - t, w, t); fillRect(x, y, t, h); fillRect(x + w - t, y, t, h);
}
// Esquinas curvas: el radio se adapta al tamaño (cajas pequeñas o finas quedan rectas).
static int autoR(int w, int h) { int m = min(w, h); return m >= 14 ? min(10, m / 3) : 0; }
static int rInset(int r, int i) {  // hueco a la izquierda en la fila i (desde arriba) de un rectángulo de radio r
    if (r <= 0 || i >= r) return 0;
    float dy = r - i - 0.5f;
    return r - (int)floorf(sqrtf((float)r * r - dy * dy) + 0.5f);
}
static void fillRound(int x, int y, int w, int h, int r) {
    r = min(r, min(w, h) / 2);
    if (r <= 0) { fillRect(x, y, w, h); return; }
    for (int i = 0; i < r; i++) {
        int in = rInset(r, i);
        fillRect(x + in, y + i, w - 2 * in, 1);
        fillRect(x + in, y + h - 1 - i, w - 2 * in, 1);
    }
    if (h - 2 * r > 0) fillRect(x, y + r, w, h - 2 * r);
}
static void fillBox(int x, int y, int w, int h) { fillRound(x, y, w, h, autoR(w, h)); }
static void ringRound(int x, int y, int w, int h, int r, int t) {
    r = min(r, min(w, h) / 2); t = max(1, t);
    int lim = min(h / 2, max(r, t));
    for (int ri = 0; ri < lim; ri++) {
        int outer = rInset(r, ri);
        int inner = ri < t ? -1 : t + rInset(max(0, r - t), ri - t);
        for (int side = 0; side < 2; side++) {
            int yy = side ? y + h - 1 - ri : y + ri;
            if (inner < 0) fillRect(x + outer, yy, w - 2 * outer, 1);
            else { fillRect(x + outer, yy, max(0, inner - outer), 1); fillRect(x + w - inner, yy, max(0, inner - outer), 1); }
        }
    }
    if (h - 2 * lim > 0) { fillRect(x, y + lim, t, h - 2 * lim); fillRect(x + w - t, y + lim, t, h - 2 * lim); }
}
static void rectLine(int x, int y, int w, int h, int t = 2) {
    int r = autoR(w, h);
    if (r <= 0) rectLineSq(x, y, w, h, t); else ringRound(x, y, w, h, r, t);
}
static void fillEllipse(int cx, int cy, int rx, int ry) {
    rx = max(rx, 1); ry = max(ry, 1);
    for (int dy = -ry; dy <= ry; dy++) {
        float k = max(0.f, 1.f - (float)(dy * dy) / (ry * ry));
        int dx = (int)(rx * sqrtf(k));
        SDL_RenderDrawLine(R, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}
static void blob(int cx, int cy, int rx, int ry, int t, Col fill, Col edge) {
    setCol(edge); fillEllipse(cx, cy, rx, ry);
    setCol(fill); fillEllipse(cx, cy, rx - t, ry - t);
}
static void thickLine(float x1, float y1, float x2, float y2, int t) {
    float dx = x2 - x1, dy = y2 - y1;
    int n = (int)max(1.f, sqrtf(dx * dx + dy * dy));
    for (int i = 0; i <= n; i++) { float k = (float)i / n; fillRect((int)(x1 + dx * k) - t / 2, (int)(y1 + dy * k) - t / 2, t, t); }
}
static void arc(float cx, float cy, float r, float a0, float a1, int t) {  // grados, 0 = derecha, 90 = arriba
    int n = max(8, (int)(fabsf(a1 - a0) / 6));
    float px = 0, py = 0;
    for (int i = 0; i <= n; i++) {
        float a = (a0 + (a1 - a0) * i / n) * PI / 180.f, x = cx + r * cosf(a), y = cy - r * sinf(a);
        if (i) thickLine(px, py, x, y, t);
        px = x; py = y;
    }
}

// ------------------------------------------------- Fuente bitmap 5x7 (ASCII 0x20..'Z')
static const unsigned char FONT[64][5] = {
    {0,0,0,0,0},{0,0,0x5F,0,0},{0,7,0,7,0},{0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},
    {0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0,5,3,0,0},{0,0x1C,0x22,0x41,0},
    {0,0x41,0x22,0x1C,0},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},{0,0x50,0x30,0,0},
    {8,8,8,8,8},{0,0x60,0x60,0,0},{0x20,0x10,0x08,0x04,0x02},{0x3E,0x51,0x49,0x45,0x3E},
    {0,0x42,0x7F,0x40,0},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},
    {0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{1,0x71,9,5,3},{0x36,0x49,0x49,0x49,0x36},
    {6,0x49,0x49,0x29,0x1E},{0,0x36,0x36,0,0},{0,0x56,0x36,0,0},{0x08,0x14,0x22,0x41,0},
    {0x14,0x14,0x14,0x14,0x14},{0,0x41,0x22,0x14,0x08},{2,1,0x51,9,6},{0x32,0x49,0x79,0x41,0x3E},
    {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},
    {0x7F,0x49,0x49,0x49,0x41},{0x7F,9,9,9,1},{0x3E,0x41,0x49,0x49,0x7A},{0x7F,8,8,8,0x7F},
    {0,0x41,0x7F,0x41,0},{0x20,0x40,0x41,0x3F,1},{0x7F,8,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
    {0x7F,2,0x0C,2,0x7F},{0x7F,4,8,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},{0x7F,9,9,9,6},
    {0x3E,0x41,0x51,0x21,0x5E},{0x7F,9,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},{1,1,0x7F,1,1},
    {0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,8,0x14,0x63},
    {7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43},
    {0,0x7F,0x41,0x41,0},{2,4,8,0x10,0x20},{0,0x41,0x41,0x7F,0},{4,2,1,2,4},{0x40,0x40,0x40,0x40,0x40}};

static const char *LAT = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";

static int nextCp(const string &s, size_t &i) {  // decodifica UTF-8
    unsigned char c = s[i++];
    if (c < 0x80) return c;
    if ((c >> 5) == 6 && i < s.size()) return ((c & 0x1F) << 6) | ((unsigned char)s[i++] & 0x3F);
    if ((c >> 4) == 14) i = min(s.size(), i + 2);
    else if ((c >> 3) == 30) i = min(s.size(), i + 3);
    return '?';
}
static int countGlyphs(const string &s) { int n = 0; for (size_t i = 0; i < s.size(); n++) nextCp(s, i); return n; }
static int textW(const string &s, int sc) { return countGlyphs(s) * 6 * sc; }

static void drawText(int x, int y, const string &s, int sc, Col c, int maxW = 0) {
    setCol(c);
    int n = 0;
    for (size_t i = 0; i < s.size(); n++) {
        if (maxW && (n + 1) * 6 * sc > maxW) break;
        int cp = nextCp(s, i);
        bool acc = false;
        if (cp >= 0xC0 && cp <= 0xFF) { cp = LAT[cp - 0xC0]; acc = true; }
        if (cp >= 'a' && cp <= 'z') cp -= 32;
        if (cp < 0x20 || cp > '_') cp = '?';
        int px = x + n * 6 * sc;
        for (int col = 0; col < 5; col++) {
            unsigned char bits = FONT[cp - 0x20][col];
            for (int row = 0; row < 7;) {
                if (!(bits >> row & 1)) { row++; continue; }
                int start = row;
                while (row < 7 && (bits >> row & 1)) row++;
                fillRect(px + col * sc, y + start * sc, sc, (row - start) * sc);
            }
        }
        if (acc) fillRect(px + sc, y - 2 * sc, 3 * sc, sc);
    }
}
static void drawTextC(int cx, int y, const string &s, int sc, Col c) { drawText(cx - textW(s, sc) / 2, y, s, sc, c); }

// ------------------------------------------------------------------ Estado
enum View { MONTH, WEEK, DAY, TASKS, NOTES, ROUTINE, INVENTORY, GAMES };
enum Mode { BROWSE, IN_TIME, IN_TITLE, IN_DELETE, IN_SYNC, IN_RDAYS, IN_RDEL,
            IN_INAME, IN_IPLACE, IN_IQTY, IN_IEDIT, IN_ICARRY, IN_IHOUR, IN_IDAYS, IN_ICONF, IN_IMG, IN_IFILTER,
            IN_ASK, IN_EPICK, IN_EMENU };
enum Mood { M_IDLE, M_THUMBS, M_WAVE, M_WINK };

static Agenda ag;
static View view = MONTH;
static Mode mode = BROWSE;
static Date cur;
static int hourSel = 9, pendH = 9, pendM = 0;
static string input, msg;
static float g_now = 0, g_msgEnd = 0, g_moodEnd = 0, g_armA = 170;
static Mood g_mood = M_WAVE;
static char trig = 0;
static Uint32 trigTick = 0;
static bool running = true;
static int rRow = 0, rCol = 0, pendMask = 31;  // HORARIO: día de la semana y apartado seleccionados
static bool pendRoutine = false;
static int invSel = 0, pendThing = -1;               // INVENTARIO
static bool invCarry = false, g_imgColor = false, g_deskAlarms = true;
static string invFilter, pendName, pendPlace;
static SDL_Texture *g_tex[2] = {nullptr, nullptr};  // foto principal y secundaria del objeto seleccionado
static string g_texKey[2];
static int g_texW[2] = {0, 0}, g_texH[2] = {0, 0};
static const char *IMG_DIR = "imagenes";
static bool weekExp = false;                 // SEMANA: día seleccionado desplegado
static int weekScroll = 0, g_routineColor = 0;
static float g_weekA = 0;                    // 0 = contraído, 1 = desplegado (animado)
static Date g_wsDay{0, 0, 0};

// Tareas, notas, fichas editables y preguntas encadenadas
static int tkSel = 0, ntSel = 0, ntScroll = 0;
static bool tkDone = false;                // TAREAS: false = pendientes, true = hechas
static string tkFilter, ntFilter;
static function<void(const string &)> g_askCb;  // IN_ASK: qué hacer con el texto escrito
static function<void()> g_askEsc;               // IN_ASK: qué hacer con Esc
static string g_askLabel, g_askHint;
static SDL_Keycode g_lastKey = 0;                // última tecla pulsada (para no escribir la letra que abrió una pregunta)
static bool g_askMulti = false;                 // texto largo: "\n" escrito = salto de línea
struct EField { string label, hint; function<string()> get; function<string(const string &)> set; };  // set: "" = bien, otro = error
struct Editor { bool on = false; string title, status; vector<EField> f; function<void()> touch, del; };
static Editor g_ed;
static void startAsk(const string &label, const string &hint, const string &def, function<void(const string &)> cb,
                     function<void()> esc = nullptr, char trigger = 0, bool multi = false);

// Sincronización, alarmas y reloj
static mutex g_mx;                       // protege 'ag' entre la interfaz y el hilo de sync
static thread g_syncThread;
static atomic<bool> g_syncBusy{false};
static bool g_syncDone = false, g_syncOk = false;
static string g_syncMsg, g_syncTarget;
static string g_alarmText;
static float g_alarmEnd = 0;
static SDL_AudioDeviceID g_audio = 0;
static const char *CFG = "agenda_sync.cfg";
static void saveCfg() { ofstream c(CFG); c << g_syncTarget << "\n" << (g_deskAlarms ? 1 : 0) << "\n" << g_routineColor << "\n"; }


// Marquesina: si un texto no cabe en su hueco, se desliza a la izquierda en bucle (pausa de 1,2 s y 45 px/s).
static void drawTextMq(int x, int y, const string &s, int sc, Col c, int maxW) {
    int w = textW(s, sc);
    if (maxW <= 0 || w <= maxW) { drawText(x, y, s, sc, c); return; }
    bool had = SDL_RenderIsClipEnabled(R) != 0;
    SDL_Rect old{0, 0, 0, 0};
    if (had) SDL_RenderGetClipRect(R, &old);
    SDL_Rect cr{x, y - 2 * sc, maxW, 10 * sc};
    if (had) {  // intersección con el recorte que ya hubiera
        int x0 = max(cr.x, old.x), y0 = max(cr.y, old.y), x1 = min(cr.x + cr.w, old.x + old.w), y1 = min(cr.y + cr.h, old.y + old.h);
        cr = {x0, y0, max(0, x1 - x0), max(0, y1 - y0)};
    }
    SDL_RenderSetClipRect(R, &cr);
    int gap = 18 * sc, per = w + gap;
    float T = 1.2f + per / 45.f, ph = fmodf(g_now, T);
    int off = ph < 1.2f ? 0 : min(per, (int)((ph - 1.2f) * 45.f));
    drawText(x - off, y, s, sc, c);
    drawText(x - off + per, y, s, sc, c);
    SDL_RenderSetClipRect(R, had ? &old : nullptr);
}
static string lowerAscii(string s) { for (auto &ch : s) ch = (char)tolower((unsigned char)ch); return s; }
static string toEdit(const string &s) { string o; for (char ch : s) { if (ch == '\n') o += "\\n"; else o += ch; } return o; }
static string fromEdit(const string &s) {
    string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size() && (s[i + 1] == 'n' || s[i + 1] == 'N')) { o += '\n'; i++; } else o += s[i];
    }
    return o;
}

// Geometría (coordenadas lógicas 800x600)
static const int TX = 24, TY = 18, TW = 54, TH = 30, TG = 57;
static const int RY = 118, RH = 58, RX0 = 70, RCW = 150;  // vista HORARIO
static const int GX = 25, GY = 130, CW = 70, CH = 66;
static const int WY = 98, WH = 62;
static const int DY = 96, DH = 18;

static void setMood(Mood m, float dur) { g_mood = m; g_moodEnd = g_now + dur; }
static void flash(const string &s) { msg = s; g_msgEnd = g_now + 2.5f; }

// --------------------------------------------------------------- Mascota
static void sparkle(int x, int y, int s) { setCol(BRIGHT); fillRect(x - s, y - 1, 2 * s + 1, 3); fillRect(x - 1, y - s, 3, 2 * s + 1); }

static void drawMascot(int cx, float t) {
    int b = (int)(sinf(t * 2.5f) * 3);
    int hy = 140 + b;
    bool blink = fmodf(t, 4.f) < 0.12f;
    bool thumbs = g_mood == M_THUMBS && g_armA < 25;

    setCol(MID); fillRect(cx - 80, 300, 160, 3);  // suelo

    // piernas y pies
    blob(cx - 13, 266, 11, 26, 3, DIMC, BRIGHT);
    blob(cx + 13, 266, 11, 26, 3, DIMC, BRIGHT);
    setCol(BRIGHT); fillRect(cx - 28, 288, 26, 11); fillRect(cx + 2, 288, 26, 11);
    // torso
    setCol(BRIGHT); fillRect(cx - 26, 176, 52, 68);
    setCol(DIMC);   fillRect(cx - 23, 179, 46, 62);
    setCol(BRIGHT); thickLine(cx - 14, 178, cx, 200, 3); thickLine(cx + 14, 178, cx, 200, 3);
    thickLine(cx, 200, cx, 241, 2);
    blob(cx, 220, 11, 11, 2, BGC, BRIGHT);
    drawTextC(cx, 214, "A", 2, BRIGHT);
    // brazo izquierdo (colgando, con vaivén)
    float sw = sinf(t * 2.5f) * 3;
    setCol(BRIGHT);
    thickLine(cx - 26, 188, cx - 42 + sw, 220, 7); thickLine(cx - 42 + sw, 220, cx - 40 + sw, 248, 7);
    fillEllipse((int)(cx - 40 + sw), 252, 7, 7);
    // brazo derecho (animado)
    float sx = cx + 26, sy = 188, ex = cx + 46, ey = 214, L = 34, a = g_armA * PI / 180.f;
    float hx = ex + L * sinf(a), hy2 = ey - L * cosf(a);
    thickLine(sx, sy, ex, ey, 7); thickLine(ex, ey, hx, hy2, 7);
    fillEllipse((int)hx, (int)hy2, 10, 10);
    if (thumbs) {
        thickLine(hx, hy2 - 8, hx, hy2 - 24, 8);
        for (int i = 0; i < 4; i++)
            if (sinf(t * 8 + i * 1.7f) > 0) sparkle(cx + 70 - i * 6 + (i % 2) * 24, 100 + i * 28 - (i % 2) * 20, 4 + i % 2 * 2);
    }
    // cabeza
    blob(cx, hy, 38, 40, 3, DIMC, BRIGHT);
    setCol(BRIGHT); arc((float)cx, (float)hy, 35, 28, 152, 8);
    fillEllipse(cx - 14, hy - 44, 12, 10); fillEllipse(cx + 5, hy - 45, 9, 8);
    // ojos
    int ey2 = hy - 6, eh = blink ? 1 : 7;
    setCol(BRIGHT);
    fillEllipse(cx - 14, ey2, 5, eh);
    if (g_mood == M_WINK) fillRect(cx + 8, ey2 - 1, 13, 3); else fillEllipse(cx + 14, ey2, 5, eh);
    // sonrisa y mejillas
    arc((float)cx, (float)(hy + 6), g_mood == M_THUMBS ? 24.f : 20.f, 205, 335, 3);
    setCol(MID); fillEllipse(cx - 27, hy + 10, 4, 3); fillEllipse(cx + 27, hy + 10, 4, 3);
}

// ------------------------------------------------------------------ Paneles
static void box(int x, int y, int w, int h) {
    setCol(MID); ringRound(x, y, w, h, 14, 2);
}

static string clip(int n, const char *one, const char *many) { return to_string(n) + " " + (n == 1 ? one : many); }

// Color de las rutinas (elegible con K). Los eventos usan siempre el verde.
struct PalEntry { const char *name; Col c; };
static const PalEntry PAL[8] = {{"AZUL", {80, 150, 255}}, {"CIAN", {60, 225, 235}}, {"NARANJA", {255, 155, 50}}, {"MAGENTA", {245, 90, 225}},
                                {"ROJO", {255, 85, 85}}, {"AMARILLO", {255, 225, 70}}, {"VIOLETA", {175, 120, 255}}, {"BLANCO", {235, 235, 235}}};
static const Col EVENT_GREEN{70, 255, 100};
static Col routineCol() { return PAL[g_routineColor & 7].c; }
static Col itemCol(const Item &it) {
    if (it.kind == 1) return routineCol();
    if (it.kind == 2) return it.done ? MID : it.overdue ? PAL[4].c : PAL[5].c;
    return BRIGHT;
}
static void drawSquare(int x, int y, Col c) { setCol(BGC); fillRect(x - 1, y - 1, 9, 9); setCol(c); fillRect(x, y, 7, 7); }
static void drawSquareHollow(int x, int y, Col c) { setCol(BGC); fillRect(x - 1, y - 1, 9, 9); setCol(c); rectLineSq(x, y, 7, 7, 2); }
static void kindsOf(const vector<Item> &v, bool &ev, bool &rt, bool &tk) {  // qué tipos hay en un día (tareas: solo pendientes)
    ev = rt = tk = false;
    for (auto &it : v) { if (it.kind == 0) ev = true; else if (it.kind == 1) rt = true; else if (!it.done) tk = true; }
}

static void drawHeader() {
    const char *names[8] = {"1MES", "2SEM", "3DIA", "4TAR", "5NOT", "6RUT", "7INV", "8JUE"};
    for (int i = 0; i < 8; i++) {
        int x = TX + i * TG;
        if ((int)view == i) { setCol(BRIGHT); fillBox(x, TY, TW, TH); drawText(x + 3, TY + 8, names[i], 2, BGC); }
        else { setCol(MID); rectLine(x, TY, TW, TH, 2); drawText(x + 3, TY + 8, names[i], 2, MID); }
    }
    if (g_syncBusy) { if (fmodf(g_now, 1.f) < 0.6f) drawText(482, 25, "SYNC...", 2, BRIGHT); }
    else drawText(482, 25, "AGENDA 3000", 2, BRIGHT);
    time_t n = time(nullptr);
    char buf[16];
    strftime(buf, sizeof buf, "%H:%M:%S", localtime(&n));
    drawText(618, 21, buf, 3, BRIGHT);
}

static string fitGlyphs(const string &s, int maxG) {
    size_t i = 0; int n = 0;
    while (i < s.size() && n < maxG) { nextCp(s, i); n++; }
    return s.substr(0, i);
}
static string tailGlyphs(const string &s, int n) {
    int c = countGlyphs(s);
    if (c <= n) return s;
    size_t i = 0;
    for (int k = 0; k < c - n; k++) nextCp(s, i);
    return s.substr(i);
}
static vector<string> wrapGlyphs(const string &s, int n) {
    vector<string> out;
    size_t i = 0;
    while (i < s.size()) { size_t st = i; int c = 0; while (i < s.size() && c < n) { nextCp(s, i); c++; } out.push_back(s.substr(st, i - st)); }
    return out;
}
static vector<string> wrapLines(const string &s, int n) {  // respeta los saltos de línea y parte las largas
    vector<string> out;
    size_t st = 0;
    while (st <= s.size()) {
        size_t e = s.find('\n', st);
        string ln = s.substr(st, e == string::npos ? string::npos : e - st);
        if (ln.empty()) out.push_back("");
        else for (auto &p : wrapGlyphs(ln, n)) out.push_back(p);
        if (e == string::npos) break;
        st = e + 1;
    }
    return out;
}
static string itemLine(const Item &it) {
    if (it.kind == 2)
        return string(it.done ? "[X] " : "[ ] ") + (it.tm ? timeStr(it.hour, it.minute) + " " : string()) + (it.overdue ? "! " : "") + it.title +
               (it.desc.empty() ? "" : " (+)");
    return timeStr(it.hour, it.minute) + (it.routine ? "*" : " ") + it.title + (it.tag.empty() ? "" : " >" + it.tag) + (it.desc.empty() ? "" : " (+)");
}

static void drawMonth() {
    Date t = today();
    string title = string(MONTHS[cur.m - 1]) + " " + to_string(cur.y);
    drawTextC(272, 70, title, 3, BRIGHT);
    for (int c = 0; c < 7; c++) drawText(GX + c * CW + 23, 108, DAYS_SHORT[c], 2, MID);
    int first = weekday({cur.y, cur.m, 1}), n = daysInMonth(cur.y, cur.m);
    for (int d = 1; d <= n; d++) {
        int idx = first + d - 1, x = GX + idx % 7 * CW, y = GY + idx / 7 * CH;
        Date dd{cur.y, cur.m, d};
        bool sel = dd == cur;
        bool hasEv, hasRt, hasTk;
        kindsOf(ag.itemsOn(dd), hasEv, hasRt, hasTk);
        if (sel) { setCol(BRIGHT, 150 + (int)(60 * sinf(g_now * 6))); fillBox(x + 2, y + 2, CW - 4, CH - 4); }
        else if (dd == t) { setCol(BRIGHT); rectLine(x + 2, y + 2, CW - 4, CH - 4, 2); }
        else { setCol(DIMC); rectLine(x + 2, y + 2, CW - 4, CH - 4, 1); }
        drawText(x + 9, y + 9, to_string(d), 3, sel ? BGC : (dd == t ? BRIGHT : MID));
        if (hasEv) drawSquare(x + 9, y + CH - 16, EVENT_GREEN);
        if (hasRt) drawSquare(x + 21, y + CH - 16, routineCol());
        if (hasTk) drawSquareHollow(x + 33, y + CH - 16, PAL[5].c);
    }
}

static const int WEEK_COMPACT = 26;
static void weekGeom(int ys[8]) {  // posiciones verticales de las 7 filas según la animación
    int sel = weekday(cur), total = 7 * WH;
    int selH = (int)(WH + (total - 6 * WEEK_COMPACT - WH) * g_weekA);
    int othH = (int)(WH + (WEEK_COMPACT - WH) * g_weekA);
    int y = WY;
    for (int i = 0; i < 7; i++) { ys[i] = y; y += i == sel ? selH : othH; }
    ys[7] = y;
}

static void drawWeek() {
    Date t = today(), start = addDays(cur, -weekday(cur)), end = addDays(start, 6);
    if (!(cur == g_wsDay)) { g_wsDay = cur; weekScroll = 0; }
    string title = to_string(start.d) + " " + MONTHS[start.m - 1] + " - " + to_string(end.d) + " " + MONTHS[end.m - 1] + " " + to_string(end.y);
    drawTextC(272, 70, title, 2, BRIGHT);
    int ys[8], sel = weekday(cur);
    weekGeom(ys);
    for (int i = 0; i < 7; i++) {
        Date d = addDays(start, i);
        int y = ys[i], h = ys[i + 1] - ys[i];
        bool isSel = i == sel;
        auto its = ag.itemsOn(d);
        SDL_Rect clip{24, y, 497, h};
        SDL_RenderSetClipRect(R, &clip);  // el contenido nunca se sale de su fila mientras anima
        if (isSel) { setCol(DIMC); fillBox(24, y, 497, h - 2); setCol(BRIGHT); rectLine(24, y, 497, h - 2, 2); }
        else { setCol(DIMC); fillRect(24, y + h - 2, 497, 1); }
        Col lc = (isSel || d == t) ? BRIGHT : MID;
        string label = string(DAYS_FULL[i]) + " " + to_string(d.d);
        if (isSel && g_weekA > 0.5f) {  // desplegado: lista completa del día
            drawText(32, y + 6, label, 2, lc);
            if (d == t) drawText(32 + textW(label, 2) + 12, y + 6, "HOY", 2, BRIGHT);
            int n = (int)its.size(), cap = max(1, (h - 34) / 18);
            weekScroll = max(0, min(weekScroll, n - cap));
            if (its.empty()) drawText(40, y + 30, "SIN ACTIVIDADES", 2, MID);
            for (int k = 0; k < cap && weekScroll + k < n; k++) {
                auto &it = its[weekScroll + k];
                drawTextMq(40, y + 30 + k * 18, itemLine(it), 2, itemCol(it), 465);
            }
            if (n > cap) {
                string info = to_string(weekScroll + 1) + "-" + to_string(min(n, weekScroll + cap)) + "/" + to_string(n);
                drawText(512 - textW(info, 2), y + 6, info, 2, MID);
            }
        } else if (!isSel && g_weekA > 0.5f) {  // otros días plegados: título y marcas
            drawText(32, y + 5, label, 2, lc);
            bool hasEv, hasRt, hasTk;
            kindsOf(its, hasEv, hasRt, hasTk);
            if (hasEv) drawSquare(474, y + 9, EVENT_GREEN);
            if (hasRt) drawSquare(486, y + 9, routineCol());
            if (hasTk) drawSquareHollow(498, y + 9, PAL[5].c);
        } else {  // contraído: hasta 3 actividades
            drawText(32, y + 6, label, 2, lc);
            if (d == t) drawText(32, y + 26, "HOY", 2, BRIGHT);
            for (int k = 0; k < (int)its.size() && k < 3; k++) {
                if (k == 2 && its.size() > 3) { drawText(190, y + 6 + k * 18, "+" + to_string(its.size() - 2) + " MAS", 2, MID); break; }
                drawTextMq(190, y + 6 + k * 18, itemLine(its[k]), 2, itemCol(its[k]), 325);
            }
        }
        SDL_RenderSetClipRect(R, nullptr);
    }
}

static void drawDay() {
    Date t = today();
    time_t n = time(nullptr);
    int nowH = localtime(&n)->tm_hour;
    string title = string(DAYS_FULL[weekday(cur)]) + " " + to_string(cur.d) + " de " + MONTHS[cur.m - 1] + " de " + to_string(cur.y);
    drawTextC(272, 70, title, 2, BRIGHT);
    auto its = ag.itemsOn(cur);
    for (int h = 0; h < 24; h++) {
        int y = DY + h * DH;
        bool sel = h == hourSel, any = false;
        for (auto &it : its) if (it.hour == h) any = true;
        if (sel) { setCol(BRIGHT, 200 + (int)(40 * sinf(g_now * 6))); fillBox(24, y, 497, DH); }
        else { setCol(DIMC); fillRect(24, y + DH - 1, 497, 1); }
        Col c = sel ? BGC : (any ? BRIGHT : MID);
        if (cur == t && h == nowH) drawText(26, y + 2, ">", 2, c);
        drawText(40, y + 2, timeStr(h, 0), 2, c);
        int x = 112;
        for (auto &it : its) {
            if (it.hour != h || x - 112 >= 405) continue;
            string sx = itemLine(it);
            drawTextMq(x, y + 2, sx, 2, sel ? BGC : itemCol(it), 405 - (x - 112));
            x += textW(sx, 2) + 36;
        }
    }
}

// HORARIO: filas = días de la semana, columnas = apartados (mañana / tarde / noche)
static int sectionOf(int hour) { return hour < 12 ? 0 : hour < 20 ? 1 : 2; }
static void drawRoutines() {
    static const char *SEC[3] = {"MAÑANA", "TARDE", "NOCHE"};
    drawTextC(272, 70, "HORARIO SEMANAL", 2, BRIGHT);
    for (int c = 0; c < 3; c++) drawTextC(RX0 + c * RCW + RCW / 2, 98, SEC[c], 2, MID);
    int wdToday = weekday(today());
    for (int r = 0; r < 7; r++) {
        int y = RY + r * RH;
        setCol(DIMC); fillRect(24, y + RH - 1, 497, 1);
        drawText(30, y + 6, DAYS_SHORT[r], 2, (r == rRow || r == wdToday) ? BRIGHT : MID);
        if (r == wdToday) drawText(30, y + 24, "HOY", 1, BRIGHT);
        auto list = ag.routinesOnWeekday(r);
        for (int c = 0; c < 3; c++) {
            int x = RX0 + c * RCW;
            bool sel = r == rRow && c == rCol;
            if (sel) { setCol(DIMC); fillBox(x, y + 1, RCW - 2, RH - 3); setCol(BRIGHT); rectLine(x, y + 1, RCW - 2, RH - 3, 2); }
            int k = 0, total = 0;
            for (int i : list) if (sectionOf(ag.rt[i].hour) == c) total++;
            for (int i : list) {
                if (sectionOf(ag.rt[i].hour) != c) continue;
                if (k == 4 && total > 5) { drawText(x + 6, y + 5 + k * 10, "+" + to_string(total - 4) + " MAS", 1, MID); break; }
                if (k >= 5) break;
                bool ended = !ag.rt[i].end.empty() && ag.rt[i].end < dateStr(today());  // terminadas: atenuadas
                drawTextMq(x + 6, y + 5 + k * 10, timeStr(ag.rt[i].hour, ag.rt[i].minute) + " " + ag.rt[i].title + (ag.rt[i].desc.empty() ? "" : " (+)"), 1, ended ? MID : routineCol(), RCW - 12);
                k++;
            }
        }
    }
    if (all_of(ag.rt.begin(), ag.rt.end(), [](const Routine &x) { return x.deleted; }))
        drawTextC(272 + 40, 150, "PULSA A PARA CREAR UNA RUTINA", 1, MID);
}

// ------------------------------------------------------------ Inventario
static vector<int> invList() {  // objetos visibles, agrupados por ruta
    vector<int> v;
    string f = lowerAscii(invFilter);
    for (int i = 0; i < (int)ag.th.size(); i++) {
        auto &t = ag.th[i];
        if (t.deleted || (invCarry && t.carry <= 0)) continue;
        if (!f.empty() && lowerAscii(t.name + " " + t.place).find(f) == string::npos) continue;
        v.push_back(i);
    }
    sort(v.begin(), v.end(), [](int a, int b) {
        string pa = lowerAscii(ag.th[a].place), pb = lowerAscii(ag.th[b].place);
        return pa != pb ? pa < pb : lowerAscii(ag.th[a].name) < lowerAscii(ag.th[b].name);
    });
    return v;
}
static int selThing() {
    auto v = invList();
    if (v.empty()) return -1;
    invSel = max(0, min(invSel, (int)v.size() - 1));
    return v[invSel];
}
static bool thingOk(int i) { return i >= 0 && i < (int)ag.th.size() && !ag.th[i].deleted; }
static const int IR0 = 104, IRH = 31, IROWS = 13;
static int invTop(int n) { return max(0, min(invSel - 6, n - IROWS)); }
static string dayMaskStr(int m) {
    if (m == 127) return "TODOS";
    if (m == 31) return "LU-VI";
    if (m == 96) return "FINDE";
    string r;
    for (int i = 0; i < 7; i++) if ((m >> i) & 1) { if (!r.empty()) r += ","; r += DAYS_SHORT[i]; }
    return r;
}

static SDL_Texture *makeTexLinear(SDL_Surface *s) {
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_Texture *t = SDL_CreateTextureFromSurface(R, s);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    return t;
}
static void freeTex(int sl) { if (g_tex[sl]) { SDL_DestroyTexture(g_tex[sl]); g_tex[sl] = nullptr; } g_texKey[sl].clear(); }
static void freeTexAll() { freeTex(0); freeTex(1); }
static SDL_Texture *loadImageTexture(const string &path, int &w, int &h) {
    SDL_Surface *s0 = IMG_Load(path.c_str());
    if (!s0) return nullptr;
    SDL_Surface *s = SDL_ConvertSurfaceFormat(s0, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(s0);
    if (!s) return nullptr;
    if (max(s->w, s->h) > 1024) {  // miniatura: basta con 1024 px
        float k = 1024.f / max(s->w, s->h);
        SDL_Surface *d = SDL_CreateRGBSurfaceWithFormat(0, max(1, (int)(s->w * k)), max(1, (int)(s->h * k)), 32, SDL_PIXELFORMAT_ARGB8888);
        if (d) { SDL_BlitScaled(s, nullptr, d, nullptr); SDL_FreeSurface(s); s = d; }
    }
    if (!g_imgColor) {  // monocromo: se tiñe de verde al dibujar
        SDL_LockSurface(s);
        for (int y = 0; y < s->h; y++) {
            Uint32 *row = (Uint32 *)((Uint8 *)s->pixels + y * s->pitch);
            for (int x = 0; x < s->w; x++) {
                Uint32 p = row[x];
                int l = (int)((((p >> 16) & 255) * 30 + ((p >> 8) & 255) * 59 + (p & 255) * 11) / 100);
                row[x] = (p & 0xFF000000u) | ((Uint32)l << 16) | ((Uint32)l << 8) | (Uint32)l;
            }
        }
        SDL_UnlockSurface(s);
    }
    w = s->w; h = s->h;
    SDL_Texture *t = makeTexLinear(s);
    SDL_FreeSurface(s);
    return t;
}
static void ensureTex(const Thing &t) {
    const string *names[2] = {&t.image, &t.image2};
    for (int sl = 0; sl < 2; sl++) {
        string key = names[sl]->empty() ? string() : *names[sl] + (g_imgColor ? "|c" : "|g");
        if (key == g_texKey[sl]) continue;
        freeTex(sl);
        g_texKey[sl] = key;
        if (!key.empty()) g_tex[sl] = loadImageTexture(string(IMG_DIR) + "/" + *names[sl], g_texW[sl], g_texH[sl]);
    }
}

// Copia la imagen a la carpeta imagenes/ (así sigue funcionando aunque muevas el original) y la asocia al objeto.
// Un objeto admite dos fotos: si ya tiene una, se pregunta cuál de las dos pasa a ser la principal.
static bool importImage(string p) {
    namespace fs = std::filesystem;
    p = trimStr(p);
    if (p.size() >= 2 && ((p.front() == '"' && p.back() == '"') || (p.front() == '\'' && p.back() == '\''))) p = p.substr(1, p.size() - 2);
    if (p.rfind("file://", 0) == 0) p = p.substr(7);
    int si = selThing();
    if (si < 0) { flash("SELECCIONA UN OBJETO"); return false; }
    if (!ag.th[si].image.empty() && !ag.th[si].image2.empty()) { flash("YA TIENE DOS FOTOS: W CAMBIA LA PRINCIPAL, Z QUITA LA SEGUNDA"); return false; }
    error_code ec;
    fs::path src = fs::u8path(p);
    string ext = lowerAscii(src.extension().u8string());
    if (ext != ".jpg" && ext != ".jpeg" && ext != ".png") { flash("SOLO JPG O PNG"); return false; }
    if (!fs::is_regular_file(src, ec)) { flash("ARCHIVO NO ENCONTRADO"); return false; }
    if (fs::file_size(src, ec) > 25u * 1024 * 1024) { flash("IMAGEN MAYOR DE 25 MB"); return false; }
    SDL_Surface *chk = IMG_Load(src.u8string().c_str());  // comprueba que de verdad es una imagen
    if (!chk) { flash("IMAGEN NO VALIDA"); return false; }
    SDL_FreeSurface(chk);
    string fname = ag.th[si].id + "_" + to_string(nowMs()) + ext;
    fs::create_directories(IMG_DIR, ec);
    fs::copy_file(src, fs::path(IMG_DIR) / fname, fs::copy_options::overwrite_existing, ec);
    if (ec) { flash("NO SE PUDO COPIAR"); return false; }
    if (ag.th[si].image.empty()) {
        ag.th[si].image = fname;
        ag.touchPhotos(si);
        setMood(M_THUMBS, 2.4f); flash("IMAGEN IMPORTADA");
        return true;
    }
    string id = ag.th[si].id;
    startAsk("NUEVA FOTO P/S [S]: ", "P = LA NUEVA PASA A PRINCIPAL Y LA ACTUAL A SECUNDARIA     S = LA ACTUAL SIGUE DE PRINCIPAL", "S",
             [si, id, fname](const string &v) {
                 if (!thingOk(si) || ag.th[si].id != id) return;
                 Thing &t = ag.th[si];
                 if (!v.empty() && (v[0] == 'p' || v[0] == 'P')) { t.image2 = t.image; t.image = fname; } else t.image2 = fname;
                 ag.touchPhotos(si);
                 setMood(M_THUMBS, 2.4f); flash("OBJETO CON DOS FOTOS");
             },
             [fname] { error_code e2; std::filesystem::remove(std::filesystem::path(IMG_DIR) / fname, e2); });
    return true;
}

// ----------------------------------------------------- Mascota con hoja de fotogramas
struct SpriteFrameRect { int x, y, w, h; };
static SDL_Texture *g_sprite = nullptr;
static vector<SpriteFrameRect> g_frames;
static float g_spScale = 1.f;

static vector<pair<int, int>> runsOf(const vector<char> &v, int minLen) {  // tramos consecutivos de "ocupado"
    vector<pair<int, int>> out;
    int st = -1;
    for (int i = 0; i <= (int)v.size(); i++) {
        bool on = i < (int)v.size() && v[i];
        if (on && st < 0) st = i;
        if (!on && st >= 0) { if (i - st >= minLen) out.push_back({st, i - 1}); st = -1; }
    }
    return out;
}
// Fotogramas = filas separadas por huecos vacíos, y dentro de cada fila, columnas separadas por huecos (orden de lectura).
static vector<SpriteFrameRect> detectFrames(const vector<char> &bg, int W, int H) {
    vector<SpriteFrameRect> fr;
    vector<char> rowAny(H, 0);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) if (!bg[(size_t)y * W + x]) { rowAny[y] = 1; break; }
    for (auto band : runsOf(rowAny, 20)) {
        vector<char> colAny(W, 0);
        for (int y = band.first; y <= band.second; y++) for (int x = 0; x < W; x++) if (!bg[(size_t)y * W + x]) colAny[x] = 1;
        for (auto c : runsOf(colAny, 20)) {
            int y0 = band.second, y1 = band.first;
            for (int y = band.first; y <= band.second; y++)
                for (int x = c.first; x <= c.second; x++) if (!bg[(size_t)y * W + x]) { y0 = min(y0, y); y1 = max(y1, y); break; }
            fr.push_back({c.first, y0, c.second - c.first + 1, y1 - y0 + 1});
        }
    }
    return fr;
}

static void loadSprite() {
    static const char *names[] = {"mascota.png", "Hoja_de_pegatinas_del_Chico_Retro_leyendo_su_agenda.png"};
    SDL_Surface *s0 = nullptr;
    for (auto n : names) if ((s0 = IMG_Load(n))) break;
    if (!s0) return;
    SDL_Surface *s = SDL_ConvertSurfaceFormat(s0, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(s0);
    if (!s) return;
    int W = s->w, H = s->h;
    SDL_LockSurface(s);
    auto at = [&](int x, int y) -> Uint32 & { return ((Uint32 *)((Uint8 *)s->pixels + (size_t)y * s->pitch))[x]; };
    auto whiteish = [&](int x, int y, int lim) {
        Uint32 p = at(x, y);
        return min((int)((p >> 16) & 255), min((int)((p >> 8) & 255), (int)(p & 255))) >= lim;
    };
    bool hasAlpha = false;
    for (int y = 0; y < H && !hasAlpha; y++) for (int x = 0; x < W; x++) if ((at(x, y) >> 24) < 200) { hasAlpha = true; break; }

    vector<char> bg((size_t)W * H, 0);  // true = fondo (transparente)
    if (hasAlpha) {
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) bg[(size_t)y * W + x] = (at(x, y) >> 24) < 16;
    } else {  // sin transparencia: el blanco conectado con los bordes es fondo
        vector<int> st;
        auto push = [&](int x, int y) {
            size_t i = (size_t)y * W + x;
            if (!bg[i] && whiteish(x, y, 232)) { bg[i] = 1; st.push_back((int)i); }
        };
        for (int x = 0; x < W; x++) { push(x, 0); push(x, H - 1); }
        for (int y = 0; y < H; y++) { push(0, y); push(W - 1, y); }
        while (!st.empty()) {
            int i = st.back(); st.pop_back();
            int x = i % W, y = i / W;
            if (x > 0) push(x - 1, y);
            if (x < W - 1) push(x + 1, y);
            if (y > 0) push(x, y - 1);
            if (y < H - 1) push(x, y + 1);
        }
        vector<char> halo;  // quita el borde casi blanco pegado al fondo (antialiasing)
        halo = bg;
        for (int y = 1; y < H - 1; y++)
            for (int x = 1; x < W - 1; x++) {
                size_t i = (size_t)y * W + x;
                if (!bg[i] && whiteish(x, y, 205) && (bg[i - 1] || bg[i + 1] || bg[i - W] || bg[i + W])) halo[i] = 1;
            }
        bg.swap(halo);
    }
    // color -> monocromo verde fósforo (la luminosidad se conserva; los contornos negros quedan oscuros)
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            Uint32 p = at(x, y);
            if (bg[(size_t)y * W + x]) { at(x, y) = 0; continue; }
            int a = hasAlpha ? (int)(p >> 24) : 255;
            double lum = (0.30 * ((p >> 16) & 255) + 0.59 * ((p >> 8) & 255) + 0.11 * (p & 255)) / 255.0;
            double l = pow(lum, 0.85) * 255.0;
            at(x, y) = ((Uint32)a << 24) | ((Uint32)(l * 0.43) << 16) | ((Uint32)l << 8) | (Uint32)(l * 0.55);
        }
    SDL_UnlockSurface(s);
    g_frames = detectFrames(bg, W, H);
    if (g_frames.empty() || g_frames.size() > 64) g_frames = {{0, 0, W, H}};
    g_sprite = makeTexLinear(s);
    SDL_FreeSurface(s);
    if (!g_sprite) return;
    SDL_SetTextureBlendMode(g_sprite, SDL_BLENDMODE_BLEND);
    int mw = 1, mh = 1;
    for (auto &f : g_frames) { mw = max(mw, f.w); mh = max(mh, f.h); }
    g_spScale = min(226.f / mw, 236.f / mh);
}

// Fotograma según el estado: lectura con pasar de página en reposo, guiño, y "¡eureka!" en los avisos.
static int spriteFrame(float t) {
    int n = (int)g_frames.size();
    auto cl = [&](int i) { return min(i, n - 1); };
    if (g_mood == M_THUMBS) return cl(11);
    if (g_mood == M_WINK) return cl(3);
    if (g_mood == M_WAVE) return cl(fmodf(g_now, 0.5f) < 0.25f ? 11 : 10);
    static const int SEQ[] = {0, 0, 0, 1, 2, 4, 5, 6, 7, 8, 9, 10};
    return cl(SEQ[(int)(t * 5) % 12]);
}
static void drawSprite(int cx, float t) {
    const SpriteFrameRect &f = g_frames[spriteFrame(t)];
    int w = (int)(f.w * g_spScale), h = (int)(f.h * g_spScale);
    int bob = (int)(sinf(t * 2.5f) * 2);
    SDL_Rect src{f.x, f.y, f.w, f.h}, dst{cx - w / 2, 310 - h + bob, w, h};  // anclado al suelo, centrado
    Uint8 m = (Uint8)(255 * g_flick);
    SDL_SetTextureColorMod(g_sprite, m, m, m);
    SDL_RenderCopy(R, g_sprite, &src, &dst);
}

static void drawInventory() {
    auto lst = invList();
    int n = (int)lst.size();
    invSel = max(0, min(invSel, max(0, n - 1)));
    drawText(32, 72, "INVENTARIO", 2, BRIGHT);
    const char *tabs[2] = {"GUARDADO", "LLEVO"};
    int tx[2] = {290, 416}, tw[2] = {118, 78};
    for (int i = 0; i < 2; i++) {
        if ((i == 1) == invCarry) { setCol(BRIGHT); fillBox(tx[i], 66, tw[i], 24); drawText(tx[i] + 8, 71, tabs[i], 2, BGC); }
        else { setCol(MID); rectLine(tx[i], 66, tw[i], 24, 2); drawText(tx[i] + 8, 71, tabs[i], 2, MID); }
    }
    if (!invFilter.empty()) drawText(32, 92, "FILTRO: " + invFilter, 1, MID, 470);
    if (n == 0) {
        drawTextC(272, 220, invCarry ? "NO LLEVAS NADA" : "SIN OBJETOS", 2, MID);
        drawTextC(272, 246, invCarry ? "USA L EN GUARDADO PARA LLEVAR" : "PULSA A PARA AÑADIR", 1, MID);
        return;
    }
    int top = invTop(n);
    for (int k = 0; k < IROWS && top + k < n; k++) {
        auto &t = ag.th[lst[top + k]];
        int y = IR0 + k * IRH;
        bool sel = top + k == invSel;
        if (sel) { setCol(DIMC); fillBox(24, y, 497, IRH - 2); setCol(BRIGHT); rectLine(24, y, 497, IRH - 2, 2); }
        else { setCol(DIMC); fillRect(24, y + IRH - 2, 497, 1); }
        drawTextMq(32, y + 4, t.name + (t.desc.empty() ? "" : " (+)"), 2, BRIGHT, 240);
        if (!t.image.empty()) drawText(280, y + 8, t.image2.empty() ? "FOTO" : "2 FOTOS", 1, MID);
        if (t.notify) drawText(340, y + 4, "!", 2, BRIGHT);
        string q = "X" + to_string(invCarry ? t.carry : t.qty);
        drawText(512 - textW(q, 2), y + 4, q, 2, BRIGHT);
        string sub = invCarry ? "DE " + to_string(t.qty) + "  " + t.place
                              : (t.carry > 0 ? "LLEVO " + to_string(t.carry) + "  " : string()) + t.place;
        drawTextMq(32, y + 21, "> " + sub, 1, MID, 470);
    }
}

static void drawInvPanel() {
    box(540, 358, 250, 182);
    int si = selThing();
    if (si < 0) { drawText(552, 368, "SIN SELECCION", 2, MID); return; }
    Thing &t = ag.th[si];
    ensureTex(t);
    const int fy[2] = {364, 450};
    for (int sl = 0; sl < 2; sl++) {  // principal arriba, secundaria abajo
        setCol(MID); rectLine(548, fy[sl], 84, 84, 2);
        if (g_tex[sl]) {
            float sc = min(76.f / g_texW[sl], 76.f / g_texH[sl]);
            int w = (int)(g_texW[sl] * sc), h = (int)(g_texH[sl] * sc);
            SDL_Rect dst{552 + (76 - w) / 2, fy[sl] + 4 + (76 - h) / 2, w, h};
            if (g_imgColor) SDL_SetTextureColorMod(g_tex[sl], 255, 255, 255); else SDL_SetTextureColorMod(g_tex[sl], 110, 255, 140);
            SDL_RenderCopy(R, g_tex[sl], nullptr, &dst);
        } else {
            const string &nm = sl ? t.image2 : t.image;
            drawTextC(590, fy[sl] + 38, nm.empty() ? (sl ? "2A FOTO" : "I: IMAGEN") : "NO IMG", 1, MID);
        }
    }
    drawTextMq(640, 368, t.name, 1, BRIGHT, 146);
    drawText(640, 382, "CANT X" + to_string(t.qty), 1, MID);
    drawText(640, 394, "LLEVO X" + to_string(t.carry), 1, MID);
    if (t.notify) { drawText(640, 406, "AVISO " + timeStr(t.nh, t.nm), 1, BRIGHT); drawText(640, 418, dayMaskStr(t.nmask), 1, BRIGHT, 146); }
    else drawText(640, 406, "SIN AVISO", 1, MID);
    int y = 434;
    for (auto &l : wrapLines(t.desc, 24)) { if (y > 464) break; drawText(640, y, l, 1, BRIGHT); y += 10; }
    y = max(y + 4, 474);
    drawText(640, y, "RUTA:", 1, MID); y += 10;
    for (auto &l : wrapGlyphs(t.place.empty() ? "SIN RUTA" : t.place, 24)) { if (y > 530) break; drawText(640, y, l, 1, MID); y += 10; }
}

// --------------------------------------------------------- Tareas, notas y ficha editable
static string prioName(int p) { return p == 2 ? "ALTA" : p == 1 ? "MEDIA" : "BAJA"; }
static string oneLine(string s) { for (auto &ch : s) if (ch == '\n') ch = ' '; return s; }

static vector<int> taskList() {  // pendientes o hechas, por fecha límite, prioridad y nombre
    vector<int> v;
    string f = lowerAscii(tkFilter);
    for (int i = 0; i < (int)ag.tk.size(); i++) {
        auto &x = ag.tk[i];
        if (x.deleted || x.done != tkDone) continue;
        if (!f.empty() && lowerAscii(x.title + " " + x.desc).find(f) == string::npos) continue;
        v.push_back(i);
    }
    sort(v.begin(), v.end(), [](int a, int b) {
        const AgTask &x = ag.tk[a], &y = ag.tk[b];
        string dx = x.due.empty() ? "9999" : x.due, dy = y.due.empty() ? "9999" : y.due;
        if (dx != dy) return dx < dy;
        if (x.pri != y.pri) return x.pri > y.pri;
        return lowerAscii(x.title) < lowerAscii(y.title);
    });
    return v;
}
static vector<int> noteList() {  // fijadas primero, luego las más recientes
    vector<int> v;
    string f = lowerAscii(ntFilter);
    for (int i = 0; i < (int)ag.nt.size(); i++) {
        auto &x = ag.nt[i];
        if (x.deleted) continue;
        if (!f.empty() && lowerAscii(x.title + " " + x.text).find(f) == string::npos) continue;
        v.push_back(i);
    }
    sort(v.begin(), v.end(), [](int a, int b) {
        const AgNote &x = ag.nt[a], &y = ag.nt[b];
        if (x.pin != y.pin) return x.pin;
        return x.updated > y.updated;
    });
    return v;
}
static int selTask() { auto v = taskList(); if (v.empty()) return -1; tkSel = max(0, min(tkSel, (int)v.size() - 1)); return v[tkSel]; }
static int selNote() { auto v = noteList(); if (v.empty()) return -1; ntSel = max(0, min(ntSel, (int)v.size() - 1)); return v[ntSel]; }
static int listTop(int sel, int n) { return max(0, min(sel - 6, n - IROWS)); }
static Col taskCol(const AgTask &x) {
    if (x.done) return MID;
    return (!x.due.empty() && x.due < dateStr(today())) ? PAL[4].c : PAL[5].c;
}

static void drawTabPair(const char *a, const char *b, bool second, int x0, int wa, int wb) {  // dos pestañas pequeñas (como GUARDADO / LLEVO)
    const char *t[2] = {a, b};
    int tx[2] = {x0, x0 + wa + 10}, tw[2] = {wa, wb};
    for (int i = 0; i < 2; i++) {
        if ((i == 1) == second) { setCol(BRIGHT); fillBox(tx[i], 66, tw[i], 24); drawText(tx[i] + 8, 71, t[i], 2, BGC); }
        else { setCol(MID); rectLine(tx[i], 66, tw[i], 24, 2); drawText(tx[i] + 8, 71, t[i], 2, MID); }
    }
}

static void drawTasks() {
    auto lst = taskList();
    int n = (int)lst.size();
    tkSel = max(0, min(tkSel, max(0, n - 1)));
    drawText(32, 72, "TAREAS", 2, BRIGHT);
    drawTabPair("PENDIENTES", "HECHAS", tkDone, 150, 136, 88);
    if (!tkFilter.empty()) drawText(32, 92, "BUSCAR: " + tkFilter, 1, MID, 470);
    if (n == 0) {
        drawTextC(272, 220, tkDone ? "NO HAY TAREAS HECHAS" : "NO HAY TAREAS PENDIENTES", 2, MID);
        drawTextC(272, 246, "PULSA A PARA AÑADIR UNA", 1, MID);
        return;
    }
    int top = listTop(tkSel, n);
    for (int k = 0; k < IROWS && top + k < n; k++) {
        auto &x = ag.tk[lst[top + k]];
        int y = IR0 + k * IRH;
        bool sel = top + k == tkSel;
        if (sel) { setCol(DIMC); fillBox(24, y, 497, IRH - 2); setCol(BRIGHT); rectLine(24, y, 497, IRH - 2, 2); }
        else { setCol(DIMC); fillRect(24, y + IRH - 2, 497, 1); }
        Col c = taskCol(x);
        drawText(32, y + 4, x.done ? "[X]" : "[ ]", 2, c);
        string t = string(x.pri == 2 ? "!!! " : x.pri == 1 ? "! " : "") + x.title + (x.desc.empty() ? "" : " (+)");
        drawTextMq(76, y + 4, t, 2, c, 292);
        if (x.done) { setCol(MID); fillRect(76, y + 11, min(textW(t, 2), 292), 2); }
        string due = x.due.empty() ? string("SIN FECHA") : dmyStr(x.due) + (x.dt >= 0 ? " " + timeStr(x.dt / 60, x.dt % 60) : "");
        drawText(512 - textW(due, 2), y + 4, due, 2, x.due.empty() ? MID : c);
        drawTextMq(32, y + 21, "> " + (x.desc.empty() ? string("PRIORIDAD ") + prioName(x.pri) : oneLine(x.desc)), 1, MID, 470);
    }
}

static void drawNotes() {
    auto lst = noteList();
    int n = (int)lst.size();
    ntSel = max(0, min(ntSel, max(0, n - 1)));
    drawText(32, 72, "NOTAS", 2, BRIGHT);
    drawText(130, 75, clip(n, "NOTA", "NOTAS"), 1, MID);
    if (!ntFilter.empty()) drawText(32, 92, "BUSCAR: " + ntFilter, 1, MID, 470);
    if (n == 0) {
        drawTextC(272, 220, "SIN NOTAS", 2, MID);
        drawTextC(272, 246, "PULSA A PARA AÑADIR UNA", 1, MID);
        return;
    }
    int top = listTop(ntSel, n);
    for (int k = 0; k < IROWS && top + k < n; k++) {
        auto &x = ag.nt[lst[top + k]];
        int y = IR0 + k * IRH;
        bool sel = top + k == ntSel;
        if (sel) { setCol(DIMC); fillBox(24, y, 497, IRH - 2); setCol(BRIGHT); rectLine(24, y, 497, IRH - 2, 2); }
        else { setCol(DIMC); fillRect(24, y + IRH - 2, 497, 1); }
        drawTextMq(32, y + 4, string(x.pin ? "* " : "") + x.title, 2, BRIGHT, 480);
        drawTextMq(32, y + 21, "> " + (x.text.empty() ? string("SIN TEXTO") : oneLine(x.text)), 1, MID, 470);
    }
}

static void drawTaskPanel() {
    box(540, 358, 250, 182);
    int si = selTask();
    if (si < 0) { drawText(552, 368, "SIN SELECCION", 2, MID); return; }
    auto &x = ag.tk[si];
    drawText(552, 366, x.done ? "TAREA HECHA" : "TAREA", 2, taskCol(x));
    int y = 388;
    for (auto &l : wrapLines(x.title, 38)) { if (y > 410) break; drawText(552, y, l, 1, BRIGHT); y += 10; }
    y += 4;
    drawText(552, y, "LIMITE: " + (x.due.empty() ? string("SIN FECHA") : x.due + (x.dt >= 0 ? " " + timeStr(x.dt / 60, x.dt % 60) : "")), 1, MID); y += 12;
    drawText(552, y, "PRIORIDAD: " + prioName(x.pri), 1, MID); y += 12;
    if (!x.due.empty() && !x.done && x.due < dateStr(today())) { drawText(552, y, "VENCIDA", 1, PAL[4].c); y += 12; }
    y += 2;
    for (auto &l : wrapLines(x.desc, 38)) { if (y > 530) break; drawText(552, y, l, 1, MID); y += 10; }
}
static void drawNotePanel() {
    box(540, 358, 250, 182);
    int si = selNote();
    if (si < 0) { drawText(552, 368, "SIN SELECCION", 2, MID); return; }
    auto &x = ag.nt[si];
    drawText(552, 366, x.pin ? "NOTA FIJADA" : "NOTA", 2, BRIGHT);
    int y = 388;
    for (auto &l : wrapLines(x.title, 38)) { if (y > 400) break; drawText(552, y, l, 1, BRIGHT); y += 10; }
    y += 4;
    auto lines = wrapLines(x.text, 38);
    int cap = (532 - y) / 10, total = (int)lines.size();
    ntScroll = max(0, min(ntScroll, max(0, total - cap)));
    for (int k = 0; k < cap && ntScroll + k < total; k++) drawText(552, y + k * 10, lines[ntScroll + k], 1, MID);
    if (total > cap) { string info = to_string(ntScroll + 1) + "-" + to_string(min(total, ntScroll + cap)) + "/" + to_string(total); drawText(782 - textW(info, 1), 370, info, 1, MID); }
}

// Ficha editable (tecla E): lista de campos numerados; se elige uno y se escribe el valor nuevo.
static void drawEditor() {
    if (!g_ed.on) return;
    setCol(BGC); fillBox(28, 68, 489, 464);
    setCol(BRIGHT); rectLine(28, 68, 489, 464, 2);
    drawText(40, 80, "FICHA: " + g_ed.title, 2, BRIGHT);
    int y = 112;
    string longTxt, longLbl;
    for (size_t k = 0; k < g_ed.f.size(); k++) {
        string v = g_ed.f[k].get();
        if (g_ed.f[k].label == "DESCRIPCION" || g_ed.f[k].label == "TEXTO") { longTxt = v; longLbl = g_ed.f[k].label; }
        v = oneLine(v);
        if (v.empty()) v = "-";
        drawText(40, y, to_string(k + 1) + " " + g_ed.f[k].label, 2, MID);
        drawTextMq(222, y, v, 2, BRIGHT, 285);
        y += 26;
    }
    if (!longTxt.empty()) {
        y += 8;
        drawText(40, y, longLbl + " COMPLETA", 1, MID); y += 14;
        for (auto &l : wrapLines(longTxt, 77)) { if (y > 480) break; drawText(40, y, l, 1, BRIGHT); y += 10; }
    }
    if (!g_ed.status.empty()) drawText(40, 490, g_ed.status, 1, BRIGHT, 465);
    drawText(40, 508, "NUMERO + ENTER EDITA UN CAMPO    X BORRAR    ENTER VACIO O ESC SALE", 1, MID, 470);
}

// ----------------------------------------------------------------- Minijuegos
enum GameId { G_MENU, G_HACK, G_LOCK, G_DEF };
static GameId gGame = G_MENU;
static bool g2P = false, gEnded = false, g_keyRepeat = false;
static int gMenuSel = 0, gBest[3][2] = {{0, 0}, {0, 0}, {0, 0}};
static games::Hack gHack;
static games::Lock gLock;
static games::Def gDef;
struct HRect { int x, y, w, h, idx; };
static vector<HRect> g_hackRects;
static const char *SCORES_FILE = "agenda_juegos.txt";
static const char *GAME_NAMES[3] = {"HACKEO", "GANZUA", "DEFENSA"};
static Col playerCol(int p) { return p ? PAL[2].c : BRIGHT; }  // J1 verde, J2 naranja

static void loadBest() { ifstream f(SCORES_FILE); for (auto &r : gBest) for (int &v : r) { v = 0; f >> v; } }
static void saveBest() { ofstream f(SCORES_FILE); for (auto &r : gBest) for (int v : r) f << v << " "; }
static bool gameOver() {
    return gGame == G_HACK ? (gHack.won || gHack.lost) : gGame == G_LOCK ? gLock.over : gGame == G_DEF ? gDef.over : false;
}
static int gameScore() { return gGame == G_HACK ? gHack.score() : gGame == G_LOCK ? gLock.score : gGame == G_DEF ? gDef.score : 0; }
static void checkEnd() {
    if (gGame == G_MENU || gEnded || !gameOver()) return;
    gEnded = true;
    int sc = gameScore(), &best = gBest[gGame - 1][g2P ? 1 : 0];
    bool rec = sc > best;
    if (rec) { best = sc; saveBest(); }
    setMood(sc > 0 ? M_THUMBS : M_WINK, 2.4f);
    flash("PUNTOS: " + to_string(sc) + (rec ? "  NUEVO RECORD!" : ""));
}

struct Pad { float dx = 0, dy = 0; bool act = false; };
static bool g_prevAct[2] = {true, true};
static void startGame(GameId g) {
    gGame = g; gEnded = false;
    g_prevAct[0] = g_prevAct[1] = true;  // una tecla aún pulsada no cuenta como disparo
    if (g == G_HACK) gHack.init(g2P); else if (g == G_LOCK) gLock.init(); else if (g == G_DEF) gDef.init(g2P);
}
// J1: W A S D + ESPACIO.  J2: flechas + ENTER.  En un jugador valen ambos juegos de teclas.
static void readPads(Pad out[2]) {
    const Uint8 *k = SDL_GetKeyboardState(nullptr);
    auto axis = [&](int neg, int pos) { return (k[pos] ? 1.f : 0.f) - (k[neg] ? 1.f : 0.f); };
    Pad a, b;
    a.dx = axis(SDL_SCANCODE_A, SDL_SCANCODE_D); a.dy = axis(SDL_SCANCODE_W, SDL_SCANCODE_S); a.act = k[SDL_SCANCODE_SPACE] != 0;
    b.dx = axis(SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT); b.dy = axis(SDL_SCANCODE_UP, SDL_SCANCODE_DOWN);
    b.act = k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER];
    if (!g2P) { a.dx = games::clampf(a.dx + b.dx, -1, 1); a.dy = games::clampf(a.dy + b.dy, -1, 1); a.act = a.act || b.act; b = Pad(); }
    out[0] = a; out[1] = b;
}
static void gamesUpdate(float dt) {
    if (gGame == G_MENU) return;
    Pad p[2];
    readPads(p);
    if (!gameOver()) {
        if (gGame == G_LOCK) {  // coop: J1 mueve la ganzúa, J2 hace girar
            gLock.update(dt, p[0].dx, g2P ? p[1].act : p[0].act);
            if (gLock.opened) { setMood(M_THUMBS, 1.2f); flash("CERRADURA ABIERTA!"); }
            if (gLock.broke) { setMood(M_WINK, 1.0f); flash("SE ROMPIO LA GANZUA"); }
        } else if (gGame == G_DEF) {
            for (int i = 0; i < gDef.nb; i++) {
                gDef.cx[i] = games::clampf(gDef.cx[i] + p[i].dx * dt * 230.f, 0, games::Def::W);
                gDef.cy[i] = games::clampf(gDef.cy[i] + p[i].dy * dt * 230.f, 0, games::Def::GROUND - 12);
                if (p[i].act && !g_prevAct[i]) gDef.fire(i);
            }
            gDef.update(dt);
        }
    }
    g_prevAct[0] = p[0].act; g_prevAct[1] = p[1].act;
    checkEnd();
}

static bool gamesKey(SDL_Keycode k) {  // true = tecla consumida
    bool pass = k >= SDLK_1 && k <= SDLK_8;  // cambiar de pestaña siempre se puede
    bool act = k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE;
    if (gGame == G_MENU) {
        switch (k) {
            case SDLK_UP: gMenuSel = (gMenuSel + 2) % 3; return true;
            case SDLK_DOWN: gMenuSel = (gMenuSel + 1) % 3; return true;
            case SDLK_LEFT: g2P = false; return true;
            case SDLK_RIGHT: g2P = true; return true;
            case SDLK_TAB: g2P = !g2P; return true;
            case SDLK_ESCAPE: view = MONTH; return true;
            default:
                if (act) { if (!g_keyRepeat) startGame((GameId)(gMenuSel + 1)); return true; }
                return !(pass || k == SDLK_q);
        }
    }
    if (pass) return false;
    if (k == SDLK_ESCAPE) { gGame = G_MENU; return true; }
    if (gameOver()) { if (act && !g_keyRepeat) startGame(gGame); return true; }
    if (gGame == G_HACK) {
        int p = -1, dx = 0, dy = 0;
        bool isAct = false, second = g2P;
        switch (k) {
            case SDLK_a: p = 0; dx = -1; break;
            case SDLK_d: p = 0; dx = 1; break;
            case SDLK_w: p = 0; dy = -1; break;
            case SDLK_s: p = 0; dy = 1; break;
            case SDLK_SPACE: p = 0; isAct = true; break;
            case SDLK_LEFT: p = second ? 1 : 0; dx = -1; break;
            case SDLK_RIGHT: p = second ? 1 : 0; dx = 1; break;
            case SDLK_UP: p = second ? 1 : 0; dy = -1; break;
            case SDLK_DOWN: p = second ? 1 : 0; dy = 1; break;
            case SDLK_RETURN: case SDLK_KP_ENTER: p = second ? 1 : 0; isAct = true; break;
            default: break;
        }
        if (p < 0) return true;
        if (isAct) { if (!g_keyRepeat && (!g2P || gHack.turn == p)) { gHack.guess(gHack.cur[p]); checkEnd(); } }
        else gHack.move(p, dx, dy);
    }
    return true;  // GANZUA y DEFENSA se controlan leyendo el estado del teclado en gamesUpdate
}

static void gamesClick(int mx, int my) {
    if (gGame == G_MENU) {
        if (my >= 106 && my < 132) { if (mx >= 110 && mx < 240) g2P = false; else if (mx >= 250 && mx < 416) g2P = true; return; }
        for (int i = 0; i < 3; i++)
            if (my >= 146 + i * 96 && my < 232 + i * 96 && mx >= 28 && mx < 517) { gMenuSel = i; startGame((GameId)(i + 1)); return; }
        return;
    }
    if (mx < 24 || mx > 521 || my < 64) return;
    if (gameOver()) { startGame(gGame); return; }
    if (gGame == G_DEF) gDef.fire(0);
    else if (gGame == G_HACK) {
        int p = g2P ? gHack.turn : 0;
        for (auto &r : g_hackRects)
            if (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h) {
                if (gHack.cur[p] == r.idx) { gHack.guess(r.idx); checkEnd(); } else gHack.cur[p] = r.idx;
                return;
            }
    }
}

static void drawOverlay(const string &title, const string &sub) {
    setCol(BGC, 235); fillBox(72, 250, 400, 100);
    setCol(BRIGHT); rectLine(72, 250, 400, 100, 2);
    drawTextC(272, 268, title, 3, BRIGHT);
    drawTextC(272, 316, sub, 1, MID);
}

static void drawGameMenu() {
    drawTextC(272, 70, "MINIJUEGOS", 3, BRIGHT);
    const char *pl[2] = {"1 JUGADOR", "2 COOP LOCAL"};
    int px[2] = {110, 250}, pw[2] = {130, 166};
    for (int i = 0; i < 2; i++) {
        if ((i == 1) == g2P) { setCol(BRIGHT); fillBox(px[i], 106, pw[i], 26); drawText(px[i] + 10, 112, pl[i], 2, BGC); }
        else { setCol(MID); rectLine(px[i], 106, pw[i], 26, 2); drawText(px[i] + 10, 112, pl[i], 2, MID); }
    }
    static const char *desc[3][2] = {
        {"ADIVINA LA CONTRASEÑA DEL TERMINAL. CADA INTENTO REVELA CUANTAS", "LETRAS ESTAN EN SU SITIO. 4 INTENTOS. EN COOP SE TURNAN."},
        {"MUEVE LA GANZUA, ENCUENTRA EL PUNTO EXACTO Y GIRA LA CERRADURA", "SIN FORZARLA. EN COOP: J1 MUEVE LA GANZUA Y J2 HACE LA TENSION."},
        {"INTERCEPTA LOS MISILES ANTES DE QUE DESTRUYAN TUS CIUDADES.", "EN COOP HAY DOS BASES: CADA JUGADOR DISPARA DESDE LA SUYA."}};
    for (int i = 0; i < 3; i++) {
        int y = 146 + i * 96;
        bool sel = i == gMenuSel;
        if (sel) { setCol(DIMC); fillBox(28, y, 489, 86); setCol(BRIGHT); rectLine(28, y, 489, 86, 2); }
        else { setCol(MID); rectLine(28, y, 489, 86, 1); }
        drawText(44, y + 10, GAME_NAMES[i], 3, sel ? BRIGHT : MID);
        string best = "MEJOR " + to_string(gBest[i][g2P ? 1 : 0]);
        drawText(508 - textW(best, 2), y + 14, best, 2, MID);
        drawText(44, y + 46, desc[i][0], 1, MID);
        drawText(44, y + 58, desc[i][1], 1, MID);
    }
    drawText(32, 440, "J1: W A S D + ESPACIO      J2: FLECHAS + ENTER      RATON EN DEFENSA", 1, MID);
    drawText(32, 454, "EN 1 JUGADOR SIRVEN LOS DOS JUEGOS DE TECLAS A LA VEZ", 1, MID);
}

static void drawHack() {
    using games::Hack;
    auto &h = gHack;
    drawText(32, 72, "HACKEO DE TERMINAL", 2, BRIGHT);
    drawText(318, 72, "INTENTOS", 2, MID);
    for (int i = 0; i < Hack::MAXTRIES; i++) {
        if (i < h.tries) drawSquare(432 + i * 16, 76, EVENT_GREEN);
        else { setCol(MID); rectLineSq(431 + i * 16, 75, 9, 9, 1); }
    }
    if (h.two) drawText(32, 96, string("TURNO: JUGADOR ") + to_string(h.turn + 1), 2, playerCol(h.turn));
    else drawText(32, 96, "ELIGE UNA PALABRA", 2, MID);
    g_hackRects.clear();
    const int colx[2] = {32, 272};
    for (int c = 0; c < 2; c++)
        for (int r = 0; r < Hack::ROWS; r++) {
            int ln = c * Hack::ROWS + r, y = 124 + r * 22, x0 = colx[c] + 56;
            char ad[8];
            snprintf(ad, sizeof ad, "%04X", 0xF000 + ln * 14);
            drawText(colx[c], y, ad, 2, MID);
            int wi = -1;
            for (int i = 0; i < Hack::N; i++) if (h.wline[i] == ln) wi = i;
            const string &line = h.lines[ln];
            if (wi < 0) { drawText(x0, y, line, 2, MID); continue; }
            int off = h.woff[wi], wx = x0 + off * 12, ww = Hack::WLEN * 12 + 4;
            drawText(x0, y, line.substr(0, off), 2, MID);
            drawText(x0 + (off + Hack::WLEN) * 12, y, line.substr(off + Hack::WLEN), 2, MID);
            g_hackRects.push_back({wx - 2, y - 3, ww, 20, wi});
            bool act0 = h.cur[0] == wi && (!h.two || h.turn == 0), act1 = h.two && h.cur[1] == wi && h.turn == 1;
            string txt = h.used[wi] ? string(Hack::WLEN, '-') : h.words[wi];
            Col tc = h.used[wi] ? MID : BRIGHT;
            if (act0 || act1) { setCol(playerCol(act1 ? 1 : 0)); fillBox(wx - 2, y - 3, ww, 20); tc = BGC; }
            else {
                if (h.cur[0] == wi) { setCol(playerCol(0)); rectLine(wx - 2, y - 3, ww, 20, 2); }
                if (h.two && h.cur[1] == wi) { setCol(playerCol(1)); rectLine(wx - 2, y - 3, ww, 20, 2); }
            }
            drawText(wx, y, txt, 2, tc);
        }
    drawText(32, 352, "REGISTRO", 2, MID);
    int n = (int)h.log.size(), st = max(0, n - 4);
    for (int k = st; k < n; k++) drawText(32, 374 + (k - st) * 18, "> " + h.log[k], 2, BRIGHT, 470);
    if (h.won) drawOverlay("ACCESO CONCEDIDO", "PUNTOS " + to_string(h.score()) + "   ENTER: OTRA VEZ   ESC: MENU");
    else if (h.lost) drawOverlay("TERMINAL BLOQUEADO", "LA CLAVE ERA " + h.words[h.answer] + "   ENTER: REINTENTAR   ESC: MENU");
}

static void drawLock() {
    auto &L = gLock;
    drawText(32, 72, "GANZUA", 2, BRIGHT);
    drawText(140, 72, "NIVEL " + to_string(L.level) + "  PUNTOS " + to_string(L.score), 2, MID);
    drawText(32, 96, "GANZUAS", 2, MID);
    for (int i = 0; i < L.picks; i++) drawSquare(132 + i * 14, 99, EVENT_GREEN);
    float sx = L.shake > 0 ? sinf(g_now * 90.f) * 2.5f : 0.f, sy = L.shake > 0 ? cosf(g_now * 70.f) * 2.f : 0.f;
    int cx = 262 + (int)sx, cy = 310 + (int)sy;
    blob(cx, cy, 128, 128, 4, DIMC, MID);
    blob(cx, cy, 98, 98, 3, BGC, MID);
    float phi = L.turn * PI / 2, ux = cosf(phi), uy = sinf(phi);  // la ranura gira con la cerradura
    setCol(BRIGHT);
    thickLine(cx - ux * 66, cy - uy * 66, cx + ux * 66, cy + uy * 66, 14);
    setCol(BGC); fillEllipse(cx, cy, 9, 9);
    float pa = L.pick * PI / 180.f, dx = -cosf(pa), dy = sinf(pa);  // la ganzúa barre la mitad inferior
    setCol(PAL[7].c);
    thickLine(cx, cy, cx + dx * 150, cy + dy * 150, 5);
    fillEllipse((int)(cx + dx * 150), (int)(cy + dy * 150), 7, 7);
    drawText(462, 124, "GIRO", 1, MID);
    drawText(492, 124, "FORZ", 1, MID);
    setCol(MID); ringRound(462, 136, 20, 220, 8, 2); ringRound(492, 136, 20, 220, 8, 2);
    int th = (int)(L.turn * 212), sh = (int)(games::clampf(L.stress / 0.9f, 0.f, 1.f) * 212);
    setCol(BRIGHT); if (th > 0) fillBox(466, 138 + 212 - th, 12, th);
    setCol(PAL[4].c); if (sh > 0) fillBox(496, 138 + 212 - sh, 12, sh);
    if (L.over) drawOverlay("SIN GANZUAS", "PUNTOS " + to_string(L.score) + "   ENTER: OTRA VEZ   ESC: MENU");
}

static void drawDef() {
    auto &D = gDef;
    const int ox = 24, oy = 64, GY = oy + (int)games::Def::GROUND;
    drawText(32, 70, "OLEADA " + to_string(D.wave) + "  PUNTOS " + to_string(D.score), 2, BRIGHT);
    setCol(MID); fillRect(ox + 8, GY, 481, 3);
    setCol(DIMC); fillBox(ox + 8, GY + 3, 481, 30);
    for (auto &c : D.cities) {
        int x = ox + (int)c.x;
        if (c.alive) { setCol(EVENT_GREEN); fillBox(x - 14, GY - 16, 28, 16); fillRect(x - 8, GY - 24, 6, 8); fillRect(x + 3, GY - 22, 5, 6); }
        else { setCol(MID); fillRect(x - 10, GY - 3, 5, 3); fillRect(x - 2, GY - 5, 5, 5); fillRect(x + 6, GY - 2, 4, 2); }
    }
    for (int b = 0; b < D.nb; b++) {
        int x = ox + (int)D.bx[b];
        setCol(playerCol(b)); thickLine(x - 18, GY, x, GY - 22, 3); thickLine(x + 18, GY, x, GY - 22, 3);
        drawText(x - 18, GY + 8, "X" + to_string(D.ammo[b]), 1, playerCol(b));
    }
    for (auto &e : D.en) {
        setCol(PAL[4].c); thickLine(ox + e.x0, oy + e.y0, ox + e.x(), oy + e.y(), 1);
        setCol(PAL[7].c); fillRect(ox + (int)e.x() - 1, oy + (int)e.y() - 1, 3, 3);
    }
    for (auto &s : D.shots) {
        setCol(BRIGHT); thickLine(ox + s.x0, oy + s.y0, ox + s.x(), oy + s.y(), 1);
        fillRect(ox + (int)s.x() - 1, oy + (int)s.y() - 1, 3, 3);
    }
    for (auto &b : D.booms) {
        int r = (int)b.r;
        if (r < 3) continue;
        Col c = b.friendly ? BRIGHT : PAL[4].c;
        setCol(c, 110); fillEllipse(ox + (int)b.x, oy + (int)b.y, r, r);
        setCol(c); ringRound(ox + (int)b.x - r, oy + (int)b.y - r, 2 * r, 2 * r, r, 2);
    }
    for (int p = 0; p < D.nb; p++) {
        int x = ox + (int)D.cx[p], y = oy + (int)D.cy[p];
        setCol(playerCol(p));
        fillRect(x - 12, y - 1, 8, 3); fillRect(x + 5, y - 1, 8, 3); fillRect(x - 1, y - 12, 3, 8); fillRect(x - 1, y + 5, 3, 8);
    }
    if (D.over) drawOverlay("CIUDADES DESTRUIDAS", "OLEADA " + to_string(D.wave) + "   PUNTOS " + to_string(D.score) + "   ENTER: OTRA VEZ   ESC: MENU");
}

static void drawGames() {
    if (gGame == G_MENU) drawGameMenu();
    else if (gGame == G_HACK) drawHack();
    else if (gGame == G_LOCK) drawLock();
    else drawDef();
}

static void drawGamePanel() {
    box(540, 358, 250, 182);
    drawText(552, 368, gGame == G_MENU ? "MINIJUEGOS" : GAME_NAMES[gGame - 1], 2, BRIGHT);
    drawText(552, 392, g2P ? "2 JUGADORES COOP" : "1 JUGADOR", 1, playerCol(g2P));
    int mi = gGame == G_MENU ? gMenuSel : gGame - 1;
    drawText(552, 406, "MEJOR: " + to_string(gBest[mi][g2P ? 1 : 0]), 1, MID);
    string a, b, c;
    if (gGame == G_HACK) { a = "INTENTOS " + to_string(gHack.tries); b = "CLAVE: 7 LETRAS"; c = g2P ? "TURNO J" + to_string(gHack.turn + 1) : string("1 JUGADOR"); }
    else if (gGame == G_LOCK) { a = "NIVEL " + to_string(gLock.level); b = "GANZUAS " + to_string(gLock.picks); c = "PUNTOS " + to_string(gLock.score); }
    else if (gGame == G_DEF) { a = "OLEADA " + to_string(gDef.wave); b = "CIUDADES " + to_string(gDef.aliveCities()) + "/6"; c = "PUNTOS " + to_string(gDef.score); }
    if (gGame != G_MENU) {
        drawText(552, 428, a, 2, BRIGHT, 230); drawText(552, 448, b, 2, BRIGHT, 230); drawText(552, 468, c, 2, BRIGHT, 230);
    } else drawText(552, 430, "ELIGE UN JUEGO", 2, MID);
    drawText(552, 510, "J1 VERDE", 1, playerCol(0));
    if (g2P) drawText(640, 510, "J2 NARANJA", 1, playerCol(1));
}

static void gameHints(string &l1, string &l2) {
    switch (gGame) {
        case G_MENU:
            l1 = "ARRIBA/ABAJO ELEGIR   IZQ/DER 1 O 2 JUGADORES (TAB TAMBIEN)   ENTER JUGAR   ESC CALENDARIO";
            l2 = "J1: W A S D + ESPACIO      J2: FLECHAS + ENTER      (EL CLIC TAMBIEN FUNCIONA)"; break;
        case G_HACK:
            l1 = "MOVER: J1 W A S D / J2 FLECHAS   ELEGIR: ESPACIO / ENTER (O CLIC EN LA PALABRA)";
            l2 = "EN COOP SE TURNAN LOS INTENTOS: EL OTRO JUGADOR PUEDE SUGERIR MOVIENDO SU CURSOR   ESC MENU"; break;
        case G_LOCK:
            l1 = "J1: A / D MUEVE LA GANZUA      J2: ESPACIO O ENTER (MANTENER) GIRA LA CERRADURA";
            l2 = "EN 1 JUGADOR: A/D O FLECHAS MUEVEN, ESPACIO/ENTER GIRA.  SI FUERZAS DEMASIADO SE ROMPE   ESC MENU"; break;
        default:
            l1 = "MIRA: J1 W A S D / J2 FLECHAS (O RATON)      DISPARO: ESPACIO / ENTER / CLIC";
            l2 = "EN COOP CADA JUGADOR TIENE SU BASE Y SU MUNICION   LAS CIUDADES SON COMUNES   ESC MENU"; break;
    }
}

static void drawEvents() {
    if (view == GAMES) { drawGamePanel(); return; }
    if (view == INVENTORY) { drawInvPanel(); return; }
    if (view == TASKS) { drawTaskPanel(); return; }
    if (view == NOTES) { drawNotePanel(); return; }
    box(540, 358, 250, 182);
    if (view == ROUTINE) {
        drawText(552, 368, string("RUTINAS ") + DAYS_FULL[rRow], 2, BRIGHT, 230);
        auto list = ag.routinesOnWeekday(rRow);
        if (list.empty()) { drawText(552, 396, "SIN RUTINAS", 2, MID); return; }
        for (int k = 0; k < (int)list.size() && k < 8; k++) {
            int y = 392 + k * 18;
            if (k == 7 && list.size() > 8) { drawText(552, y, "+" + to_string(list.size() - 7) + " MAS", 2, MID); break; }
            auto &r = ag.rt[list[k]];
            string pre = (mode == IN_RDEL || mode == IN_EPICK) ? to_string(k + 1) + " " : "";
            drawTextMq(552, y, pre + timeStr(r.hour, r.minute) + " " + r.title, 2, routineCol(), 230);
        }
        return;
    }
    drawText(552, 368, "AGENDA DIA " + to_string(cur.d), 2, BRIGHT);
    auto its = ag.itemsOn(cur);
    if (its.empty()) { drawText(552, 396, "SIN ACTIVIDADES", 2, MID); return; }
    for (int k = 0; k < (int)its.size() && k < 8; k++) {
        int y = 392 + k * 18;
        if (k == 7 && its.size() > 8) { drawText(552, y, "+" + to_string(its.size() - 7) + " MAS", 2, MID); break; }
        string pre = (mode == IN_DELETE || mode == IN_EPICK) ? to_string(k + 1) + " " : "";
        drawTextMq(552, y, pre + itemLine(its[k]), 2, itemCol(its[k]), 230);
    }
}

static string promptText() {
    switch (mode) {
        case IN_TIME: return "HORA HH:MM [" + timeStr(pendH, 0) + "]: ";
        case IN_TITLE: return pendRoutine ? "RUTINA: " : "TITULO: ";
        case IN_DELETE: case IN_RDEL: return "BORRAR NUMERO: ";
        case IN_INAME: return "OBJETO: ";
        case IN_IPLACE: return "RUTA/UBICACION: ";
        case IN_IQTY: return "CANTIDAD [1]: ";
        case IN_IEDIT: return "NUEVA RUTA: ";
        case IN_ICARRY: return "LLEVO CUANTOS (0=NO): ";
        case IN_IHOUR: return "AVISAR A LAS HH:MM [08:00]: ";
        case IN_IDAYS: return "DIAS [TODOS]: ";
        case IN_ICONF: return "BORRAR? S/N: ";
        case IN_IMG: return "IMAGEN JPG/PNG (O ARRASTRALA): ";
        case IN_IFILTER: return "FILTRO (VACIO=TODO): ";
        case IN_EPICK: return "EDITAR NUMERO: ";
        case IN_EMENU: return "CAMPO (NUMERO, X BORRAR, ENTER SALIR): ";
        default: return "";
    }
}

static void drawFooter() {
    setCol(DIMC); fillBox(20, 548, 760, 40);
    string cursor = fmodf(g_now, 1.f) < 0.5f ? "_" : " ";
    if (g_now < g_alarmEnd) {
        bool on = fmodf(g_now, 0.6f) < 0.3f;
        if (on) { setCol(BRIGHT); fillBox(20, 548, 760, 40); }
        drawTextC(400, 560, g_alarmText, 2, on ? BGC : BRIGHT);
    } else if (mode == IN_ASK) {
        drawText(28, 552, g_askHint, 1, g_askHint.rfind("ERROR", 0) == 0 ? BRIGHT : MID, 760);
        int room = max(8, 61 - countGlyphs(g_askLabel));
        drawText(28, 566, g_askLabel + tailGlyphs(input, room) + cursor, 2, BRIGHT, 745);
    } else if (mode == IN_SYNC) {
        drawText(28, 552, "COM3 /DEV/RFCOMM0  IP:PUERTO  :PUERTO (ESPERAR)   AÑADE TODAS PARA REENVIAR TODAS LAS FOTOS", 1, MID);
        drawText(28, 566, "DESTINO [" + g_syncTarget + "]: " + tailGlyphs(input, 30) + cursor, 2, BRIGHT, 745);
    } else if (mode == IN_RDAYS || mode == IN_IDAYS) {
        drawText(28, 552, "LU-VI   LMXJVSD (X = MIERCOLES)   TODOS   LABORABLES   FINDE", 1, MID);
        string def = mode == IN_RDAYS ? string(DAYS_SHORT[rRow]) : "TODOS";
        drawText(28, 566, "DIAS [" + def + "]: " + input + cursor, 2, BRIGHT, 745);
    } else if (mode != BROWSE) {
        string p = promptText();
        if (mode == IN_IMG) drawText(28, 552, "EJ: C:\\USERS\\YO\\FOTOS\\LLAVES.JPG   (CTRL+V PEGA LA RUTA)", 1, MID);
        int room = max(8, 61 - countGlyphs(p));
        drawText(28, mode == IN_IMG ? 566 : 560, p + tailGlyphs(input, room) + cursor, 2, BRIGHT, 745);
    } else if (g_now < g_msgEnd) {
        drawTextC(400, 560, msg, 2, BRIGHT);
    } else if (view == GAMES) {
        string l1, l2;
        gameHints(l1, l2);
        drawText(28, 553, l1, 1, MID);
        drawText(28, 567, l2, 1, MID);
    } else if (view == INVENTORY) {
        drawText(28, 553, "A NUEVO  E RUTA  R NOMBRE  D DESCRIP  L LLEVO  M AVISO  I FOTO  W CAMBIA PRINCIPAL  Z QUITA 2A  F FILTRO  C COLOR  +/-", 1, MID);
        drawText(28, 567, "FLECHAS/TAB GUARDADO-LLEVO  X BORRAR  S SYNC  O IMPORTAR  B AVISOS PC  Q SALIR   (ARRASTRA UN JPG/PNG A LA VENTANA)", 1, MID);
    } else if (view == TASKS) {
        drawText(28, 553, "A NUEVA  E EDITAR  ESPACIO/ENTER HECHA  X BORRAR  P PRIORIDAD  F BUSCAR  TAB PENDIENTES-HECHAS  1-8 VISTAS", 1, MID);
        drawText(28, 567, "S SYNC  O IMPORTAR  B AVISOS PC (" + string(g_deskAlarms ? "SI" : "NO") + ")  Q SALIR      LAS VENCIDAS SALEN EN ROJO", 1, MID);
    } else if (view == NOTES) {
        drawText(28, 553, "A NUEVA  E EDITAR  X BORRAR  P FIJAR  + AÑADE LINEA  - BORRA ULTIMA LINEA  F BUSCAR  IZQ/DER DESPLAZA TEXTO", 1, MID);
        drawText(28, 567, "ENTER EDITAR  S SYNC  O IMPORTAR  1-8 VISTAS  Q SALIR", 1, MID);
    } else {
        drawText(28, 553, "FLECHAS MOVER  ENTER ABRIR  A AÑADIR  J TAREA  E EDITAR  X BORRAR  T HOY  1-8 VISTAS  TAB DESPLEGAR DIA (SEMANA)", 1, MID);
        drawText(28, 567, string("S SYNC  O IMPORTAR  B AVISOS PC (") + (g_deskAlarms ? "SI" : "NO") + ")  K COLOR RUTINAS (" + PAL[g_routineColor & 7].name + ")  Q SALIR", 1, MID);
    }
}

static string idleCaption() {
    if (view == GAMES) return gGame == G_MENU ? string("A JUGAR!") : (g2P ? string("COOP LOCAL") : string("SUERTE!"));
    if (view == INVENTORY) { int n = (int)invList().size(); return n ? clip(n, "OBJETO", "OBJETOS") : string("SIN OBJETOS"); }
    if (view == TASKS) { int n = (int)taskList().size(); return n ? clip(n, tkDone ? "HECHA" : "PENDIENTE", tkDone ? "HECHAS" : "PENDIENTES") : string(tkDone ? "NADA HECHO" : "TODO AL DIA"); }
    if (view == NOTES) { int n = (int)noteList().size(); return n ? clip(n, "NOTA", "NOTAS") : string("SIN NOTAS"); }
    if (view == ROUTINE) {
        int n = (int)ag.routinesOnWeekday(rRow).size();
        return n ? clip(n, "RUTINA", "RUTINAS") : string("SIN RUTINAS");
    }
    auto its = ag.itemsOn(cur);
    if (its.empty()) return "DIA LIBRE";
    if (cur == today()) {
        time_t n = time(nullptr);
        tm *lt = localtime(&n);
        int nm = lt->tm_hour * 60 + lt->tm_min;
        for (auto &it : its) if (it.hour * 60 + it.minute >= nm) return fitGlyphs("> " + timeStr(it.hour, it.minute) + " " + it.title, 19);
    }
    return clip((int)its.size(), "EVENTO", "EVENTOS");
}

static void drawAll() {
    setCol(BGC); SDL_RenderClear(R);
    box(20, 60, 505, 480);
    box(540, 60, 250, 290);
    drawHeader();
    if (view == MONTH) drawMonth(); else if (view == WEEK) drawWeek(); else if (view == DAY) drawDay(); else if (view == TASKS) drawTasks(); else if (view == NOTES) drawNotes(); else if (view == ROUTINE) drawRoutines(); else if (view == INVENTORY) drawInventory(); else drawGames();
    if (g_sprite) drawSprite(665, g_now); else drawMascot(665, g_now);
    string cap = g_mood == M_THUMBS ? "BUEN TRABAJO!" : g_mood == M_WAVE ? "HOLA!" : g_mood == M_WINK ? "ENTENDIDO!" : idleCaption();
    drawTextC(665, 320, cap, 2, BRIGHT);
    drawEvents();
    drawEditor();
    drawFooter();

    // efecto CRT: líneas de barrido, banda móvil y viñeta
    for (int y = 0; y < 600; y += 3) { SDL_SetRenderDrawColor(R, 0, 0, 0, 70); SDL_RenderDrawLine(R, 0, y, 799, y); }
    SDL_SetRenderDrawColor(R, 120, 255, 150, 10);
    fillRect(0, (int)fmodf(g_now * 70, 700) - 80, 800, 60);
    for (int i = 0; i < 10; i++) { SDL_SetRenderDrawColor(R, 0, 0, 0, (10 - i) * 7); rectLineSq(i, i, 800 - 2 * i, 600 - 2 * i, 1); }
}

// ------------------------------------------------------------------- Entrada
static void startInput(Mode m, char trigger) {
    mode = m; input.clear(); trig = trigger; trigTick = SDL_GetTicks();
    SDL_StartTextInput();
}
static void endInput() { mode = BROWSE; SDL_StopTextInput(); }

static void step(int dir) {  // anterior / siguiente según la vista
    if (view == ROUTINE) return;
    if (view == INVENTORY) { invSel = max(0, invSel + dir); return; }
    if (view == TASKS) { tkSel = max(0, tkSel + dir); return; }
    if (view == NOTES) { ntSel = max(0, ntSel + dir); ntScroll = 0; return; }
    if (view == WEEK && weekExp) { weekScroll = max(0, weekScroll + dir); return; }
    cur = view == MONTH ? addMonths(cur, dir) : addDays(cur, view == WEEK ? 7 * dir : dir);
}

static void startSync(const string &target, bool allPhotos = false) {
    if (g_syncBusy) { flash("SYNC EN CURSO"); return; }
    if (g_syncThread.joinable()) g_syncThread.join();
    g_syncBusy = true;
    g_syncCancel = false;
    g_syncThread = thread([target, allPhotos] {
        SyncResult r = syncTarget(ag, g_mx, target, allPhotos);
        lock_guard<mutex> l(g_mx);
        g_syncMsg = r.msg; g_syncOk = r.ok; g_syncDone = true; g_syncBusy = false;
    });
    flash("SINCRONIZANDO...");
}

// "LU-VI", "LMXJVSD", "TODOS", "LABORABLES", "FINDE"; vacío = día seleccionado. 0 = no válido.
static int parseDays(string in, int def) {
    for (auto &c : in) c = (char)toupper((unsigned char)c);
    in.erase(remove_if(in.begin(), in.end(), [](unsigned char c) { return c == ' ' || c == ','; }), in.end());
    if (in.empty()) return def;
    if (in == "TODOS" || in == "DIARIO") return 127;
    if (in == "LABORABLES") return 31;
    if (in == "FINDE") return 96;
    if (in.size() == 5 && in[2] == '-') {
        int a = -1, b = -1;
        for (int i = 0; i < 7; i++) { if (in.compare(0, 2, DAYS_SHORT[i]) == 0) a = i; if (in.compare(3, 2, DAYS_SHORT[i]) == 0) b = i; }
        if (a < 0 || b < 0) return 0;
        int m = 0;
        for (int i = a;; i = (i + 1) % 7) { m |= 1 << i; if (i == b) break; }  // admite rangos que cruzan el domingo
        return m;
    }
    int m = 0;
    for (char c : in) {
        const char *p = strchr("LMXJVSD", c);
        if (!p || !c) return 0;
        m |= 1 << (p - "LMXJVSD");
    }
    return m;
}

// ------------------------------------------------- Preguntas encadenadas y fichas
static void startAsk(const string &label, const string &hint, const string &def, function<void(const string &)> cb,
                     function<void()> esc, char trigger, bool multi) {
    g_askLabel = label; g_askHint = hint; g_askCb = std::move(cb); g_askEsc = std::move(esc); g_askMulti = multi;
    if (!trigger && g_lastKey >= 'a' && g_lastKey <= 'z') trigger = (char)g_lastKey;  // la letra que abrió la pregunta no se escribe
    startInput(IN_ASK, trigger);
    input = def;
}
static void askConfirm(const string &what, function<void()> yes) {
    startAsk(what + " S/N: ", "", "", [yes](const string &a) { if (!a.empty() && strchr("sSyY", a[0])) yes(); });
}
static void doneAdd(const string &m) { flash(m); setMood(M_THUMBS, 2.4f); }

static bool numParts(const string &s, vector<int> &out, size_t &firstLen) {  // "12/11", "12-11-2030", "2030-11-12"
    out.clear();
    string cur;
    firstLen = 0;
    bool first = true;
    for (size_t i = 0; i <= s.size(); i++) {
        char c = i < s.size() ? s[i] : '/';
        if (c == '/' || c == '-' || c == '.') {
            if (cur.empty() || cur.size() > 4) return false;
            out.push_back(atoi(cur.c_str()));
            if (first) { firstLen = cur.size(); first = false; }
            cur.clear();
        } else if (isdigit((unsigned char)c)) cur += c;
        else return false;
    }
    return out.size() == 2 || out.size() == 3;
}
// Fecha escrita por el usuario -> "AAAA-MM-DD" ("" si no vale). Sin año se usa el de 'base' (y, con rollover, el siguiente si ya pasó).
static string dateParse(const string &in, Date base, bool rollover) {
    string s = trimStr(in);
    if (validDateStr(s)) return s;
    vector<int> p;
    size_t fl;
    if (!numParts(s, p, fl)) return "";
    int y, m, d;
    bool hasYear = p.size() == 3;
    if (hasYear && fl == 4) { y = p[0]; m = p[1]; d = p[2]; }
    else { d = p[0]; m = p[1]; y = hasYear ? p[2] : base.y; if (hasYear && y < 100) y += 2000; }
    char b[16];
    snprintf(b, sizeof b, "%04d-%02d-%02d", y, m, d);
    if (!validDateStr(b)) return "";
    if (!hasYear && rollover && string(b) < dateStr(base)) {
        snprintf(b, sizeof b, "%04d-%02d-%02d", y + 1, m, d);
        if (!validDateStr(b)) return "";
    }
    return b;
}
static bool hmParse(const string &in, int &minutes) {  // "9", "9:30"
    string s = trimStr(in);
    size_t c = s.find(':');
    string a = c == string::npos ? s : s.substr(0, c), b = c == string::npos ? "0" : s.substr(c + 1);
    auto num = [](const string &x) { return !x.empty() && x.size() <= 2 && x.find_first_not_of("0123456789") == string::npos; };
    if (!num(a) || !num(b)) return false;
    int h = atoi(a.c_str()), m = atoi(b.c_str());
    if (h > 23 || m > 59) return false;
    minutes = h * 60 + m;
    return true;
}
static Date toDate(const string &k) { Date d{1970, 1, 1}; sscanf(k.c_str(), "%d-%d-%d", &d.y, &d.m, &d.d); return d; }
static vector<string> splitWords(const string &s) { vector<string> r; istringstream ss(s); string w; while (ss >> w) r.push_back(w); return r; }
static bool yesNo(const string &v, bool &out) {
    string u = lowerAscii(trimStr(v));
    if (u == "s" || u == "si" || u == "y" || u == "1") { out = true; return true; }
    if (u == "n" || u == "no" || u == "0") { out = false; return true; }
    return false;
}

// ---- Fichas (tecla E)
static void openEditor(const string &title, vector<EField> f, function<void()> touch, function<void()> del) {
    g_ed = Editor();
    g_ed.on = true; g_ed.title = title; g_ed.f = std::move(f); g_ed.touch = std::move(touch); g_ed.del = std::move(del);
    startInput(IN_EMENU, g_lastKey >= 'a' && g_lastKey <= 'z' ? (char)g_lastKey : 0);
}
static void closeEditor() { g_ed = Editor(); }

static void editEvent(int i) {
    vector<EField> f;
    f.push_back({"TITULO", "", [i] { return ag.ev[i].title; }, [i](const string &v) -> string {
        string t = cleanTitle(v);
        if (t.empty()) return "TITULO VACIO";
        ag.ev[i].title = t; return "";
    }});
    f.push_back({"FECHA", "AAAA-MM-DD  O  DD/MM", [i] { return dateStr(ag.ev[i].date); }, [i](const string &v) -> string {
        string d = dateParse(v, ag.ev[i].date, false);
        if (d.empty()) return "FECHA NO VALIDA";
        if (!ag.ev[i].end.empty() && ag.ev[i].end < d) return "LA FECHA DE FIN NO PUEDE SER ANTERIOR";
        ag.ev[i].date = toDate(d); return "";
    }});
    f.push_back({"HORA", "HH:MM", [i] { return timeStr(ag.ev[i].hour, ag.ev[i].minute); }, [i](const string &v) -> string {
        int m;
        if (!hmParse(v, m)) return "HORA NO VALIDA";
        if (ag.ev[i].end.empty() && ag.ev[i].endMin >= 0 && ag.ev[i].endMin <= m) return "LA HORA DE FIN DEBE SER POSTERIOR";
        ag.ev[i].hour = m / 60; ag.ev[i].minute = m % 60; return "";
    }});
    f.push_back({"FIN FECHA", "OPCIONAL: AAAA-MM-DD O DD/MM (VACIO = SIN FIN)", [i] { return ag.ev[i].end; }, [i](const string &v) -> string {
        Event &e = ag.ev[i];
        if (trimStr(v).empty()) { e.end.clear(); if (e.endMin >= 0 && e.endMin <= e.hour * 60 + e.minute) e.endMin = -1; return ""; }
        string d = dateParse(v, e.date, true);
        if (d.empty()) return "FECHA NO VALIDA";
        if (d < dateStr(e.date)) return "LA FECHA DE FIN NO PUEDE SER ANTERIOR";
        e.end = d; return "";
    }});
    f.push_back({"FIN HORA", "OPCIONAL: HH:MM (VACIO = SIN HORA DE FIN)", [i] { return ag.ev[i].endMin >= 0 ? timeStr(ag.ev[i].endMin / 60, ag.ev[i].endMin % 60) : string(); },
                 [i](const string &v) -> string {
        Event &e = ag.ev[i];
        if (trimStr(v).empty()) { e.endMin = -1; return ""; }
        int m;
        if (!hmParse(v, m)) return "HORA NO VALIDA";
        if (e.end.empty() && m <= e.hour * 60 + e.minute) return "LA HORA DE FIN DEBE SER POSTERIOR";
        e.endMin = m; return "";
    }});
    f.push_back({"DESCRIPCION", "\\N = SALTO DE LINEA", [i] { return ag.ev[i].desc; }, [i](const string &v) -> string { ag.ev[i].desc = cleanText(v, 1000); return ""; }});
    openEditor("EVENTO", f, [i] { ag.touchEvent(i); }, [i] { ag.removeAt(i); });
}
static void editRoutine(int i) {
    vector<EField> f;
    f.push_back({"NOMBRE", "", [i] { return ag.rt[i].title; }, [i](const string &v) -> string {
        string t = cleanTitle(v);
        if (t.empty()) return "NOMBRE VACIO";
        ag.rt[i].title = t; return "";
    }});
    f.push_back({"HORA", "HH:MM", [i] { return timeStr(ag.rt[i].hour, ag.rt[i].minute); }, [i](const string &v) -> string {
        int m;
        if (!hmParse(v, m)) return "HORA NO VALIDA";
        ag.rt[i].hour = m / 60; ag.rt[i].minute = m % 60; return "";
    }});
    f.push_back({"DIAS", "LU-VI  LMXJVSD (X = MIERCOLES)  TODOS  FINDE", [i] { return dayMaskStr(ag.rt[i].mask); }, [i](const string &v) -> string {
        int m = parseDays(v, 0);
        if (!m) return "DIAS NO VALIDOS";
        ag.rt[i].mask = m; return "";
    }});
    f.push_back({"HASTA", "OPCIONAL: FECHA DE FIN (AAAA-MM-DD O DD/MM). VACIO = SIN FIN", [i] { return ag.rt[i].end; }, [i](const string &v) -> string {
        if (trimStr(v).empty()) { ag.rt[i].end.clear(); return ""; }
        string d = dateParse(v, today(), true);
        if (d.empty()) return "FECHA NO VALIDA";
        ag.rt[i].end = d; return "";
    }});
    f.push_back({"DESCRIPCION", "\\N = SALTO DE LINEA", [i] { return ag.rt[i].desc; }, [i](const string &v) -> string { ag.rt[i].desc = cleanText(v, 1000); return ""; }});
    openEditor("RUTINA", f, [i] { ag.touchRoutine(i); }, [i] { ag.removeRoutine(i); });
}
static void editTask(int i) {
    vector<EField> f;
    f.push_back({"TAREA", "", [i] { return ag.tk[i].title; }, [i](const string &v) -> string {
        string t = cleanTitle(v);
        if (t.empty()) return "ESCRIBE EL NOMBRE DE LA TAREA";
        ag.tk[i].title = t; return "";
    }});
    f.push_back({"FECHA LIMITE", "OPCIONAL: AAAA-MM-DD O DD/MM (VACIO = SIN FECHA)", [i] { return ag.tk[i].due; }, [i](const string &v) -> string {
        if (trimStr(v).empty()) { ag.tk[i].due.clear(); ag.tk[i].dt = -1; return ""; }
        string d = dateParse(v, today(), false);
        if (d.empty()) return "FECHA NO VALIDA";
        ag.tk[i].due = d; return "";
    }});
    f.push_back({"HORA LIMITE", "OPCIONAL: HH:MM (ACTIVA UN AVISO). VACIO = SIN HORA", [i] { return ag.tk[i].dt >= 0 ? timeStr(ag.tk[i].dt / 60, ag.tk[i].dt % 60) : string(); },
                 [i](const string &v) -> string {
        if (trimStr(v).empty()) { ag.tk[i].dt = -1; return ""; }
        if (ag.tk[i].due.empty()) return "PRIMERO PON UNA FECHA LIMITE";
        int m;
        if (!hmParse(v, m)) return "HORA NO VALIDA";
        ag.tk[i].dt = m; return "";
    }});
    f.push_back({"PRIORIDAD", "0 BAJA   1 MEDIA   2 ALTA", [i] { return to_string(ag.tk[i].pri) + " " + prioName(ag.tk[i].pri); }, [i](const string &v) -> string {
        string u = lowerAscii(trimStr(v));
        int p = u.empty() ? -1 : u[0] == 'b' ? 0 : u[0] == 'm' ? 1 : u[0] == 'a' ? 2 : isdigit((unsigned char)u[0]) ? u[0] - '0' : -1;
        if (p < 0 || p > 2) return "PRIORIDAD NO VALIDA";
        ag.tk[i].pri = p; return "";
    }});
    f.push_back({"DESCRIPCION", "\\N = SALTO DE LINEA", [i] { return ag.tk[i].desc; }, [i](const string &v) -> string { ag.tk[i].desc = cleanText(v, 1000); return ""; }});
    f.push_back({"HECHA", "S / N", [i] { return string(ag.tk[i].done ? "SI" : "NO"); }, [i](const string &v) -> string {
        bool b;
        if (!yesNo(v, b)) return "ESCRIBE S O N";
        ag.tk[i].done = b; return "";
    }});
    openEditor("TAREA", f, [i] { ag.touchTask(i); }, [i] { ag.removeTask(i); });
}
static void editNote(int i) {
    vector<EField> f;
    f.push_back({"TITULO", "", [i] { return ag.nt[i].title; }, [i](const string &v) -> string {
        string t = cleanTitle(v);
        ag.nt[i].title = t.empty() ? "SIN TITULO" : t; return "";
    }});
    f.push_back({"TEXTO", "\\N = SALTO DE LINEA", [i] { return ag.nt[i].text; }, [i](const string &v) -> string { ag.nt[i].text = cleanText(v, 1800); return ""; }});
    f.push_back({"FIJADA", "S / N (LAS FIJADAS SALEN ARRIBA)", [i] { return string(ag.nt[i].pin ? "SI" : "NO"); }, [i](const string &v) -> string {
        bool b;
        if (!yesNo(v, b)) return "ESCRIBE S O N";
        ag.nt[i].pin = b; return "";
    }});
    openEditor("NOTA", f, [i] { ag.touchNote(i); }, [i] { ag.removeNote(i); });
}
static void editItem(const Item &it) {
    if (it.kind == 0) editEvent(it.idx); else if (it.kind == 1) editRoutine(it.idx); else editTask(it.idx);
}

// ---- Altas con datos extra encadenados (Esc en cualquier paso = terminar con lo ya escrito)
static void askEventDesc(int i) {
    startAsk("DESCRIPCION: ", "OPCIONAL (\\N = SALTO DE LINEA)   ENTER = SIN DESCRIPCION   ESC = TERMINAR", "",
             [i](const string &v) {
                 string d = cleanText(fromEdit(v), 1000);
                 if (!d.empty()) { ag.ev[i].desc = d; ag.touchEvent(i); }
                 doneAdd("EVENTO AÑADIDO");
             }, [] { doneAdd("EVENTO AÑADIDO"); }, 0, true);
}
static void askEventEnd(int i, const string &err = "") {
    startAsk("FIN: ", err.empty() ? string("OPCIONAL  12/11   18:30   12/11 18:30   ENTER = SIN FIN   ESC = TERMINAR") : "ERROR: " + err, "",
             [i](const string &v) {
                 string in = trimStr(v), endD;
                 int endM = -1;
                 if (!in.empty()) {
                     for (auto &tok : splitWords(in)) {
                         int mnt;
                         if (tok.find(':') != string::npos) { if (!hmParse(tok, mnt)) { askEventEnd(i, "HORA NO VALIDA"); return; } endM = mnt; }
                         else { string d = dateParse(tok, ag.ev[i].date, true); if (d.empty()) { askEventEnd(i, "FECHA NO VALIDA"); return; } endD = d; }
                     }
                     Event &e = ag.ev[i];
                     if (!endD.empty() && endD < dateStr(e.date)) { askEventEnd(i, "LA FECHA DE FIN NO PUEDE SER ANTERIOR"); return; }
                     if (endD.empty() && endM >= 0 && endM <= e.hour * 60 + e.minute) { askEventEnd(i, "LA HORA DE FIN DEBE SER POSTERIOR"); return; }
                     e.end = endD; e.endMin = endM;
                     ag.touchEvent(i);
                 }
                 askEventDesc(i);
             }, [] { doneAdd("EVENTO AÑADIDO"); });
}
static void askRoutineDesc(int i) {
    startAsk("DESCRIPCION: ", "OPCIONAL (\\N = SALTO DE LINEA)   ENTER = SIN DESCRIPCION   ESC = TERMINAR", "",
             [i](const string &v) {
                 string d = cleanText(fromEdit(v), 1000);
                 if (!d.empty()) { ag.rt[i].desc = d; ag.touchRoutine(i); }
                 doneAdd("RUTINA CREADA");
             }, [] { doneAdd("RUTINA CREADA"); }, 0, true);
}
static void askRoutineEnd(int i, const string &err = "") {
    startAsk("HASTA: ", err.empty() ? string("OPCIONAL: FECHA DE FIN DE LA RUTINA (12/11 O AAAA-MM-DD)   ENTER = SIN FIN   ESC = TERMINAR") : "ERROR: " + err, "",
             [i](const string &v) {
                 if (!trimStr(v).empty()) {
                     string d = dateParse(v, today(), true);
                     if (d.empty()) { askRoutineEnd(i, "FECHA NO VALIDA"); return; }
                     ag.rt[i].end = d; ag.touchRoutine(i);
                 }
                 askRoutineDesc(i);
             }, [] { doneAdd("RUTINA CREADA"); });
}
static void askThingDesc(int i) {
    startAsk("DESCRIPCION: ", "OPCIONAL (\\N = SALTO DE LINEA)   ENTER = SIN DESCRIPCION", "",
             [i](const string &v) {
                 string d = cleanText(fromEdit(v), 1000);
                 if (!d.empty() && thingOk(i)) { ag.th[i].desc = d; ag.touchThingDesc(i); }
             }, nullptr, 0, true);
}

struct TaskDraft { string title, due; int dt = -1, pri = 1; };
static void taskCreate(shared_ptr<TaskDraft> d, const string &desc) {
    int i = ag.addTask(d->title, d->due, d->dt, d->pri, desc);
    doneAdd("TAREA AÑADIDA");
    if (view == TASKS) {
        tkDone = false;
        auto l = taskList();
        for (int k = 0; k < (int)l.size(); k++) if (l[k] == i) tkSel = k;
    }
}
static void askTaskDesc(shared_ptr<TaskDraft> d) {
    startAsk("DESCRIPCION: ", "OPCIONAL (\\N = SALTO DE LINEA)   ENTER = SIN DESCRIPCION   ESC = TERMINAR", "",
             [d](const string &v) { taskCreate(d, fromEdit(v)); }, [d] { taskCreate(d, ""); }, 0, true);
}
static void askTaskPri(shared_ptr<TaskDraft> d, const string &err = "") {
    startAsk("PRIORIDAD: ", err.empty() ? string("0 BAJA   1 MEDIA   2 ALTA   (ENTER = MEDIA)   ESC = TERMINAR") : "ERROR: " + err, "1",
             [d](const string &v) {
                 string u = lowerAscii(trimStr(v));
                 int p = u.empty() ? 1 : u[0] == 'b' ? 0 : u[0] == 'm' ? 1 : u[0] == 'a' ? 2 : isdigit((unsigned char)u[0]) ? u[0] - '0' : -1;
                 if (p < 0 || p > 2) { askTaskPri(d, "PRIORIDAD NO VALIDA"); return; }
                 d->pri = p;
                 askTaskDesc(d);
             }, [d] { taskCreate(d, ""); });
}
static void askTaskTime(shared_ptr<TaskDraft> d, const string &err = "") {
    startAsk("HORA LIMITE: ", err.empty() ? string("OPCIONAL: HH:MM (ACTIVA UN AVISO)   ENTER = SIN HORA   ESC = TERMINAR") : "ERROR: " + err, "",
             [d](const string &v) {
                 if (!trimStr(v).empty()) {
                     int m;
                     if (!hmParse(v, m)) { askTaskTime(d, "HORA NO VALIDA"); return; }
                     d->dt = m;
                 }
                 askTaskPri(d);
             }, [d] { taskCreate(d, ""); });
}
static void askTaskDue(shared_ptr<TaskDraft> d, const string &def, const string &err = "") {
    startAsk("FECHA LIMITE: ", err.empty() ? string("OPCIONAL: AAAA-MM-DD O DD/MM   ENTER = SIN FECHA   ESC = TERMINAR") : "ERROR: " + err, def,
             [d](const string &v) {
                 if (trimStr(v).empty()) { d->due.clear(); askTaskPri(d); return; }
                 string k = dateParse(v, today(), false);
                 if (k.empty()) { askTaskDue(d, v, "FECHA NO VALIDA"); return; }
                 d->due = k;
                 askTaskTime(d);
             }, [d] { taskCreate(d, ""); });
}
static void askNewTask(const string &defDue, char trigger = 0) {
    startAsk("TAREA: ", "NOMBRE DE LA TAREA   (ESC = CANCELAR)", "",
             [defDue](const string &v) {
                 auto d = make_shared<TaskDraft>();
                 d->title = cleanTitle(v);
                 if (d->title.empty()) return;
                 askTaskDue(d, defDue);
             }, nullptr, trigger);
}
static void askNewNote(char trigger = 0) {
    startAsk("TITULO: ", "TITULO DE LA NOTA   (ESC = CANCELAR)", "",
             [](const string &t) {
                 string title = cleanTitle(t);
                 startAsk("TEXTO: ", "OPCIONAL (\\N = SALTO DE LINEA)   ENTER = SIN TEXTO   ESC = TERMINAR", "",
                          [title](const string &v) {
                              string tx = cleanText(fromEdit(v), 1800);
                              if (title.empty() && tx.empty()) return;
                              int i = ag.addNote(title, tx, false);
                              doneAdd("NOTA AÑADIDA");
                              if (view == NOTES) { auto l = noteList(); for (int k = 0; k < (int)l.size(); k++) if (l[k] == i) ntSel = k; }
                          }, [title] {
                              if (title.empty()) return;
                              int i = ag.addNote(title, "", false);
                              doneAdd("NOTA AÑADIDA");
                              if (view == NOTES) { auto l = noteList(); for (int k = 0; k < (int)l.size(); k++) if (l[k] == i) ntSel = k; }
                          }, 0, true);
             }, nullptr, trigger);
}

static void toggleTask(int i) {
    ag.tk[i].done = !ag.tk[i].done;
    ag.touchTask(i);
    if (ag.tk[i].done) { setMood(M_THUMBS, 2.4f); flash("TAREA HECHA"); } else flash("TAREA PENDIENTE");
}
static bool tasksKey(SDL_Keycode k) {  // teclas de la pestaña TAREAS; true = consumida
    int n = (int)taskList().size(), si = selTask();
    switch (k) {
        case SDLK_UP: tkSel = max(0, tkSel - 1); return true;
        case SDLK_DOWN: tkSel = min(max(0, n - 1), tkSel + 1); return true;
        case SDLK_PAGEUP: tkSel = max(0, tkSel - 10); return true;
        case SDLK_PAGEDOWN: tkSel = min(max(0, n - 1), tkSel + 10); return true;
        case SDLK_LEFT: tkDone = false; tkSel = 0; return true;
        case SDLK_RIGHT: tkDone = true; tkSel = 0; return true;
        case SDLK_TAB: tkDone = !tkDone; tkSel = 0; return true;
        case SDLK_a: askNewTask("", 'a'); return true;
        case SDLK_f: startAsk("BUSCAR: ", "TEXTO A BUSCAR EN TAREAS (VACIO = TODAS)", tkFilter, [](const string &v) { tkFilter = trimStr(v); tkSel = 0; }, nullptr, 'f'); return true;
        default: break;
    }
    bool needs = k == SDLK_SPACE || k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_e || k == SDLK_x || k == SDLK_p;
    if (!needs) return false;
    if (si < 0) { flash("NO HAY TAREA SELECCIONADA"); return true; }
    switch (k) {
        case SDLK_e: editTask(si); break;
        case SDLK_x: askConfirm("BORRAR TAREA?", [si] { ag.removeTask(si); setMood(M_WINK, 2.0f); flash("TAREA BORRADA"); }); break;
        case SDLK_p: ag.tk[si].pri = (ag.tk[si].pri + 1) % 3; ag.touchTask(si); flash("PRIORIDAD " + prioName(ag.tk[si].pri)); break;
        default: if (!g_keyRepeat) toggleTask(si);
    }
    return true;
}
static void noteAddLine(int si) {
    startAsk("LINEA: ", "TEXTO QUE SE AÑADE AL FINAL DE LA NOTA", "", [si](const string &v) {
        string l = cleanText(v, 400);
        if (l.empty()) return;
        string &t = ag.nt[si].text;
        t = cleanText(t.empty() ? l : t + "\n" + l, 1800);
        ag.touchNote(si);
    });
}
static bool notesKey(SDL_Keycode k) {
    int n = (int)noteList().size(), si = selNote();
    switch (k) {
        case SDLK_UP: ntSel = max(0, ntSel - 1); ntScroll = 0; return true;
        case SDLK_DOWN: ntSel = min(max(0, n - 1), ntSel + 1); ntScroll = 0; return true;
        case SDLK_PAGEUP: ntSel = max(0, ntSel - 10); ntScroll = 0; return true;
        case SDLK_PAGEDOWN: ntSel = min(max(0, n - 1), ntSel + 10); ntScroll = 0; return true;
        case SDLK_LEFT: ntScroll = max(0, ntScroll - 5); return true;
        case SDLK_RIGHT: ntScroll += 5; return true;
        case SDLK_a: askNewNote('a'); return true;
        case SDLK_f: startAsk("BUSCAR: ", "TEXTO A BUSCAR EN NOTAS (VACIO = TODAS)", ntFilter, [](const string &v) { ntFilter = trimStr(v); ntSel = 0; }, nullptr, 'f'); return true;
        default: break;
    }
    bool plus = k == SDLK_EQUALS || k == SDLK_PLUS || k == SDLK_KP_PLUS, minus = k == SDLK_MINUS || k == SDLK_KP_MINUS;
    bool needs = k == SDLK_e || k == SDLK_x || k == SDLK_p || k == SDLK_RETURN || k == SDLK_KP_ENTER || plus || minus;
    if (!needs) return false;
    if (si < 0) { flash("NO HAY NOTA SELECCIONADA"); return true; }
    if (plus) { noteAddLine(si); return true; }
    if (minus) {
        string &t = ag.nt[si].text;
        size_t c = t.rfind('\n');
        if (t.empty()) flash("LA NOTA NO TIENE TEXTO");
        else { t = c == string::npos ? string() : t.substr(0, c); ag.touchNote(si); flash("ULTIMA LINEA BORRADA"); }
        return true;
    }
    switch (k) {
        case SDLK_p: ag.nt[si].pin = !ag.nt[si].pin; ag.touchNote(si); flash(ag.nt[si].pin ? "NOTA FIJADA" : "NOTA SUELTA"); break;
        case SDLK_x: askConfirm("BORRAR NOTA?", [si] { ag.removeNote(si); setMood(M_WINK, 2.0f); flash("NOTA BORRADA"); }); break;
        default: editNote(si);
    }
    return true;
}

static string cleanPath(string p) {
    p = trimStr(p);
    if (p.size() >= 2 && ((p.front() == '"' && p.back() == '"') || (p.front() == '\'' && p.back() == '\'')))
        p = p.substr(1, p.size() - 2);
    if (p.rfind("file://", 0) == 0) p = p.substr(7);
    return p;
}
static void importAgendaFile(const string &raw) {  // agenda.txt de otro equipo (p. ej. el que exporta el móvil)
    string m;
    bool ok = ag.importFile(cleanPath(raw), m);
    setMood(ok ? M_THUMBS : M_WINK, 2.4f);
    flash(m);
}

static bool commitInv() {  // devuelve true si el modo actual es del inventario
    string in = input;
    switch (mode) {
        case IN_INAME:
            pendName = cleanTitle(in);
            if (pendName.empty()) endInput(); else startInput(IN_IPLACE, 0);
            return true;
        case IN_IPLACE: pendPlace = cleanTitle(in); startInput(IN_IQTY, 0); return true;
        case IN_IQTY: {
            int q = in.empty() ? 1 : atoi(in.c_str());
            endInput();
            int i = ag.addThing(pendName, pendPlace, q);
            invCarry = false; invFilter.clear();
            auto v = invList();
            for (size_t k = 0; k < v.size(); k++) if (v[k] == i) invSel = (int)k;
            setMood(M_THUMBS, 2.4f); flash("OBJETO AÑADIDO");
            askThingDesc(i);
            return true;
        }
        case IN_IEDIT:
            endInput();
            if (thingOk(pendThing)) { ag.th[pendThing].place = cleanTitle(in); ag.touchThing(pendThing); flash("RUTA ACTUALIZADA"); }
            return true;
        case IN_ICARRY:
            endInput();
            if (thingOk(pendThing) && !in.empty()) {
                ag.th[pendThing].carry = max(0, min(999999, atoi(in.c_str())));
                ag.touchThing(pendThing);
                flash(ag.th[pendThing].carry ? "LLEVAS " + to_string(ag.th[pendThing].carry) : string("YA NO LO LLEVAS"));
            }
            return true;
        case IN_IHOUR: {
            int h = 8, m = 0;
            if (!in.empty() && sscanf(in.c_str(), "%d:%d", &h, &m) < 1) h = -1;
            if (h < 0 || h > 23 || m < 0 || m > 59) { endInput(); flash("HORA NO VALIDA"); return true; }
            pendH = h; pendM = m;
            startInput(IN_IDAYS, 0);
            return true;
        }
        case IN_IDAYS: {
            int m = parseDays(in, 127);
            endInput();
            if (!m) { flash("DIAS NO VALIDOS"); return true; }
            if (thingOk(pendThing)) {
                Thing &t = ag.th[pendThing];
                t.notify = true; t.nh = pendH; t.nm = pendM; t.nmask = m;
                ag.touchThing(pendThing);
                desktopNotify("AVISO ACTIVADO", t.name + " - " + timeStr(t.nh, t.nm) + " (" + dayMaskStr(m) + ")");
                setMood(M_THUMBS, 2.4f); flash("AVISO ACTIVADO");
            }
            return true;
        }
        case IN_ICONF:
            endInput();
            if (thingOk(pendThing) && !in.empty() && strchr("sSyY", in[0])) {
                ag.removeThing(pendThing);
                setMood(M_WINK, 2.0f); flash("OBJETO BORRADO");
            }
            return true;
        case IN_IMG: endInput(); importImage(in); return true;
        case IN_IFILTER: endInput(); invFilter = trimStr(in); invSel = 0; return true;
        default: return false;
    }
}

static void commitInput() {
    if (mode == IN_ASK) {
        auto cb = std::move(g_askCb);
        string in = input;
        g_askCb = nullptr; g_askEsc = nullptr;
        endInput();
        if (cb) cb(in);
        return;
    }
    if (mode == IN_EMENU) {
        string in = trimStr(input);
        string up = in;
        for (auto &c : up) c = (char)toupper((unsigned char)c);
        if (in.empty()) { endInput(); closeEditor(); return; }
        if ((up == "X" || up == "B") && g_ed.del) {
            endInput();
            auto d = g_ed.del;
            startAsk("BORRAR? S/N: ", "", "", [d](const string &a) {
                if (!a.empty() && strchr("sSyY", a[0])) { d(); closeEditor(); setMood(M_WINK, 2.0f); flash("BORRADO"); }
                else startInput(IN_EMENU, 0);
            }, [] { startInput(IN_EMENU, 0); });
            return;
        }
        int n = atoi(in.c_str());
        if (n < 1 || n > (int)g_ed.f.size()) { g_ed.status = "NUMERO NO VALIDO"; input.clear(); return; }
        endInput();
        EField &fd = g_ed.f[n - 1];
        g_ed.status.clear();
        startAsk(fd.label + ": ", fd.hint, toEdit(fd.get()), [n](const string &v) {
            if (!g_ed.on || n > (int)g_ed.f.size()) return;
            string e = g_ed.f[n - 1].set(fromEdit(v));
            if (!e.empty()) g_ed.status = "ERROR: " + e;
            else { g_ed.touch(); g_ed.status = "GUARDADO"; }
            startInput(IN_EMENU, 0);
        }, [] { startInput(IN_EMENU, 0); }, 0, true);
        return;
    }
    if (mode == IN_EPICK) {
        int n = atoi(input.c_str());
        endInput();
        if (view == ROUTINE) { auto l = ag.routinesOnWeekday(rRow); if (n >= 1 && n <= (int)l.size()) editRoutine(l[n - 1]); }
        else { auto its = ag.itemsOn(cur); if (n >= 1 && n <= (int)its.size()) editItem(its[n - 1]); }
        return;
    }
    if (commitInv()) return;
    if (mode == IN_RDAYS) {
        int m = parseDays(input, 1 << rRow);
        if (!m) { endInput(); flash("DIAS NO VALIDOS"); return; }
        pendMask = m;
        startInput(IN_TIME, 0);
    } else if (mode == IN_TIME) {
        int h = pendH, m = 0;
        if (!input.empty() && sscanf(input.c_str(), "%d:%d", &h, &m) < 1) h = -1;
        if (h < 0 || h > 23 || m < 0 || m > 59) { endInput(); flash("HORA NO VALIDA"); return; }
        pendH = h; pendM = m; startInput(IN_TITLE, 0);
    } else if (mode == IN_TITLE) {
        string t = input;
        replace(t.begin(), t.end(), '|', '/');
        endInput();
        if (t.empty()) return;
        if (pendRoutine) askRoutineEnd(ag.addRoutine(pendMask, pendH, pendM, t));
        else askEventEnd(ag.add(cur, pendH, pendM, t));
    } else if (mode == IN_SYNC) {
        string raw = trimStr(input), up = raw;
        for (auto &c : up) c = (char)toupper((unsigned char)c);
        bool all = false;  // TODAS (solo o al final) = reenviar todas las fotos
        if (up == "TODAS") { raw.clear(); all = true; }
        else if (up.size() > 6 && up.compare(up.size() - 6, 6, " TODAS") == 0) { raw = trimStr(raw.substr(0, raw.size() - 6)); all = true; }
        string t = trimStr(raw.empty() ? g_syncTarget : raw);
        endInput();
        if (t.empty()) return;
        g_syncTarget = t;
        saveCfg();
        startSync(t, all);
    } else if (mode == IN_RDEL) {
        auto list = ag.routinesOnWeekday(rRow);
        int n = atoi(input.c_str());
        endInput();
        if (n >= 1 && n <= (int)list.size()) {
            ag.removeRoutine(list[n - 1]);
            setMood(M_WINK, 2.0f); flash("RUTINA BORRADA");
        }
    } else if (mode == IN_DELETE) {
        auto its = ag.itemsOn(cur);
        int n = atoi(input.c_str());
        endInput();
        if (n >= 1 && n <= (int)its.size()) {
            Item it = its[n - 1];
            if (it.kind == 1) { ag.skipRoutine(it.idx, cur); flash("RUTINA OMITIDA HOY"); }
            else if (it.kind == 2) { ag.removeTask(it.idx); flash("TAREA BORRADA"); }
            else { ag.removeAt(it.idx); flash("EVENTO BORRADO"); }
            setMood(M_WINK, 2.0f);
        }
    }
}

static bool invKey(SDL_Keycode k) {  // teclas propias de la pestaña INVENTARIO; true = consumida
    int si = selThing(), n = (int)invList().size();
    switch (k) {
        case SDLK_UP: invSel = max(0, invSel - 1); return true;
        case SDLK_DOWN: invSel = min(max(0, n - 1), invSel + 1); return true;
        case SDLK_PAGEUP: case SDLK_p: invSel = max(0, invSel - 10); return true;
        case SDLK_PAGEDOWN: case SDLK_n: invSel = min(max(0, n - 1), invSel + 10); return true;
        case SDLK_LEFT: invCarry = false; invSel = 0; return true;
        case SDLK_RIGHT: invCarry = true; invSel = 0; return true;
        case SDLK_TAB: invCarry = !invCarry; invSel = 0; return true;
        case SDLK_a: pendThing = -1; startInput(IN_INAME, 'a'); return true;
        case SDLK_f: startInput(IN_IFILTER, 'f'); return true;
        case SDLK_c: g_imgColor = !g_imgColor; return true;
        default: break;
    }
    bool needs = k == SDLK_e || k == SDLK_d || k == SDLK_r || k == SDLK_w || k == SDLK_z || k == SDLK_l || k == SDLK_m || k == SDLK_x || k == SDLK_i || k == SDLK_EQUALS || k == SDLK_PLUS ||
                 k == SDLK_KP_PLUS || k == SDLK_MINUS || k == SDLK_KP_MINUS;
    if (!needs) return false;
    if (si < 0) { flash("NO HAY OBJETO SELECCIONADO"); return true; }
    Thing &t = ag.th[si];
    pendThing = si;
    switch (k) {
        case SDLK_e: startInput(IN_IEDIT, 'e'); break;
        case SDLK_r:
            startAsk("NOMBRE: ", "NUEVO NOMBRE DEL OBJETO", t.name, [si](const string &v) {
                string n = cleanTitle(v);
                if (!n.empty() && thingOk(si)) { ag.th[si].name = n; ag.touchThing(si); flash("NOMBRE ACTUALIZADO"); }
            }, nullptr, 'r');
            break;
        case SDLK_d:
            startAsk("DESCRIPCION: ", "\\N = SALTO DE LINEA   VACIO = SIN DESCRIPCION", toEdit(t.desc), [si](const string &v) {
                if (!thingOk(si)) return;
                ag.th[si].desc = cleanText(fromEdit(v), 1000);
                ag.touchThingDesc(si);
                flash("DESCRIPCION ACTUALIZADA");
            }, nullptr, 'd', true);
            break;
        case SDLK_w:
            if (t.image.empty() || t.image2.empty()) flash("NECESITA DOS FOTOS");
            else { swap(t.image, t.image2); ag.touchPhotos(si); flash("FOTO PRINCIPAL CAMBIADA"); }
            break;
        case SDLK_z:
            if (t.image2.empty()) flash("NO HAY SEGUNDA FOTO");
            else askConfirm("QUITAR LA SEGUNDA FOTO?", [si] { if (thingOk(si)) { ag.th[si].image2.clear(); ag.touchPhotos(si); flash("SEGUNDA FOTO QUITADA"); } });
            break;
        case SDLK_l: startInput(IN_ICARRY, 'l'); break;
        case SDLK_x: startInput(IN_ICONF, 'x'); break;
        case SDLK_i: startInput(IN_IMG, 'i'); break;
        case SDLK_m:
            if (t.notify) { t.notify = false; ag.touchThing(si); flash("AVISO DESACTIVADO"); }
            else { pendH = t.nh; startInput(IN_IHOUR, 'm'); }
            break;
        default: {
            int d = (k == SDLK_MINUS || k == SDLK_KP_MINUS) ? -1 : 1;
            int &f = invCarry ? t.carry : t.qty;
            f = max(0, min(999999, f + d));
            ag.touchThing(si);
        }
    }
    return true;
}

static void onKey(SDL_Keycode k) {
    g_lastKey = k;
    if (mode != BROWSE) {
        if (k == SDLK_v && (SDL_GetModState() & KMOD_CTRL)) {  // pegar desde el portapapeles
            char *c = SDL_GetClipboardText();
            if (c) {
                string p = c;
                SDL_free(c);
                bool multi = mode == IN_ASK && g_askMulti;
                string q;
                for (char ch : p) { if (ch == '\r') continue; if (ch == '\n') q += multi ? "\\n" : " "; else q += ch == '\t' ? ' ' : ch; }
                if (input.size() + q.size() <= (multi ? 2100u : 260u)) input += q;
            }
            return;
        }
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER) commitInput();
        else if (k == SDLK_ESCAPE) {
            auto esc = std::move(g_askEsc);
            bool wasAsk = mode == IN_ASK;
            if (mode == IN_EMENU) closeEditor();
            g_askCb = nullptr; g_askEsc = nullptr;
            endInput();
            if (wasAsk && esc) esc();
        }
        else if (k == SDLK_BACKSPACE && !input.empty()) {
            while (!input.empty() && ((unsigned char)input.back() & 0xC0) == 0x80) input.pop_back();
            if (!input.empty()) input.pop_back();
        }
        return;
    }
    if (view == GAMES && gamesKey(k)) return;
    if (view == INVENTORY && invKey(k)) return;
    if (view == TASKS && tasksKey(k)) return;
    if (view == NOTES && notesKey(k)) return;
    bool R_ = view == ROUTINE;
    switch (k) {
        case SDLK_LEFT:  if (R_) rCol = max(0, rCol - 1); else cur = addDays(cur, -1); break;
        case SDLK_RIGHT: if (R_) rCol = min(2, rCol + 1); else cur = addDays(cur, 1); break;
        case SDLK_UP:    if (R_) rRow = (rRow + 6) % 7; else if (view == DAY) hourSel = max(0, hourSel - 1); else if (view == WEEK && weekExp) weekScroll = max(0, weekScroll - 1); else cur = addDays(cur, -7); break;
        case SDLK_DOWN:  if (R_) rRow = (rRow + 1) % 7; else if (view == DAY) hourSel = min(23, hourSel + 1); else if (view == WEEK && weekExp) weekScroll++; else cur = addDays(cur, 7); break;
        case SDLK_PAGEUP: case SDLK_p: step(-1); break;
        case SDLK_PAGEDOWN: case SDLK_n: step(1); break;
        case SDLK_1: view = MONTH; break;
        case SDLK_2: view = WEEK; break;
        case SDLK_3: view = DAY; break;
        case SDLK_4: view = TASKS; break;
        case SDLK_5: view = NOTES; break;
        case SDLK_6: view = ROUTINE; rRow = weekday(cur); break;
        case SDLK_7: view = INVENTORY; break;
        case SDLK_8: view = GAMES; break;
        case SDLK_e:
            if (R_) { auto l = ag.routinesOnWeekday(rRow); if (l.empty()) flash("NO HAY RUTINAS"); else if (l.size() == 1) editRoutine(l[0]); else startInput(IN_EPICK, 'e'); }
            else { auto its = ag.itemsOn(cur); if (its.empty()) flash("NO HAY ACTIVIDADES"); else if (its.size() == 1) editItem(its[0]); else startInput(IN_EPICK, 'e'); }
            break;
        case SDLK_j: if (!R_) askNewTask(dateStr(cur), 'j'); break;
        case SDLK_o:
            startAsk("ARCHIVO: ", "RUTA DE UN agenda.txt A IMPORTAR (O ARRASTRALO A LA VENTANA)   CTRL+V PEGA LA RUTA", "",
                     [](const string &v) { if (!trimStr(v).empty()) importAgendaFile(v); }, nullptr, 'o');
            break;
        case SDLK_TAB: if (view == WEEK) weekExp = !weekExp; break;
        case SDLK_k:
            g_routineColor = (g_routineColor + 1) % 8;
            saveCfg();
            flash(string("COLOR DE RUTINAS: ") + PAL[g_routineColor].name);
            break;
        case SDLK_b: g_deskAlarms = !g_deskAlarms; saveCfg(); flash(g_deskAlarms ? "AVISOS DE ESCRITORIO: SI" : "AVISOS DE ESCRITORIO: NO"); break;
        case SDLK_RETURN: case SDLK_KP_ENTER:
            if (R_) { cur = addDays(today(), (rRow - weekday(today()) + 7) % 7); view = DAY; }  // ver cómo queda ese día
            else view = DAY;
            break;
        case SDLK_ESCAPE: if (view == WEEK && weekExp) weekExp = false; else view = view == DAY ? WEEK : MONTH; break;
        case SDLK_t: if (R_) rRow = weekday(today()); else cur = today(); break;
        case SDLK_a:
            if (R_) { pendRoutine = true; pendH = rCol == 0 ? 7 : rCol == 1 ? 13 : 21; startInput(IN_RDAYS, 'a'); }
            else { pendRoutine = false; pendH = view == DAY ? hourSel : 9; startInput(IN_TIME, 'a'); }
            break;
        case SDLK_x:
            if (R_) {
                if (ag.routinesOnWeekday(rRow).empty()) flash("NO HAY RUTINAS");
                else startInput(IN_RDEL, 'x');
            } else if (ag.itemsOn(cur).empty()) flash("NO HAY ACTIVIDADES");
            else startInput(IN_DELETE, 'x');
            break;
        case SDLK_s: startInput(IN_SYNC, 's'); break;
        case SDLK_q: running = false; break;
    }
}

static void onClick(int mx, int my) {
    for (int i = 0; i < 8; i++)
        if (mx >= TX + i * TG && mx < TX + i * TG + TW && my >= TY && my < TY + TH) { view = (View)i; if (view == ROUTINE) rRow = weekday(cur); return; }
    if (view == MONTH) {
        int first = weekday({cur.y, cur.m, 1}), c = (mx - GX) / CW, r = (my - GY) / CH;
        if (mx < GX || my < GY || c > 6 || r > 5) return;
        int d = r * 7 + c - first + 1;
        if (d < 1 || d > daysInMonth(cur.y, cur.m)) return;
        Date nd{cur.y, cur.m, d};
        if (nd == cur) view = DAY; else cur = nd;
    } else if (view == WEEK) {  // clic: despliega el día; otro clic en el mismo día lo contrae
        int ys[8];
        weekGeom(ys);
        if (mx < 24 || mx > 521 || my < ys[0] || my >= ys[7]) return;
        int r = 0;
        while (r < 6 && my >= ys[r + 1]) r++;
        Date nd = addDays(addDays(cur, -weekday(cur)), r);
        if (nd == cur) weekExp = !weekExp; else { cur = nd; weekExp = true; }
    } else if (view == GAMES) {
        gamesClick(mx, my);
    } else if (view == INVENTORY) {
        if (my >= 66 && my < 90 && mx >= 290) {
            if (mx < 408) { invCarry = false; invSel = 0; } else if (mx >= 416 && mx < 494) { invCarry = true; invSel = 0; }
            return;
        }
        int n = (int)invList().size(), r = (my - IR0) / IRH;
        if (mx < 24 || mx > 521 || my < IR0 || r >= IROWS) return;
        int idx = invTop(n) + r;
        if (idx < n) invSel = idx;
    } else if (view == TASKS) {
        if (my >= 66 && my < 90 && mx >= 150) { if (mx < 286) { tkDone = false; tkSel = 0; } else if (mx >= 296 && mx < 384) { tkDone = true; tkSel = 0; } return; }
        auto l = taskList();
        int n = (int)l.size(), r = (my - IR0) / IRH;
        if (mx < 24 || mx > 521 || my < IR0 || r >= IROWS) return;
        int idx = listTop(tkSel, n) + r;
        if (idx < n) { tkSel = idx; if (mx < 70) toggleTask(l[idx]); }
    } else if (view == NOTES) {
        int n = (int)noteList().size(), r = (my - IR0) / IRH;
        if (mx < 24 || mx > 521 || my < IR0 || r >= IROWS) return;
        int idx = listTop(ntSel, n) + r;
        if (idx < n) { ntSel = idx; ntScroll = 0; }
    } else if (view == ROUTINE) {
        int r = (my - RY) / RH;
        if (mx < 24 || mx > 521 || my < RY || r > 6) return;
        int c = mx < RX0 ? rCol : min(2, (mx - RX0) / RCW);
        if (r == rRow && c == rCol && mx >= RX0) { pendRoutine = true; pendH = c == 0 ? 7 : c == 1 ? 13 : 21; startInput(IN_RDAYS, 0); }
        else { rRow = r; rCol = c; }
    } else {
        int r = (my - DY) / DH;
        if (mx < 24 || mx > 521 || my < DY || r > 23) return;
        if (r == hourSel) { pendRoutine = false; pendH = hourSel; startInput(IN_TIME, 0); } else hourSel = r;
    }
}

// ---------------------------------------------------------------- Alarmas
static void initAudio() {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return;
    SDL_AudioSpec w{};
    w.freq = 22050; w.format = AUDIO_S16SYS; w.channels = 1; w.samples = 1024; w.callback = nullptr;
    g_audio = SDL_OpenAudioDevice(nullptr, 0, &w, nullptr, 0);
    if (g_audio) SDL_PauseAudioDevice(g_audio, 0);
}
static void beep() {
    if (!g_audio) return;
    static vector<short> buf;
    if (buf.empty())
        for (int p = 0; p < 3; p++) {
            for (int i = 0; i < 3300; i++) {
                float env = min(1.f, min(i, 3300 - i) / 300.f);
                buf.push_back((short)(7000 * env * sinf(2 * PI * 880 * i / 22050.f)));
            }
            buf.insert(buf.end(), 1800, 0);
        }
    SDL_ClearQueuedAudio(g_audio);
    SDL_QueueAudio(g_audio, buf.data(), (Uint32)(buf.size() * sizeof(short)));
}

// Se ejecuta cada vez que cambia el minuto del reloj del sistema.
static Date g_lastToday{0, 0, 0};
static long g_lastMinute = -1;
static void clockTick() {
    time_t nt = time(nullptr);
    if (nt / 60 == g_lastMinute) return;
    g_lastMinute = nt / 60;
    tm *lt = localtime(&nt);
    Date td = today();
    if (!(td == g_lastToday)) {  // cambio de día: si estabas en "hoy", sigue a "hoy"
        if (g_lastToday.y && cur == g_lastToday) { cur = td; hourSel = lt->tm_hour; }
        g_lastToday = td;
    }
    string txt;
    int n = 0;
    string body;
    for (auto &it : ag.itemsOn(td))
        if (it.alarm && it.hour == lt->tm_hour && it.minute == lt->tm_min) {
            if (!n++) { txt = string(it.kind == 2 ? "TAREA " : "ALARMA ") + timeStr(lt->tm_hour, lt->tm_min) + " " + it.title; body = it.desc.empty() ? txt : txt + " - " + cutGlyphs(oneLine(it.desc), 80); }
        }
    if (n) {
        if (n > 1) txt += " (+" + to_string(n - 1) + ")";
        g_alarmText = txt; g_alarmEnd = g_now + 12.f;
        setMood(M_WAVE, 6.f);
        beep();
        if (g_deskAlarms) desktopNotify("AGENDA 3000", n > 1 ? txt : body);
    }
    // objetos marcados: aviso de escritorio "lleva esto"
    string list;
    int cnt = 0, wd = weekday(td);
    for (auto &t : ag.th)
        if (!t.deleted && t.notify && ((t.nmask >> wd) & 1) && t.nh == lt->tm_hour && t.nm == lt->tm_min) {
            if (cnt++) list += ", ";
            list += to_string(t.carry > 0 ? t.carry : t.qty) + "x " + t.name + (t.place.empty() ? "" : " [" + t.place + "]");
        }
    if (cnt) {
        desktopNotify("LLEVA ESTO (" + to_string(cnt) + ")", list);
        if (!n) { g_alarmText = "LLEVA: " + fitGlyphs(list, 45); g_alarmEnd = g_now + 12.f; setMood(M_WAVE, 6.f); beep(); }
    }
}

// ---------------------------------------------------- Fotos al sincronizar
static SDL_Surface *scaledCopy(SDL_Surface *s, int maxPx) {
    float k = min(1.f, (float)maxPx / (float)max(s->w, s->h));
    SDL_Surface *d = SDL_CreateRGBSurfaceWithFormat(0, max(1, (int)(s->w * k)), max(1, (int)(s->h * k)), 32, SDL_PIXELFORMAT_ARGB8888);
    if (d) SDL_BlitScaled(s, nullptr, d, nullptr);
    return d;
}
static string readBytes(const string &p) { ifstream f(p, ios::binary); stringstream ss; ss << f.rdbuf(); return ss.str(); }
// Foto reducida para enviarla: JPEG de 256 px (calidad 75); si pesa demasiado, 160 px; si falla, PNG de 160 px.
static string pcImgRead(const string &name) {
    SDL_Surface *s0 = IMG_Load((string(IMG_DIR) + "/" + name).c_str());
    if (!s0) return "";
    SDL_Surface *s = SDL_ConvertSurfaceFormat(s0, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(s0);
    if (!s) return "";
    string tmp = "agenda_tmp_" + to_string(nowMs()) + ".img", out;
    const int tries[2][2] = {{256, 75}, {160, 60}};
    for (auto &t : tries) {
        SDL_Surface *d = scaledCopy(s, t[0]);
        if (!d) continue;
        int rc = IMG_SaveJPG(d, tmp.c_str(), t[1]);
        SDL_FreeSurface(d);
        if (rc == 0) { out = readBytes(tmp); if (!out.empty() && out.size() * 4 / 3 < 150000) break; out.clear(); }
    }
    if (out.empty()) {
        SDL_Surface *d = scaledCopy(s, 160);
        if (d) { if (IMG_SavePNG(d, tmp.c_str()) == 0) out = readBytes(tmp); SDL_FreeSurface(d); }
        if (out.size() * 4 / 3 >= 150000) out.clear();
    }
    SDL_FreeSurface(s);
    error_code ec;
    std::filesystem::remove(tmp, ec);
    return out;
}
// Foto recibida: se comprueba que es una imagen y se guarda en imagenes/ con un nombre propio.
static string pcImgWrite(const string &bytes, const string &id) {
    static int counter = 0;
    if (bytes.size() < 16 || bytes.size() > 600000) return "";
    string ext;
    if ((unsigned char)bytes[0] == 0xFF && (unsigned char)bytes[1] == 0xD8) ext = ".jpg";
    else if ((unsigned char)bytes[0] == 0x89 && bytes[1] == 'P') ext = ".png";
    else return "";
    SDL_Surface *chk = IMG_Load_RW(SDL_RWFromConstMem(bytes.data(), (int)bytes.size()), 1);
    if (!chk) return "";
    SDL_FreeSurface(chk);
    error_code ec;
    std::filesystem::create_directories(IMG_DIR, ec);
    string name = id + "_" + to_string(nowMs()) + "_" + to_string(++counter) + ext;
    ofstream f(string(IMG_DIR) + "/" + name, ios::binary);
    f.write(bytes.data(), (streamsize)bytes.size());
    return f ? name : string();
}

// ---------------------------------------------------------------------- Main
int main(int argc, char **argv) {
    // Compilado con -mwindows no hay consola: los errores graves se muestran en una ventana.
    auto fatal = [](const string &m) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "AGENDA 3000", m.c_str(), nullptr); return 1; };
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return fatal(string("No se pudo iniciar SDL: ") + SDL_GetError());
    SDL_Window *win = SDL_CreateWindow("AGENDA 3000", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1000, 750, SDL_WINDOW_RESIZABLE);
    if (!win) return fatal(string("No se pudo crear la ventana: ") + SDL_GetError());
    R = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!R) R = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!R) return fatal(string("No se pudo crear el renderizador: ") + SDL_GetError());
    SDL_RenderSetLogicalSize(R, 800, 600);
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
    SDL_StopTextInput();
    initAudio();
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG);
    loadSprite();
    g_imgRead = pcImgRead;
    g_imgWrite = pcImgWrite;

    {
        ifstream c(CFG);
        getline(c, g_syncTarget);
        string l2;
        if (getline(c, l2)) g_deskAlarms = l2 != "0";
        string l3;
        if (getline(c, l3)) g_routineColor = max(0, min(7, atoi(l3.c_str())));
    }
    if (argc > 1) g_syncTarget = argv[1];
    ag.load();
    loadBest();
    cur = today();
    time_t n0 = time(nullptr);
    hourSel = localtime(&n0)->tm_hour;
    setMood(M_WAVE, 2.4f);

    Uint32 last = SDL_GetTicks();
    while (running) {
        Uint32 now0 = SDL_GetTicks();
        g_now = now0 / 1000.f;
        unique_lock<mutex> lk(g_mx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            else if (e.type == SDL_KEYDOWN) { g_keyRepeat = e.key.repeat != 0; onKey(e.key.keysym.sym); }
            else if (e.type == SDL_TEXTINPUT && mode != BROWSE) {
                if (trig && SDL_GetTicks() - trigTick < 80 && strlen(e.text.text) == 1 && tolower(e.text.text[0]) == trig) { trig = 0; continue; }
                if (input.size() < 260) input += e.text.text;
            }
            else if (e.type == SDL_DROPFILE) {  // arrastrar un JPG/PNG a la ventana
                if (e.drop.file) {
                    string f = e.drop.file;
                    size_t dot = f.rfind('.');
                    string ex = dot == string::npos ? string() : lowerAscii(f.substr(dot));
                    if (mode != BROWSE) {}
                    else if (ex == ".txt") importAgendaFile(f);
                    else if (view == INVENTORY) importImage(f);
                    else flash("ABRE INVENTARIO (7) Y ELIGE UN OBJETO");
                    SDL_free(e.drop.file);
                }
            }
            else if (e.type == SDL_MOUSEMOTION) {  // DEFENSA con ratón (1 jugador)
                if (view == GAMES && gGame == G_DEF && !g2P && mode == BROWSE && !gameOver()) {
                    gDef.cx[0] = games::clampf((float)e.motion.x - 24.f, 0, games::Def::W);
                    gDef.cy[0] = games::clampf((float)e.motion.y - 64.f, 0, games::Def::GROUND - 12);
                }
            }
            else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && mode == BROWSE) onClick(e.button.x, e.button.y);
            else if (e.type == SDL_MOUSEWHEEL && mode == BROWSE) step(e.wheel.y > 0 ? -1 : 1);
        }
        Uint32 now = SDL_GetTicks();
        float dt = (now - last) / 1000.f;
        last = now;
        g_now = now / 1000.f;

        // animación del brazo y estado de ánimo
        if (g_mood != M_IDLE && g_now > g_moodEnd) g_mood = M_IDLE;
        float target = g_mood == M_THUMBS ? 0.f : g_mood == M_WAVE ? 25.f + 28.f * sinf(g_now * 9) : g_mood == M_WINK ? 110.f : 170.f;
        float diff = target - g_armA, stepA = 500.f * dt;
        g_armA += fabsf(diff) < stepA ? diff : (diff > 0 ? stepA : -stepA);
        { float ta = weekExp ? 1.f : 0.f; g_weekA += (ta - g_weekA) * min(1.f, dt * 14.f); if (fabsf(ta - g_weekA) < 0.01f) g_weekA = ta; }
        g_flick = 0.95f + 0.05f * sinf(g_now * 60);
        if (fmodf(g_now, 9.f) < 0.06f) g_flick = 0.78f;

        clockTick();
        if (view == GAMES && mode == BROWSE) gamesUpdate(min(dt, 0.05f));
        if (g_syncDone) {
            g_syncDone = false;
            flash(g_syncMsg);
            setMood(g_syncOk ? M_THUMBS : M_IDLE, 2.4f);
        }
        drawAll();
        lk.unlock();
        SDL_RenderPresent(R);
        SDL_Delay(4);
    }
    g_syncCancel = true;
    if (g_syncThread.joinable()) g_syncThread.join();
    if (g_sprite) SDL_DestroyTexture(g_sprite);
    freeTexAll();
    notifyCleanup();
    IMG_Quit();
    SDL_DestroyRenderer(R);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
