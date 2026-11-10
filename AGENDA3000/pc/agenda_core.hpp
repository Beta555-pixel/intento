// agenda_core.hpp: modelo de datos y sincronización entre dispositivos.
//
// Transportes (el protocolo es el mismo en todos):
//   - Puerto serie: cable USB-serie / null-modem, o Bluetooth SPP (aparece como COMx en Windows
//     y /dev/rfcommN o /dev/tty.* en Linux/macOS).
//   - TCP: cable Ethernet, Wi-Fi, USB tethering o Bluetooth PAN.  "host:puerto" conecta, ":puerto" espera.
// Fusión: cada evento tiene id único y marca de tiempo; gana la modificación más reciente.
// Los borrados se guardan como "tombstones" para poder propagarse. Los relojes de ambos equipos se
// comparan en el saludo inicial y se compensa el desfase antes de decidir qué cambio es más reciente.
// SIN cifrado ni autenticación: úsalo con cable, Bluetooth emparejado o una red de confianza.
#pragma once
#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
#else
  #include <arpa/inet.h>
  #include <cerrno>
  #include <csignal>
  #include <fcntl.h>
  #include <netdb.h>
  #include <netinet/in.h>
  #include <poll.h>
  #include <sys/select.h>
  #include <sys/socket.h>
  #include <termios.h>
  #include <unistd.h>
#endif
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// ------------------------------------------------------------------ Tiempo
inline thread_local long long g_clockSkewMs = 0;  // solo para pruebas
inline long long nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count() + g_clockSkewMs;
}

// ------------------------------------------------------------------ Fechas
struct Date { int y, m, d; };
inline bool operator==(const Date &a, const Date &b) { return a.y == b.y && a.m == b.m && a.d == b.d; }

inline int daysInMonth(int y, int m) {
    static const int dm[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return dm[m - 1];
}
inline int weekday(Date d) {  // 0 = lunes
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int y = d.y - (d.m < 3);
    return ((y + y / 4 - y / 100 + y / 400 + t[d.m - 1] + d.d) % 7 + 6) % 7;
}
inline Date addDays(Date d, int n) {
    std::tm t{};
    t.tm_year = d.y - 1900; t.tm_mon = d.m - 1; t.tm_mday = d.d + n; t.tm_hour = 12; t.tm_isdst = -1;
    std::mktime(&t);
    return {t.tm_year + 1900, t.tm_mon + 1, t.tm_mday};
}
inline Date addMonths(Date d, int n) {
    int idx = d.y * 12 + (d.m - 1) + n;
    Date r{idx / 12, idx % 12 + 1, d.d};
    r.d = std::min(r.d, daysInMonth(r.y, r.m));
    return r;
}
inline Date today() {
    std::time_t now = std::time(nullptr);
    std::tm *t = std::localtime(&now);
    return {t->tm_year + 1900, t->tm_mon + 1, t->tm_mday};
}
inline std::string dateStr(Date d) { char b[16]; snprintf(b, sizeof b, "%04d-%02d-%02d", d.y, d.m, d.d); return b; }
inline std::string timeStr(int h, int m) { char b[8]; snprintf(b, sizeof b, "%02d:%02d", h, m); return b; }

// ------------------------------------------------------------------ Texto largo, fechas y base64
// Texto libre en una sola línea del archivo: la barra \ y el salto de línea se escapan como \\ y \n;
// la barra vertical | (separador de campos) se sustituye por /.
inline std::string encText(const std::string &s) {
    std::string o;
    for (char c : s) {
        if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') {}
        else if (c == '|') o += '/';
        else o += c;
    }
    return o;
}
inline std::string decText(const std::string &s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size() && (s[i + 1] == 'n' || s[i + 1] == '\\')) { o += s[i + 1] == 'n' ? '\n' : '\\'; i++; }
        else o += s[i];
    }
    return o;
}
inline std::string cutGlyphs(const std::string &s, size_t maxG) {  // recorta por caracteres sin partir UTF-8
    size_t i = 0, n = 0;
    while (i < s.size() && n < maxG) {
        unsigned char c = (unsigned char)s[i];
        size_t l = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
        i = std::min(s.size(), i + l);
        n++;
    }
    return s.substr(0, i);
}
inline std::string cleanText(const std::string &in, size_t maxG) {  // texto largo: conserva saltos de línea
    std::string t;
    for (unsigned char c : in) {
        if (c == '\r' || c == 127 || (c < 32 && c != '\n')) continue;
        t += c == '|' ? '/' : (char)c;
    }
    return cutGlyphs(t, maxG);
}
inline bool validDateStr(const std::string &s) {  // "AAAA-MM-DD" válida
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
    for (int i : {0, 1, 2, 3, 5, 6, 8, 9}) if (!isdigit((unsigned char)s[i])) return false;
    int y = atoi(s.substr(0, 4).c_str()), m = atoi(s.substr(5, 2).c_str()), d = atoi(s.substr(8, 2).c_str());
    return y >= 1000 && m >= 1 && m <= 12 && d >= 1 && d <= daysInMonth(y, m);
}
inline std::string dmyStr(const std::string &k) { return k.size() == 10 ? k.substr(8) + "/" + k.substr(5, 2) : std::string(); }
inline bool hexId(const std::string &s) {
    if (s.empty() || s.size() > 32) return false;
    for (char c : s) if (!isxdigit((unsigned char)c)) return false;
    return true;
}
inline bool safeImgName(const std::string &s) {  // nombre de archivo de imagen: sin rutas
    if (s.size() > 64 || (!s.empty() && s[0] == '.')) return false;
    for (char c : s) if (!isalnum((unsigned char)c) && c != '.' && c != '_' && c != '-') return false;
    return true;
}

inline std::string b64enc(const std::string &in) {
    static const char *T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    o.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        unsigned v = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8) | (unsigned char)in[i + 2];
        o += T[v >> 18]; o += T[(v >> 12) & 63]; o += T[(v >> 6) & 63]; o += T[v & 63];
    }
    if (i + 1 == in.size()) {
        unsigned v = (unsigned char)in[i] << 16;
        o += T[v >> 18]; o += T[(v >> 12) & 63]; o += "==";
    } else if (i + 2 == in.size()) {
        unsigned v = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8);
        o += T[v >> 18]; o += T[(v >> 12) & 63]; o += T[(v >> 6) & 63]; o += '=';
    }
    return o;
}
inline bool b64dec(const std::string &in, std::string &out) {
    out.clear();
    unsigned v = 0;
    int bits = 0;
    for (char c : in) {
        int d;
        if (c >= 'A' && c <= 'Z') d = c - 'A';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 26;
        else if (c >= '0' && c <= '9') d = c - '0' + 52;
        else if (c == '+') d = 62;
        else if (c == '/') d = 63;
        else if (c == '=' || c == '\r' || c == '\n' || c == ' ') continue;
        else return false;
        v = (v << 6) | (unsigned)d;
        bits += 6;
        if (bits >= 8) { bits -= 8; out += (char)((v >> bits) & 255); }
    }
    return true;
}

// Ganchos de fotos que pone la interfaz (el núcleo no depende de SDL):
//   g_imgRead(nombre)       -> bytes de la foto ya reducida para enviarla ("" = no se pudo leer)
//   g_imgWrite(bytes, id)   -> guarda una foto recibida en la carpeta de imágenes y devuelve su nombre ("" = error)
inline std::function<std::string(const std::string &)> g_imgRead;
inline std::function<std::string(const std::string &, const std::string &)> g_imgWrite;

// ------------------------------------------------------------------ Eventos
struct Event {
    std::string id;
    Date date{1970, 1, 1};
    int hour = 0, minute = 0;
    std::string title;
    long long updated = 0;  // ms desde epoch según el reloj del equipo que hizo el cambio
    bool deleted = false;   // tombstone
    // ampliación (línea X): fecha de fin "AAAA-MM-DD" (vacía = ninguna), hora de fin en minutos (-1 = ninguna), descripción
    std::string end, desc;
    int endMin = -1;
    long long xu = 0;       // marca de la ampliación
};

inline std::string cleanTitle(const std::string &in) {
    std::string t;
    for (unsigned char c : in) if (c >= 32 && c != 127) t += (c == '|' ? '/' : (char)c);
    if (t.size() > 120) {
        t.resize(120);
        while (!t.empty() && ((unsigned char)t.back() & 0xC0) == 0x80) t.pop_back();  // no cortar UTF-8
        if (!t.empty() && ((unsigned char)t.back() & 0xC0) == 0xC0) t.pop_back();
    }
    return t;
}

inline std::string encodeEvent(const Event &e) {
    return e.id + "|" + dateStr(e.date) + "|" + std::to_string(e.hour) + "|" + std::to_string(e.minute) + "|" +
           std::to_string(e.updated) + "|" + (e.deleted ? "1" : "0") + "|" + e.title;
}

inline bool decodeEvent(const std::string &line, Event &e) {  // valida todo: el otro lado no es de fiar
    std::vector<std::string> p;
    size_t pos = 0;
    for (int i = 0; i < 6; i++) {
        size_t q = line.find('|', pos);
        if (q == std::string::npos) return false;
        p.push_back(line.substr(pos, q - pos));
        pos = q + 1;
    }
    if (p[0].empty() || p[0].size() > 32) return false;
    for (char c : p[0]) if (!isxdigit((unsigned char)c)) return false;
    Date d;
    if (sscanf(p[1].c_str(), "%d-%d-%d", &d.y, &d.m, &d.d) != 3 || d.y < 1970 || d.y > 2200 || d.m < 1 || d.m > 12 ||
        d.d < 1 || d.d > daysInMonth(d.y, d.m))
        return false;
    try {
        e.hour = std::stoi(p[2]); e.minute = std::stoi(p[3]);
        e.updated = std::stoll(p[4]);
        e.deleted = std::stoi(p[5]) != 0;
    } catch (...) { return false; }
    if (e.hour < 0 || e.hour > 23 || e.minute < 0 || e.minute > 59) return false;
    e.id = p[0]; e.date = d; e.title = cleanTitle(line.substr(pos));
    return true;
}

// Rutina semanal: se repite los días marcados en 'mask' (bit0 = lunes ... bit6 = domingo).
// 'skips' guarda fechas omitidas ("AAAA-MM-DD,AAAA-MM-DD") sin borrar la regla.
struct Routine {
    std::string id;
    int mask = 0, hour = 0, minute = 0;
    std::string title, skips;
    long long updated = 0;
    bool deleted = false;
    std::string end, desc;  // "hasta" (fecha de fin de la rutina) y descripción (línea X)
    long long xu = 0;
};
inline std::string encodeRoutine(const Routine &r) {
    return r.id + "|" + std::to_string(r.mask) + "|" + std::to_string(r.hour) + "|" + std::to_string(r.minute) + "|" +
           std::to_string(r.updated) + "|" + (r.deleted ? "1" : "0") + "|" + r.skips + "|" + r.title;
}
inline bool decodeRoutine(const std::string &line, Routine &r) {
    std::vector<std::string> p;
    size_t pos = 0;
    for (int i = 0; i < 7; i++) {
        size_t q = line.find('|', pos);
        if (q == std::string::npos) return false;
        p.push_back(line.substr(pos, q - pos));
        pos = q + 1;
    }
    if (p[0].empty() || p[0].size() > 32) return false;
    for (char c : p[0]) if (!isxdigit((unsigned char)c)) return false;
    try {
        r.mask = std::stoi(p[1]); r.hour = std::stoi(p[2]); r.minute = std::stoi(p[3]);
        r.updated = std::stoll(p[4]); r.deleted = std::stoi(p[5]) != 0;
    } catch (...) { return false; }
    if (r.mask < 1 || r.mask > 127 || r.hour < 0 || r.hour > 23 || r.minute < 0 || r.minute > 59) return false;
    if (p[6].size() > 2000 || p[6].find_first_not_of("0123456789-,") != std::string::npos) return false;
    r.id = p[0]; r.skips = p[6]; r.title = cleanTitle(line.substr(pos));
    return true;
}

// Objeto del inventario: dónde está guardado (ruta), cuántos hay, cuántos llevo y aviso opcional.
struct Thing {
    std::string id, name, place, image;  // 'image' = nombre de archivo dentro de la carpeta imagenes/
    int qty = 1, carry = 0;
    bool notify = false;
    int nh = 8, nm = 0, nmask = 127;  // aviso de escritorio: hora, minuto y días (bit0 = lunes)
    long long updated = 0;
    bool deleted = false;
    std::string desc, image2;  // descripción (línea X) y foto secundaria (solo local, línea P)
    long long xu = 0, iu = 0;  // marcas de la ampliación y del conjunto de fotos
};
// wire = true: las rutas de las fotos son locales y nunca viajan por el cable
inline std::string encodeThing(const Thing &t, bool wire = false) {
    return t.id + "|" + std::to_string(t.qty) + "|" + std::to_string(t.carry) + "|" + (t.notify ? "1" : "0") + "|" +
           std::to_string(t.nh) + "|" + std::to_string(t.nm) + "|" + std::to_string(t.nmask) + "|" + std::to_string(t.updated) +
           "|" + (t.deleted ? "1" : "0") + "|" + (wire ? std::string() : t.image) + "|" + t.place + "|" + t.name;
}
inline bool decodeThing(const std::string &line, Thing &t, bool wire = false) {
    std::vector<std::string> p;
    size_t pos = 0;
    for (int i = 0; i < 11; i++) {
        size_t q = line.find('|', pos);
        if (q == std::string::npos) return false;
        p.push_back(line.substr(pos, q - pos));
        pos = q + 1;
    }
    if (p[0].empty() || p[0].size() > 32) return false;
    for (char c : p[0]) if (!isxdigit((unsigned char)c)) return false;
    try {
        t.qty = std::stoi(p[1]); t.carry = std::stoi(p[2]); t.notify = std::stoi(p[3]) != 0;
        t.nh = std::stoi(p[4]); t.nm = std::stoi(p[5]); t.nmask = std::stoi(p[6]);
        t.updated = std::stoll(p[7]); t.deleted = std::stoi(p[8]) != 0;
    } catch (...) { return false; }
    if (t.qty < 0 || t.qty > 999999 || t.carry < 0 || t.carry > 999999) return false;
    if (t.nh < 0 || t.nh > 23 || t.nm < 0 || t.nm > 59 || t.nmask < 1 || t.nmask > 127) return false;
    // el nombre de imagen viene del otro equipo: solo caracteres seguros, sin rutas
    if (!wire && !safeImgName(p[9])) return false;
    t.id = p[0]; t.image = wire ? std::string() : p[9]; t.place = cleanTitle(p[10]); t.name = cleanTitle(line.substr(pos));
    return !t.name.empty();
}

// Tarea: fecha límite y hora opcionales, prioridad 0 baja / 1 media / 2 alta.
struct AgTask {
    std::string id, due, desc, title;  // due = "AAAA-MM-DD" o vacío
    int pri = 1, dt = -1;              // dt = minutos desde medianoche, -1 = sin hora
    bool done = false, deleted = false;
    long long updated = 0;
};
inline std::string encodeTask(const AgTask &k) {
    return k.id + "|" + std::to_string(k.updated) + "|" + (k.deleted ? "1" : "0") + "|" + (k.done ? "1" : "0") + "|" +
           std::to_string(k.pri) + "|" + k.due + "|" + std::to_string(k.dt) + "|" + encText(k.desc) + "|" + k.title;
}
inline bool splitN(const std::string &line, int n, std::vector<std::string> &p, size_t &pos) {
    p.clear(); pos = 0;
    for (int i = 0; i < n; i++) {
        size_t q = line.find('|', pos);
        if (q == std::string::npos) return false;
        p.push_back(line.substr(pos, q - pos));
        pos = q + 1;
    }
    return true;
}
inline bool decodeTask(const std::string &line, AgTask &k) {
    std::vector<std::string> p;
    size_t pos;
    if (!splitN(line, 8, p, pos) || !hexId(p[0])) return false;
    try {
        k.updated = std::stoll(p[1]); k.deleted = std::stoi(p[2]) != 0; k.done = std::stoi(p[3]) != 0;
        k.pri = std::stoi(p[4]); k.dt = std::stoi(p[6]);
    } catch (...) { return false; }
    if (k.updated < 0 || k.updated > 1000000000000000LL || k.pri < 0 || k.pri > 2 || k.dt < -1 || k.dt > 1439) return false;
    if (!p[5].empty() && !validDateStr(p[5])) return false;
    k.title = cleanTitle(line.substr(pos));
    if (k.title.empty()) return false;
    k.id = p[0]; k.due = p[5]; k.desc = cleanText(decText(p[7]), 1000);
    return true;
}

// Nota: título y texto de varias líneas; las fijadas salen primero.
struct AgNote {
    std::string id, title, text;
    bool pin = false, deleted = false;
    long long updated = 0;
};
inline std::string encodeNote(const AgNote &n) {
    return n.id + "|" + std::to_string(n.updated) + "|" + (n.deleted ? "1" : "0") + "|" + (n.pin ? "1" : "0") + "|" + n.title + "|" + encText(n.text);
}
inline bool decodeNote(const std::string &line, AgNote &n) {
    std::vector<std::string> p;
    size_t pos;
    if (!splitN(line, 5, p, pos) || !hexId(p[0])) return false;
    try { n.updated = std::stoll(p[1]); n.deleted = std::stoi(p[2]) != 0; n.pin = std::stoi(p[3]) != 0; } catch (...) { return false; }
    if (n.updated < 0 || n.updated > 1000000000000000LL) return false;
    n.id = p[0]; n.title = cleanTitle(p[4]); n.text = cleanText(decText(line.substr(pos)), 1800);
    return true;
}

// Ampliación (X): fecha de fin, hora de fin y descripción de un evento, rutina u objeto. Va en línea aparte
// para que los equipos antiguos (que no la conocen) la ignoren sin romperse.
struct ExtRec { std::string id, end, desc; long long xu = 0; int et = -1; };
inline std::string encodeExt(const std::string &id, long long xu, const std::string &end, int et, const std::string &desc) {
    return id + "|" + std::to_string(xu) + "|" + end + "|" + std::to_string(et) + "|" + encText(desc);
}
inline bool decodeExt(const std::string &line, ExtRec &x) {
    std::vector<std::string> p;
    size_t pos;
    if (!splitN(line, 4, p, pos) || !hexId(p[0])) return false;
    try { x.xu = std::stoll(p[1]); x.et = std::stoi(p[3]); } catch (...) { return false; }
    if (x.xu < 0 || x.xu > 1000000000000000LL || x.et < -1 || x.et > 1439) return false;
    if (!p[2].empty() && !validDateStr(p[2])) return false;
    x.id = p[0]; x.end = p[2]; x.desc = cleanText(decText(line.substr(pos)), 1000);
    return true;
}

// Conjunto de fotos de un objeto (principal + secundaria), en base64. 'iu' = marca de tiempo del conjunto.
struct ImgSet { std::string id, p1, p2; long long iu = 0; };
struct ImgAssembler {  // reconstruye las fotos a partir de líneas  I|id|iu|foto|k|n|base64  (foto 0 cierra el conjunto)
    struct Part { std::vector<std::string> ch; int got = 0; bool any = false; };
    struct Cur { long long iu = -1; Part p[3]; };
    std::unordered_map<std::string, Cur> cur;
    std::vector<ImgSet> done;
    size_t bytes = 0;
    bool feed(const std::string &line) {  // 'line' sin el prefijo "I|"
        std::vector<std::string> p;
        size_t pos;
        if (!splitN(line, 5, p, pos) || !hexId(p[0])) return false;
        long long iu; int foto, k, n;
        try { iu = std::stoll(p[1]); foto = std::stoi(p[2]); k = std::stoi(p[3]); n = std::stoi(p[4]); } catch (...) { return false; }
        if (iu < 0 || iu > 1000000000000000LL || foto < 0 || foto > 2 || k < 0 || n < 0 || n > 200) return false;
        Cur &c = cur[p[0]];
        if (c.iu != iu) { c = Cur(); c.iu = iu; }
        if (foto == 0) {
            auto join = [&](Part &pt, std::string &out) {
                if (!pt.any) return 1;                         // no existe
                if (pt.got != (int)pt.ch.size()) return -1;    // incompleta
                for (auto &s : pt.ch) out += s;
                return 0;
            };
            ImgSet s;
            s.id = p[0]; s.iu = iu;
            int a = join(c.p[1], s.p1), b = join(c.p[2], s.p2);
            cur.erase(p[0]);
            if (a == 0 && b >= 0) done.push_back(std::move(s));  // sin principal o con la secundaria rota no se aplica nada
            return true;
        }
        std::string data = line.substr(pos);
        if (n == 0 || k >= n || data.size() > 4000) return false;
        Part &pt = c.p[foto];
        if (!pt.any) { pt.any = true; pt.ch.assign(n, std::string()); }
        if ((int)pt.ch.size() != n) return false;
        if (pt.ch[k].empty()) { pt.got++; bytes += data.size(); }
        pt.ch[k] = data;
        return bytes < 64u * 1024 * 1024;
    }
};

// Todo lo recibido (por cable o de un archivo) antes de fusionarlo.
struct LocPhoto { std::string id, file2; long long iu = 0; };
struct Wire {
    std::vector<Event> ev;
    std::vector<Routine> rt;
    std::vector<Thing> th;
    std::vector<AgTask> tk;
    std::vector<AgNote> nt;
    std::vector<ExtRec> ex;
    std::vector<LocPhoto> loc;  // líneas P (solo en el archivo del PC)
    size_t total() const { return ev.size() + rt.size() + th.size() + tk.size() + nt.size() + ex.size(); }
};
// wire=true: formato por cable (los eventos llevan prefijo "E|"; las fotos del equipo remoto se ignoran).
// wire=false: formato de archivo (eventos sin prefijo; admite líneas P). Devuelve false si no se reconoce.
inline bool parseLine(const std::string &line, Wire &w, bool wire) {
    if (line.size() > 2 && line[1] == '|') {
        std::string r = line.substr(2);
        switch (line[0]) {
            case 'E': if (!wire) break; { Event e; if (!decodeEvent(r, e)) return false; w.ev.push_back(e); return true; }
            case 'R': { Routine x; if (!decodeRoutine(r, x)) return false; w.rt.push_back(x); return true; }
            case 'T': { Thing x; if (!decodeThing(r, x, wire)) return false; w.th.push_back(x); return true; }
            case 'K': { AgTask x; if (!decodeTask(r, x)) return false; w.tk.push_back(x); return true; }
            case 'N': { AgNote x; if (!decodeNote(r, x)) return false; w.nt.push_back(x); return true; }
            case 'X': { ExtRec x; if (!decodeExt(r, x)) return false; w.ex.push_back(x); return true; }
            case 'P': {
                if (wire) return false;
                std::vector<std::string> p;
                size_t pos;
                if (!splitN(r, 2, p, pos) || !hexId(p[0])) return false;
                LocPhoto l;
                try { l.iu = std::stoll(p[1]); } catch (...) { return false; }
                l.id = p[0]; l.file2 = r.substr(pos);
                if (!safeImgName(l.file2)) return false;
                w.loc.push_back(l);
                return true;
            }
            default: break;
        }
    }
    if (wire) return false;
    Event e;
    if (!decodeEvent(line, e)) return false;
    w.ev.push_back(e);
    return true;
}

// Elemento de un día: kind 0 = evento, 1 = rutina, 2 = tarea.
struct Item {
    int hour = 0, minute = 0;
    std::string title;
    bool routine = false;   // kind == 1
    int idx = 0;            // índice dentro de ev, rt o tk según 'kind'
    int kind = 0;
    std::string desc, tag;  // tag = fin del evento ("12/11" o "18:30")
    bool tm = false, done = false, overdue = false, alarm = true;
};

struct Agenda {
    std::vector<Event> ev;
    std::vector<Routine> rt;
    std::vector<Thing> th;
    std::vector<AgTask> tk;
    std::vector<AgNote> nt;
    std::string file = "agenda.txt";

    static std::string newId() {
        static std::mt19937_64 g{std::random_device{}() ^ (uint64_t)nowMs()};
        char b[20];
        snprintf(b, sizeof b, "%016llx", (unsigned long long)g());
        return b;
    }
    void load() {
        std::ifstream f(file);
        std::string line;
        bool migrated = false;
        Wire w;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line.rfind("I|", 0) == 0) continue;
            if (parseLine(line, w, false)) continue;
            // formato antiguo: AAAA-MM-DD|hora|min|titulo
            std::istringstream ss(line);
            std::string a, b, c, t;
            if (!std::getline(ss, a, '|') || !std::getline(ss, b, '|') || !std::getline(ss, c, '|') || !std::getline(ss, t)) continue;
            Date d;
            Event e;
            if (sscanf(a.c_str(), "%d-%d-%d", &d.y, &d.m, &d.d) != 3) continue;
            try { e.hour = std::stoi(b); e.minute = std::stoi(c); } catch (...) { continue; }
            e.id = newId(); e.date = d; e.title = cleanTitle(t); e.updated = nowMs();
            w.ev.push_back(e);
            migrated = true;
        }
        ev = std::move(w.ev); rt = std::move(w.rt); th = std::move(w.th); tk = std::move(w.tk); nt = std::move(w.nt);
        mergeExt(w.ex, 0, true);
        for (auto &l : w.loc)
            for (auto &t : th) if (t.id == l.id) { t.iu = l.iu; t.image2 = l.file2; }
        for (auto &t : th) if (t.iu == 0 && !t.image.empty()) t.iu = t.updated;  // fotos antiguas sin marca
        if (migrated) save();
    }
    void save() const {
        std::string tmp = file + ".tmp";
        {
            std::ofstream f(tmp);
            for (auto &e : ev) {
                f << encodeEvent(e) << '\n';
                if (e.xu || !e.end.empty() || !e.desc.empty() || e.endMin >= 0) f << "X|" << encodeExt(e.id, e.xu ? e.xu : e.updated, e.end, e.endMin, e.desc) << '\n';
            }
            for (auto &r : rt) {
                f << "R|" << encodeRoutine(r) << '\n';
                if (r.xu || !r.end.empty() || !r.desc.empty()) f << "X|" << encodeExt(r.id, r.xu ? r.xu : r.updated, r.end, -1, r.desc) << '\n';
            }
            for (auto &t : th) {
                f << "T|" << encodeThing(t) << '\n';
                if (t.xu || !t.desc.empty()) f << "X|" << encodeExt(t.id, t.xu ? t.xu : t.updated, "", -1, t.desc) << '\n';
                if (t.iu || !t.image2.empty()) f << "P|" << t.id << '|' << t.iu << '|' << t.image2 << '\n';
            }
            for (auto &k : tk) f << "K|" << encodeTask(k) << '\n';
            for (auto &n : nt) f << "N|" << encodeNote(n) << '\n';
        }
        std::remove(file.c_str());
        std::rename(tmp.c_str(), file.c_str());
    }
    std::vector<int> on(Date d) const {  // eventos vivos que empiezan un día, por hora
        std::vector<int> r;
        for (int i = 0; i < (int)ev.size(); i++) if (!ev[i].deleted && ev[i].date == d) r.push_back(i);
        std::sort(r.begin(), r.end(), [&](int a, int b) { return ev[a].hour * 60 + ev[a].minute < ev[b].hour * 60 + ev[b].minute; });
        return r;
    }
    int liveCount() const { int n = 0; for (auto &e : ev) n += !e.deleted; return n; }
    int add(Date d, int h, int m, const std::string &title) {
        Event e;
        e.id = newId(); e.date = d; e.hour = h; e.minute = m; e.title = cleanTitle(title); e.updated = nowMs();
        ev.push_back(e);
        save();
        return (int)ev.size() - 1;
    }
    void touchEvent(int i) { ev[i].updated = ev[i].xu = nowMs(); save(); }  // tras cambiar cualquier campo
    void removeAt(int i) { ev[i].deleted = true; ev[i].updated = nowMs(); save(); }

    // ---- Rutinas
    static bool routineHas(const Routine &r, Date d) {
        if (r.deleted || !((r.mask >> weekday(d)) & 1)) return false;
        std::string k = dateStr(d);
        if (!r.end.empty() && k > r.end) return false;
        return r.skips.find(k) == std::string::npos;
    }
    // Eventos (también los de varios días), rutinas (hasta su fecha de fin) y tareas de un día, por hora.
    // Las tareas vencidas y sin terminar salen en el día de hoy.
    std::vector<Item> itemsOn(Date d) const {
        std::vector<Item> v;
        std::string k = dateStr(d), td = dateStr(today());
        for (int i = 0; i < (int)ev.size(); i++) {
            const Event &e = ev[i];
            if (e.deleted) continue;
            std::string ds = dateStr(e.date), en = (!e.end.empty() && e.end >= ds) ? e.end : ds;
            if (k < ds || k > en) continue;
            bool first = k == ds, last = k == en;
            Item it;
            it.hour = first ? e.hour : 0; it.minute = first ? e.minute : 0; it.title = e.title; it.idx = i; it.kind = 0;
            it.desc = e.desc; it.alarm = first;
            if (!last) it.tag = dmyStr(en);
            else if (e.endMin >= 0 && (en != ds || e.endMin > e.hour * 60 + e.minute)) it.tag = timeStr(e.endMin / 60, e.endMin % 60);
            v.push_back(it);
        }
        for (int i = 0; i < (int)rt.size(); i++)
            if (routineHas(rt[i], d)) {
                Item it;
                it.hour = rt[i].hour; it.minute = rt[i].minute; it.title = rt[i].title; it.routine = true; it.idx = i; it.kind = 1; it.desc = rt[i].desc;
                v.push_back(it);
            }
        for (int i = 0; i < (int)tk.size(); i++) {
            const AgTask &x = tk[i];
            if (x.deleted) continue;
            bool od = !x.done && !x.due.empty() && x.due < td && k == td;
            if (x.due != k && !od) continue;
            bool tm = x.dt >= 0 && !od;
            Item it;
            it.hour = tm ? x.dt / 60 : (od ? 0 : 9); it.minute = tm ? x.dt % 60 : 0;
            it.title = x.title; it.idx = i; it.kind = 2; it.desc = x.desc; it.tm = tm; it.done = x.done; it.overdue = od;
            it.alarm = !x.done && !od;
            v.push_back(it);
        }
        std::stable_sort(v.begin(), v.end(), [](const Item &a, const Item &b) { return a.hour * 60 + a.minute < b.hour * 60 + b.minute; });
        return v;
    }
    std::vector<int> routinesOnWeekday(int wd) const {
        std::vector<int> r;
        for (int i = 0; i < (int)rt.size(); i++) if (!rt[i].deleted && ((rt[i].mask >> wd) & 1)) r.push_back(i);
        std::sort(r.begin(), r.end(), [&](int a, int b) { return rt[a].hour * 60 + rt[a].minute < rt[b].hour * 60 + rt[b].minute; });
        return r;
    }
    int addRoutine(int mask, int h, int m, const std::string &title) {
        Routine r;
        r.id = newId(); r.mask = mask; r.hour = h; r.minute = m; r.title = cleanTitle(title); r.updated = nowMs();
        rt.push_back(r);
        save();
        return (int)rt.size() - 1;
    }
    void touchRoutine(int i) { rt[i].updated = rt[i].xu = nowMs(); save(); }
    void removeRoutine(int i) { rt[i].deleted = true; rt[i].updated = nowMs(); save(); }
    void skipRoutine(int i, Date d) {
        std::string ds = dateStr(d);
        if (rt[i].skips.find(ds) != std::string::npos) return;
        if (!rt[i].skips.empty()) rt[i].skips += ",";
        rt[i].skips += ds;
        while (rt[i].skips.size() > 1900) {  // descarta las omisiones más antiguas
            size_t c = rt[i].skips.find(',');
            rt[i].skips.erase(0, c == std::string::npos ? rt[i].skips.size() : c + 1);
        }
        rt[i].updated = nowMs();
        save();
    }

    // ---- Tareas y notas
    int addTask(const std::string &title, const std::string &due, int dt, int pri, const std::string &desc) {
        AgTask k;
        k.id = newId(); k.title = cleanTitle(title); k.due = due; k.dt = due.empty() ? -1 : dt; k.pri = std::max(0, std::min(2, pri));
        k.desc = cleanText(desc, 1000); k.updated = nowMs();
        tk.push_back(k);
        save();
        return (int)tk.size() - 1;
    }
    void touchTask(int i) { tk[i].updated = nowMs(); save(); }
    void removeTask(int i) { tk[i].deleted = true; tk[i].updated = nowMs(); save(); }
    int addNote(const std::string &title, const std::string &text, bool pin) {
        AgNote n;
        n.id = newId(); n.title = cleanTitle(title.empty() ? "SIN TITULO" : title); n.text = cleanText(text, 1800); n.pin = pin; n.updated = nowMs();
        nt.push_back(n);
        save();
        return (int)nt.size() - 1;
    }
    void touchNote(int i) { nt[i].updated = nowMs(); save(); }
    void removeNote(int i) { nt[i].deleted = true; nt[i].updated = nowMs(); save(); }

    // ---- Inventario
    int addThing(const std::string &name, const std::string &place, int qty) {
        Thing t;
        t.id = newId(); t.name = cleanTitle(name); t.place = cleanTitle(place); t.qty = std::max(0, std::min(qty, 999999)); t.updated = nowMs();
        th.push_back(t);
        save();
        return (int)th.size() - 1;
    }
    void touchThing(int i) { th[i].updated = nowMs(); save(); }  // llamar tras modificar un campo
    void touchThingDesc(int i) { th[i].updated = th[i].xu = nowMs(); save(); }
    void touchPhotos(int i) { th[i].updated = th[i].iu = nowMs(); save(); }  // cambió alguna de las dos fotos
    void removeThing(int i) { th[i].deleted = true; th[i].updated = nowMs(); save(); }

    // ---- Fusión: gana la modificación más reciente (en empate gana el borrado)
    struct MergeStats { int added = 0, changed = 0, removed = 0, photos = 0; };
    template <class T, class Same, class Keep>
    static void mergeById(std::vector<T> &loc, const std::vector<T> &remote, long long off, Same same, Keep keep, MergeStats &st) {
        std::unordered_map<std::string, int> idx;
        for (int i = 0; i < (int)loc.size(); i++) idx[loc[i].id] = i;
        for (T r : remote) {
            r.updated -= off;  // llevar la marca al reloj local
            auto it = idx.find(r.id);
            if (it == idx.end()) {
                idx[r.id] = (int)loc.size();
                loc.push_back(r);
                if (!r.deleted) st.added++;
                continue;
            }
            T &l = loc[it->second];
            bool take = r.updated > l.updated || (r.updated == l.updated && r.deleted && !l.deleted);
            if (!take) continue;
            if (!same(l, r)) {
                if (r.deleted && !l.deleted) st.removed++;
                else if (!r.deleted && l.deleted) st.added++;
                else st.changed++;
            }
            keep(l, r);  // los campos que el otro equipo no envía (ampliaciones, fotos) se conservan
            l = r;
        }
    }
    void mergeExt(const std::vector<ExtRec> &ex, long long off, bool force = false) {
        for (auto &e : ex) {
            long long xu = e.xu - off;
            for (auto &x : ev) if (x.id == e.id && (force || xu > x.xu)) { x.end = e.end; x.endMin = e.et; x.desc = e.desc; x.xu = xu; }
            for (auto &x : rt) if (x.id == e.id && (force || xu > x.xu)) { x.end = e.end; x.desc = e.desc; x.xu = xu; }
            for (auto &x : th) if (x.id == e.id && (force || xu > x.xu)) { x.desc = e.desc; x.xu = xu; }
        }
    }
    MergeStats merge(const Wire &w, long long off) {
        MergeStats st;
        mergeById(ev, w.ev, off, [](const Event &a, const Event &b) {
            return a.date == b.date && a.hour == b.hour && a.minute == b.minute && a.title == b.title && a.deleted == b.deleted;
        }, [](const Event &l, Event &r) { if (l.xu) { r.end = l.end; r.endMin = l.endMin; r.desc = l.desc; r.xu = l.xu; } }, st);
        mergeById(rt, w.rt, off, [](const Routine &a, const Routine &b) {
            return a.mask == b.mask && a.hour == b.hour && a.minute == b.minute && a.title == b.title && a.skips == b.skips && a.deleted == b.deleted;
        }, [](const Routine &l, Routine &r) { if (l.xu) { r.end = l.end; r.desc = l.desc; r.xu = l.xu; } }, st);
        mergeById(th, w.th, off, [](const Thing &a, const Thing &b) {
            return a.name == b.name && a.place == b.place && a.qty == b.qty && a.carry == b.carry &&
                   a.notify == b.notify && a.nh == b.nh && a.nm == b.nm && a.nmask == b.nmask && a.deleted == b.deleted;
        }, [](const Thing &l, Thing &r) {
            r.image = l.image; r.image2 = l.image2; r.iu = l.iu;
            if (l.xu) { r.desc = l.desc; r.xu = l.xu; }
        }, st);
        mergeById(tk, w.tk, off, [](const AgTask &a, const AgTask &b) {
            return a.title == b.title && a.due == b.due && a.dt == b.dt && a.pri == b.pri && a.done == b.done && a.desc == b.desc && a.deleted == b.deleted;
        }, [](const AgTask &, AgTask &) {}, st);
        mergeById(nt, w.nt, off, [](const AgNote &a, const AgNote &b) {
            return a.title == b.title && a.text == b.text && a.pin == b.pin && a.deleted == b.deleted;
        }, [](const AgNote &, AgNote &) {}, st);
        mergeExt(w.ex, off);
        return st;
    }
    // Fotos recibidas: el conjunto remoto (principal + secundaria) sustituye al local solo si es más nuevo.
    int applyImages(const std::vector<ImgSet> &sets, long long off) {
        int n = 0;
        if (!g_imgWrite) return 0;
        for (auto &s : sets) {
            for (auto &t : th) {
                if (t.id != s.id || t.deleted) continue;
                long long iu = s.iu - off;
                if (iu <= t.iu) break;
                std::string b1, b2, n1, n2;
                if (!b64dec(s.p1, b1) || b1.empty()) break;
                n1 = g_imgWrite(b1, t.id);
                if (n1.empty() || !safeImgName(n1)) break;
                if (!s.p2.empty()) {
                    if (b64dec(s.p2, b2) && !b2.empty()) n2 = g_imgWrite(b2, t.id);
                    if (!safeImgName(n2)) n2.clear();
                }
                t.image = n1; t.image2 = n2; t.iu = iu;
                n++;
                break;
            }
        }
        return n;
    }

    // Importa un archivo agenda.txt (p. ej. el que exporta el móvil) fusionándolo con la agenda actual.
    bool importFile(const std::string &path, std::string &msg) {
        std::ifstream f(path);
        if (!f) { msg = "NO SE PUDO ABRIR EL ARCHIVO"; return false; }
        Wire w;
        ImgAssembler as;
        std::string line;
        int lines = 0;
        while (std::getline(f, line) && lines++ < 400000) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line.size() > 20000) continue;
            if (line.rfind("I|", 0) == 0) as.feed(line.substr(2));
            else if (line.rfind("P|", 0) != 0) parseLine(line, w, false);
        }
        if (w.total() == 0) { msg = "ARCHIVO NO VALIDO"; return false; }
        MergeStats st = merge(w, 0);
        st.photos = applyImages(as.done, 0);
        save();
        msg = "IMPORTADO +" + std::to_string(st.added) + " ~" + std::to_string(st.changed) + " -" + std::to_string(st.removed);
        if (st.photos) msg += "  FOTOS " + std::to_string(st.photos);
        return true;
    }
};

// -------------------------------------------------------------- Transportes
inline std::atomic<bool> g_syncCancel{false};

struct Stream {
    virtual ~Stream() {}
    virtual int read(char *buf, int n) = 0;  // >0 bytes, 0 = nada en 200 ms, <0 = error/cerrado
    virtual bool write(const char *buf, int n) = 0;
};

#ifdef _WIN32
typedef SOCKET sock_t;
static const sock_t BAD_SOCK = INVALID_SOCKET;
inline void closeSock(sock_t s) { closesocket(s); }
inline void setNonBlock(sock_t s) { u_long m = 1; ioctlsocket(s, FIONBIO, &m); }
inline bool sockWouldBlock() { int e = WSAGetLastError(); return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
inline void netInit() { static bool done = false; if (!done) { WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); done = true; } }
static const int SEND_FLAGS = 0;
#else
typedef int sock_t;
static const sock_t BAD_SOCK = -1;
inline void closeSock(sock_t s) { ::close(s); }
inline void setNonBlock(sock_t s) { fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK); }
inline bool sockWouldBlock() { return errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR; }
inline void netInit() {}
#ifdef MSG_NOSIGNAL
static const int SEND_FLAGS = MSG_NOSIGNAL;
#else
static const int SEND_FLAGS = 0;
#endif
#endif

inline int waitSock(sock_t s, bool wr, int ms) {
    fd_set f;
    FD_ZERO(&f);
    FD_SET(s, &f);
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    return select((int)s + 1, wr ? nullptr : &f, wr ? &f : nullptr, nullptr, &tv);
}

struct SockStream : Stream {
    sock_t s;
    explicit SockStream(sock_t x) : s(x) { setNonBlock(s); }
    ~SockStream() override { closeSock(s); }
    int read(char *b, int n) override {
        int w = waitSock(s, false, 200);
        if (w == 0) return 0;
        if (w < 0) return sockWouldBlock() ? 0 : -1;
        int k = (int)recv(s, b, n, 0);
        if (k > 0) return k;
        if (k == 0) return -1;
        return sockWouldBlock() ? 0 : -1;
    }
    bool write(const char *b, int n) override {
        int off = 0;
        while (off < n) {
            if (g_syncCancel) return false;
            int w = waitSock(s, true, 500);
            if (w < 0) return false;
            if (w == 0) continue;
            int k = (int)send(s, b + off, n - off, SEND_FLAGS);
            if (k < 0) { if (sockWouldBlock()) continue; return false; }
            off += k;
        }
        return true;
    }
};

#ifdef _WIN32
struct WinSerial : Stream {
    HANDLE h;
    explicit WinSerial(HANDLE x) : h(x) {}
    ~WinSerial() override { CloseHandle(h); }
    int read(char *b, int n) override {
        DWORD got = 0;
        if (!ReadFile(h, b, (DWORD)n, &got, nullptr)) return -1;
        return got > 0 ? (int)got : 0;
    }
    bool write(const char *b, int n) override {
        int off = 0;
        while (off < n) {
            if (g_syncCancel) return false;
            DWORD w = 0;
            if (!WriteFile(h, b + off, (DWORD)(n - off), &w, nullptr)) return false;
            off += (int)w;
        }
        return true;
    }
};
inline Stream *openSerial(const std::string &path, std::string &err) {
    std::string full = path.rfind("\\\\.\\", 0) == 0 ? path : "\\\\.\\" + path;
    HANDLE h = CreateFileA(full.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) { err = "NO SE PUDO ABRIR " + path; return nullptr; }
    DCB dcb{};
    dcb.DCBlength = sizeof dcb;
    if (GetCommState(h, &dcb)) {
        dcb.BaudRate = CBR_115200; dcb.ByteSize = 8; dcb.Parity = NOPARITY; dcb.StopBits = ONESTOPBIT;
        dcb.fOutxCtsFlow = 0; dcb.fOutxDsrFlow = 0; dcb.fOutX = 0; dcb.fInX = 0;
        dcb.fRtsControl = RTS_CONTROL_ENABLE; dcb.fDtrControl = DTR_CONTROL_ENABLE;
        SetCommState(h, &dcb);
    }
    COMMTIMEOUTS to{};
    to.ReadIntervalTimeout = MAXDWORD; to.ReadTotalTimeoutMultiplier = MAXDWORD; to.ReadTotalTimeoutConstant = 200;
    to.WriteTotalTimeoutConstant = 2000;
    SetCommTimeouts(h, &to);
    return new WinSerial(h);
}
#else
struct FdStream : Stream {
    int fd;
    explicit FdStream(int f) : fd(f) {}
    ~FdStream() override { if (fd >= 0) ::close(fd); }
    int read(char *b, int n) override {
        pollfd p{fd, POLLIN, 0};
        int r = poll(&p, 1, 200);
        if (r == 0) return 0;
        if (r < 0) return errno == EINTR ? 0 : -1;
        ssize_t k = ::read(fd, b, n);
        if (k > 0) return (int)k;
        if (k == 0) return -1;
        return (errno == EAGAIN || errno == EINTR) ? 0 : -1;
    }
    bool write(const char *b, int n) override {
        int off = 0;
        while (off < n) {
            if (g_syncCancel) return false;
            pollfd p{fd, POLLOUT, 0};
            int r = poll(&p, 1, 500);
            if (r < 0 && errno != EINTR) return false;
            if (r <= 0) continue;
            ssize_t k = ::write(fd, b + off, n - off);
            if (k < 0) { if (errno == EAGAIN || errno == EINTR) continue; return false; }
            off += (int)k;
        }
        return true;
    }
};
inline Stream *openSerial(const std::string &path, std::string &err) {
    int fd = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) { err = "NO SE PUDO ABRIR " + path; return nullptr; }
    termios t;
    if (tcgetattr(fd, &t) == 0) {
        cfmakeraw(&t);
        cfsetispeed(&t, B115200); cfsetospeed(&t, B115200);
        t.c_cflag |= CLOCAL | CREAD;
        t.c_cflag &= ~CRTSCTS;
        tcsetattr(fd, TCSANOW, &t);
        tcflush(fd, TCIOFLUSH);
    }
    return new FdStream(fd);
}
#endif

inline Stream *tcpConnect(const std::string &host, int port, std::string &err) {
    netInit();
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0) { err = "HOST NO ENCONTRADO"; return nullptr; }
    Stream *out = nullptr;
    for (addrinfo *a = res; a && !out; a = a->ai_next) {
        sock_t s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == BAD_SOCK) continue;
        setNonBlock(s);
        bool ok = connect(s, a->ai_addr, (int)a->ai_addrlen) == 0;
        if (!ok && sockWouldBlock()) {
            for (int i = 0; i < 50 && !g_syncCancel; i++) {
                int w = waitSock(s, true, 200);
                if (w > 0) { int e = 0; socklen_t l = sizeof e; getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&e, &l); ok = e == 0; break; }
                if (w < 0) break;
            }
        }
        if (ok) out = new SockStream(s); else closeSock(s);
    }
    freeaddrinfo(res);
    if (!out) err = "NO SE PUDO CONECTAR";
    return out;
}

inline Stream *tcpListen(int port, std::string &err) {
    netInit();
    sock_t l = socket(AF_INET, SOCK_STREAM, 0);
    if (l == BAD_SOCK) { err = "ERROR DE SOCKET"; return nullptr; }
    int one = 1;
    setsockopt(l, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = htons((unsigned short)port);
    if (bind(l, (sockaddr *)&a, sizeof a) != 0 || listen(l, 1) != 0) { closeSock(l); err = "PUERTO " + std::to_string(port) + " OCUPADO"; return nullptr; }
    for (int i = 0; i < 450 && !g_syncCancel; i++) {  // ~90 s
        int w = waitSock(l, false, 200);
        if (w > 0) {
            sock_t c = accept(l, nullptr, nullptr);
            closeSock(l);
            if (c == BAD_SOCK) { err = "ERROR AL ACEPTAR"; return nullptr; }
            return new SockStream(c);
        }
        if (w < 0 && !sockWouldBlock()) break;
    }
    closeSock(l);
    err = g_syncCancel ? "CANCELADO" : "NADIE SE CONECTO";
    return nullptr;
}

inline std::string trimStr(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// "COM3", "/dev/rfcomm0", "/dev/ttyUSB0" -> serie | "IP:5050" -> conectar | ":5050" -> esperar
inline Stream *openTarget(const std::string &raw, std::string &err) {
    std::string t = trimStr(raw);
    if (t.empty()) { err = "DESTINO VACIO"; return nullptr; }
    std::string up = t;
    for (auto &c : up) c = (char)toupper((unsigned char)c);
    if (t[0] == '/' || up.rfind("COM", 0) == 0 || t.rfind("\\\\.\\", 0) == 0) return openSerial(t, err);
    size_t c = t.rfind(':');
    if (c == std::string::npos) { err = "FORMATO: COM3, /DEV/RFCOMM0, IP:PUERTO O :PUERTO"; return nullptr; }
    int port = c + 1 < t.size() ? atoi(t.c_str() + c + 1) : 5050;
    if (port < 1 || port > 65535) { err = "PUERTO NO VALIDO"; return nullptr; }
    return c == 0 ? tcpListen(port, err) : tcpConnect(t.substr(0, c), port, err);
}

// ------------------------------------------------------------- Protocolo
struct SyncResult { bool ok = false; std::string msg; };

inline bool readLine(Stream &s, std::string &buf, std::string &line, long long deadline) {
    for (;;) {
        size_t q = buf.find('\n');
        if (q != std::string::npos) {
            line = buf.substr(0, q);
            buf.erase(0, q + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return true;
        }
        if (g_syncCancel || nowMs() > deadline) return false;
        char tmp[512];
        int k = s.read(tmp, sizeof tmp);
        if (k < 0) return false;
        if (k > 0) {
            buf.append(tmp, k);
            if (buf.size() > 8192 && buf.find('\n') == std::string::npos) return false;
        }
    }
}

// Marcas de la última sincronización con cada destino: solo se reenvían las fotos cambiadas después.
inline std::unordered_map<std::string, long long> loadPeers(const char *path = "agenda_peers.txt") {
    std::unordered_map<std::string, long long> m;
    std::ifstream f(path);
    std::string l;
    while (std::getline(f, l)) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        size_t t = l.rfind('\t');
        if (t == std::string::npos) continue;
        try { m[l.substr(0, t)] = std::stoll(l.substr(t + 1)); } catch (...) {}
    }
    return m;
}
inline void savePeers(const std::unordered_map<std::string, long long> &m, const char *path = "agenda_peers.txt") {
    std::ofstream f(path);
    for (auto &kv : m) f << kv.first << '\t' << kv.second << '\n';
}

// Líneas I| de las fotos que cambiaron después de 'since' (0 = todas). Si una foto no se puede leer no se envía
// su conjunto, para no borrar en el otro equipo las que ya tiene.
inline std::vector<std::string> photoLines(const std::vector<Thing> &snap, long long since) {
    std::vector<std::string> out;
    if (!g_imgRead) return out;
    for (auto &t : snap) {
        if (t.deleted || t.image.empty() || t.iu <= since) continue;
        std::string a = g_imgRead(t.image), b;
        if (a.empty()) continue;
        if (!t.image2.empty()) { b = g_imgRead(t.image2); if (b.empty()) continue; }
        auto emit = [&](int n, const std::string &data) {
            std::string e = b64enc(data);
            int total = (int)((e.size() + 2999) / 3000);
            for (int k = 0; k < total; k++)
                out.push_back("I|" + t.id + "|" + std::to_string(t.iu) + "|" + std::to_string(n) + "|" + std::to_string(k) + "|" +
                              std::to_string(total) + "|" + e.substr((size_t)k * 3000, 3000));
        };
        emit(1, a);
        if (!b.empty()) emit(2, b);
        out.push_back("I|" + t.id + "|" + std::to_string(t.iu) + "|0|0|0|");
    }
    return out;
}

// Ambos extremos ejecutan lo mismo: envían su agenda y fusionan la del otro (a la vez, en una sola fase).
//   AGENDA1 <reloj_ms> / E| R| T| X| K| N| (datos) / I| (fotos) / END
// Los equipos antiguos ignoran las líneas que no conocen.
inline SyncResult exchange(Agenda &ag, std::mutex &mx, Stream &s, long long since = 0) {
    Agenda snap;
    { std::lock_guard<std::mutex> l(mx); snap.ev = ag.ev; snap.rt = ag.rt; snap.th = ag.th; snap.tk = ag.tk; snap.nt = ag.nt; }
    long long skew = g_clockSkewMs;
    std::atomic<bool> wok{true};
    std::thread writer([&] {
        g_clockSkewMs = skew;
        std::string out = "AGENDA1 " + std::to_string(nowMs()) + "\n";
        auto ext = [&](const char *, const std::string &id, long long xu, long long upd, const std::string &end, int et, const std::string &desc, bool has) {
            if (has) out += std::string("X|") + encodeExt(id, xu ? xu : upd, end, et, desc) + "\n";
        };
        for (auto &e : snap.ev) {
            out += "E|" + encodeEvent(e) + "\n";
            ext("E", e.id, e.xu, e.updated, e.end, e.endMin, e.desc, e.xu || !e.end.empty() || !e.desc.empty() || e.endMin >= 0);
        }
        for (auto &r : snap.rt) {
            out += "R|" + encodeRoutine(r) + "\n";
            ext("R", r.id, r.xu, r.updated, r.end, -1, r.desc, r.xu || !r.end.empty() || !r.desc.empty());
        }
        for (auto &t : snap.th) {
            out += "T|" + encodeThing(t, true) + "\n";
            ext("T", t.id, t.xu, t.updated, "", -1, t.desc, t.xu || !t.desc.empty());
        }
        for (auto &k : snap.tk) out += "K|" + encodeTask(k) + "\n";
        for (auto &n : snap.nt) out += "N|" + encodeNote(n) + "\n";
        bool ok = s.write(out.data(), (int)out.size());
        if (ok) {  // las fotos se preparan después de enviar los datos: así el otro lado no espera
            std::string ph;
            for (auto &l : photoLines(snap.th, since)) {
                ph += l; ph += '\n';
                if (ph.size() > 60000) { ok = s.write(ph.data(), (int)ph.size()); ph.clear(); if (!ok) break; }
            }
            if (ok) { ph += "END\n"; ok = s.write(ph.data(), (int)ph.size()); }
        }
        wok = ok;
    });

    std::string buf, line;
    Wire w;
    ImgAssembler as;
    bool hello = false, end = false, tooBig = false;
    long long peerNow = 0, recvAt = 0, deadline = nowMs() + 30000;
    int nLines = 0;
    while (readLine(s, buf, line, deadline)) {
        deadline = nowMs() + 20000;
        if (!hello) {
            if (line.rfind("AGENDA1 ", 0) != 0) break;
            peerNow = atoll(line.c_str() + 8);
            recvAt = nowMs();
            hello = true;
        } else if (line == "END") { end = true; break; }
        else {
            if (++nLines > 400000) { tooBig = true; break; }
            if (line.rfind("I|", 0) == 0) as.feed(line.substr(2));
            else parseLine(line, w, true);
            if (w.ev.size() > 50000 || w.rt.size() > 5000 || w.th.size() > 20000 || w.tk.size() > 50000 || w.nt.size() > 20000 || w.ex.size() > 80000) { tooBig = true; break; }
        }
    }
    if (tooBig) g_syncCancel = true;  // corta también la escritura
    writer.join();
    if (tooBig) { g_syncCancel = false; return {false, "DEMASIADOS DATOS"}; }

    if (g_syncCancel) return {false, "SYNC CANCELADA"};
    if (!hello) return {false, "SIN RESPUESTA DEL OTRO DISPOSITIVO"};
    if (!end) return {false, "TRANSMISION INCOMPLETA"};
    if (!wok) return {false, "ERROR AL ENVIAR"};

    long long offset = peerNow - recvAt;  // >0: el otro reloj va adelantado
    bool skewed = std::llabs(offset) >= 2000;
    if (!skewed) offset = 0;
    Agenda::MergeStats st;
    {
        std::lock_guard<std::mutex> l(mx);
        st = ag.merge(w, offset);
        st.photos = ag.applyImages(as.done, offset);
        ag.save();
    }
    std::string m = "SYNC OK +" + std::to_string(st.added) + " ~" + std::to_string(st.changed) + " -" + std::to_string(st.removed);
    if (st.photos) m += "  FOTOS " + std::to_string(st.photos);
    if (skewed) m += "  RELOJ " + std::string(offset > 0 ? "+" : "") + std::to_string(offset / 1000) + "S COMPENSADO";
    return {true, m};
}

// allPhotos = true reenvía todas las fotos, no solo las cambiadas desde la última sincronización con ese destino.
inline SyncResult syncTarget(Agenda &ag, std::mutex &mx, const std::string &target, bool allPhotos = false) {
#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);
#endif
    std::string err, key = trimStr(target);
    std::unique_ptr<Stream> s(openTarget(target, err));
    if (!s) return {false, "SYNC: " + err};
    auto peers = loadPeers();
    long long started = nowMs(), since = allPhotos ? 0 : (peers.count(key) ? peers[key] : 0);
    SyncResult r = exchange(ag, mx, *s, since);
    if (r.ok) { peers[key] = started; savePeers(peers); }
    return r;
}
