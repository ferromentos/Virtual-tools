// Freecam.ini (optional, next to the .asi): if works, it works
// thanks to AI 
//   [Freecam] ViewParam=matView   MoveSpeed=80
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cmath>

// ---------------------------------------------------------------- logging
static char g_modDir[MAX_PATH] = "";
static char g_logPath[MAX_PATH] = "";
static char g_iniPath[MAX_PATH] = "";
static void tlog(const char *fmt, ...) {
    if (!g_logPath[0]) return;
    FILE *f = fopen(g_logPath, "a");
    if (!f) return;
    char b[1024]; va_list ap; va_start(ap, fmt);
    vsnprintf(b, sizeof(b), fmt, ap); va_end(ap);
    fprintf(f, "%s\n", b); fclose(f);
}

// ---------------------------------------------------------------- config
static char  g_cfgViewParam[64] = ""; 
static float g_cfgSpeed = 80.0f;
static void load_cfg(void) {
    GetPrivateProfileStringA("Freecam", "ViewParam", "", g_cfgViewParam, sizeof(g_cfgViewParam), g_iniPath);
    g_cfgSpeed = (float)GetPrivateProfileIntA("Freecam", "MoveSpeed", 80, g_iniPath);
    tlog("cfg: ViewParam='%s' speed=%.0f", g_cfgViewParam, g_cfgSpeed);
}

// v0.15
static int g_hookIn[5] = {0,0,0,0,0};   /
static LONG WINAPI exc_filter(EXCEPTION_POINTERS *ep) {
    static volatile LONG inF = 0;
    if (InterlockedExchange(&inF, 1)) return EXCEPTION_CONTINUE_SEARCH;
    if (!g_logPath[0] || !ep || !ep->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
    FILE *f = fopen(g_logPath, "a");
    if (!f) return EXCEPTION_CONTINUE_SEARCH;
    void *addr = ep->ExceptionRecord->ExceptionAddress;
    char mod[MAX_PATH] = "?"; unsigned long long off = 0;
    HMODULE hm = NULL;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)addr, &hm)) {
        GetModuleFileNameA(hm, mod, MAX_PATH);
        char *bs = strrchr(mod, '\\'); if (bs) memmove(mod, bs + 1, strlen(bs + 1) + 1);
        off = (unsigned long long)((char*)addr - (char*)hm);
    }
    fprintf(f, "\n!!! CRASH code=%08X addr=%p em %s+0x%llX\n",
            ep->ExceptionRecord->ExceptionCode, addr, mod, off);
#if defined(_M_X64) || defined(__x86_64__)
    const CONTEXT *c = ep->ContextRecord;
    if (c) fprintf(f, "  RIP=%p RAX=%p RDX=%p RCX=%p RBX=%p RSP=%p RBP=%p RSI=%p RDI=%p\n",
                   (void*)c->Rip, (void*)c->Rax, (void*)c->Rdx, (void*)c->Rcx,
                   (void*)c->Rbx, (void*)c->Rsp, (void*)c->Rbp, (void*)c->Rsi, (void*)c->Rdi);
#endif
    fprintf(f, "  ultimo hook ST=%d DP=%d DUP=%d DIPUP=%d VSF=%d\n",
            g_hookIn[0], g_hookIn[1], g_hookIn[2], g_hookIn[3], g_hookIn[4]);
    fclose(f);
    return EXCEPTION_CONTINUE_SEARCH;
}

// ---------------------------------------------------------------- freecam state
static bool  g_free = false;
static int   g_ovr = 0;   // How often do exchange the matri
static bool  g_userMoved = false;   // user has already flown (freeze g_detV) — v0.13
static float g_eye[3] = {46.0f, 120.0f, -80.0f};
static float g_at[3]  = {46.0f, 60.0f, 20.0f};
static float g_upv[3] = {0.0f, 1.0f, 0.0f};   // real game camera up (v0.12)
static float g_yaw = 0.0f, g_pitch = -0.35f;
static DWORD g_lastMove = 0;

// v0.13:
static float g_detV = 1.0f;
static float det3(const D3DMATRIX *m) {
    return m->_11*(m->_22*m->_33 - m->_23*m->_32)
         - m->_12*(m->_21*m->_33 - m->_23*m->_31)
         + m->_13*(m->_21*m->_32 - m->_22*m->_31);
}

static void dir_from_angles(float *d) {
    float cp = cosf(g_pitch);
    d[0] = sinf(g_yaw) * cp;
    d[1] = sinf(g_pitch);
    d[2] = cosf(g_yaw) * cp;
}

// view in D3D format (line = vector, translation in 4th line) = D3DXMatrixLookAtLH
static void build_view(D3DMATRIX *m, const float *eye, const float *at) {
    float zx = at[0] - eye[0], zy = at[1] - eye[1], zz = at[2] - eye[2];
    float l = sqrtf(zx * zx + zy * zy + zz * zz);
    if (l < 1e-6f) { zx = 0; zy = 0; zz = 1; } else { zx /= l; zy /= l; zz /= l; }
    float ux = g_upv[0], uy = g_upv[1], uz = g_upv[2];   // up real capturado da camera do jogo
    float xx = uy * zz - uz * zy;      // up x z
    float xy = uz * zx - ux * zz;
    float xz = ux * zy - uy * zx;
    float xl = sqrtf(xx * xx + xy * xy + xz * xz);
    if (xl < 1e-6f) { xx = 1; xy = 0; xz = 0; } else { xx /= xl; xy /= xl; xz /= xl; }
    // v0.13:
    if (g_detV < 0) { xx = -xx; xy = -xy; xz = -xz; }
    float yx = zy * xz - zz * xy;          // z x x
    float yy = zz * xx - zx * xz;
    float yz = zx * xy - zy * xx;
    if (g_detV < 0) { yx = -yx; yy = -yy; yz = -yz; }   // y = x x z
    memset(m, 0, sizeof(*m));
    m->_11 = xx; m->_12 = yx; m->_13 = zx;
    m->_21 = xy; m->_22 = yy; m->_23 = zy;
    m->_31 = xz; m->_32 = yz; m->_33 = zz;
    m->_41 = -(eye[0] * xx + eye[1] * xy + eye[2] * xz);
    m->_42 = -(eye[0] * yx + eye[1] * yy + eye[2] * yz);
    m->_43 = -(eye[0] * zx + eye[1] * zy + eye[2] * zz);
    m->_44 = 1.0f;
}
static void transpose(D3DMATRIX *m) {
    D3DMATRIX t = *m;
    m->_11 = t._11; m->_12 = t._21; m->_13 = t._31; m->_14 = t._41;
    m->_21 = t._12; m->_22 = t._22; m->_23 = t._32; m->_24 = t._42;
    m->_31 = t._13; m->_32 = t._23; m->_33 = t._33; m->_34 = t._43;
    m->_41 = t._14; m->_42 = t._24; m->_43 = t._34; m->_44 = t._44;
}
static bool is_view_name(const char *n) {
    if (!n || !n[0]) return false;
    char l[64]; int i = 0;
    for (; n[i] && i < 63; i++) l[i] = (char)tolower((unsigned char)n[i]);
    l[i] = 0;
    if (strstr(l, "proj")) return false;
    if (g_cfgViewParam[0]) return strcmp(l, g_cfgViewParam) == 0;
    return strstr(l, "view") != NULL;
}

// ================================================================
// v0.4 — the game transformation chain
#define ME(m,r,c) (((const float*)(m))[(r)*4+(c)])
#define MW(m,r,c) (((float*)(m))[(r)*4+(c)])

static void mmul(D3DMATRIX *o, const D3DMATRIX *A, const D3DMATRIX *B) {
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) {
        double sum = 0;
        for (int k = 0; k < 4; k++) sum += (double)ME(A,r,k) * (double)ME(B,k,c);
        MW(o,r,c) = (float)sum;
    }
}
static bool minv(const D3DMATRIX *in, D3DMATRIX *out) {
    double a[4][8];
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) {
        a[r][c] = ME(in,r,c);
        a[r][4+c] = (r == c) ? 1.0 : 0.0;
    }
    for (int i = 0; i < 4; i++) {
        int p = i;
        for (int r = i+1; r < 4; r++) if (fabs(a[r][i]) > fabs(a[p][i])) p = r;
        if (fabs(a[p][i]) < 1e-12) return false;
        if (p != i) for (int c = 0; c < 8; c++) { double t=a[i][c]; a[i][c]=a[p][c]; a[p][c]=t; }
        double d = a[i][i];
        for (int c = 0; c < 8; c++) a[i][c] /= d;
        for (int r = 0; r < 4; r++) if (r != i) {
            double f = a[r][i];
            if (f == 0) continue;
            for (int c = 0; c < 8; c++) a[r][c] -= f * a[i][c];
        }
    }
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) MW(out,r,c) = (float)a[r][4+c];
    return true;
}

static bool g_haveProj = false;
static D3DMATRIX g_proj;
static bool g_haveViewM = false;      // V adopted (game view)
static D3DMATRIX g_viewM;
static bool g_viewT = false;          // true = game uses vector-column convention (transposed)
static float trans_len(const D3DMATRIX *m) {   
    float r = m->_41*m->_41 + m->_42*m->_42 + m->_43*m->_43;
    float c = m->_14*m->_14 + m->_24*m->_24 + m->_34*m->_34;
    return r > c ? r : c;
}
static bool is_col_conv(const D3DMATRIX *m) {  
    float r = m->_41*m->_41 + m->_42*m->_42 + m->_43*m->_43;
    float c = m->_14*m->_14 + m->_24*m->_24 + m->_34*m->_34;
    return c > r && c > 1.0f;
}
static D3DMATRIX g_K;                 // P^-1 V^-1 V' P  (cache)
static int  g_Kver = -1, g_ver = 0;   
static bool g_Kok = false;
static int  g_nMul = 0, g_nPersp = 0, g_npB = 0, g_npA = 0;
static D3DMATRIX g_projs[24]; static int g_nProjs = 0;   // projecoes distintas (persistente)
static bool g_wvpDone = false;                           
static bool ortho3(const D3DMATRIX *m);                  
//--- derivation: M = W*V*P => X = V*P = W^-1*M => V = X*P^-1 (valida if orto) ---
static D3DMATRIX g_Vgame, g_Puse;    // view real do jogo + projecao usada
static bool g_haveX = false, g_Kready = false;
static bool g_convCol = false;       // uploads em convencao transposta
static int  g_nWvp = 0, g_nDeriv = 0, g_Kapp = 0, g_nSeq = 0;
static char g_seq[700] = "";         // sequencia de SetMatrix do frame
// world by effect (pairing)
#define MAXE 24
static void *g_wEff[MAXE]; static D3DMATRIX g_wMat[MAXE]; static int g_nWE = 0;
// v0.13: pool de worlds distintos p/ pareamento forcado do Estagio A.
// O jogo chama WVP ANTES do matWorld (ordem invertida) — o pareamento por
// efeito pegava o world errado. Aqui guardamos TODOS os worlds dos ultimos
// 2 frames e testamos todos os pares (upload x world); o V verdadeiro forma
// um cluster grande, o lixo espalha e morre no cluster.
#define MAXW 64
static D3DMATRIX g_wPool[MAXW]; static int g_nWP = 0, g_wAge[MAXW];
static void wpool_add(const D3DMATRIX *m) {
    // guarda na forma linha E transposta (nao da p/ saber a conv. do world
    // quando a translacao e' zero); dedup remove duplicata se simetrico
    for (int t = 0; t < 2; t++) {
        D3DMATRIX w = *m;
        if (t) { transpose(&w); if (memcmp(&w, m, 64) == 0) break; }
        int dup = -1;
        for (int i = 0; i < g_nWP; i++)
            if (memcmp(&g_wPool[i], &w, 64) == 0) { dup = i; break; }
        if (dup >= 0) { g_wAge[dup] = 0; continue; }
        if (g_nWP < MAXW) { g_wPool[g_nWP] = w; g_wAge[g_nWP] = 0; g_nWP++; continue; }
        int oi = 0;
        for (int i = 1; i < g_nWP; i++) if (g_wAge[i] > g_wAge[oi]) oi = i;
        g_wPool[oi] = w; g_wAge[oi] = 0;
    }
}
static void wpool_age(void) {   // fim do frame: descarta worlds com mais de 2 frames
    for (int i = g_nWP - 1; i >= 0; i--)
        if (++g_wAge[i] > 2) { g_wPool[i] = g_wPool[--g_nWP]; g_wAge[i] = g_wAge[g_nWP]; }
}
static void we_set(void *e, const D3DMATRIX *m) {
    wpool_add(m);
    int i;
    for (i = 0; i < g_nWE; i++) if (g_wEff[i] == e) break;
    if (i == g_nWE) { if (g_nWE >= MAXE) return; g_nWE++; g_wEff[i] = e; }
    g_wMat[i] = *m;
}
static bool we_get(void *e, D3DMATRIX *o) {
    for (int i = 0; i < g_nWE; i++) if (g_wEff[i] == e) { *o = g_wMat[i]; return true; }
    return false;
}
static bool is_world_name(const char *n) {
    if (!n || !n[0]) return false;
    char l[64]; int i = 0;
    for (; n[i] && i < 63; i++) l[i] = (char)tolower((unsigned char)n[i]);
    l[i] = 0;
    if (strstr(l, "viewproj") || strstr(l, "view")) return false;
    return strstr(l, "world") != NULL;
}
static void seq_add(const char *n, const D3DMATRIX *m) {
    if ((int)strlen(g_seq) > 560 || !m) return;
    char t[96];
    snprintf(t, sizeof(t), " %s[r=(%.0f,%.0f,%.0f) c=(%.0f,%.0f,%.0f)]",
             n ? n : "?", m->_41, m->_42, m->_43, m->_14, m->_24, m->_34);
    strncat(g_seq, t, sizeof(g_seq) - strlen(g_seq) - 1);
    g_nSeq++;
}
// validador de view rigida (D3DX linha: translacao na linha4; ou coluna: na coluna4)
static bool is_view(const D3DMATRIX *m) {
    float a[3][3] = {{m->_11,m->_12,m->_13},{m->_21,m->_22,m->_23},{m->_31,m->_32,m->_33}};
    for (int i = 0; i < 3; i++) {
        float d = a[i][0]*a[i][0] + a[i][1]*a[i][1] + a[i][2]*a[i][2];
        if (fabsf(d - 1.0f) > 0.04f) return false;
    }
    for (int i = 0; i < 3; i++) for (int k = i+1; k < 3; k++) {
        float d = a[i][0]*a[k][0] + a[i][1]*a[k][1] + a[i][2]*a[k][2];
        if (fabsf(d) > 0.04f) return false;
    }
    if (fabsf(m->_44 - 1.0f) > 2e-3f) return false;
    float r = m->_41*m->_41 + m->_42*m->_42 + m->_43*m->_43;
    float c = m->_14*m->_14 + m->_24*m->_24 + m->_34*m->_34;
    if (r > 1.0f && c <= 1e-4f*r + 1e-4f) return true;   // transl. na linha4 (D3DX linha)
    if (c > 1.0f && r <= 1e-4f*c + 1e-4f) return true;   // transl. na coluna4
    return false;
}
// tenta derivar V de (M=WVP, W=world)
static bool derive_V(const D3DMATRIX *Min, const D3DMATRIX *Win) {
    for (int t = 0; t < 4; t++) {
        bool col = (t & 1), swap = (t & 2);
        D3DMATRIX M = *Min, W = *Win;
        if (col) { transpose(&M); transpose(&W); }
        D3DMATRIX Wi;
        if (!minv(&W, &Wi)) continue;
        D3DMATRIX X;
        if (swap) mmul(&X, &Wi, &M);      // X = W^-1 * M   (M = W*X)
        else      mmul(&X, &M, &Wi);      // X = M * W^-1   (M = X*W)
        if (col) transpose(&X);           // volta p/ forma linha
        for (int i = 0; i < g_nProjs; i++) {
            D3DMATRIX Pi;
            if (!minv(&g_projs[i], &Pi)) continue;
            D3DMATRIX V;
            mmul(&V, &X, &Pi);            // V = X * P^-1
            if (is_view(&V)) {
                g_Vgame = V; g_Puse = g_projs[i];
                g_convCol = col; g_viewT = col; g_haveX = true; g_Kready = false; g_Kver = -1;
                g_nDeriv++;
                tlog("V DERIVADA (conv=%s %s): row=(%.1f,%.1f,%.1f) col=(%.1f,%.1f,%.1f) P[%d]",
                     col ? "transposta" : "linha", swap ? "W^-1*M" : "M*W^-1",
                     V._41, V._42, V._43, V._14, V._24, V._34, i);
                return true;
            }
        }
    }
    return false;
}
// === v0.10: derivar V e P dos WVP coletados ===
// M = W*V*P com W rigida:
//   sum_i M_ij^2 (i=1..3) = f1^2 / f2^2 / A^2   (invariante a rotacao de W!)
//   M_i4/M_i3 = B/A (i<4),  M_44 = o3*B  (o3 = parte profunda em o)
//   linhas 1..3 de M = linhas 1..3 de V*P se R_w = I  -> V = M/f  (cluster p/ objetos)
//   o = (M_41/f1, M_42/f2, (M_43+1)/A) = row4(V) + d , d_j = dot(tw, eixo_j)
//     -> com tw do matWorld pareado: row4(V) = o - d (exato, qualquer R_w) -> cluster
#define MAXU 96
static D3DMATRIX g_upM[MAXU];
static D3DMATRIX g_upW[MAXU];        // world pareado completo (v0.12: Estagio A)
static float     g_upT[MAXU][3];
static bool      g_upHasT[MAXU];
static int  g_nUp = 0, g_nUpSkip = 0, g_nUpTr = 0, g_nAmb = 0;
static int  g_nWvpT = 0, g_nWvpV = 0;
static float g_lf1 = 0, g_lf2 = 0, g_lA = 0, g_lB = 0, g_lP43 = 0;
static int   g_nPc = 0, g_pcBest = 0;
static char  g_derHow = '-';          // A = world-pareado (v0.12), B = linhas+row4
static unsigned long g_lastDerivTick = 0;
static char  g_derMsg[96] = "sem dados";
static int   g_aPair = 0, g_aInv = 0, g_aView = 0;   // diag Estagio A v2 (v0.13)
static int  g_xrow4 = 0, g_rotC = 0, g_rotN = 0, g_r4C = 0, g_r4N = 0, g_r4P = 0, g_r4U = 0;
static void store_upload(void *eff, const D3DMATRIX *m) {
    if (!m || g_nUp >= MAXU) return;
    D3DMATRIX mm = *m;
    float r = mm._41*mm._41 + mm._42*mm._42 + mm._43*mm._43;
    float c = mm._14*mm._14 + mm._24*mm._24 + mm._34*mm._34;
    if (c > 4*r && c > 1e-6f) { transpose(&mm); g_nUpTr++; }
    else if (!(r > 4*c && r > 1e-6f)) { g_nUpSkip++; return; }   // ambiguo
    g_upM[g_nUp] = mm;
    g_upHasT[g_nUp] = false;
    D3DMATRIX w;
    bool hw = we_get(eff, &w);
    if (!hw && g_nWE > 0) { w = g_wMat[g_nWE-1]; hw = true; }    // fallback
    if (hw) {
        float wr = w._41*w._41 + w._42*w._42 + w._43*w._43;
        float wc = w._14*w._14 + w._24*w._24 + w._34*w._34;
        if (wc > wr) { g_upT[g_nUp][0]=w._14; g_upT[g_nUp][1]=w._24; g_upT[g_nUp][2]=w._34; }
        else         { g_upT[g_nUp][0]=w._41; g_upT[g_nUp][1]=w._42; g_upT[g_nUp][2]=w._43; }
        g_upW[g_nUp] = w;
        g_upHasT[g_nUp] = true;
    }
    g_nUp++;
}
static void derive_from_uploads(void) {
    g_rotC = g_rotN = g_r4C = g_r4N = g_r4P = g_r4U = g_xrow4 = 0;
    snprintf(g_derMsg, sizeof(g_derMsg), "nUp=%d tr=%d skip=%d", g_nUp, g_nUpTr, g_nUpSkip);
    if (g_nUp < 4) return;
    // ---- P: normas das colunas (linhas 1..3) + cluster entre uploads ----
    // M = W*V*P (linha). col j de M (j<=3) = col j de U * P_jj  =>  normas = (f1,f2,A).
    // M_j4 = U_j3 * P_34 => b_j = A*M_j4/M_j3 = P_34 (== +-1 na perspectiva).
    // row4(M) = (d1*f1, d2*f2, d3*A + P_43, d3*P_34) => P_43 = M_43 - (M_44/P_34)*A.
    struct Pc { float f1, f2, A, p34, p43; int cnt; };
    Pc pcl[24]; int npc = 0;
    for (int u = 0; u < g_nUp; u++) {
        const D3DMATRIX *M = &g_upM[u];
        float s1 = M->_11*M->_11 + M->_21*M->_21 + M->_31*M->_31;
        float s2 = M->_12*M->_12 + M->_22*M->_22 + M->_32*M->_32;
        float s3 = M->_13*M->_13 + M->_23*M->_23 + M->_33*M->_33;
        if (s1 < 1e-4f || s2 < 1e-4f || s3 < 1e-4f) continue;
        float c1 = sqrtf(s1), c2 = sqrtf(s2), a = sqrtf(s3);
        if (c1 < 0.05f || c1 > 50 || c2 < 0.05f || c2 > 50 || a < 0.05f || a > 50) continue;
        float mx = fabsf(M->_13);
        if (fabsf(M->_23) > mx) mx = fabsf(M->_23);
        if (fabsf(M->_33) > mx) mx = fabsf(M->_33);
        if (mx < 1e-4f) continue;
        float bs[3]; int nb = 0;
        if (fabsf(M->_13) > 0.3f*mx) bs[nb++] = a * M->_14 / M->_13;
        if (fabsf(M->_23) > 0.3f*mx) bs[nb++] = a * M->_24 / M->_23;
        if (fabsf(M->_33) > 0.3f*mx) bs[nb++] = a * M->_34 / M->_33;
        if (!nb) continue;
        float bmin = bs[0], bmax = bs[0], bsum = 0;
        for (int i = 0; i < nb; i++) { if (bs[i] < bmin) bmin = bs[i]; if (bs[i] > bmax) bmax = bs[i]; bsum += bs[i]; }
        if (nb > 1 && fabsf(bmax - bmin) > 0.02f * (fabsf(bmax) + 1e-6f)) continue;
        float p34 = bsum / nb;
        if (fabsf(fabsf(p34) - 1.0f) > 0.1f) continue;           // P_34 deve ser +-1
        float d3 = M->_44 / p34;
        if (fabsf(d3) > 5000) continue;
        float p43 = M->_43 - d3 * a;
        if (!(p43 < 0.0f) || p43 < -100000.0f) continue;         // bias de prof. negativo
        int ci = -1;
        for (int i = 0; i < npc; i++) {
            Pc &p = pcl[i];
            if (fabsf(p.f1-c1) <= 0.01f*(fabsf(c1)+1e-6f) &&
                fabsf(p.f2-c2) <= 0.01f*(fabsf(c2)+1e-6f) &&
                fabsf(p.A -a ) <= 0.01f*(fabsf(a )+1e-6f) &&
                fabsf(p.p34-p34) <= 0.01f*(fabsf(p34)+1e-6f) &&
                fabsf(p.p43-p43) <= 0.02f*(fabsf(p43)+1e-6f)) { ci = i; break; }
        }
        if (ci >= 0) pcl[ci].cnt++;
        else if (npc < 24) { pcl[npc].f1 = c1; pcl[npc].f2 = c2; pcl[npc].A = a;
                             pcl[npc].p34 = p34; pcl[npc].p43 = p43; pcl[npc].cnt = 1; npc++; }
    }
    g_nPc = npc;
    int bp = -1;
    for (int i = 0; i < npc; i++) if (bp < 0 || pcl[i].cnt > pcl[bp].cnt) bp = i;
    g_pcBest = (bp >= 0) ? pcl[bp].cnt : 0;
    if (bp < 0 || pcl[bp].cnt < 3) {
        snprintf(g_derMsg, sizeof(g_derMsg), "P nao solve (nUp=%d npc=%d best=%d)",
                 g_nUp, npc, g_pcBest);
        return;
    }
    float f1 = pcl[bp].f1, f2 = pcl[bp].f2, A = pcl[bp].A;
    float P34 = pcl[bp].p34, P43 = pcl[bp].p43;
    g_lf1 = f1; g_lf2 = f2; g_lA = A; g_lB = P34; g_lP43 = P43;
    // ==== Estagio A v2 (v0.13): pareamento FORCADO upload x world ====
    // O ordenho do jogo (WVP antes do matWorld) quebrava o pareamento por
    // efeito e o Estagio A nunca acertava (0/96 no v0.12). Agora testamos
    // todos os pares do pool de worlds: o V verdadeiro vira cluster grande
    // (todos os uploads concordam), pareamento errado da R_rel*V espalhado.
    // Escala/rotacao de W sao removidas por W^-1; P errado quebra a
    // ortonormalidade e o is_view rejeita — o cluster valida P junto.
    D3DMATRIX Pm; memset(&Pm, 0, 64);
    Pm._11 = f1; Pm._22 = f2; Pm._33 = A; Pm._34 = P34; Pm._43 = P43;
    D3DMATRIX Pinv;
    g_aPair = g_aInv = g_aView = 0;
    if (minv(&Pm, &Pinv)) {
        D3DMATRIX winv[MAXW]; bool wok[MAXW];
        for (int w = 0; w < g_nWP; w++) wok[w] = minv(&g_wPool[w], &winv[w]);
        struct VC { D3DMATRIX m; int u; };
        static VC vca[384]; int na = 0;
        for (int u = 0; u < g_nUp && na < 384; u++) {
            for (int w = 0; w < g_nWP && na < 384; w++) {
                g_aPair++;
                if (!wok[w]) { g_aInv++; continue; }
                D3DMATRIX X, V;
                mmul(&X, &winv[w], &g_upM[u]);     // X = W^-1 * M = V*P
                mmul(&V, &X, &Pinv);               // V = X * P^-1
                if (is_view(&V)) { vca[na].m = V; vca[na].u = u; na++; g_aView++; }
            }
        }
        int bia = -1, bca = 0, bdu = 0;
        for (int i = 0; i < na; i++) {
            int cnt = 0, nus = 0, us[MAXU];
            for (int j = 0; j < na; j++) {
                float d = 0;
                for (int rr = 0; rr < 4; rr++) for (int cc = 0; cc < 4; cc++) {
                    float e = ME(&vca[i].m,rr,cc) - ME(&vca[j].m,rr,cc);
                    if (e < 0) e = -e;
                    if (e > d) d = e;
                }
                if (d < 0.004f) {
                    cnt++;
                    int k; for (k = 0; k < nus; k++) if (us[k] == vca[j].u) break;
                    if (k == nus && nus < MAXU) us[nus++] = vca[j].u;
                }
            }
            if (cnt > bca || (cnt == bca && nus > bdu)) { bca = cnt; bdu = nus; bia = i; }
        }
        g_rotN = na; g_rotC = bca;
        if (bia >= 0 && bca >= 2 && bdu >= 2) {
            g_Vgame = vca[bia].m; g_Puse = Pm;
            g_haveX = true; g_Kready = false; g_Kver = -1;
            g_convCol = false; g_viewT = false;
            if (!g_userMoved) g_detV = (det3(&g_Vgame) < 0) ? -1.0f : 1.0f;
            g_nDeriv++; g_lastDerivTick = GetTickCount();
            g_derHow = 'A'; g_derMsg[0] = 0;
            return;
        }
    } else g_aInv = -1;   // P nao invertivel
    g_derHow = 'B';
    // ---- V linhas 1..3 + cluster (fallback Estagio B) ----
    D3DMATRIX vc[96]; int nv = 0;
    for (int u = 0; u < g_nUp && nv < 96; u++) {
        const D3DMATRIX *M = &g_upM[u];
        D3DMATRIX v; memset(&v, 0, 64);
        v._11 = M->_11/f1; v._12 = M->_12/f2; v._13 = M->_13/A;
        v._21 = M->_21/f1; v._22 = M->_22/f2; v._23 = M->_23/A;
        v._31 = M->_31/f1; v._32 = M->_32/f2; v._33 = M->_33/A;
        v._44 = 1.0f;
        if (ortho3(&v)) vc[nv++] = v;
    }
    g_rotN = nv;
    int bi = -1, bc = 0;
    for (int i = 0; i < nv; i++) {
        int cnt = 0;
        for (int j = 0; j < nv; j++) {
            float d = 0;
            for (int rr = 0; rr < 3; rr++) for (int cc = 0; cc < 3; cc++) {
                float e = ME(&vc[i],rr,cc) - ME(&vc[j],rr,cc);
                if (e < 0) e = -e;
                if (e > d) d = e;
            }
            if (d < 0.004f) cnt++;
        }
        if (cnt > bc) { bc = cnt; bi = i; }
    }
    g_rotC = bc;
    if (bi < 0 || bc < 2) { snprintf(g_derMsg, sizeof(g_derMsg), "sem cluster rot (nv=%d bc=%d)", nv, bc); return; }
    D3DMATRIX Vu = vc[bi];
    // ---- row4(V) = o - d ----
    float r4c[96][3]; bool r4p[96]; int nr4 = 0;
    for (int u = 0; u < g_nUp && nr4 < 96; u++) {
        const D3DMATRIX *M = &g_upM[u];
        float o1 = M->_41/f1, o2 = M->_42/f2, o3 = (M->_43 - P43)/A;
        if (fabsf(o1) > 5000 || fabsf(o2) > 5000 || fabsf(o3) > 5000) continue;
        if (fabsf(M->_44 - o3*P34) > 0.03f*fabsf(M->_44) + 1.0f) continue;  // valida M_44=d3*P_34
        g_xrow4++;
        float d[3] = {0,0,0};
        if (g_upHasT[u])
            for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++)
                d[j] += g_upT[u][i] * ME(&Vu,i,j);
        r4c[nr4][0] = o1 - d[0]; r4c[nr4][1] = o2 - d[1]; r4c[nr4][2] = o3 - d[2];
        r4p[nr4] = g_upHasT[u];
        nr4++;
    }
    g_r4N = nr4;
    int ci = -1, cc = 0, cp = 0, cu = 0;
    for (int i = 0; i < nr4; i++) {
        int cnt = 0, np = 0, nu = 0;
        for (int j = 0; j < nr4; j++) {
            float d = 0;
            for (int k = 0; k < 3; k++) { float e = r4c[i][k] - r4c[j][k]; if (e < 0) e = -e; if (e > d) d = e; }
            if (d < 0.4f) { cnt++; if (r4p[j]) np++; else nu++; }
        }
        if (cnt > cc) { cc = cnt; cp = np; cu = nu; ci = i; }
    }
    g_r4C = cc; g_r4P = cp; g_r4U = cu;
    if (ci < 0 || !(cc >= 2 || (cp >= 1 && cu >= 1))) {
        snprintf(g_derMsg, sizeof(g_derMsg), "sem cluster row4 (nr4=%d cc=%d p=%d u=%d)", nr4, cc, cp, cu);
        return;
    }
    D3DMATRIX V = Vu;
    V._41 = r4c[ci][0]; V._42 = r4c[ci][1]; V._43 = r4c[ci][2]; V._44 = 1.0f;
    D3DMATRIX P; memset(&P, 0, 64);
    P._11 = f1; P._22 = f2; P._33 = A; P._34 = P34; P._43 = P43;
    g_Vgame = V; g_Puse = P;
    g_haveX = true; g_Kready = false; g_Kver = -1;
    g_convCol = false; g_viewT = false;
    if (!g_userMoved) g_detV = (det3(&g_Vgame) < 0) ? -1.0f : 1.0f;  // congelado apos voar
    g_nDeriv++; g_lastDerivTick = GetTickCount();
    g_derHow = 'B';
    g_derMsg[0] = 0;
}
static int g_is_proj(const D3DMATRIX *m) {
    for (int i = 0; i < g_nProjs; i++) if (memcmp(m, &g_projs[i], 64) == 0) return 1;
    return 0;
}
static D3DMATRIX g_firstA; static bool g_haveFirstA = false;
static int  g_ovrW = 0;               // WVP corrigidos

static unsigned long long mhash(const D3DMATRIX *m) {
    unsigned long long h = 1469598103934665603ULL;
    const unsigned char *b = (const unsigned char*)m;
    for (size_t i = 0; i < sizeof(D3DMATRIX); i++) { h ^= b[i]; h *= 1099511628211ULL; }
    return h;
}
static bool ortho3(const D3DMATRIX *m) {
    float a[3][3] = {{m->_11,m->_12,m->_13},{m->_21,m->_22,m->_23},{m->_31,m->_32,m->_33}};
    for (int i = 0; i < 3; i++) {
        float d = a[i][0]*a[i][0] + a[i][1]*a[i][1] + a[i][2]*a[i][2];
        if (fabsf(d - 1.0f) > 0.05f) return false;
    }
    for (int i = 0; i < 3; i++) for (int k = i+1; k < 3; k++) {
        float d = a[i][0]*a[k][0] + a[i][1]*a[k][1] + a[i][2]*a[k][2];
        if (fabsf(d) > 0.05f) return false;
    }
    return ME(m,3,0)==0 && ME(m,3,1)==0 && ME(m,3,2)==0 && ME(m,3,3)==1;
}
struct Cand {
    unsigned long long h;
    int cnt, parN, parIdx;
    unsigned long long par[4];
    D3DMATRIX m;
};
static Cand g_cand[64]; static int g_nCand = 0;
static Cand *cand_find(const D3DMATRIX *m) {
    unsigned long long h = mhash(m);
    for (int i = 0; i < g_nCand; i++) if (g_cand[i].h == h) return &g_cand[i];
    if (g_nCand >= 64) return NULL;
    Cand *c = &g_cand[g_nCand++];
    memset(c, 0, sizeof(*c));
    c->h = h; c->m = *m;
    return c;
}
static void note_op(const D3DMATRIX *m, const D3DMATRIX *other) {
    if (!m) return;
    Cand *c = cand_find(m);
    if (!c) return;
    c->cnt++;
    if (other) {
        unsigned long long oh = mhash(other);
        int i;
        for (i = 0; i < 4; i++) if (c->par[i] == oh) break;
        if (i == 4) { c->par[c->parIdx++ & 3] = oh; c->parN++; }
    }
}

// --- hooks D3DX ---
typedef D3DMATRIX* (WINAPI *PFN_Persp)(D3DMATRIX*, float, float, float, float);
typedef D3DMATRIX* (WINAPI *PFN_Mul)(D3DMATRIX*, CONST D3DMATRIX*, CONST D3DMATRIX*);
static PFN_Persp g_oPersp = NULL;
static PFN_Mul   g_oMul   = NULL;

static D3DMATRIX* WINAPI Mine_Persp(D3DMATRIX *o, float fa, float as, float zn, float zf) {
    D3DMATRIX *r = g_oPersp(o, fa, as, zn, zf);
    if (o) {
        g_proj = *o; g_haveProj = true; g_Kver = -1;
        if (g_nProjs < 24 && !g_is_proj(o)) g_projs[g_nProjs++] = *o;
    }
    g_nPersp++;
    return r;
}
static bool make_K(void) {
    if (g_Kver == g_ver) return g_Kok;
    g_Kver = g_ver;
    g_Kok = false;
    if (!g_free || !g_haveProj || !g_haveViewM) return false;
    D3DMATRIX Vinv, Pinv, T, Vp;
    build_view(&Vp, g_eye, g_at);
    if (!minv(&g_viewM, &Vinv)) return false;
    if (!minv(&g_proj, &Pinv)) return false;
    mmul(&T, &Vinv, &Vp);          // V^-1 * V'
    mmul(&g_K, &Pinv, &T);         // P^-1 * V^-1 * V'
    D3DMATRIX tmp; mmul(&tmp, &g_K, &g_proj);
    g_K = tmp;                     // * P
    g_Kok = true;
    return true;
}
static void apply_upload(D3DMATRIX *m) {   // corrige o worldViewProj recebido
    if (!g_free || !g_haveX || !m) return;
    if (!g_Kready) {
        if (g_Kver == g_ver) return;       // ja tentou e falhou neste frame
        g_Kver = g_ver; g_Kready = false;
        D3DMATRIX Vp, Vi, Pi, t1, t2;
        build_view(&Vp, g_eye, g_at);
        if (!minv(&g_Vgame, &Vi)) return;
        if (!minv(&g_Puse, &Pi)) return;
        mmul(&t1, &Vi, &Vp);        // V^-1 * V'
        mmul(&t2, &Pi, &t1);        // P^-1 * ...
        mmul(&g_K, &t2, &g_Puse);   // * P
        g_Kready = true;
    }
    if (!g_Kready) return;
    // convencao detectada POR upload: linha4 grande = forma linha; coluna4 grande = transposta
    float r = m->_41*m->_41 + m->_42*m->_42 + m->_43*m->_43;
    float c = m->_14*m->_14 + m->_24*m->_24 + m->_34*m->_34;
    bool tr = false;
    if (c > 4*r && c > 1e-6f) tr = true;
    else if (!(r > 4*c && r > 1e-6f)) { g_nAmb++; return; }
    if (tr) transpose(m);
    D3DMATRIX out; mmul(&out, m, &g_K);
    if (tr) transpose(&out);
    *m = out;
    g_Kapp++;
}
static void apply_K(D3DMATRIX *m) {
    if (!g_free || !m) return;
    if (!make_K()) return;
    D3DMATRIX t;
    mmul(&t, m, &g_K);
    *m = t;
    g_ovrW++;
}
static bool is_wvp_name(const char *n) {
    if (!n || !n[0]) return false;
    char l[64]; int i = 0;
    for (; n[i] && i < 63; i++) l[i] = (char)tolower((unsigned char)n[i]);
    l[i] = 0;
    if (!strstr(l, "viewproj")) return false;
    if (strstr(l, "light") || strstr(l, "shadow")) return false;
    return true;
}
static int g_sub = 0;   // operandos V substituidos por V'
static int g_vMatch = 0, g_vZero = 0;   // V apareceu como operando / frames seguidos sem aparecer
static D3DMATRIX g_sampA, g_sampB; static bool g_haveSamp = false;
struct ML { D3DMATRIX out, a, b; };
static ML g_ring[16]; static int g_ringN = 0, g_ringI = 0;
static D3DMATRIX* WINAPI Mine_Mul(D3DMATRIX *o, CONST D3DMATRIX *A, CONST D3DMATRIX *B) {
    g_nMul++;
    if (!g_haveSamp && A && B && (ortho3(B) || g_is_proj(B))) {
        g_sampA = *A; g_sampB = *B; g_haveSamp = true;
    }
    note_op(A, B);
    note_op(B, A);
    if (g_nProjs && B && g_is_proj(B)) g_npB++;
    if (g_nProjs && A && g_is_proj(A)) g_npA++;
    {
        ML &e = g_ring[g_ringI++ & 15];
        if (o) e.out = *o;
        if (A) e.a = *A; else memset(&e.a, 0, 64);
        if (B) e.b = *B; else memset(&e.b, 0, 64);
        if (g_ringN < 16) g_ringN++;
    }
    // freecam: troca o operando que e' a view do jogo pela nossa V'
    if (g_haveViewM && ((A && memcmp(A, &g_viewM, 64) == 0) || (B && memcmp(B, &g_viewM, 64) == 0)))
        g_vMatch++;
    if (g_free && g_haveViewM) {
        D3DMATRIX Vp;
        const D3DMATRIX *ra = A, *rb = B;
        bool ea = (A && memcmp(A, &g_viewM, 64) == 0);
        bool eb = (B && memcmp(B, &g_viewM, 64) == 0);
        if (ea || eb) {
            build_view(&Vp, g_eye, g_at);
            if (g_viewT) transpose(&Vp);     // mesma convencao da view do jogo
            if (ea) { ra = &Vp; g_sub++; }
            if (eb) { rb = &Vp; g_sub++; }
            return g_oMul(o, ra, rb);
        }
    }
    return g_oMul(o, A, B);
}

// --- adocao da view + log ---
static unsigned long long g_prevBest = 0;
static bool g_bestChanged = false;
static void scan_view(void) {
    derive_from_uploads();
    g_nUp = g_nUpSkip = g_nUpTr = 0;
    int best = -1, bestD = -1;
    for (int i = 0; i < g_nCand; i++) {
        Cand *c = &g_cand[i];
        if (c->cnt >= 6 && ortho3(&c->m) && trans_len(&c->m) > 1.0f) {
            int distinct = 0;
            for (int k = 0; k < 4; k++) if (c->par[k]) distinct++;
            if (distinct < 3) continue;
            int bd = 0;
            if (best >= 0) for (int k = 0; k < 4; k++) if (g_cand[best].par[k]) bd++;
            if (best < 0 || distinct > bestD || (distinct == bestD && c->cnt > g_cand[best].cnt)) {
                best = i; bestD = distinct;
            }
        }
    }
    if (best >= 0) {
        if (g_prevBest && g_cand[best].h != g_prevBest) g_bestChanged = true;
        g_prevBest = g_cand[best].h;
    }
    if (!g_haveViewM && best >= 0 && g_bestChanged) {
        g_viewM = g_cand[best].m;
        g_viewT = is_col_conv(&g_viewM);
        g_haveViewM = true;
        g_Kver = -1;
        tlog("convencao do jogo: %s", g_viewT ? "vetor-coluna (transposta)" : "vetor-linha (D3DX)");
        tlog("VIEW adotada (parceria): cnt=%d row=(%.1f,%.1f,%.1f) col=(%.1f,%.1f,%.1f)\n  V=%.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f",
             g_cand[best].cnt,
             g_cand[best].m._41, g_cand[best].m._42, g_cand[best].m._43,
             g_cand[best].m._14, g_cand[best].m._24, g_cand[best].m._34,
             g_cand[best].m._11,g_cand[best].m._12,g_cand[best].m._13,g_cand[best].m._14,
             g_cand[best].m._21,g_cand[best].m._22,g_cand[best].m._23,g_cand[best].m._24,
             g_cand[best].m._31,g_cand[best].m._32,g_cand[best].m._33,g_cand[best].m._34,
             g_cand[best].m._41,g_cand[best].m._42,g_cand[best].m._43,g_cand[best].m._44);
    }
    // valida: a V adotada tem que existir na cadeia
    if (g_haveViewM) {
        if (g_vMatch > 0) { g_vZero = 0; }
        else if (++g_vZero >= 5) {
            tlog("VIEW invalidada (nunca apareceu como operando) - re-adotando");
            g_haveViewM = false; g_vZero = 0; g_bestChanged = false; g_prevBest = 0;
            g_Kver = -1;
        }
    }
    g_vMatch = 0;
    g_wvpDone = false;
    g_nWvp = 0; g_nWvpT = 0; g_nWvpV = 0; g_Kapp = 0; g_nSeq = 0;
    g_nAmb = 0;
    g_nWE = 0;   // world precisa ser do mesmo frame do WVP
    wpool_age(); // v0.13: pool de worlds envelece (mantem 2 frames)
    // zera contadores do frame
    for (int i = 0; i < g_nCand; i++) g_cand[i].cnt = 0;
    g_nMul = g_nPersp = g_npB = g_npA = 0;
    g_haveFirstA = false;
    g_ver++;
}
static void log_chain(void) {
    if (g_seq[0]) { tlog("seq SetMatrix:%s", g_seq); g_seq[0] = 0; }
    if (g_haveX) {
        float t1 = g_Vgame._41, t2 = g_Vgame._42, t3 = g_Vgame._43;
        float ex = -(g_Vgame._11*t1 + g_Vgame._12*t2 + g_Vgame._13*t3);
        float ey = -(g_Vgame._21*t1 + g_Vgame._22*t2 + g_Vgame._23*t3);
        float ez = -(g_Vgame._31*t1 + g_Vgame._32*t2 + g_Vgame._33*t3);
        tlog("derive ok(%c): P=(%.4f,%.4f,%.4f,%.4f,%.4f) npc=%d/%d rot=%d/%d r4=%d p=%d u=%d x4=%d nUp=%d eye=(%.1f,%.1f,%.1f) dir=(%.2f,%.2f,%.2f) Kapp=%d det=%.0f A=%d/%d/%d%s%s",
             g_derHow,
             g_lf1, g_lf2, g_lA, g_lB, g_lP43, g_pcBest, g_nPc, g_rotC, g_rotN, g_r4C, g_r4P, g_r4U, g_xrow4, g_nUp,
             ex, ey, ez,
             g_Vgame._13, g_Vgame._23, g_Vgame._33,
             g_Kapp, det3(&g_Vgame), g_aPair, g_aInv, g_aView,
             g_derMsg[0] ? " | ult.falha: " : "", g_derMsg);
    } else {
        tlog("derive FALHOU(%c): %s | P=(%.4f,%.4f,%.4f,%.4f,%.4f) npc=%d/%d nUp=%d tr=%d skip=%d wvpT=%d wvpV=%d A=%d/%d/%d nWP=%d",
             g_derHow, g_derMsg, g_lf1, g_lf2, g_lA, g_lB, g_lP43, g_pcBest, g_nPc,
             g_nUp, g_nUpTr, g_nUpSkip, g_nWvpT, g_nWvpV, g_aPair, g_aInv, g_aView, g_nWP);
    }
    if (g_nUp > 0) {
        const D3DMATRIX *w0 = &g_upM[0];
        tlog("wvp0= %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f",
             w0->_11,w0->_12,w0->_13,w0->_14, w0->_21,w0->_22,w0->_23,w0->_24,
             w0->_31,w0->_32,w0->_33,w0->_34, w0->_41,w0->_42,w0->_43,w0->_44);
    }
    int bi = -1;
    for (int i = 0; i < g_nCand; i++)
        if (g_cand[i].cnt >= 4 && (bi < 0 || g_cand[i].cnt > g_cand[bi].cnt)) bi = i;
    if (bi >= 0)
        tlog("cand1 cnt=%d: %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f",
             g_cand[bi].cnt,
             g_cand[bi].m._11,g_cand[bi].m._12,g_cand[bi].m._13,g_cand[bi].m._14,
             g_cand[bi].m._21,g_cand[bi].m._22,g_cand[bi].m._23,g_cand[bi].m._24,
             g_cand[bi].m._31,g_cand[bi].m._32,g_cand[bi].m._33,g_cand[bi].m._34,
             g_cand[bi].m._41,g_cand[bi].m._42,g_cand[bi].m._43,g_cand[bi].m._44);
    if (g_haveSamp) {
        tlog("amostra Mul(A,B): A=%.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f",
             g_sampA._11,g_sampA._12,g_sampA._13,g_sampA._14, g_sampA._21,g_sampA._22,g_sampA._23,g_sampA._24,
             g_sampA._31,g_sampA._32,g_sampA._33,g_sampA._34, g_sampA._41,g_sampA._42,g_sampA._43,g_sampA._44);
        tlog("                B=%.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f%s",
             g_sampB._11,g_sampB._12,g_sampB._13,g_sampB._14, g_sampB._21,g_sampB._22,g_sampB._23,g_sampB._24,
             g_sampB._31,g_sampB._32,g_sampB._33,g_sampB._34, g_sampB._41,g_sampB._42,g_sampB._43,g_sampB._44,
             (g_haveViewM && memcmp(&g_sampB, &g_viewM, 64) == 0) ? "  <=B e' a VIEW" : "");
        g_haveSamp = false;
    }
    // top candidatos
    char buf[400]; int o = 0, shown = 0;
    for (int i = 0; i < g_nCand && shown < 3; i++) {
        Cand *c = &g_cand[i];
        if (c->cnt < 4) continue;
        int distinct = 0;
        for (int k = 0; k < 4; k++) if (c->par[k]) distinct++;
        o += snprintf(buf+o, sizeof(buf)-o, "[%d orth=%d par=%d row=(%.0f,%.0f,%.0f) col=(%.0f,%.0f,%.0f)]",
                      c->cnt, ortho3(&c->m) ? 1 : 0, distinct,
                      c->m._41, c->m._42, c->m._43, c->m._14, c->m._24, c->m._34);
        shown++;
    }
    tlog("chain: mul=%d persp=%d wvp=%d deriv=%d Kapp=%d seq=%d X=%d%s %s",
         g_nMul, g_nPersp, g_nWvp, g_nDeriv, g_Kapp, g_nSeq, g_haveX ? 1 : 0,
         g_haveX ? (g_convCol ? " [conv=transposta]" : " [conv=linha]") : "", buf);
}

// posiciona eye/at na camera ATUAL do jogo (extraido da V adotada)
static bool g_needInit = false;
static void init_from_game_cam(void) {
    const D3DMATRIX *v = NULL;
    bool colT = false;
    if (g_haveX) { v = &g_Vgame; colT = false; }          // V derivada (D3DX linha)
    else if (g_haveViewM) { v = &g_viewM; colT = g_viewT; }
    else return;
    // v0.13: congela a mao da view — build_view() precisa reproduzir exatamente
    g_detV = (det3(v) < 0) ? -1.0f : 1.0f;
    float f[3];
    if (colT) {
        // M = [R | t], t = -R*e  ->  e = -R^T * t   (t na coluna 4)
        float tx = v->_14, ty = v->_24, tz = v->_34;
        g_eye[0] = -(v->_11*tx + v->_21*ty + v->_31*tz);
        g_eye[1] = -(v->_12*tx + v->_22*ty + v->_32*tz);
        g_eye[2] = -(v->_13*tx + v->_23*ty + v->_33*tz);
        f[0] = v->_31; f[1] = v->_32; f[2] = v->_33;   // 3a linha = eixo +z da camera
    } else {
        g_eye[0] = -(v->_41*v->_11 + v->_42*v->_12 + v->_43*v->_13);
        g_eye[1] = -(v->_41*v->_21 + v->_42*v->_22 + v->_43*v->_23);
        g_eye[2] = -(v->_41*v->_31 + v->_42*v->_32 + v->_43*v->_33);
        f[0] = v->_13; f[1] = v->_23; f[2] = v->_33;   // coluna 3 = direcao da visada
        // v0.12: up real da camera = coluna 2 (eixo y da camera D3DX linha)
        g_upv[0] = v->_12; g_upv[1] = v->_22; g_upv[2] = v->_32;
        float ul = sqrtf(g_upv[0]*g_upv[0] + g_upv[1]*g_upv[1] + g_upv[2]*g_upv[2]);
        if (ul < 1e-6f) { g_upv[0] = 0; g_upv[1] = 1; g_upv[2] = 0; }
        else { g_upv[0] /= ul; g_upv[1] /= ul; g_upv[2] /= ul; }
    }
    float fl = sqrtf(f[0]*f[0] + f[1]*f[1] + f[2]*f[2]);
    if (fl < 1e-6f) { f[0]=0; f[1]=0; f[2]=1; } else { f[0]/=fl; f[1]/=fl; f[2]/=fl; }
    float L = 150.0f;
    g_at[0] = g_eye[0] + f[0]*L;
    g_at[1] = g_eye[1] + f[1]*L;
    g_at[2] = g_eye[2] + f[2]*L;
    if (f[1] >  1.0f) f[1] =  1.0f;
    if (f[1] < -1.0f) f[1] = -1.0f;
    g_pitch = asinf(f[1]);
    g_yaw = atan2f(f[0], f[2]);
    g_lastMove = 0;
    unsigned long age = g_lastDerivTick ? (GetTickCount() - g_lastDerivTick) / 1000 : 999;
    // v0.13: valida a reconstrucao — build_view(eye,at) tem que reproduzir a V
    // do jogo EXATAMENTE (recon~0 => K=identidade no sync => imagem intacta).
    float recon = 99.0f;
    if (g_haveX && !colT) {
        D3DMATRIX vb;
        build_view(&vb, g_eye, g_at);
        recon = 0;
        for (int rr = 0; rr < 4; rr++) for (int cc = 0; cc < 4; cc++) {
            float e = fabsf(ME(&vb,rr,cc) - ME(v,rr,cc));
            if (e > recon) recon = e;
        }
    }
    tlog("cam sincronizada%s: eye=(%.1f,%.1f,%.1f) dir=(%.2f,%.2f,%.2f) up=(%.2f,%.2f,%.2f) Vage=%lus det=%.0f recon=%.4f",
         g_haveX ? " [V derivada]" : "", g_eye[0], g_eye[1], g_eye[2], f[0], f[1], f[2],
         g_upv[0], g_upv[1], g_upv[2], age, g_detV, recon);
}
static void update_move(void) {
    DWORD now = GetTickCount();
    float dt = g_lastMove ? (now - g_lastMove) / 1000.0f : 0.016f;
    g_lastMove = now;
    g_Kver = -1;
    g_Kready = false;
    if (dt > 0.1f) dt = 0.1f;
    float sp = g_cfgSpeed;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) sp *= 4.0f;
    float d[3]; dir_from_angles(d);
    float r[3] = {d[2], 0.0f, -d[0]};
    float rl = sqrtf(r[0] * r[0] + r[2] * r[2]);
    if (rl > 1e-6f) { r[0] /= rl; r[2] /= rl; }
    float mx = 0, mz = 0, my = 0;
    if (GetAsyncKeyState('W') & 0x8000) { mz += 1; g_userMoved = true; }
    if (GetAsyncKeyState('S') & 0x8000) { mz -= 1; g_userMoved = true; }
    if (GetAsyncKeyState('D') & 0x8000) { mx += 1; g_userMoved = true; }
    if (GetAsyncKeyState('A') & 0x8000) { mx += 1 * -1; g_userMoved = true; }
    if (GetAsyncKeyState('E') & 0x8000) { my += 1; g_userMoved = true; }
    if (GetAsyncKeyState('Q') & 0x8000) { my -= 1; g_userMoved = true; }
    float dx = (d[0] * mz + r[0] * mx) * sp * dt;
    float dy = (d[1] * mz + my) * sp * dt;
    float dz = (d[2] * mz + r[2] * mx) * sp * dt;
    g_eye[0] += dx; g_eye[1] += dy; g_eye[2] += dz;
    g_at[0]  += dx; g_at[1]  += dy; g_at[2]  += dz;
    float turn = 1.6f * dt, tilt = 1.2f * dt;
    bool turned = false;
    if (GetAsyncKeyState(VK_LEFT)  & 0x8000) { g_yaw -= turn; turned = true; }
    if (GetAsyncKeyState(VK_RIGHT) & 0x8000) { g_yaw += turn; turned = true; }
    if (GetAsyncKeyState(VK_UP)    & 0x8000) { g_pitch += tilt; turned = true; }
    if (GetAsyncKeyState(VK_DOWN)  & 0x8000) { g_pitch -= tilt; turned = true; }
    if (g_pitch >  1.5f) g_pitch =  1.5f;
    if (g_pitch < -1.5f) g_pitch = -1.5f;
    if (turned || mx || mz || my) {
        float nd[3]; dir_from_angles(nd);
        float L = sqrtf((g_at[0] - g_eye[0]) * (g_at[0] - g_eye[0]) +
                        (g_at[1] - g_eye[1]) * (g_at[1] - g_eye[1]) +
                        (g_at[2] - g_eye[2]) * (g_at[2] - g_eye[2]));
        if (L < 1.0f) L = 120.0f;
        g_at[0] = g_eye[0] + nd[0] * L;
        g_at[1] = g_eye[1] + nd[1] * L;
        g_at[2] = g_eye[2] + nd[2] * L;
    }
}

// ---------------------------------------------------------------- effect hooks
// vtable do ID3DXEffect (declared em d3dx9effect.h, DECLARE_INTERFACE_(ID3DXEffect,...))
enum {
    EFF_GetParameterDesc   = 4,
    EFF_GetParameter       = 8,
    EFF_SetValue           = 20,
    EFF_GetTechnique       = 12,
    EFF_GetTechniqueDesc   = 5,
    EFF_SetMatrix          = 38,
    EFF_SetMatrixTranspose = 44,
    EFF_GetDesc            = 3
};
struct D3DXPARAMETER_DESC_LOCA {
    const char *Name; const char *Semantic;
    int Class; int Type;
    unsigned Rows, Columns, Elements, Annotations, StructMembers;
    DWORD Flags; unsigned Bytes;
};
struct D3DXEFFECT_DESC_LOCA {
    const char *Creator; unsigned Parameters, Techniques, Functions;
};
struct D3DXTECHNIQUE_DESC_LOCA {
    const char *Name; unsigned Passes, Annotations;
};

typedef HRESULT (STDMETHODCALLTYPE *PFN_EffGetParamDesc)(void*, const char*, D3DXPARAMETER_DESC_LOCA*);
typedef const char* (STDMETHODCALLTYPE *PFN_EffGetParam)(void*, unsigned);
typedef HRESULT (STDMETHODCALLTYPE *PFN_EffSetValue)(void*, const char*, const void*, size_t);
typedef HRESULT (STDMETHODCALLTYPE *PFN_EffSetMatrix)(void*, const char*, const D3DMATRIX*);
typedef HRESULT (STDMETHODCALLTYPE *PFN_EffSetMatrixT)(void*, const char*, const D3DMATRIX*);
typedef const char* (STDMETHODCALLTYPE *PFN_EffGetTechnique)(void*, unsigned);
typedef HRESULT (STDMETHODCALLTYPE *PFN_EffGetTechDesc)(void*, const char*, D3DXTECHNIQUE_DESC_LOCA*);

static void *g_effVt = NULL;                       // vtable ja patcheada
static PFN_EffGetParamDesc g_oGetParamDesc = NULL;
static PFN_EffGetParam     g_oGetParam = NULL;
static PFN_EffSetValue     g_oSetValue = NULL;
static PFN_EffSetMatrix    g_oSetMatrix = NULL;
static PFN_EffSetMatrixT   g_oSetMatrixT = NULL;

// estatistica por nome de parametro (p/ descobrir quem e' a view)
#define MAXP 48
static char  g_pName[MAXP][48];
static int   g_pN[MAXP];
static bool  g_pOrtho[MAXP];
static D3DMATRIX g_pLast[MAXP];
static int   g_nP = 0;
static int   g_pFind(const char *n) {
    for (int i = 0; i < g_nP; i++) if (!strcmp(g_pName[i], n)) return i;
    return -1;
}
static int  g_pAdd(const char *n) {
    int i = g_pFind(n);
    if (i >= 0) return i;
    if (g_nP >= MAXP) return -1;
    i = g_nP++;
    snprintf(g_pName[i], sizeof(g_pName[i]), "%s", n ? n : "?");
    g_pN[i] = 0; g_pOrtho[i] = false; memset(&g_pLast[i], 0, sizeof(D3DMATRIX));
    return i;
}
static void track_param(const char *name, const D3DMATRIX *m) {
    if (!name || !name[0]) return;
    int i = g_pAdd(name);
    if (i < 0) return;
    g_pN[i]++;
    if (m) { g_pLast[i] = *m; g_pOrtho[i] = ortho3(m); }
}
static const char* pname(void *eff, const char *h) {
    if (!h) return "";
    // D3DXHANDLE de parametro e' normalmente a propria string; confere com GetParameterDesc
    if (g_oGetParamDesc) {
        D3DXPARAMETER_DESC_LOCA d; memset(&d, 0, sizeof(d));
        if (SUCCEEDED(g_oGetParamDesc(eff, h, &d)) && d.Name) return d.Name;
    }
    return h;
}
static bool want_override(const char *name) {
    return g_free && is_view_name(name);
}
static HRESULT STDMETHODCALLTYPE Mine_SetMatrix(void *eff, const char *h, const D3DMATRIX *m) {
    const char *n = pname(eff, h);
    track_param(n, m);
    if (m && is_world_name(n)) { we_set(eff, m); seq_add(n, m); }
    if (m && is_wvp_name(n)) {
        g_nWvp++;
        seq_add("WVP", m);
        store_upload(eff, m);
        // v0.11: caminho antigo (derive_V com P capturado) REMOVIDO — gerava V de lixo.
        // so o derive_from_uploads() (normas+cluster) pode setar g_haveX.
        if (g_haveX && g_free) { D3DMATRIX c = *m; apply_upload(&c); return g_oSetMatrix(eff, h, &c); }
    }
    if (is_wvp_name(n) && m && g_ringN && !g_wvpDone) {
        for (int i = 0; i < g_ringN; i++) {
            ML &e = g_ring[(g_ringI - 1 - i) & 15];
            if (memcmp(&e.out, m, 64) == 0) {
                tlog("WVP veio de Mul(A,B):\n  A=%.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f%s\n  B=%.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f%s",
                     e.a._11,e.a._12,e.a._13,e.a._14, e.a._21,e.a._22,e.a._23,e.a._24,
                     e.a._31,e.a._32,e.a._33,e.a._34, e.a._41,e.a._42,e.a._43,e.a._44,
                     ortho3(&e.a) ? " [A orto]" : "",
                     e.b._11,e.b._12,e.b._13,e.b._14, e.b._21,e.b._22,e.b._23,e.b._24,
                     e.b._31,e.b._32,e.b._33,e.b._34, e.b._41,e.b._42,e.b._43,e.b._44,
                     g_is_proj(&e.b) ? " [B=PROJ]" : (ortho3(&e.b) ? " [B orto]" : ""));
                g_wvpDone = true;
                break;
            }
        }
    }
    if (want_override(n)) {
        D3DMATRIX v; build_view(&v, g_eye, g_at); g_ovr++;
        return g_oSetMatrix(eff, h, &v);
    }
    return g_oSetMatrix(eff, h, m);
}
static HRESULT STDMETHODCALLTYPE Mine_SetMatrixT(void *eff, const char *h, const D3DMATRIX *m) {
    const char *n = pname(eff, h);
    track_param(n, m);
    if (m && is_wvp_name(n)) g_nWvpT++;
    if (m && is_wvp_name(n) && g_haveX && g_free) {
        D3DMATRIX w = *m;
        apply_upload(&w);
        return g_oSetMatrixT(eff, h, &w);
    }
    if (want_override(n)) {
        D3DMATRIX v; build_view(&v, g_eye, g_at); transpose(&v); g_ovr++;
        return g_oSetMatrixT(eff, h, &v);
    }
    return g_oSetMatrixT(eff, h, m);
}
static HRESULT STDMETHODCALLTYPE Mine_SetValue(void *eff, const char *h, const void *data, size_t bytes) {
    const char *n = pname(eff, h);
    if (bytes == 64 && data) {
        track_param(n, (const D3DMATRIX*)data);
        if (is_wvp_name(n)) {
            g_nWvpV++;
            if (g_haveX && g_free) {
                D3DMATRIX c = *(const D3DMATRIX*)data;
                apply_upload(&c);
                return g_oSetValue(eff, h, &c, 64);
            }
        }
        if (want_override(n)) {
            D3DMATRIX v; build_view(&v, g_eye, g_at); g_ovr++;
            return g_oSetValue(eff, h, &v, 64);
        }
    }
    return g_oSetValue(eff, h, data, bytes);
}

static void dump_params(void *eff) {
    D3DXEFFECT_DESC_LOCA ed; memset(&ed, 0, sizeof(ed));
    void **vt = *(void***)eff;
    HRESULT (STDMETHODCALLTYPE *gDesc)(void*, D3DXEFFECT_DESC_LOCA*) =
        (HRESULT (STDMETHODCALLTYPE*)(void*, D3DXEFFECT_DESC_LOCA*))vt[EFF_GetDesc];
    if (SUCCEEDED(gDesc(eff, &ed)))
        tlog("fx: effect criado params=%u techs=%u funcs=%u creator=%.60s",
             ed.Parameters, ed.Techniques, ed.Functions, ed.Creator ? ed.Creator : "?");
    for (unsigned i = 0; i < ed.Parameters && i < 400; i++) {
        const char *h = g_oGetParam(eff, i);
        if (!h) break;
        D3DXPARAMETER_DESC_LOCA d; memset(&d, 0, sizeof(d));
        if (FAILED(g_oGetParamDesc(eff, h, &d))) continue;
        if (d.Rows == 4 && d.Columns == 4)
            tlog("fxparam[%u]: name='%s' sem='%s' class=%d type=%d mat %ux%u%s",
                 i, d.Name ? d.Name : "(anon)", d.Semantic ? d.Semantic : "",
                 d.Class, d.Type, d.Rows, d.Columns,
                 is_view_name(d.Name) ? "  <= VIEW?" : "");
        g_pAdd(d.Name ? d.Name : "(anon)");
    }
    for (unsigned i = 0; i < 64; i++) {
        PFN_EffGetTechnique gt = (PFN_EffGetTechnique)vt[EFF_GetTechnique];
        PFN_EffGetTechDesc  gd = (PFN_EffGetTechDesc)vt[EFF_GetTechniqueDesc];
        const char *h = gt(eff, i);
        if (!h) break;
        D3DXTECHNIQUE_DESC_LOCA td; memset(&td, 0, sizeof(td));
        if (SUCCEEDED(gd(eff, h, &td)) && td.Name)
            tlog("fxtech[%u]: '%s' passes=%u", i, td.Name, td.Passes);
    }
}
static void patch_effect_vt(void *eff) {
    void **vt = *(void***)eff;
    if ((void*)vt == g_effVt) return;
    if (g_effVt) { tlog("fx: OUTRA vtable detectada (%p) - nao patcheada", vt); return; }
    DWORD prot;
    const size_t span = (EFF_SetMatrixTranspose - EFF_SetValue + 1) * sizeof(void*);
    VirtualProtect(&vt[EFF_SetValue], span, PAGE_READWRITE, &prot);
    g_oGetParamDesc = (PFN_EffGetParamDesc)vt[EFF_GetParameterDesc];
    g_oGetParam     = (PFN_EffGetParam)vt[EFF_GetParameter];
    g_oSetValue     = (PFN_EffSetValue)vt[EFF_SetValue];
    g_oSetMatrix    = (PFN_EffSetMatrix)vt[EFF_SetMatrix];
    g_oSetMatrixT   = (PFN_EffSetMatrixT)vt[EFF_SetMatrixTranspose];
    vt[EFF_SetMatrix]          = (void*)Mine_SetMatrix;
    vt[EFF_SetMatrixTranspose] = (void*)Mine_SetMatrixT;
    vt[EFF_SetValue]           = (void*)Mine_SetValue;
    VirtualProtect(&vt[EFF_SetValue], span, prot, &prot);
    g_effVt = vt;
    tlog("fx: vtable patcheada %p (SetMatrix=%p SetMatrixT=%p SetValue=%p)",
         vt, (void*)Mine_SetMatrix, (void*)Mine_SetMatrixT, (void*)Mine_SetValue);
}

// ---------------------------------------------------------------- D3DXCreateEffect (IAT)
typedef HRESULT (WINAPI *PFN_CreateEffect)(IDirect3DDevice9*, const void*, UINT,
                                           const void*, void*, DWORD, void*, void**, void**);
static PFN_CreateEffect g_oCreateEffect = NULL;
static HRESULT WINAPI Mine_CreateEffect(IDirect3DDevice9 *dev, const void *src, UINT len,
                                        const void *macros, void *incl, DWORD flags, void *pool,
                                        void **eff, void **errors) {
    HRESULT hr = g_oCreateEffect(dev, src, len, macros, incl, flags, pool, eff, errors);
    if (SUCCEEDED(hr) && eff && *eff) {
        patch_effect_vt(*eff);
        if (g_oGetParam) dump_params(*eff);
        tlog("fx: CreateEffect hr=%08x eff=%p", hr, *eff);
    }
    return hr;
}

// ---------------------------------------------------------------- device hooks
typedef HRESULT (WINAPI *PFN_EndScene)(IDirect3DDevice9*);
typedef HRESULT (WINAPI *PFN_Reset)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
typedef HRESULT (STDMETHODCALLTYPE *PFN_VSConstF)(IDirect3DDevice9*, UINT, CONST float*, UINT);
static void *g_oEndScene = NULL, *g_oReset = NULL, *g_oVSF = NULL;
static void *g_oST = NULL, *g_oDP = NULL, *g_oDUP = NULL, *g_oDIPUP = NULL;
static int g_frames = 0, g_nVSF = 0, g_logSec = 0;
static int g_nVSFZero = 0, g_vsfHit = 0, g_dpLog = 0, g_dupLog = 0, g_dipupLog = 0;
static int g_freeLog = 0;   // v0.16: draws registrados com freecam LIGADO (caça à linha)
static unsigned g_stSeen = 0;

// v0.14: compara um bloco de constantes com V/P do jogo (linha ou transposta).
// A linha que "segue a camera" deve ser desenhada com a view do jogo em
// constantes de vertex shader (nunca passa pelo worldViewProj corrigido).
static bool mat_eq16(const float *p, const D3DMATRIX *m, bool tr) {
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) {
        float v = tr ? ME(m,c,r) : ME(m,r,c);
        float d = p[r*4+c] - v;
        if (d < 0) d = -d;
        if (v < 0) v = -v;
        if (d > 0.03f + 0.0005f*v) return false;
    }
    return true;
}
static HRESULT STDMETHODCALLTYPE Mine_VSF(IDirect3DDevice9 *d, UINT reg, CONST float *p, UINT cnt) {
    g_nVSF++;
    if (g_hookIn[4] == 0) { g_hookIn[4] = 1; tlog("VSF in: reg=%u cnt=%u p=%p", reg, cnt, (const void*)p); }
    int hit = 0;
    if (cnt >= 4 && cnt <= 16 && p && g_haveX) {
        if      (mat_eq16(p, &g_Vgame, false)) hit = 1;
        else if (mat_eq16(p, &g_Vgame, true))  hit = 2;
        else if (g_haveProj && mat_eq16(p, &g_proj, false)) hit = 3;
        else if (g_haveProj && mat_eq16(p, &g_proj, true))  hit = 4;
        else {
            D3DMATRIX vp; mmul(&vp, &g_Vgame, &g_proj);
            if      (mat_eq16(p, &vp, false)) hit = 5;
            else if (mat_eq16(p, &vp, true))  hit = 6;
        }
        if (hit && !g_vsfHit) {
            g_vsfHit = 1;
            tlog("VSF: constante = %s (reg=%u cnt=%u)%s",
                 hit<=2 ? "VIEW" : (hit<=4 ? "PROJ" : "VIEW*PROJ"), reg, cnt,
                 hit%2==0 ? " [transp]" : "");
        }
    }
    if (hit && g_free) {
        float z[64]; memset(z, 0, sizeof(z));
        g_nVSFZero++;
        return ((PFN_VSConstF)g_oVSF)(d, reg, z, cnt);
    }
    HRESULT hr = ((PFN_VSConstF)g_oVSF)(d, reg, p, cnt);
    if (g_hookIn[4] == 1) { g_hookIn[4] = 2; tlog("VSF out: hr=%08X", (unsigned)hr); }
    return hr;
}
// v0.14: diagnosticos p/ achar o caminho da linha se nao for por constantes
typedef HRESULT (WINAPI *PFN_ST)(IDirect3DDevice9*, D3DTRANSFORMSTATETYPE, CONST D3DMATRIX*);
typedef HRESULT (WINAPI *PFN_DP)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT);
typedef HRESULT (WINAPI *PFN_DUP)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, CONST void*, UINT);
typedef HRESULT (WINAPI *PFN_DIPUP)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT, CONST void*, D3DFORMAT, CONST void*, UINT);
// v0.16: leitura segura de vertices (nao morre em ponteiro invalido de UP draw)
static bool rd_ok(const void *p, size_t n) {
    if (!p || !n) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi)) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) return false;
    DWORD pr = mbi.Protect & 0xFF;
    if (!(pr == PAGE_READONLY || pr == PAGE_READWRITE || pr == PAGE_WRITECOPY ||
          pr == PAGE_EXECUTE_READ || pr == PAGE_EXECUTE_READWRITE || pr == PAGE_EXECUTE_WRITECOPY))
        return false;
    uintptr_t a = (uintptr_t)p;
    if ((a >> 12) != ((a + n - 1) >> 12)) return false;   // nao cruzar pagina
    return true;
}
static HRESULT WINAPI Mine_ST(IDirect3DDevice9 *d, D3DTRANSFORMSTATETYPE st, CONST D3DMATRIX *m) {
    if (g_hookIn[0] == 0) { g_hookIn[0] = 1; tlog("ST in: state=%u m=%p", (unsigned)st, (const void*)m); }
    HRESULT hr = ((PFN_ST)g_oST)(d, st, m);
    if (g_hookIn[0] == 1) { g_hookIn[0] = 2; tlog("ST out: hr=%08X", (unsigned)hr); }
    return hr;
}
static HRESULT WINAPI Mine_DP(IDirect3DDevice9 *d, D3DPRIMITIVETYPE pt, UINT start, UINT cnt) {
    if (g_hookIn[1] == 0) { g_hookIn[1] = 1; tlog("DP in: prim=%d start=%u n=%u", (int)pt, start, cnt); }
    HRESULT hr = ((PFN_DP)g_oDP)(d, pt, start, cnt);
    if (g_hookIn[1] == 1) { g_hookIn[1] = 2; tlog("DP out: hr=%08X", (unsigned)hr); }
    if (pt < 4 && ((g_dpLog < 30) || (g_free && g_freeLog < 120))) {   // point/linha = linha!
        if (g_dpLog < 30) g_dpLog++; else g_freeLog++;
        tlog("DrawPrimitive NAO-triangulo%s: prim=%d n=%d start=%u", g_free?"[free]":"", pt, cnt, start);
    }
    return hr;
}
static HRESULT WINAPI Mine_DUP(IDirect3DDevice9 *d, D3DPRIMITIVETYPE pt, UINT cnt, CONST void *v, UINT stride) {
    if (g_hookIn[2] == 0) { g_hookIn[2] = 1; tlog("DUP in: prim=%d n=%u v=%p stride=%u", (int)pt, cnt, v, stride); }
    HRESULT hr = ((PFN_DUP)g_oDUP)(d, pt, cnt, v, stride);
    if (g_hookIn[2] == 1) { g_hookIn[2] = 2; tlog("DUP out: hr=%08X", (unsigned)hr); }
    if ((g_dupLog < 30) || (g_free && g_freeLog < 120)) {
        if (g_dupLog < 30) g_dupLog++; else g_freeLog++;
        if (stride >= 12 && rd_ok(v, 12)) {
            const float *f = (const float*)v;
            tlog("DrawPrimitiveUP%s: prim=%d n=%d stride=%u v0=(%.3f,%.3f,%.3f)", g_free?"[free]":"",
                 pt, cnt, stride, f[0], f[1], f[2]);
        } else {
            tlog("DrawPrimitiveUP%s: prim=%d n=%d stride=%u v=%p", g_free?"[free]":"",
                 pt, cnt, stride, v);
        }
    }
    return hr;
}
static HRESULT WINAPI Mine_DIPUP(IDirect3DDevice9 *d, D3DPRIMITIVETYPE pt, UINT minv, UINT numv,
                                 CONST void *idx, D3DFORMAT ifmt, CONST void *v, UINT stride) {
    if (g_hookIn[3] == 0) { g_hookIn[3] = 1; tlog("DIPUP in: prim=%d minv=%u numv=%u idx=%p ifmt=%d v=%p stride=%u",
            (int)pt, minv, numv, idx, (int)ifmt, v, stride); }
    HRESULT hr = ((PFN_DIPUP)g_oDIPUP)(d, pt, minv, numv, idx, ifmt, v, stride);
    if (g_hookIn[3] == 1) { g_hookIn[3] = 2; tlog("DIPUP out: hr=%08X", (unsigned)hr); }
    if ((g_dipupLog < 30) || (g_free && g_freeLog < 120)) {
        if (g_dipupLog < 30) g_dipupLog++; else g_freeLog++;
        if (stride >= 12 && rd_ok(v, 12)) {
            const float *f = (const float*)v;
            tlog("DrawIndexedPrimitiveUP%s: prim=%d numv=%d stride=%u v0=(%.3f,%.3f,%.3f)", g_free?"[free]":"",
                 pt, numv, stride, f[0], f[1], f[2]);
        } else {
            tlog("DrawIndexedPrimitiveUP%s: prim=%d numv=%d stride=%u v=%p", g_free?"[free]":"",
                 pt, numv, stride, v);
        }
    }
    return hr;
}
static HRESULT WINAPI Mine_EndScene(IDirect3DDevice9 *d) {
    g_frames++;
    if (g_free) update_move();
    int sec = GetTickCount() / 1000;
    if (sec != g_logSec) {
        g_logSec = sec;
        log_chain();
        if (g_free || g_haveViewM)
            tlog("t=%ds free=%d eye=(%.1f,%.1f,%.1f) at=(%.1f,%.1f,%.1f) sub=%d view=%d vsf=%d vsfZ=%d",
                 sec, g_free ? 1 : 0,
                 g_eye[0], g_eye[1], g_eye[2], g_at[0], g_at[1], g_at[2],
                 g_sub, g_haveViewM ? 1 : 0, g_nVSF, g_nVSFZero);
        g_nVSF = 0;
        g_nVSFZero = 0;
    }
    // agora sim zera os contadores do frame
    scan_view();
    // init p/ camera do jogo assim que a view adotar
    if (g_needInit && g_free && (g_haveViewM || g_haveX) && !g_userMoved) {
        init_from_game_cam();
        g_needInit = false;
    }
    return ((PFN_EndScene)g_oEndScene)(d);
}
static HRESULT WINAPI Mine_Reset(IDirect3DDevice9 *d, D3DPRESENT_PARAMETERS *p) {
    return ((PFN_Reset)g_oReset)(d, p);
}
static void hook_device(IDirect3DDevice9 *d) {
    if (g_oEndScene) return;
    void **vt = *(void***)d;
    DWORD prot;
    VirtualProtect(&vt[16], sizeof(void*) * 80, PAGE_READWRITE, &prot);
    g_oEndScene = vt[42]; vt[42] = (void*)Mine_EndScene;
    g_oReset    = vt[16]; vt[16] = (void*)Mine_Reset;
    g_oST       = vt[44]; vt[44] = (void*)Mine_ST;      // SetTransform (diag)
    g_oDP       = vt[81]; vt[81] = (void*)Mine_DP;      // DrawPrimitive
    g_oDUP      = vt[83]; vt[83] = (void*)Mine_DUP;     // DrawPrimitiveUP
    g_oDIPUP    = vt[84]; vt[84] = (void*)Mine_DIPUP;   // DrawIndexedPrimitiveUP
    g_oVSF      = vt[94]; vt[94] = (void*)Mine_VSF;     // SetVertexShaderConstantF (94!)
    VirtualProtect(&vt[16], sizeof(void*) * 80, prot, &prot);
    tlog("device hook ok (ES+Reset+ST@44+DP@81+DUP@83+DIPUP@84+VSF@94)");
}

// ---------------------------------------------------------------- IAT
typedef IDirect3D9* (WINAPI *PFN_D3DCreate9)(UINT);
static PFN_D3DCreate9 g_realCreate9 = NULL;
typedef HRESULT (STDMETHODCALLTYPE *PFN_CreateDevice)(IDirect3D9*,UINT,D3DDEVTYPE,HWND,DWORD,
                                                      D3DPRESENT_PARAMETERS*,IDirect3DDevice9**);
static PFN_CreateDevice g_origCreateDevice = NULL;
static HRESULT STDMETHODCALLTYPE Mine_CreateDevice(IDirect3D9 *s, UINT a, D3DDEVTYPE t, HWND f,
                                                   DWORD bh, D3DPRESENT_PARAMETERS *pp,
                                                   IDirect3DDevice9 **out) {
    HRESULT hr = g_origCreateDevice(s, a, t, f, bh, pp, out);
    if (SUCCEEDED(hr) && out && *out) hook_device(*out);
    return hr;
}
static IDirect3D9* WINAPI Mine_Direct3DCreate9(UINT v) {
    IDirect3D9 *d9 = g_realCreate9(v);
    if (d9 && !g_origCreateDevice) {
        void **vt = *(void***)d9;
        DWORD prot;
        VirtualProtect(&vt[16], 4, PAGE_READWRITE, &prot);
        g_origCreateDevice = (PFN_CreateDevice)vt[16];
        vt[16] = (void*)Mine_CreateDevice;
        VirtualProtect(&vt[16], 4, prot, &prot);
    }
    return d9;
}
static void patch_import(const char *dll, const char *func, void *mine, void **save) {
    HMODULE exe = GetModuleHandleA(NULL);
    if (!exe) return;
    BYTE *base = (BYTE*)exe;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
    IMAGE_IMPORT_DESCRIPTOR *imp = (IMAGE_IMPORT_DESCRIPTOR*)(base +
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for (; imp->Name; imp++) {
        const char *name = (const char*)(base + imp->Name);
        if (_stricmp(name, dll) != 0) continue;
        IMAGE_THUNK_DATA64 *orig = (IMAGE_THUNK_DATA64*)(base + imp->OriginalFirstThunk);
        IMAGE_THUNK_DATA64 *thunk = (IMAGE_THUNK_DATA64*)(base + imp->FirstThunk);
        for (; orig->u1.AddressOfData; orig++, thunk++) {
            if (IMAGE_SNAP_BY_ORDINAL(orig->u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME *ibn = (IMAGE_IMPORT_BY_NAME*)(base + orig->u1.AddressOfData);
            if (strcmp((char*)ibn->Name, func) == 0) {
                DWORD prot;
                VirtualProtect(&thunk->u1.Function, 8, PAGE_READWRITE, &prot);
                *save = (void*)(uintptr_t)thunk->u1.Function;
                thunk->u1.Function = (ULONGLONG)(uintptr_t)mine;
                VirtualProtect(&thunk->u1.Function, 8, prot, &prot);
                tlog("IAT %s::%s ok", dll, func);
                return;
            }
        }
    }
    tlog("IAT %s::%s FALHOU", dll, func);
}

// ---------------------------------------------------------------- hotkey
static DWORD WINAPI hotkey_thread(LPVOID u) {
    (void)u;
    for (;;) {
        Sleep(100);
        if (GetAsyncKeyState(VK_F4) & 1) {
            g_free = !g_free;
            g_lastMove = 0;
            g_userMoved = false;
            if (g_free) g_needInit = true;   // comeca da camera atual do jogo
            tlog("freecam %s (eye=(%.1f,%.1f,%.1f))", g_free ? "ON" : "OFF",
                 g_eye[0], g_eye[1], g_eye[2]);
            Sleep(400);
        }
        if ((GetAsyncKeyState('R') & 1) && g_free) {
            g_userMoved = false;
            if (g_haveViewM || g_haveX) { init_from_game_cam(); g_needInit = false; }
            Sleep(400);
        }
        if (GetAsyncKeyState(VK_F6) & 1) {
            tlog("dump: params=%d free=%d", g_nP, g_free ? 1 : 0);
            for (int i = 0; i < g_nP; i++)
                tlog("  '%s' n=%d ortho=%d", g_pName[i], g_pN[i], g_pOrtho[i] ? 1 : 0);
            Sleep(400);
        }
    }
    return 0;
}
static bool is_child(void) {
    const wchar_t *cmd = GetCommandLineW();
    return cmd && wcsstr(cmd, L"--type=") != NULL;
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) {
    (void)r;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        if (is_child()) return TRUE;
        GetModuleFileNameA(h, g_modDir, MAX_PATH);
        char *s = strrchr(g_modDir, '\\');
        if (s) *s = 0;
        snprintf(g_logPath, sizeof(g_logPath), "%s\\freecam.log", g_modDir);
        snprintf(g_iniPath,  sizeof(g_iniPath),  "%s\\freecam.ini", g_modDir);
        tlog("=== freecam.asi v0.16 ===");
        load_cfg();
        SetUnhandledExceptionFilter(exc_filter);   // v0.15: loga o crash
        patch_import("d3d9.dll", "Direct3DCreate9", (void*)Mine_Direct3DCreate9, (void**)&g_realCreate9);
        patch_import("d3dx9_43.dll", "D3DXCreateEffect", (void*)Mine_CreateEffect, (void**)&g_oCreateEffect);
        patch_import("d3dx9_43.dll", "D3DXMatrixPerspectiveFovLH", (void*)Mine_Persp, (void**)&g_oPersp);
        patch_import("d3dx9_43.dll", "D3DXMatrixMultiply", (void*)Mine_Mul, (void**)&g_oMul);
        HANDLE t = CreateThread(NULL, 0, hotkey_thread, NULL, 0, NULL);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
