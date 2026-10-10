// dsound.dll proxy = ASI loader p/ Virtual Pool 4 (x64).
// O jogo importa DSOUND.dll (ordinal 11 = DirectSoundCreate8).
// Repassamos as 14 exportações p/ a dsound real e carregamos *.asi.
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern void *ds_real[16];

static const char *fnames[14] = {
    "DirectSoundCreate", "DirectSoundEnumerateA", "DirectSoundEnumerateW",
    "DllCanUnloadNow", "DllGetClassObject", "DirectSoundCaptureCreate",
    "DirectSoundCaptureEnumerateA", "DirectSoundCaptureEnumerateW",
    "GetDeviceID", "DirectSoundFullDuplexCreate", "DirectSoundCreate8",
    "DirectSoundCaptureCreate8", "DllRegisterServer", "DllUnregisterServer"
};

static char g_log[MAX_PATH] = "";
static void wlog(const char *m) {
    if (!g_log[0]) return;
    HANDLE h = CreateFileA(g_log, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w = 0;
    WriteFile(h, m, (DWORD)strlen(m), &w, NULL);
    WriteFile(h, "\r\n", 2, &w, NULL);
    CloseHandle(h);
}

static void load_asi_from(const wchar_t *dir) {
    wchar_t pat[MAX_PATH];
    swprintf(pat, MAX_PATH, L"%s\\*.asi", dir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        wchar_t full[MAX_PATH];
        swprintf(full, MAX_PATH, L"%s\\%s", dir, fd.cFileName);
        HMODULE m = LoadLibraryW(full);
        char aname[MAX_PATH];
        WideCharToMultiByte(CP_ACP, 0, fd.cFileName, -1, aname, MAX_PATH, NULL, NULL);
        char msg[160];
        snprintf(msg, sizeof(msg), "asi: %s -> %p", aname, (void*)m);
        wlog(msg);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static DWORD WINAPI loader_thread(LPVOID unused) {
    (void)unused;
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t *s = wcsrchr(exe, L'\\');
    wchar_t dir[MAX_PATH];
    if (s) { size_t n = (size_t)(s - exe); wcsncpy(dir, exe, n); dir[n] = 0; }
    else wcscpy(dir, L".");
    {
        char adir[MAX_PATH];
        WideCharToMultiByte(CP_ACP, 0, dir, -1, adir, MAX_PATH, NULL, NULL);
        size_t L = strlen(adir);
        snprintf(g_log, sizeof(g_log), "%s%sdsound_loader.log", adir,
                 (L && adir[L-1] == '\\') ? "" : "\\");
    }
    wlog("thread: inicio");
    // resolve a dsound real (system32 = builtin do Wine/Windows)
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    wcscat(sys, L"\\dsound.dll");
    HMODULE real = LoadLibraryW(sys);
    char msg[128];
    snprintf(msg, sizeof(msg), "real dsound: %p", (void*)real);
    wlog(msg);
    if (real) {
        for (int i = 0; i < 14; i++) {
            ds_real[i] = (void*)GetProcAddress(real, fnames[i]);
            if (!ds_real[i]) {
                snprintf(msg, sizeof(msg), "AVISO: sem %s", fnames[i]);
                wlog(msg);
            }
        }
    }
    wlog("thread: real ok");
    load_asi_from(dir);
    wchar_t scripts[MAX_PATH];
    swprintf(scripts, MAX_PATH, L"%s\\scripts", dir);
    load_asi_from(scripts);
    wlog("thread: fim");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID res) {
    (void)h; (void)res;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        HANDLE t = CreateThread(NULL, 0, loader_thread, NULL, 0, NULL);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
