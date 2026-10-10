// vp4limits.asi — Virtual Pool 4 Multiplayer (x64)
// Remove o limite de 65.535 indices por submesh no carregamento de modelos.
//
// O pipeline de meshes (ssMeshDX::WriteMeshes) cria o index buffer de cada
// submesh como D3DFMT_INDEX16 (0x65) e, se a contagem de indices >= 0xFFFF,
// loga "Whut Whoa, mesh has too many vertices for this device = %d" e CLAMPA
// a contagem para 0xFFFE — truncando o modelo (lixo visual / crash).
//
// O mesmo codigo ja possui o caminho completo D3DFMT_INDEX32 (0x66), porem
// inalcancavel: depende de `cmpl $0xffff, 0x57b0(%rax)`, um campo que nunca
// e escrito em lugar nenhum do exe (codigo morto).
//
// Patch (10 bytes em RVA 0x68BA12):
//   ANTES:  cmpl $0xffff, 0x57b0(%rax)    ; 81 B8 B0 57 00 00 FF FF 00 00
//   DEPOIS: cmpl $0xfffe, 0x48(%rbx)      ; 81 7B 48 FE FF 00 00
//           nop; nop; nop                 ; 90 90 90
// rbx = submesh atual; +0x48 = contagem de indices (o mesmo campo que o jogo
// clampa). O `ja` subsequente (0F 87, RVA 0x68BA1F) passa a levar ao caminho
// 32-bit para contagens > 65534 — exatamente os casos que o jogo truncava.
// Todo conteudo original (<= 65534 indices) continua em 16 bits: zero mudanca.
//
// Build:
//   x86_64-w64-mingw32-g++ -shared -O2 -o vp4limits.asi vp4limits.cpp \
//       -lkernel32 -static-libgcc -static-libstdc++

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

// Aplica patch se os bytes originais conferirem; sempre restaura protecao.
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

    // --- Patch principal: gate do formato do index buffer -------------------
    // cmpl $0xffff,0x57b0(%rax)  ->  cmpl $0xfffe,0x48(%rbx); nop;nop;nop
    static const BYTE kExpect[] = { 0x81, 0xB8, 0xB0, 0x57, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00 };
    static const BYTE kRepl[]   = { 0x81, 0x7B, 0x48, 0xFE, 0xFF, 0x00, 0x00, 0x90, 0x90, 0x90 };
    BYTE* site = base + (0x14068BA12ull - 0x140000000ull);
    bool ok = patch_bytes(site, kExpect, kRepl, sizeof kExpect,
                          "gate idx32 (>65534 indices => D3DFMT_INDEX32)");

    // Sanidade: o `ja` (0F 87) logo apos deve apontar para o caminho 32-bit,
    // que comeca com test %edx,%edx; mov $0x66,%r9d  (D3DFMT_INDEX32).
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
        // Fora do DllMain (padrao do loader): init em thread propria.
        HANDLE h = CreateThread(nullptr, 0, init_thread, nullptr, 0, nullptr);
        if (h) CloseHandle(h);
    }
    return TRUE;
}
