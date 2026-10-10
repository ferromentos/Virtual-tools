// version.dll proxy = ASI loader for Virtual Pool 4 (x64).
// The game imports 3 funcs from VERSION.dll; we forward them to the real
// system DLL and use DllMain to load every *.asi in the game folder.
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>

static HMODULE g_real = NULL;
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
static FARPROC p_GetFileVersionInfoA = NULL;
static FARPROC p_GetFileVersionInfoSizeA = NULL;
static FARPROC p_VerQueryValueA = NULL;

static void load_real(void) {
    if (g_real) return;
    wchar_t sys[MAX_PATH];
    UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    wcscat(sys, L"\\version.dll");
    (void)n;
    g_real = LoadLibraryW(sys);
    if (g_real) {
        p_GetFileVersionInfoA = GetProcAddress(g_real, "GetFileVersionInfoA");
        p_GetFileVersionInfoSizeA = GetProcAddress(g_real, "GetFileVersionInfoSizeA");
        p_VerQueryValueA = GetProcAddress(g_real, "VerQueryValueA");
    }
}

__declspec(dllexport) BOOL WINAPI GetFileVersionInfoA(LPCSTR a, DWORD b, DWORD c, LPVOID d) {
    load_real();
    if (!p_GetFileVersionInfoA) return FALSE;
    return ((BOOL(WINAPI*)(LPCSTR,DWORD,DWORD,LPVOID))p_GetFileVersionInfoA)(a,b,c,d);
}
__declspec(dllexport) DWORD WINAPI GetFileVersionInfoSizeA(LPCSTR a, LPDWORD b) {
    load_real();
    if (!p_GetFileVersionInfoSizeA) return 0;
    return ((DWORD(WINAPI*)(LPCSTR,LPDWORD))p_GetFileVersionInfoSizeA)(a,b);
}
__declspec(dllexport) BOOL WINAPI VerQueryValueA(LPCVOID a, LPCSTR b, LPVOID *c, PUINT d) {
    load_real();
    if (!p_VerQueryValueA) return FALSE;
    return ((BOOL(WINAPI*)(LPCVOID,LPCSTR,LPVOID*,PUINT))p_VerQueryValueA)(a,b,c,d);
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
        {
            char aname[MAX_PATH];
            WideCharToMultiByte(CP_ACP, 0, fd.cFileName, -1, aname, MAX_PATH, NULL, NULL);
            char msg[160];
            snprintf(msg, sizeof(msg), "asi: %s -> %p", aname, (void*)m);
            wlog(msg);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static DWORD WINAPI loader_thread(LPVOID unused) {
    (void)unused;
    wlog("thread: inicio");
    load_real();
    wlog("thread: real ok");
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t *s = wcsrchr(exe, L'\\');
    wchar_t dir[MAX_PATH];
    if (s) { size_t n = (size_t)(s - exe); wcsncpy(dir, exe, n); dir[n]=0; }
    else wcscpy(dir, L".");
    {
        char adir[MAX_PATH];
        WideCharToMultiByte(CP_ACP, 0, dir, -1, adir, MAX_PATH, NULL, NULL);
        size_t L = strlen(adir);
        snprintf(g_log, sizeof(g_log), "%s%sversion_loader.log", adir,
                 (L && adir[L-1]=='\\') ? "" : "\\");
        wlog("thread: log ok");
    }
    load_asi_from(dir);
    wchar_t scripts[MAX_PATH];
    swprintf(scripts, MAX_PATH, L"%s\\scripts", dir);
    load_asi_from(scripts);
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
