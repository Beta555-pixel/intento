// agenda_games.hpp: lógica de los minijuegos (sin SDL, para poder probarla sin ventana).
//   HACKEO   adivinar la contraseña de un terminal a partir de cuántas letras coinciden.
//   GANZUA   encontrar el punto exacto de una cerradura sin forzarla. En coop: J1 mueve la ganzúa, J2 gira.
//   DEFENSA  interceptar misiles antes de que destruyan las ciudades. En coop: dos bases, dos miras.
// Juegos originales basados en mecánicas clásicas; no usan texto ni arte de ninguna franquicia.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <random>
#include <string>
#include <vector>

namespace games {

inline std::mt19937 &rng() { static std::mt19937 g{std::random_device{}()}; return g; }
inline int rnd(int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng()); }
inline float rndf(float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng()); }
inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

// ================================================================== HACKEO
inline const std::vector<std::string> &wordList() {  // todas de 7 letras
    static const std::vector<std::string> w = {
        "ARCHIVO", "BALANZA", "CALLEJA", "CARTERA", "CERRADO", "COMANDO", "DESTINO", "ESCUDOS", "FUSIBLE",
        "GENERAL", "HABITAT", "IGLESIA", "LLAVERO", "MONEDAS", "NUCLEAR", "OBJETOS", "PASILLO", "PROTEGE",
        "REACTOR", "REFUGIO", "SOLDADO", "VIGILAR", "BATERIA", "CARBONO", "CENTRAL", "SISTEMA"};
    return w;
}

struct Hack {
    static const int ROWS = 10, PER_COL = 6, N = 12, WLEN = 7, LINEW = 14, MAXTRIES = 4;
    std::vector<std::string> words;  // N palabras: columna izquierda (de arriba abajo) y luego la derecha
    std::vector<int> wline, woff;    // línea (0..19) y desplazamiento de cada palabra dentro de su línea
    std::vector<std::string> lines;  // 2*ROWS líneas de LINEW caracteres
    std::vector<char> used;
    std::vector<std::string> log;
    int answer = 0, tries = MAXTRIES, turn = 0, cur[2] = {0, 0};
    bool two = false, won = false, lost = false;

    static int likeness(const std::string &a, const std::string &b) {
        int n = 0;
        for (size_t i = 0; i < a.size() && i < b.size(); i++) n += a[i] == b[i];
        return n;
    }
    void init(bool twoPlayers) {
        two = twoPlayers; won = lost = false; tries = MAXTRIES; turn = 0;
        cur[0] = 0; cur[1] = twoPlayers ? PER_COL : 0;
        log.clear();
        auto all = wordList();
        std::shuffle(all.begin(), all.end(), rng());
        words.assign(all.begin(), all.begin() + N);
        used.assign(N, 0);
        answer = rnd(0, N - 1);
        wline.assign(N, 0); woff.assign(N, 0);
        for (int c = 0; c < 2; c++) {
            std::vector<int> rows(ROWS);
            std::iota(rows.begin(), rows.end(), 0);
            std::shuffle(rows.begin(), rows.end(), rng());
            rows.resize(PER_COL);
            std::sort(rows.begin(), rows.end());
            for (int k = 0; k < PER_COL; k++) wline[c * PER_COL + k] = c * ROWS + rows[k];
        }
        static const char *F = "!@#$%^&*()-=+<>?/;:.,[]_";
        lines.assign(2 * ROWS, std::string(LINEW, ' '));
        for (auto &l : lines) for (auto &ch : l) ch = F[rnd(0, (int)std::strlen(F) - 1)];
        for (int i = 0; i < N; i++) {
            woff[i] = rnd(0, LINEW - WLEN);
            lines[wline[i]].replace(woff[i], WLEN, words[i]);
        }
    }
    void move(int p, int dx, int dy) {
        int col = cur[p] / PER_COL, row = cur[p] % PER_COL;
        row = (row + dy + PER_COL) % PER_COL;
        col = (col + dx + 2) % 2;
        cur[p] = col * PER_COL + row;
    }
    void guess(int idx) {  // el jugador de turno prueba la palabra idx
        if (won || lost || idx < 0 || idx >= N || used[idx]) return;
        used[idx] = 1;
        if (idx == answer) { won = true; log.push_back(words[idx] + " CORRECTO"); return; }
        tries--;
        log.push_back(words[idx] + "  " + std::to_string(likeness(words[idx], words[answer])) + "/" + std::to_string(WLEN));
        if (tries <= 0) { lost = true; log.push_back("TERMINAL BLOQUEADO"); }
        else if (two) turn ^= 1;
    }
    int score() const { return won ? (tries + 1) * 100 : 0; }
};

// ================================================================== GANZUA
struct Lock {
    float sweet = 90, tol = 40, pick = 90, turn = 0, stress = 0, shake = 0;  // ángulos en grados (0..180)
    int picks = 3, level = 1, score = 0;
    bool over = false, opened = false, broke = false;  // opened / broke = ocurrió en el último update

    void init() { picks = 3; level = 1; score = 0; over = false; newLock(); }
    void newLock() { sweet = rndf(15, 165); tol = std::max(8.f, 42.f - 3.5f * (level - 1)); pick = 90; turn = 0; stress = 0; shake = 0; }
    float limit() const {  // cuánto deja girar la cerradura según lo cerca que está la ganzúa del punto exacto
        return std::max(0.08f, clampf(1.3f * (1.f - std::fabs(pick - sweet) / tol), 0.f, 1.f));
    }
    void update(float dt, float pickDir, bool turning) {
        opened = broke = false;
        if (over) return;
        if (turn < 0.02f) pick = clampf(pick + pickDir * dt * 80.f, 0.f, 180.f);  // girada la cerradura, la ganzúa no se mueve
        float L = limit();
        turn += turning ? dt * 0.8f : -dt * 1.6f;
        if (turn < 0) turn = 0;
        if (turn > L) turn = L;
        bool blocked = turning && L < 0.999f && turn >= L - 1e-4f;
        stress = blocked ? stress + dt : std::max(0.f, stress - dt * 0.8f);
        shake = blocked ? 1.f : 0.f;
        if (turn >= 0.999f) {
            opened = true;
            score += 100 + 25 * level;
            level++;
            if (level % 3 == 0) picks = std::min(picks + 1, 5);
            newLock();
        } else if (stress >= 0.9f) {
            broke = true;
            picks--;
            turn = 0; stress = 0; shake = 0; pick = 90;
            if (picks <= 0) over = true;
        }
    }
};

// ================================================================== DEFENSA
struct Def {
    static constexpr float W = 497.f, H = 470.f, GROUND = 440.f;
    struct Enemy { float x0, y0, tx, ty, t, rate; bool alive; float x() const { return x0 + (tx - x0) * t; } float y() const { return y0 + (ty - y0) * t; } };
    struct Shot { float x0, y0, tx, ty, t, rate; float x() const { return x0 + (tx - x0) * t; } float y() const { return y0 + (ty - y0) * t; } };
    struct Boom { float x, y, r, maxR, age; bool friendly; };
    struct City { float x; bool alive; };

    std::vector<City> cities;
    std::vector<Enemy> en;
    std::vector<Shot> shots;
    std::vector<Boom> booms;
    int nb = 1, ammo[2] = {0, 0}, wave = 1, score = 0, toSpawn = 0, kills = 0;
    float bx[2] = {248.5f, 472.f}, cx[2] = {248.f, 300.f}, cy[2] = {200.f, 200.f}, spawnT = 0;
    bool over = false;

    int aliveCities() const { int n = 0; for (auto &c : cities) n += c.alive; return n; }
    void init(bool two) {
        nb = two ? 2 : 1;
        bx[0] = two ? 25.f : 248.5f; bx[1] = 472.f;
        cities.clear();
        for (int i = 0; i < 6; i++) cities.push_back({90.f + 63.f * i, true});
        en.clear(); shots.clear(); booms.clear();
        wave = 1; score = 0; kills = 0; over = false;
        cx[0] = two ? 150.f : 248.f; cx[1] = 350.f; cy[0] = cy[1] = 200.f;
        startWave();
    }
    void startWave() {
        toSpawn = 6 + 2 * wave;
        spawnT = 1.0f;
        for (int b = 0; b < nb; b++) ammo[b] = std::min(20, 10 + wave);
    }
    bool fire(int p) {
        if (over || p < 0 || p >= nb || ammo[p] <= 0) return false;
        float tx = clampf(cx[p], 0, W), ty = clampf(cy[p], 0, GROUND - 12);
        float len = std::max(1.f, std::hypot(tx - bx[p], ty - GROUND));
        shots.push_back({bx[p], GROUND, tx, ty, 0, 300.f / len});
        ammo[p]--;
        return true;
    }
    void update(float dt) {
        if (over) return;
        if (toSpawn > 0) {
            spawnT -= dt;
            if (spawnT <= 0) {
                spawnT = std::max(0.35f, 1.4f - 0.08f * wave);
                toSpawn--;
                std::vector<float> targets;
                for (auto &c : cities) if (c.alive) targets.push_back(c.x);
                if (rnd(0, 9) < 2) for (int b = 0; b < nb; b++) targets.push_back(bx[b]);
                if (targets.empty()) targets.push_back(W / 2);
                Enemy e{rndf(10, W - 10), 0, targets[rnd(0, (int)targets.size() - 1)], GROUND, 0, 0, true};
                e.rate = (30.f + 7.f * wave) / std::max(1.f, std::hypot(e.tx - e.x0, e.ty - e.y0));
                en.push_back(e);
            }
        }
        for (auto &e : en) {
            e.t += e.rate * dt;
            if (e.t >= 1.f) {
                e.alive = false;
                booms.push_back({e.tx, e.ty, 0, 24, 0, false});
                for (auto &c : cities) if (std::fabs(c.x - e.tx) < 18.f) c.alive = false;
            }
        }
        for (auto &s : shots) { s.t += s.rate * dt; if (s.t >= 1.f) booms.push_back({s.tx, s.ty, 0, 36, 0, true}); }
        shots.erase(std::remove_if(shots.begin(), shots.end(), [](const Shot &s) { return s.t >= 1.f; }), shots.end());
        for (auto &b : booms) {
            b.age += dt;
            b.r = b.maxR * (b.age < 0.6f ? b.age / 0.6f : std::max(0.f, (1.2f - b.age) / 0.6f));
        }
        std::vector<Boom> chain;
        for (auto &e : en) {
            if (!e.alive) continue;
            for (auto &b : booms)
                if (b.friendly && std::hypot(e.x() - b.x, e.y() - b.y) < b.r) {
                    e.alive = false; score += 25; kills++;
                    chain.push_back({e.x(), e.y(), 0, 22, 0, true});
                    break;
                }
        }
        booms.insert(booms.end(), chain.begin(), chain.end());
        booms.erase(std::remove_if(booms.begin(), booms.end(), [](const Boom &b) { return b.age >= 1.2f; }), booms.end());
        en.erase(std::remove_if(en.begin(), en.end(), [](const Enemy &e) { return !e.alive; }), en.end());
        if (aliveCities() == 0) { over = true; return; }
        if (toSpawn == 0 && en.empty()) {  // oleada superada: bonus y siguiente
            int left = 0;
            for (int b = 0; b < nb; b++) left += ammo[b];
            score += left * 5 + aliveCities() * 50;
            wave++;
            startWave();
        }
    }
};

}  // namespace games
