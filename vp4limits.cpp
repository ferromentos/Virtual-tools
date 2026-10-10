// vp4limits.asi — Virtual Pool 4 Multiplayer (x64)

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static FILE* g_log = nullptr;

static void LOG(const char* fmt, ...)
{
    if (!g_log) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fflush(g_log);
}

static void hexline(const char* tag, const BYTE* p, size_t n)
{
    char buf[3 * 32];
    size_t o = 0;
    for (size_t i = 0; i < n && o + 4 < sizeof(buf); i++)
        o += sprintf(buf + o, "%02X ", p[i]);
    LOG("%s%s\n", tag, buf);
}

// Apply patch if the original bytes check; always restore protection.
static bool patch_bytes(BYTE* addr, const BYTE* expect, const BYTE* repl,
                        size_t n, const char* name)
{
    if (memcmp(addr, expect, n) != 0) {
        LOG("[ERRO] %s: bytes nao conferem em %p — patch NAO aplicado\n", name, addr);
        hexline("  esperado: ", expect, n);
        hexline("  encontrado: ", addr, n);
        return false;
    }
    DWORD oldProt = 0;
    if (!VirtualProtect(addr, n, PAGE_EXECUTE_READWRITE, &oldProt)) {
        LOG("[ERRO] %s: VirtualProtect falhou em %p (err=%lu)\n", name, addr, GetLastError());
        return false;
    }
    memcpy(addr, repl, n);
    DWORD tmp;
    VirtualProtect(addr, n, oldProt, &tmp);
    FlushInstructionCache(GetCurrentProcess(), addr, n);
    LOG("[OK] %s aplicado em %p\n", name, addr);
    return true;
}

static DWORD WINAPI init_thread(LPVOID)
{
    g_log = fopen("vp4limits.log", "w");
    LOG("vp4limits.asi — remove o limite de 65535 indices/submesh (idx16 -> idx32 automatico)\n");

    HMODULE exe = GetModuleHandleW(NULL);
    if (!exe) {
        LOG("[ERRO] GetModuleHandle(NULL) falhou\n");
        return 0;
    }
    BYTE* base = (BYTE*)exe;
    LOG("exe base = %p\n", base);

    // --- Main patch: gate of the index buffer format -------------------
    // cmpl $0xffff,0x57b0(%rax)  ->  cmpl $0xfffe,0x48(%rbx); nop;nop;nop
    static const BYTE kExpect[] = { 0x81, 0xB8, 0xB0, 0x57, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00 };
    static const BYTE kRepl[]   = { 0x81, 0x7B, 0x48, 0xFE, 0xFF, 0x00, 0x00, 0x90, 0x90, 0x90 };
    BYTE* site = base + (0x14068BA12ull - 0x140000000ull);
    bool ok = patch_bytes(site, kExpect, kRepl, sizeof kExpect,
                          "gate idx32 (>65534 indices => D3DFMT_INDEX32)");

    // Sanity: the `ja` (0F 87) right after should point to the 32-bit path,
    // starting with test %edx,%edx; mov $0x66.%r9d (D3DFMT_INDEX32)
    LOG("pos-patch em %p: ", site);
    hexline("", site, 16);
    if (site[10] == 0x0F && site[11] == 0x87) {
        LONG rel = *(LONG*)(site + 12);
        BYTE* alvo = site + 16 + rel;
        LOG("ja -> %p (primeiros bytes: ", alvo);
        hexline("", alvo, 7);
        LOG(")  esperado 85 D2 41 B9 66 .. (test edx; mov r9d,0x66)\n");
    } else {
        LOG("[AVISO] posicoes 10..11 nao sao 0F 87 — layout inesperado do ja\n");
    }

    LOG(ok ? "PRONTO: modelos com > 65534 indices por submesh usam indices de 32 bits.\n"
           : "FALHA: veja mensagens acima.\n");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        // Outside of DllMain (loader pattern): init in thread own.
        HANDLE h = CreateThread(nullptr, 0, init_thread, nullptr, 0, nullptr);
        if (h) CloseHandle(h);
    }
    return TRUE;
}
