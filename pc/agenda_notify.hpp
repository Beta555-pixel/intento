// agenda_notify.hpp: notificaciones de escritorio sin librerías externas.
//   Windows: globo/toast del área de notificación (Shell_NotifyIcon). Enlazar con -lshell32.
//   Linux:   notify-send (paquete libnotify-bin).   macOS: osascript.
// Los textos nunca pasan por un shell: se entregan como argumentos, así un nombre malicioso
// recibido por sincronización no puede ejecutar comandos.
#pragma once
#include <string>
#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
  #include <shellapi.h>
#else
  #include <csignal>
  #include <spawn.h>
  extern char **environ;
#endif

inline std::string notifyClip(const std::string &s, size_t n) {
    if (s.size() <= n) return s;
    std::string r = s.substr(0, n);
    while (!r.empty() && ((unsigned char)r.back() & 0xC0) == 0x80) r.pop_back();  // no cortar UTF-8
    if (!r.empty() && ((unsigned char)r.back() & 0xC0) == 0xC0) r.pop_back();
    return r;
}

#ifdef _WIN32
inline NOTIFYICONDATAW g_nid{};
inline bool g_nidAdded = false;
inline bool desktopNotify(const std::string &title, const std::string &body, const char *windowTitle = "AGENDA 3000") {
    HWND hw = FindWindowA(nullptr, windowTitle);
    if (!hw) return false;
    if (!g_nidAdded) {
        g_nid.cbSize = sizeof g_nid; g_nid.hWnd = hw; g_nid.uID = 1;
        g_nid.uFlags = NIF_ICON | NIF_TIP;
        g_nid.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32516));  // IDI_INFORMATION
        lstrcpyW(g_nid.szTip, L"AGENDA 3000");
        if (!Shell_NotifyIconW(NIM_ADD, &g_nid)) return false;
        g_nidAdded = true;
    }
    auto conv = [](const std::string &s, wchar_t *dst, int cap) {
        std::string c = notifyClip(s, cap - 1);
        int n = MultiByteToWideChar(CP_UTF8, 0, c.c_str(), -1, dst, cap);
        if (n == 0) dst[0] = 0;
        dst[cap - 1] = 0;
    };
    g_nid.uFlags = NIF_INFO;
    g_nid.dwInfoFlags = NIIF_INFO;
    conv(title, g_nid.szInfoTitle, 64);
    conv(body, g_nid.szInfo, 256);
    return Shell_NotifyIconW(NIM_MODIFY, &g_nid) != 0;
}
inline void notifyCleanup() { if (g_nidAdded) { Shell_NotifyIconW(NIM_DELETE, &g_nid); g_nidAdded = false; } }
#else
inline bool desktopNotify(const std::string &title, const std::string &body, const char * = nullptr) {
    static bool init = false;
    if (!init) { std::signal(SIGCHLD, SIG_IGN); init = true; }  // no dejar procesos zombi
    std::string t = notifyClip(title, 80), b = notifyClip(body, 240);
#ifdef __APPLE__
    for (auto *s : {&t, &b}) for (auto &c : *s) if (c == '"' || c == '\\') c = ' ';
    std::string script = "display notification \"" + b + "\" with title \"" + t + "\"";
    const char *argv[] = {"osascript", "-e", script.c_str(), nullptr};
#else
    const char *argv[] = {"notify-send", "-a", "AGENDA 3000", "--", t.c_str(), b.c_str(), nullptr};
#endif
    pid_t pid;
    return posix_spawnp(&pid, argv[0], nullptr, nullptr, const_cast<char *const *>(argv), environ) == 0;
}
inline void notifyCleanup() {}
#endif
