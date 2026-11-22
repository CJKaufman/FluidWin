// Pila di cartella (come le "pile" del Dock del Mac): un clic sull'icona Download del dock apre sopra di lei un
// pannello di vetro con i file piu' recenti della cartella, in griglia, con anteprima (o icona) e nome.
// Clic su un file: si apre. Trascinandolo, si porta dove si vuole (Esplora file, desktop, una chat, un'email...):
// e' un trascinamento vero di Windows (IDataObject della shell). In fondo "Apri in Esplora file".
// Si chiude con Esc, con un clic fuori o perdendo il primo piano.
#pragma once
#include "vetro.h"
#include "menu.h"
#include <shobjidl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <windowsx.h>
#include <vector>
#include <string>
#include <algorithm>
#include <memory>
#include <thread>
#include <mutex>
#include <cwctype>

namespace pila {

inline bool chiediNomePila(HWND genitore, const std::wstring& titolo, const std::wstring& iniziale, std::wstring& out) {
    struct Prompt {
        vetro::Finestra v;
        std::wstring titolo, testo;
        size_t pos = 0, sel = 0;
        IDWriteTextFormat *fTitolo = nullptr, *fInput = nullptr, *fBtn = nullptr;
        int sopraBtn = 0, premutoBtn = 0;   // 1 = annulla, 2 = salva
        bool dragSel = false, cursoreVis = true, chiuso = false, confermato = false;
        float scrollX = 0;

        static std::wstring trim(const std::wstring& s) {
            size_t a = 0, b = s.size();
            while (a < b && iswspace(s[a])) a++;
            while (b > a && iswspace(s[b - 1])) b--;
            return s.substr(a, b - a);
        }
        bool valido() const { return !trim(testo).empty(); }

        D2D1_RECT_F rInput() const { const float d = v.dpi; return {20 * d, 50 * d, v.W - 20 * d, 88 * d}; }
        D2D1_RECT_F rSalva() const { const float d = v.dpi; return {v.W - 108 * d, v.H - 48 * d, v.W - 20 * d, v.H - 16 * d}; }
        D2D1_RECT_F rAnnulla() const { const float d = v.dpi; return {v.W - 206 * d, v.H - 48 * d, v.W - 118 * d, v.H - 16 * d}; }
        static bool dentro(const D2D1_RECT_F& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }

        int btnIn(float x, float y) const {
            if (dentro(rAnnulla(), x, y)) return 1;
            if (dentro(rSalva(), x, y)) return 2;
            return 0;
        }

        void eliminaSel() {
            if (pos == sel) return;
            const size_t a = std::min(pos, sel), b = std::max(pos, sel);
            testo.erase(a, b - a);
            pos = sel = a;
        }

        void assicuraCursore() {
            if (!fInput || !v.fdw) return;
            const float d = v.dpi;
            const D2D1_RECT_F ri = rInput();
            const float vw = (ri.right - ri.left) - 20 * d;
            const float cx = v.larghezza(testo.substr(0, pos), fInput);
            if (cx - scrollX > vw) scrollX = cx - vw;
            if (cx - scrollX < 0) scrollX = std::max(0.0f, cx - 8 * d);
        }

        size_t indiceDaX(float x) {
            if (!fInput || !v.fdw || testo.empty()) return 0;
            const float d = v.dpi;
            const D2D1_RECT_F ri = rInput();
            const float rx = x - (ri.left + 10 * d) + scrollX;
            IDWriteTextLayout* lay = nullptr;
            size_t idx = testo.size();
            if (SUCCEEDED(v.fdw->CreateTextLayout(testo.c_str(), (UINT32)testo.size(), fInput, 4000, 40 * d, &lay)) && lay) {
                BOOL trailing = FALSE, inside = FALSE;
                DWRITE_HIT_TEST_METRICS hm{};
                if (SUCCEEDED(lay->HitTestPoint(std::max(0.0f, rx), 10 * d, &trailing, &inside, &hm)))
                    idx = std::min<size_t>(testo.size(), hm.textPosition + (trailing ? 1 : 0));
                lay->Release();
            }
            return idx;
        }

        void copiaClipboard() {
            if (pos == sel) return;
            const size_t a = std::min(pos, sel), b = std::max(pos, sel);
            const std::wstring s = testo.substr(a, b - a);
            if (!OpenClipboard(v.fin)) return;
            EmptyClipboard();
            if (HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (s.size() + 1) * sizeof(wchar_t))) {
                if (wchar_t* p = (wchar_t*)GlobalLock(g)) { memcpy(p, s.c_str(), (s.size() + 1) * sizeof(wchar_t)); GlobalUnlock(g); }
                if (!SetClipboardData(CF_UNICODETEXT, g)) GlobalFree(g);
            }
            CloseClipboard();
        }

        void incollaClipboard() {
            if (!OpenClipboard(v.fin)) return;
            if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
                if (const wchar_t* p = (const wchar_t*)GlobalLock(h)) {
                    std::wstring ins;
                    for (; *p && ins.size() < 64; p++) {
                        if (*p == L'\r' || *p == L'\n') break;
                        if (*p >= 32) ins += *p;
                    }
                    GlobalUnlock(h);
                    if (!ins.empty()) {
                        eliminaSel();
                        const size_t spazio = testo.size() < 64 ? 64 - testo.size() : 0;
                        if (spazio > 0) {
                            const std::wstring t = ins.substr(0, spazio);
                            testo.insert(pos, t);
                            pos += t.size(); sel = pos;
                        }
                    }
                }
            }
            CloseClipboard();
        }

        void disegna() {
            if (!v.inizia(1.0f)) return;
            const float d = v.dpi;
            const D2D1_COLOR_F ink = v.inchiostro();

            ID2D1GradientStopCollection* stops = nullptr;
            const D2D1_GRADIENT_STOP gs[2] = {
                {0.0f, v.scuro ? D2D1::ColorF(1, 1, 1, 0.10f) : D2D1::ColorF(1, 1, 1, 0.36f)},
                {1.0f, D2D1::ColorF(1, 1, 1, 0.0f)}
            };
            if (SUCCEEDED(v.rt->CreateGradientStopCollection(gs, 2, &stops)) && stops) {
                ID2D1LinearGradientBrush* sheen = nullptr;
                if (SUCCEEDED(v.rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0, 0}, {0, v.H * 0.42f}), stops, &sheen)) && sheen) {
                    v.rt->FillRectangle({0, 0, (float)v.W, v.H * 0.42f}, sheen);
                    sheen->Release();
                }
                stops->Release();
            }

            const float rr = v.raggio();
            v.pen->SetColor(v.scuro ? D2D1::ColorF(1, 1, 1, 0.14f) : D2D1::ColorF(1, 1, 1, 0.65f));
            v.rt->DrawRoundedRectangle({{0.5f, 0.5f, v.W - 0.5f, v.H - 0.5f}, rr, rr}, v.pen, 1.0f);

            v.testo(titolo, fTitolo, {20 * d, 14 * d, v.W - 20 * d, 42 * d}, ink);

            const D2D1_RECT_F ri = rInput();
            v.pen->SetColor(v.scuro ? D2D1::ColorF(0, 0, 0, 0.28f) : D2D1::ColorF(1, 1, 1, 0.72f));
            v.rt->FillRoundedRectangle({ri, 9 * d, 9 * d}, v.pen);
            v.pen->SetColor(D2D1::ColorF(0.20f, 0.54f, 1.0f, 0.78f));
            v.rt->DrawRoundedRectangle({ri, 9 * d, 9 * d}, v.pen, 1.5f * d);

            const D2D1_RECT_F clip = {ri.left + 8 * d, ri.top + 2 * d, ri.right - 8 * d, ri.bottom - 2 * d};
            v.rt->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
            const float tx = ri.left + 10 * d - scrollX;
            if (pos != sel) {
                const size_t a = std::min(pos, sel), b = std::max(pos, sel);
                const float x1 = tx + v.larghezza(testo.substr(0, a), fInput);
                const float x2 = tx + v.larghezza(testo.substr(0, b), fInput);
                v.pen->SetColor(D2D1::ColorF(0.20f, 0.54f, 1.0f, v.scuro ? 0.42f : 0.28f));
                v.rt->FillRoundedRectangle({{x1, ri.top + 6 * d, x2, ri.bottom - 6 * d}, 3 * d, 3 * d}, v.pen);
            }
            if (testo.empty()) {
                v.testo(testi::t(L"Nome della pila…", L"Stack name…"), fInput, {ri.left + 10 * d, ri.top, ri.right - 10 * d, ri.bottom}, v.inchiostro(0.45f));
            } else {
                v.testo(testo, fInput, {tx, ri.top, tx + 4000, ri.bottom}, ink);
            }
            if (cursoreVis) {
                const float cx = tx + v.larghezza(testo.substr(0, pos), fInput);
                v.pen->SetColor(ink);
                v.rt->DrawLine({cx, ri.top + 8 * d}, {cx, ri.bottom - 8 * d}, v.pen, 1.4f * d);
            }
            v.rt->PopAxisAlignedClip();

            const D2D1_RECT_F ra = rAnnulla();
            const float rPill = (ra.bottom - ra.top) * 0.5f;
            v.pen->SetColor(v.scuro ? D2D1::ColorF(1, 1, 1, premutoBtn == 1 ? 0.18f : (sopraBtn == 1 ? 0.14f : 0.09f))
                                    : D2D1::ColorF(0, 0, 0, premutoBtn == 1 ? 0.12f : (sopraBtn == 1 ? 0.08f : 0.05f)));
            v.rt->FillRoundedRectangle({ra, rPill, rPill}, v.pen);
            v.testo(testi::t(L"Annulla", L"Cancel"), fBtn, ra, ink);

            const D2D1_RECT_F rs = rSalva();
            const bool ok = valido();
            const float aSalva = !ok ? 0.35f : (premutoBtn == 2 ? 0.80f : (sopraBtn == 2 ? 0.94f : 1.0f));
            v.pen->SetColor(D2D1::ColorF(0.04f, 0.52f, 1.0f, aSalva));
            v.rt->FillRoundedRectangle({rs, rPill, rPill}, v.pen);
            v.testo(testi::t(L"Salva", L"Save"), fBtn, rs, D2D1::ColorF(1, 1, 1, ok ? 1.0f : 0.65f));

            v.finisci();
        }

        bool messaggio(HWND, UINT m, WPARAM w, LPARAM l, LRESULT& r) {
            r = 0;
            switch (m) {
                case WM_TIMER:
                    if (w == 2) { cursoreVis = !cursoreVis; disegna(); }
                    return true;
                case WM_PAINT: disegna(); return true;
                case WM_CLOSE: chiuso = true; return true;
                case WM_NCHITTEST: {
                    POINT pt = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
                    ScreenToClient(v.fin, &pt);
                    const float x = (float)pt.x, y = (float)pt.y;
                    r = (dentro(rInput(), x, y) || btnIn(x, y) != 0) ? HTCLIENT : HTCAPTION;
                    return true;
                }
                case WM_MOUSEMOVE: {
                    const float x = (float)GET_X_LPARAM(l), y = (float)GET_Y_LPARAM(l);
                    if (dragSel) { pos = indiceDaX(x); assicuraCursore(); cursoreVis = true; disegna(); return true; }
                    const int sb = btnIn(x, y);
                    if (sb != sopraBtn) { sopraBtn = sb; disegna(); }
                    return true;
                }
                case WM_LBUTTONDBLCLK: {
                    const float x = (float)GET_X_LPARAM(l), y = (float)GET_Y_LPARAM(l);
                    if (dentro(rInput(), x, y)) { sel = 0; pos = testo.size(); cursoreVis = true; disegna(); }
                    return true;
                }
                case WM_LBUTTONDOWN: {
                    const float x = (float)GET_X_LPARAM(l), y = (float)GET_Y_LPARAM(l);
                    SetCapture(v.fin);
                    if (dentro(rInput(), x, y)) {
                        pos = indiceDaX(x);
                        if (!(GetKeyState(VK_SHIFT) & 0x8000)) sel = pos;
                        dragSel = true; cursoreVis = true; assicuraCursore(); disegna();
                    } else {
                        premutoBtn = btnIn(x, y);
                        disegna();
                    }
                    return true;
                }
                case WM_LBUTTONUP: {
                    ReleaseCapture();
                    const float x = (float)GET_X_LPARAM(l), y = (float)GET_Y_LPARAM(l);
                    dragSel = false;
                    const int b = btnIn(x, y);
                    if (premutoBtn == 1 && b == 1) chiuso = true;
                    else if (premutoBtn == 2 && b == 2 && valido()) { confermato = true; chiuso = true; }
                    premutoBtn = 0; disegna();
                    return true;
                }
                case WM_KEYDOWN: {
                    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                    cursoreVis = true;
                    if (w == VK_ESCAPE) { chiuso = true; return true; }
                    if (w == VK_RETURN) { if (valido()) { confermato = true; chiuso = true; } return true; }
                    if (ctrl && w == 'A') { sel = 0; pos = testo.size(); assicuraCursore(); disegna(); return true; }
                    if (ctrl && w == 'C') { copiaClipboard(); return true; }
                    if (ctrl && w == 'X') { copiaClipboard(); eliminaSel(); assicuraCursore(); disegna(); return true; }
                    if (ctrl && w == 'V') { incollaClipboard(); assicuraCursore(); disegna(); return true; }
                    if (w == VK_BACK) {
                        if (pos != sel) eliminaSel();
                        else if (pos > 0) { testo.erase(pos - 1, 1); pos--; sel = pos; }
                        assicuraCursore(); disegna(); return true;
                    }
                    if (w == VK_DELETE) {
                        if (pos != sel) eliminaSel();
                        else if (pos < testo.size()) testo.erase(pos, 1);
                        assicuraCursore(); disegna(); return true;
                    }
                    if (w == VK_LEFT) { if (pos > 0) pos--; if (!shift) sel = pos; assicuraCursore(); disegna(); return true; }
                    if (w == VK_RIGHT) { if (pos < testo.size()) pos++; if (!shift) sel = pos; assicuraCursore(); disegna(); return true; }
                    if (w == VK_HOME) { pos = 0; if (!shift) sel = pos; assicuraCursore(); disegna(); return true; }
                    if (w == VK_END) { pos = testo.size(); if (!shift) sel = pos; assicuraCursore(); disegna(); return true; }
                    return true;
                }
                case WM_CHAR: {
                    const wchar_t ch = (wchar_t)w;
                    if (ch >= 32 && ch != 127) {
                        eliminaSel();
                        if (testo.size() < 64) { testo.insert(pos, 1, ch); pos++; sel = pos; }
                        cursoreVis = true; assicuraCursore(); disegna();
                    }
                    return true;
                }
            }
            return false;
        }
    } pr;

    pr.titolo = titolo;
    pr.testo = iniziale;
    pr.sel = 0;
    pr.pos = pr.testo.size();
    if (!pr.v.crea(L"FluidWin.NomePila", vetro::procedura<Prompt>, &pr, true)) return false;
    pr.v.aggiornaTema();
    const HMONITOR mon = genitore ? MonitorFromWindow(genitore, MONITOR_DEFAULTTONEAREST) : MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{}; mi.cbSize = sizeof(mi); GetMonitorInfoW(mon, &mi);
    pr.v.dpi = vetro::dpiMonitor(mon);
    const float d = pr.v.dpi;
    const int w = (int)std::lround(360 * d), h = (int)std::lround(154 * d);
    const int x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - w) / 2;
    const int y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - h) / 2;
    pr.v.posiziona(x, y, w, h, HWND_TOPMOST, SWP_SHOWWINDOW);
    pr.v.alfa(1.0);
    pr.fTitolo = pr.v.formato(15 * d, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    pr.fInput = pr.v.formato(13.5f * d, DWRITE_FONT_WEIGHT_NORMAL);
    pr.fBtn = pr.v.formato(13 * d, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_TEXT_ALIGNMENT_CENTER);
    if (pr.fInput) { DWRITE_TRIMMING tr{DWRITE_TRIMMING_GRANULARITY_NONE, 0, 0}; pr.fInput->SetTrimming(&tr, nullptr); }
    SetTimer(pr.v.fin, 2, 530, nullptr);
    SetForegroundWindow(pr.v.fin);
    SetFocus(pr.v.fin);
    pr.disegna();

    MSG msg;
    while (!pr.chiuso) {
        const BOOL gm = GetMessageW(&msg, nullptr, 0, 0);
        if (gm <= 0) { if (gm == 0) PostQuitMessage((int)msg.wParam); break; }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    KillTimer(pr.v.fin, 2);
    for (IDWriteTextFormat* f : {pr.fTitolo, pr.fInput, pr.fBtn}) if (f) f->Release();
    pr.v.chiudi();
    if (pr.confermato) { out = Prompt::trim(pr.testo); return !out.empty(); }
    return false;
}

// fonte del trascinamento: finisce al rilascio del tasto, si annulla con Esc
class Fonte : public IDropSource {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r, void** p) override {
        if (r == IID_IUnknown || r == IID_IDropSource) { *p = this; return S_OK; }
        *p = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL esc, DWORD tasti) override {
        if (esc) return DRAGDROP_S_CANCEL;
        if (!(tasti & MK_LBUTTON)) return DRAGDROP_S_DROP;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }
};

class Pila {
public:
    bool aperta() const { return stato != 0; }
    bool apertaPer(const std::wstring& cartella) const { return stato != 0 && !_wcsicmp(dir.c_str(), cartella.c_str()); }

    // icona: il rettangolo dell'icona nel dock (schermo); cartella: il percorso; latoDock: lato del dock (0 basso, 1 alto,
    // 2 sinistra, 3 destra), la pila si apre dal suo lato interno
    void apri(const RECT& icona, const std::wstring& cartella, const std::wstring& nome, int latoDock = 0,
              bool perNome = false, bool modificabile = false) {
        if (!v.fin && !v.crea(L"FluidWin.Pila", vetro::procedura<Pila>, this, true)) return;
        dir = cartella; titolo = nome;
        ordinaPerNome = perNome; puoModificare = modificabile;
        scrollY = 0;
        v.aggiornaTema();
        v.dpi = vetro::dpiMonitor(MonitorFromRect(&icona, MONITOR_DEFAULTTONEAREST));
        const float d = v.dpi;
        leggi();
        const int righe = std::max(1, (int)std::min<size_t>(3, (file.size() + COLONNE - 1) / COLONNE));
        const int w = (int)std::lround((COLONNE * CELLA + 24) * d), h = (int)std::lround((48 + righe * ALTA + 50) * d);
        MONITORINFO mi{}; mi.cbSize = sizeof(mi); GetMonitorInfoW(MonitorFromRect(&icona, MONITOR_DEFAULTTONEAREST), &mi);
        // dal lato interno del dock: sopra l'icona (dock in basso), sotto (in alto), a destra o a sinistra (ai lati)
        const int st = (int)std::lround(14 * d);
        // (schermo piu' stretto della pila: limite alto mai sotto quello basso, std::clamp altrimenti non e' definito, S7)
        x = std::clamp((int)((icona.left + icona.right) / 2 - w / 2), (int)mi.rcWork.left + 8, std::max((int)mi.rcWork.left + 8, (int)mi.rcWork.right - w - 8));
        y = icona.top - st - h; sx = 0; sy = 1;
        if (latoDock == 1) { y = icona.bottom + st; sy = -1; }
        else if (latoDock >= 2) {
            x = latoDock == 2 ? icona.right + st : icona.left - st - w; sx = latoDock == 2 ? -1 : 1; sy = 0;
            y = std::clamp((int)((icona.top + icona.bottom) / 2 - h / 2), (int)mi.rcWork.top + 8, std::max((int)mi.rcWork.top + 8, (int)mi.rcWork.bottom - h - 8));
        }
        if (y < mi.rcMonitor.top + 8) y = mi.rcMonitor.top + 8;
        if (y + h > mi.rcMonitor.bottom - 8) y = std::max((int)mi.rcMonitor.top + 8, (int)mi.rcMonitor.bottom - 8 - h);
        v.posiziona(x + sx * st, y + sy * st, w, h, HWND_TOPMOST, SWP_SHOWWINDOW);
        // (rifatti se cambia lo schermo e la sua scala: prima restavano della prima apertura, S18)
        if (fTitolo && fDpi != d) { for (IDWriteTextFormat** f : {&fTitolo, &fNome, &fLink}) if (*f) { (*f)->Release(); *f = nullptr; } }
        if (!fTitolo) { fTitolo = v.formato(15 * d, DWRITE_FONT_WEIGHT_SEMI_BOLD); fNome = v.formato(11.5f * d, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER); fLink = v.formato(12.5f * d, DWRITE_FONT_WEIGHT_MEDIUM); fDpi = d; }
        caricaAnteprime();
        sopra = premuto = -1;
        stato = 1; t0 = vetro::ms(); k = 0;
        SetForegroundWindow(v.fin);
        vetro::battito(v.fin, 1);
        disegna();
    }

    void chiudi() { if (stato == 1 || stato == 2) { stato = 3; t0 = vetro::ms(); vetro::battito(v.fin, 1); } }

    bool messaggio(HWND, UINT m, WPARAM w, LPARAM l, LRESULT& r) {
        r = 0;
        switch (m) {
            case WM_TIMER: if (w == 1) passo(); return true;
            case WM_APP + 1: raccogli(); return true;   // anteprime pronte
            case WM_PAINT: disegna(); return true;
            case WM_ACTIVATE: if (LOWORD(w) == WA_INACTIVE && !trascinando && !menuAperto) chiudi(); return true;
            case WM_KEYDOWN: if (w == VK_ESCAPE) chiudi(); return true;
            case WM_CLOSE: chiudi(); return true;
            case WM_MOUSEWHEEL: {
                const float sm = scrollMax();
                if (sm > 0) {
                    const int delta = GET_WHEEL_DELTA_WPARAM(w);
                    scrollY = std::clamp(scrollY - (delta / 120.0f) * ALTA * 0.55f * v.dpi, 0.0f, sm);
                    POINT pt; GetCursorPos(&pt); ScreenToClient(v.fin, &pt);
                    sopra = cellaIn(pt.x, pt.y);
                    disegna();
                }
                return true;
            }
            case WM_MOUSEMOVE: {
                const int px = GET_X_LPARAM(l), py = GET_Y_LPARAM(l);
                if (!dentro) { TRACKMOUSEEVENT tm{sizeof(tm), TME_LEAVE, v.fin, 0}; TrackMouseEvent(&tm); dentro = true; }
                // trascinamento di un file fuori dal pannello
                if (premuto >= 0 && premuto < (int)file.size() && (w & MK_LBUTTON) && (std::abs(px - px0) > 5 || std::abs(py - py0) > 5)) {
                    trascina(premuto); premuto = -1; return true;
                }
                const int s = cellaIn(px, py);
                if (s != sopra) { sopra = s; disegna(); }
                return true;
            }
            case WM_MOUSELEAVE: dentro = false; sopra = -1; disegna(); return true;
            case WM_LBUTTONDOWN: premuto = cellaIn(GET_X_LPARAM(l), GET_Y_LPARAM(l)); px0 = GET_X_LPARAM(l); py0 = GET_Y_LPARAM(l); SetCapture(v.fin); disegna(); return true;
            case WM_LBUTTONUP: {
                ReleaseCapture();
                const int c = cellaIn(GET_X_LPARAM(l), GET_Y_LPARAM(l));
                if (c >= 0 && c == premuto) {
                    const std::wstring p = c == LINK ? dir : file[c].percorso;
                    chiudi();
                    ShellExecuteW(nullptr, L"open", p.c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL);
                }
                premuto = -1; disegna();
                return true;
            }
            case WM_RBUTTONUP: {
                const int c = cellaIn(GET_X_LPARAM(l), GET_Y_LPARAM(l));
                if (c == LINK || c < 0 || c >= (int)file.size()) return true;
                const std::wstring nomeItem = file[c].nome, percItem = file[c].percorso;
                std::vector<menu::Voce> vv = {
                    menu::titolo(nomeItem),
                    menu::voce(testi::t(L"Apri", L"Open"), 1),
                    menu::voce(testi::t(L"Apri percorso file", L"Open file location"), 2)
                };
                if (puoModificare) {
                    vv.push_back(menu::separatore());
                    vv.push_back(menu::voce(testi::t(L"Rimuovi dalla pila", L"Remove from stack"), 3));
                }
                POINT pt = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
                ClientToScreen(v.fin, &pt);
                menuAperto = true;
                const UINT s = menu::mostra(vv, pt, v.fin);
                menuAperto = false;
                if (s == 1) {
                    chiudi();
                    ShellExecuteW(nullptr, L"open", percItem.c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL);
                } else if (s == 2) {
                    chiudi();
                    ShellExecuteW(nullptr, L"open", L"explorer.exe", (L"/select,\"" + percItem + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
                } else if (s == 3 && puoModificare) {
                    if (eCollegamento(percItem)) {
                        DeleteFileW(percItem.c_str());
                    } else {
                        std::wstring from = percItem;
                        from.push_back(L'\0'); from.push_back(L'\0');
                        SHFILEOPSTRUCTW op{}; op.wFunc = FO_DELETE; op.pFrom = from.c_str();
                        op.fFlags = FOF_ALLOWUNDO | FOF_SILENT | FOF_NOCONFIRMATION;
                        SHFileOperationW(&op);
                    }
                    leggi();
                    scrollY = std::clamp(scrollY, 0.0f, scrollMax());
                    caricaAnteprime();
                    sopra = premuto = -1;
                    SetForegroundWindow(v.fin);
                    disegna();
                } else if (stato != 0) {
                    SetForegroundWindow(v.fin);
                }
                return true;
            }
        }
        return false;
    }

private:
    static constexpr int COLONNE = 4, LINK = 1000;
    static constexpr float CELLA = 92, ALTA = 100;
    struct File { std::wstring nome, percorso; bool cartella = false; ID2D1Bitmap* anteprima = nullptr; };
    vetro::Finestra v;
    std::vector<File> file;
    std::wstring dir, titolo;
    IDWriteTextFormat *fTitolo = nullptr, *fNome = nullptr, *fLink = nullptr;
    float fDpi = 0;   // scala con cui sono stati fatti i tre formati
    int stato = 0;   // 0 chiusa, 1 entra, 2 aperta, 3 esce
    ULONGLONG t0 = 0; double k = 0;
    int x = 0, y = 0, sopra = -1, premuto = -1, px0 = 0, py0 = 0;
    int sx = 0, sy = 1;   // da dove scivola entrando (verso il dock): in basso di serie, in alto o ai lati col dock li'
    bool dentro = false, trascinando = false, menuAperto = false;
    bool ordinaPerNome = false, puoModificare = false;
    float scrollY = 0;
    Fonte fonte;

    static bool eCollegamento(const std::wstring& s) {
        if (s.size() <= 4) return false;
        const wchar_t* e = s.c_str() + s.size() - 4;
        return !_wcsicmp(e, L".lnk") || !_wcsicmp(e, L".url");
    }

    float scrollMax() const {
        const float d = v.dpi;
        const int righeTot = ((int)file.size() + COLONNE - 1) / COLONNE;
        const float vpH = (float)v.H - 46 * d - 48 * d;
        return std::max(0.0f, righeTot * ALTA * d - vpH);
    }

    void libera() { for (auto& f : file) if (f.anteprima) f.anteprima->Release(); file.clear(); }

    void leggi() {
        libera();
        std::vector<std::pair<ULONGLONG, File>> tutti;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
                if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;
                File f;
                f.nome = fd.cFileName;
                f.cartella = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                if (!f.cartella && eCollegamento(f.nome)) f.nome.resize(f.nome.size() - 4);
                f.percorso = dir + L"\\" + fd.cFileName;
                const ULONGLONG t = ((ULONGLONG)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
                const ULONGLONG c = ((ULONGLONG)fd.ftCreationTime.dwHighDateTime << 32) | fd.ftCreationTime.dwLowDateTime;
                tutti.push_back({std::max(t, c), std::move(f)});   // (un file scaricato puo' avere la data di modifica vecchia)
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        if (ordinaPerNome) {
            std::sort(tutti.begin(), tutti.end(), [](const auto& a, const auto& b) {
                if (a.second.cartella != b.second.cartella) return a.second.cartella;
                return lstrcmpiW(a.second.nome.c_str(), b.second.nome.c_str()) < 0;
            });
            for (auto& item : tutti) file.push_back(std::move(item.second));
        } else {
            std::sort(tutti.begin(), tutti.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            for (size_t i = 0; i < tutti.size() && i < (size_t)COLONNE * 3; i++) file.push_back(std::move(tutti[i].second));
        }
    }

    // anteprime (foto, PDF, video) o icone, dalla shell: in un thread a parte (le anteprime possono costare centinaia
    // di millisecondi), il pannello compare subito e le immagini arrivano appena pronte (WM_APP + 1)
    struct Immagine { std::wstring percorso; std::vector<UINT> px; int w = 0, h = 0; };
    struct Lavoro { std::mutex mx; std::vector<Immagine> pronte; unsigned giro = 0; };
    std::shared_ptr<Lavoro> lavoro = std::make_shared<Lavoro>();
    unsigned giro = 0;

    void caricaAnteprime() {
        const int lato = (int)std::lround(56 * v.dpi);
        std::vector<std::wstring> percorsi; for (auto& f : file) percorsi.push_back(f.percorso);
        const unsigned g = ++giro;
        std::shared_ptr<Lavoro> l = lavoro;
        // il giro corrente lo decide il thread principale, qui: un thread di un'apertura precedente vede che non e'
        // piu' il suo e smette. Prima ogni thread rimetteva il proprio giro e svuotava la coda, anche quello vecchio
        // dopo il nuovo: le anteprime nuove sparivano (audit FW-AUDIT-021)
        { std::lock_guard<std::mutex> k(l->mx); l->giro = g; l->pronte.clear(); }
        HWND h = v.fin;
        std::thread([l, percorsi, lato, g, h] {
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            for (const auto& p : percorsi) {
                Immagine im; im.percorso = p;
                IShellItemImageFactory* fab = nullptr;
                if (SUCCEEDED(SHCreateItemFromParsingName(p.c_str(), nullptr, IID_PPV_ARGS(&fab)))) {
                    HBITMAP hb = nullptr;
                    if (SUCCEEDED(fab->GetImage({lato, lato}, SIIGBF_BIGGERSIZEOK, &hb)) && hb) {
                        BITMAP bm{}; GetObject(hb, sizeof(bm), &bm);
                        if (bm.bmWidth > 0 && bm.bmHeight > 0 && bm.bmWidth <= 2048 && bm.bmHeight <= 2048) {
                            im.px.resize((size_t)bm.bmWidth * bm.bmHeight); im.w = bm.bmWidth; im.h = bm.bmHeight;
                            BITMAPINFO bi{}; bi.bmiHeader = {sizeof(BITMAPINFOHEADER), bm.bmWidth, -bm.bmHeight, 1, 32, BI_RGB};
                            HDC scr = GetDC(nullptr); GetDIBits(scr, hb, 0, bm.bmHeight, im.px.data(), &bi, DIB_RGB_COLORS); ReleaseDC(nullptr, scr);
                            bool alfa = false; for (UINT x : im.px) if (x >> 24) { alfa = true; break; }
                            for (UINT& x : im.px) {
                                const UINT a = alfa ? (x >> 24) : 255;
                                x = (a << 24) | ((((x >> 16) & 255) * a / 255) << 16) | ((((x >> 8) & 255) * a / 255) << 8) | ((x & 255) * a / 255);
                            }
                        }
                        DeleteObject(hb);
                    }
                    fab->Release();
                }
                {
                    std::lock_guard<std::mutex> k(l->mx);
                    if (l->giro != g) break;   // c'e' un'apertura piu' nuova: questo lavoro non serve piu'
                    l->pronte.push_back(std::move(im));
                }
                PostMessageW(h, WM_APP + 1, 0, 0);
            }
            CoUninitialize();
        }).detach();
    }

    void raccogli() {
        std::vector<Immagine> arrivate;
        { std::lock_guard<std::mutex> k(lavoro->mx); if (lavoro->giro == giro) arrivate.swap(lavoro->pronte); }
        if (arrivate.empty() || !v.rt || stato == 0) return;
        for (auto& im : arrivate) {
            if (im.px.empty()) continue;
            for (auto& f : file) if (f.percorso == im.percorso && !f.anteprima)
                v.rt->CreateBitmap(D2D1::SizeU(im.w, im.h), im.px.data(), im.w * 4,
                                   D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)), &f.anteprima);
        }
        disegna();
    }

    D2D1_RECT_F rettCella(int i) const {
        const float d = v.dpi, l = 12 * d + (i % COLONNE) * CELLA * d, t = 48 * d + (i / COLONNE) * ALTA * d - scrollY;
        return {l, t, l + CELLA * d, t + ALTA * d};
    }
    D2D1_RECT_F rettLink() const { const float d = v.dpi; return {12 * d, v.H - 44 * d, v.W - 12 * d, v.H - 10 * d}; }

    int cellaIn(int px, int py) const {
        const D2D1_RECT_F l = rettLink();
        if (px >= l.left && px < l.right && py >= l.top && py < l.bottom) return LINK;
        const float d = v.dpi;
        if (py < 48 * d || py >= v.H - 46 * d) return -1;
        for (int i = 0; i < (int)file.size(); i++) { const D2D1_RECT_F r = rettCella(i); if (px >= r.left && px < r.right && py >= r.top && py < r.bottom) return i; }
        return -1;
    }

    void trascina(int i) {
        IShellItem* it = nullptr;
        if (FAILED(SHCreateItemFromParsingName(file[i].percorso.c_str(), nullptr, IID_PPV_ARGS(&it)))) return;
        IDataObject* dati = nullptr;
        if (SUCCEEDED(it->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&dati))) && dati) {
            ReleaseCapture();
            trascinando = true;
            ShowWindow(v.fin, SW_HIDE);   // (via dal mezzo: si vede dove lo si lascia)
            DWORD effetto = 0;
            DoDragDrop(dati, &fonte, DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK, &effetto);
            trascinando = false;
            dati->Release();
            stato = 0; vetro::fermaBattito(v.fin, 1); libera();
        }
        it->Release();
    }

    void passo() {
        const ULONGLONG ora = vetro::ms();
        if (stato == 1) { k = std::min(1.0, (ora - t0) / 200.0); if (k >= 1) { stato = 2; vetro::fermaBattito(v.fin, 1); } }
        else if (stato == 3) {
            k = 1 - std::min(1.0, (ora - t0) / 160.0);
            if (k <= 0) { stato = 0; vetro::fermaBattito(v.fin, 1); ShowWindow(v.fin, SW_HIDE); libera(); return; }
        } else return;
        const double e = stato == 3 ? vetro::uscita(k) : vetro::molla(k);
        const int sp = (int)std::lround((1 - e) * 14 * v.dpi);   // (scivola dal lato del dock)
        SetWindowPos(v.fin, HWND_TOPMOST, x + sx * sp, y + sy * sp, 0, 0, SWP_NOACTIVATE | SWP_NOSIZE);
        v.alfa(std::min(1.0, k * 1.5));
    }

    // render target ricreato (vetro::Finestra::giroRt): le anteprime del vecchio non valgono, si rifanno (FW-AUDIT-012)
    unsigned giroAnteprime = 0;

    void disegna() {
        if (stato == 0) return;
        if (giroAnteprime != v.giroRt) {
            giroAnteprime = v.giroRt;
            bool c = false;
            for (auto& f : file) if (f.anteprima) { f.anteprima->Release(); f.anteprima = nullptr; c = true; }
            if (c) caricaAnteprime();
        }
        if (!v.inizia()) return;
        const float d = v.dpi;
        const D2D1_COLOR_F ink = v.inchiostro();
        v.testo(titolo, fTitolo, {16 * d, 8 * d, v.W - 16 * d, 40 * d}, ink);
        if (file.empty()) v.testo(testi::t(L"La cartella è vuota", L"The folder is empty"), fNome, {12 * d, 48 * d, v.W - 12 * d, 120 * d}, v.inchiostro(0.6f));
        const D2D1_RECT_F vp = {0, 48 * d, (float)v.W, v.H - 46 * d};
        v.rt->PushAxisAlignedClip(vp, D2D1_ANTIALIAS_MODE_ALIASED);
        for (int i = 0; i < (int)file.size(); i++) {
            const D2D1_RECT_F r = rettCella(i);
            if (r.bottom < vp.top || r.top > vp.bottom) continue;
            if (i == sopra || i == premuto) {
                v.pen->SetColor(v.scuro ? D2D1::ColorF(1, 1, 1, i == premuto ? 0.16f : 0.09f) : D2D1::ColorF(0, 0, 0, i == premuto ? 0.10f : 0.05f));
                v.rt->FillRoundedRectangle({{r.left + 3 * d, r.top + 2 * d, r.right - 3 * d, r.bottom - 2 * d}, 10 * d, 10 * d}, v.pen);
            }
            const float cx = (r.left + r.right) / 2, l = 28 * d;
            if (ID2D1Bitmap* b = file[i].anteprima) {
                const D2D1_SIZE_F s = b->GetSize();
                const float f = std::min(2 * l / s.width, 2 * l / s.height), w = s.width * f, h = s.height * f;
                const float cy = r.top + 10 * d + l;
                v.rt->DrawBitmap(b, {cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2}, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            }
            v.testo(file[i].nome, fNome, {r.left + 4 * d, r.top + 72 * d, r.right - 4 * d, r.top + 92 * d}, ink);
        }
        v.rt->PopAxisAlignedClip();
        const float sm = scrollMax();
        if (sm > 0) {
            const float vpH = vp.bottom - vp.top, th = std::max(24 * d, vpH * vpH / (vpH + sm));
            const float ty = vp.top + (vpH - th) * (scrollY / sm);
            v.pen->SetColor(v.scuro ? D2D1::ColorF(1, 1, 1, 0.28f) : D2D1::ColorF(0, 0, 0, 0.22f));
            v.rt->FillRoundedRectangle({{v.W - 7 * d, ty + 2 * d, v.W - 3 * d, ty + th - 2 * d}, 2 * d, 2 * d}, v.pen);
        }
        const D2D1_RECT_F rl = rettLink();
        if (sopra == LINK) { v.pen->SetColor(v.scuro ? D2D1::ColorF(1, 1, 1, 0.09f) : D2D1::ColorF(0, 0, 0, 0.05f)); v.rt->FillRoundedRectangle({rl, 8 * d, 8 * d}, v.pen); }
        v.testo(testi::t(L"Apri in Esplora file", L"Open in File Explorer"), fLink, {rl.left + 8 * d, rl.top, rl.right, rl.bottom},
                v.scuro ? D2D1::ColorF(0.40f, 0.68f, 1.0f, 1) : D2D1::ColorF(0.0f, 0.42f, 0.95f, 1));
        v.finisci();
    }
};

}  // namespace pila
