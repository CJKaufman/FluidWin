// Finestre di controllo periferiche Attack Shark (RK86 RGB e mouse V6) in stile Liquid Glass di FluidWin.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "vetro.h"
#include "testi.h"
#include "attackshark.h"

namespace attackshark_ui {

namespace dettagli {

struct Zona {
    D2D1_RECT_F r;
    int id;
};

inline int zonaIn(const std::vector<Zona>& zone, float x, float y) {
    for (int i = (int)zone.size() - 1; i >= 0; i--) {
        const D2D1_RECT_F& r = zone[(size_t)i].r;
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return zone[(size_t)i].id;
    }
    return -1;
}

struct FormatiUi {
    IDWriteTextFormat *fTitolo = nullptr, *fVoce = nullptr, *fSotto = nullptr,
                      *fCentro = nullptr, *fPill = nullptr, *fBadge = nullptr;
    float fDpi = 0.0f;

    void libera() {
        for (IDWriteTextFormat** f : {&fTitolo, &fVoce, &fSotto, &fCentro, &fPill, &fBadge}) {
            if (*f) {
                (*f)->Release();
                *f = nullptr;
            }
        }
        fDpi = 0.0f;
    }

    void ricrea(vetro::Finestra& v, float d) {
        if (fTitolo && std::abs(fDpi - d) < 0.001f) return;
        libera();
        if (!v.fdw) return;
        fDpi = d;
        fTitolo = v.formato(18.5f * d, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        fVoce = v.formato(13.5f * d, DWRITE_FONT_WEIGHT_MEDIUM);
        fSotto = v.formato(11.5f * d);
        fCentro = v.formato(12.2f * d, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_TEXT_ALIGNMENT_CENTER);
        fPill = v.formato(11.0f * d, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
        fBadge = v.formato(12.0f * d, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_TRAILING);
    }
};

struct MorphAnim {
    static constexpr double DURATA_CRESCE = 240.0, DURATA_RIENTRA = 160.0;

    int morph = 0;
    bool rientra = false;
    double mx = 1.0, k = 1.0;
    ULONGLONG tM = 0, t0 = 0;
    RECT rDa{}, rA{};

    double forma() const {
        return rientra ? mx * mx * mx : 1.0 - std::pow(1.0 - mx, 3);
    }

    void gira(int dir) {
        const double e = forma();
        morph = dir;
        rientra = dir < 0;
        tM = vetro::ms();
        mx = rientra ? std::cbrt(e) : 1.0 - std::cbrt(1.0 - e);
    }

    void applicaForma(vetro::Finestra& v) const {
        const double e = forma();
        auto mix = [e](LONG a, LONG b) { return (int)std::lround(a + (b - a) * e); };
        const int x = mix(rDa.left, rA.left);
        const int y = mix(rDa.top, rA.top);
        const int w = std::max(1, mix(rDa.right, rA.right) - x);
        const int h = std::max(1, mix(rDa.bottom, rA.bottom) - y);
        v.posiziona(x, y, w, h, nullptr, SWP_NOZORDER);
    }

    void preparaApertura(vetro::Finestra& v, float largo, float alto) {
        RECT a = vetro::areaPrincipale(true);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            a = mi.rcWork;
        }
        POINT mouse{};
        const bool haMouse = GetCursorPos(&mouse) != FALSE;
        if (haMouse) {
            MONITORINFO miM{};
            miM.cbSize = sizeof(miM);
            const HMONITOR monM = MonitorFromPoint(mouse, MONITOR_DEFAULTTONEAREST);
            if (GetMonitorInfoW(monM, &miM)) a = miM.rcWork;
        }

        v.dpi = vetro::dpiMonitor(MonitorFromPoint({a.left, a.top}, MONITOR_DEFAULTTOPRIMARY));
        const float d = v.dpi;
        const int w = (int)std::lround(std::min(largo * d, (float)(a.right - a.left) - 40 * d));
        const int h = (int)std::lround(std::min(alto * d, (float)(a.bottom - a.top) - 40 * d));
        const int x = (a.left + a.right - w) / 2;
        const int y = (a.top + a.bottom - h) / 2;
        rA = {x, y, x + w, y + h};

        const int pw = std::max(1, w / 10);
        const int ph = std::max(1, h / 10);
        const POINT orig = haMouse ? mouse : POINT{(rA.left + rA.right) / 2, (rA.top + rA.bottom) / 2};
        const int px = std::clamp((int)orig.x - pw / 2, (int)a.left, std::max((int)a.left, (int)a.right - pw));
        const int py = std::clamp((int)orig.y - ph / 2, (int)a.top, std::max((int)a.top, (int)a.bottom - ph));
        rDa = {px, py, px + pw, py + ph};

        morph = 1;
        mx = 0;
        rientra = false;
        tM = vetro::ms();
        t0 = tM;
        k = 0;
    }

    bool avanza(vetro::Finestra& v, bool chiudendo, bool& animAttiva) {
        const ULONGLONG ora = vetro::ms();
        if (morph != 0) {
            const double dt = (double)std::min<ULONGLONG>(ora - tM, 50);
            tM = ora;
            const double dur = morph > 0 ? DURATA_CRESCE : DURATA_RIENTRA;
            mx = std::clamp(mx + morph * dt / dur, 0.0, 1.0);
            applicaForma(v);
            if (morph < 0 && (mx <= 0.0 || forma() < 0.01)) {
                return true;
            }
            if (morph > 0 && mx >= 1.0) {
                morph = 0;
            }
        }
        k = std::min(1.0, (ora - t0) / 180.0);
        v.alfa(chiudendo ? std::min(1.0, forma() * 2.0) : vetro::uscita(k));
        animAttiva = !(k >= 1.0 && morph == 0 && !chiudendo);
        return false;
    }

    void resetChiuso() {
        morph = 0;
        mx = 1.0;
        rientra = false;
    }
};

inline bool iniziaLayerMorph(vetro::Finestra& v, const MorphAnim& anim, float& W, float& H) {
    if (anim.morph == 0 || anim.rA.right <= anim.rA.left || anim.rA.bottom <= anim.rA.top) return false;
    const float fw = (float)(anim.rA.right - anim.rA.left);
    const float fh = (float)(anim.rA.bottom - anim.rA.top);
    const float s = std::min({1.0f, W / fw, H / fh});
    const float x = (float)(anim.rientra ? std::clamp((anim.mx - 0.35) / 0.5, 0.0, 1.0)
                                         : std::clamp((anim.forma() - 0.55) / 0.4, 0.0, 1.0));
    const float op = x * x * (3.0f - 2.0f * x);
    v.rt->SetTransform(D2D1::Matrix3x2F::Scale(s, s) *
                       D2D1::Matrix3x2F::Translation((W - fw * s) / 2.0f, (H - fh * s) / 2.0f));
    v.rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr,
                                          D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                          D2D1::IdentityMatrix(), op),
                    nullptr);
    W = fw;
    H = fh;
    return true;
}

inline void disegnaHeaderComune(vetro::Finestra& v, const FormatiUi& fmt, float W, float L, float R, float d,
                                const std::wstring& titolo, const std::wstring& statoConn, bool connesso,
                                int zChiudi, int sopra, int premuto, std::vector<Zona>& zone,
                                D2D1_COLOR_F ink, D2D1_COLOR_F tenue) {
    const float titW = v.larghezza(titolo, fmt.fTitolo);
    v.testo(titolo, fmt.fTitolo, {L + 34.0f * d, 14.0f * d, L + 34.0f * d + titW + 4.0f * d, 48.0f * d}, ink);

    const D2D1_RECT_F rc = {W - 48.0f * d, 16.0f * d, W - 20.0f * d, 44.0f * d};
    zone.push_back({rc, zChiudi});
    const float alpha = (premuto == zChiudi) ? 0.22f
                      : (sopra == zChiudi)   ? (v.scuro ? 0.16f : 0.11f)
                                             : (v.scuro ? 0.08f : 0.05f);
    v.pen->SetColor(v.scuro ? D2D1::ColorF(1, 1, 1, alpha) : D2D1::ColorF(0, 0, 0, alpha));
    const float cx = (rc.left + rc.right) * 0.5f;
    const float cy = (rc.top + rc.bottom) * 0.5f;
    v.rt->FillEllipse({{cx, cy}, 14.0f * d, 14.0f * d}, v.pen);
    v.pen->SetColor(ink);
    v.rt->DrawLine({cx - 4.2f * d, cy - 4.2f * d}, {cx + 4.2f * d, cy + 4.2f * d}, v.pen, 1.6f * d);
    v.rt->DrawLine({cx + 4.2f * d, cy - 4.2f * d}, {cx - 4.2f * d, cy + 4.2f * d}, v.pen, 1.6f * d);

    std::wstring pillTxt = statoConn;
    if (pillTxt.empty()) {
        pillTxt = connesso ? testi::t(L"Connesso", L"Connected")
                           : testi::t(L"Non connesso", L"Not connected");
    }
    const float px0 = L + 34.0f * d + titW + 10.0f * d;
    const float maxPw = std::max(0.0f, (W - 58.0f * d) - px0);
    const float pw = std::min(v.larghezza(pillTxt, fmt.fPill) + 18.0f * d, maxPw);
    if (pw >= 28.0f * d) {
        D2D1_COLOR_F pBg = v.scuro ? D2D1::ColorF(1, 1, 1, 0.10f) : D2D1::ColorF(0, 0, 0, 0.06f);
        D2D1_COLOR_F pFg = tenue;
        if (connesso) {
            const D2D1_COLOR_F verde = v.scuro ? D2D1::ColorF(0.24f, 0.84f, 0.44f, 1.0f)
                                               : D2D1::ColorF(0.10f, 0.62f, 0.28f, 1.0f);
            pBg = D2D1::ColorF(verde.r, verde.g, verde.b, v.scuro ? 0.18f : 0.14f);
            pFg = verde;
        }
        const D2D1_RECT_F rp = {px0, 21.0f * d, px0 + pw, 41.0f * d};
        v.pen->SetColor(pBg);
        v.rt->FillRoundedRectangle({rp, 10.0f * d, 10.0f * d}, v.pen);
        v.testo(pillTxt, fmt.fPill, rp, pFg);
    }

    v.pen->SetColor(v.scuro ? D2D1::ColorF(1, 1, 1, 0.08f) : D2D1::ColorF(0, 0, 0, 0.08f));
    v.rt->DrawLine({L, 60.0f * d}, {R, 60.0f * d}, v.pen, 1.0f);
}

inline void disegnaSliderIosCard(vetro::Finestra& v, const FormatiUi& fmt, float L, float R, float& y,
                                 float cardH, float gapAfter, float trackBottomOffset, float d, float rr,
                                 const std::wstring& titolo, const std::wstring& valStr, float frac,
                                 int zid, bool dragging, bool hovered, D2D1_RECT_F& outTrack,
                                 std::vector<Zona>& zone, D2D1_COLOR_F ink, D2D1_COLOR_F iosBlu,
                                 D2D1_COLOR_F cardBg, D2D1_COLOR_F cardHover) {
    const D2D1_RECT_F ra = {L, y, R, y + cardH};
    v.pen->SetColor((dragging || hovered) ? cardHover : cardBg);
    v.rt->FillRoundedRectangle({ra, rr, rr}, v.pen);

    const std::wstring valDisp = testi::isola(valStr);
    const float valW = v.larghezza(valDisp, fmt.fBadge) + 4.0f * d;
    const float valLeft = std::max(ra.left + 16 * d, ra.right - 16 * d - valW);
    const D2D1_COLOR_F valCol = (dragging || hovered)
        ? iosBlu
        : D2D1::ColorF(ink.r, ink.g, ink.b, 0.78f);

    v.testo(titolo, fmt.fVoce, {ra.left + 16 * d, ra.top + 10 * d, std::max(ra.left + 16 * d, valLeft - 8 * d), ra.top + 32 * d}, ink);
    v.testo(valDisp, fmt.fBadge, {valLeft, ra.top + 10 * d, ra.right - 16 * d, ra.top + 32 * d}, valCol);

    const float trackL = ra.left + 16 * d;
    const float trackR = ra.right - 16 * d;
    const float trackH = 4.5f * d;
    const float trackCy = ra.bottom - trackBottomOffset;
    const float trackTop = trackCy - trackH * 0.5f;
    const float trackBot = trackCy + trackH * 0.5f;
    outTrack = {trackL, trackTop, trackR, trackBot};
    zone.push_back({{ra.left, std::max(ra.top, trackCy - 18 * d), ra.right, std::min(ra.bottom, trackCy + 18 * d)}, zid});

    const float thumbX = trackL + (trackR - trackL) * std::clamp(frac, 0.0f, 1.0f);

    v.pen->SetColor(v.scuro ? D2D1::ColorF(1, 1, 1, 0.16f) : D2D1::ColorF(0, 0, 0, 0.12f));
    v.rt->FillRoundedRectangle({{trackL, trackTop, trackR, trackBot}, trackH * 0.5f, trackH * 0.5f}, v.pen);
    if (thumbX > trackL + 0.5f) {
        v.pen->SetColor(iosBlu);
        v.rt->FillRoundedRectangle({{trackL, trackTop, thumbX, trackBot}, trackH * 0.5f, trackH * 0.5f}, v.pen);
    }

    const float thumbR = (dragging ? 10.5f : 9.5f) * d;
    v.pen->SetColor(D2D1::ColorF(0, 0, 0, 0.08f));
    v.rt->FillEllipse({{thumbX, trackCy + 1.5f * d}, thumbR + 3.0f * d, thumbR + 3.0f * d}, v.pen);
    v.pen->SetColor(D2D1::ColorF(0, 0, 0, v.scuro ? 0.34f : 0.18f));
    v.rt->FillEllipse({{thumbX, trackCy + 1.5f * d}, thumbR + 1.5f * d, thumbR + 1.5f * d}, v.pen);
    v.pen->SetColor(D2D1::ColorF(1, 1, 1, 1));
    v.rt->FillEllipse({{thumbX, trackCy}, thumbR, thumbR}, v.pen);
    v.pen->SetColor(v.scuro ? D2D1::ColorF(1, 1, 1, 0.25f) : D2D1::ColorF(0, 0, 0, 0.12f));
    v.rt->DrawEllipse({{thumbX, trackCy}, thumbR, thumbR}, v.pen, 1.0f);

    y += cardH + gapAfter;
}

inline void disegnaPillolaFooter(vetro::Finestra& v, const FormatiUi& fmt, const D2D1_RECT_F& rb, float btnH,
                                 int zid, const std::wstring& txt, bool accent, int sopra, int premuto,
                                 D2D1_COLOR_F ink, D2D1_COLOR_F iosBlu, std::vector<Zona>& zone) {
    zone.push_back({rb, zid});
    if (accent) {
        const float a = (premuto == zid) ? 1.0f : (sopra == zid) ? 0.96f : 0.86f;
        v.pen->SetColor(D2D1::ColorF(iosBlu.r, iosBlu.g, iosBlu.b, a));
        v.rt->FillRoundedRectangle({rb, btnH * 0.5f, btnH * 0.5f}, v.pen);
        v.testo(txt, fmt.fCentro, rb, D2D1::ColorF(1, 1, 1, 1));
    } else {
        const float a = (premuto == zid) ? (v.scuro ? 0.20f : 0.14f)
                      : (sopra == zid)   ? (v.scuro ? 0.15f : 0.10f)
                                         : (v.scuro ? 0.08f : 0.05f);
        v.pen->SetColor(v.scuro ? D2D1::ColorF(1, 1, 1, a) : D2D1::ColorF(0, 0, 0, a));
        v.rt->FillRoundedRectangle({rb, btnH * 0.5f, btnH * 0.5f}, v.pen);
        v.pen->SetColor(v.scuro ? D2D1::ColorF(1, 1, 1, 0.10f) : D2D1::ColorF(0, 0, 0, 0.08f));
        v.rt->DrawRoundedRectangle({rb, btnH * 0.5f, btnH * 0.5f}, v.pen, 1.0f);
        v.testo(txt, fmt.fCentro, rb, ink);
    }
}

}  // namespace dettagli

class Finestra {
public:
    ~Finestra() { fmt.libera(); }

    void apri(const std::wstring& statoConnessione) {
        if (!v.fin && !v.crea(L"FluidWin.AttackSharkRK86", vetro::procedura<Finestra>, this, true, 2, true, false)) {
            return;
        }
        SetWindowTextW(v.fin, L"Attack Shark RK86");
        statoConn = statoConnessione;
        led = attackshark::LedStato{};
        connesso = attackshark::leggiLedRK86(led);
        v.aggiornaTema();

        if (apertaFlag && !chiudendo) {
            if (anim.morph < 0) {
                anim.gira(1);
                vetro::battito(v.fin, 1);
            }
            SetForegroundWindow(v.fin);
            disegna();
            return;
        }

        anim.preparaApertura(v, LARGO, ALTO);
        chiudendo = false;
        sopra = -1;
        premuto = -1;
        dragSlider = -1;
        dentro = false;
        apertaFlag = true;

        fmt.ricrea(v, v.dpi);
        v.posiziona(anim.rDa.left, anim.rDa.top,
                    anim.rDa.right - anim.rDa.left, anim.rDa.bottom - anim.rDa.top,
                    HWND_TOP, SWP_SHOWWINDOW);
        v.alfa(0);
        vetro::battito(v.fin, 1);
        SetForegroundWindow(v.fin);
        disegna();
    }

    void chiudi() {
        nascondi();
        fmt.libera();
        v.chiudi();
    }

    bool aperta() const { return apertaFlag; }

    void aggiornaTema() {
        if (!v.fin) return;
        v.aggiornaTema();
        if (apertaFlag) disegna();
    }

    bool messaggio(HWND, UINT m, WPARAM w, LPARAM l, LRESULT& r) {
        r = 0;
        switch (m) {
            case WM_TIMER:
                if (w == 1) passoAnimazione();
                return true;
            case WM_PAINT:
                disegna();
                return true;
            case WM_MOUSEMOVE: {
                if (anim.morph || chiudendo) return true;
                const float x = (float)GET_X_LPARAM(l);
                const float y = (float)GET_Y_LPARAM(l);
                if (dragSlider >= 0) {
                    aggiornaSlider(dragSlider, x);
                    return true;
                }
                if (!dentro) {
                    TRACKMOUSEEVENT tm{sizeof(tm), TME_LEAVE, v.fin, 0};
                    TrackMouseEvent(&tm);
                    dentro = true;
                }
                const int s = dettagli::zonaIn(zone, x, y);
                if (s != sopra) {
                    sopra = s;
                    disegna();
                }
                return true;
            }
            case WM_MOUSELEAVE:
                dentro = false;
                if (dragSlider < 0) {
                    sopra = -1;
                    if (!chiudendo) disegna();
                }
                return true;
            case WM_LBUTTONDOWN: {
                if (anim.morph || chiudendo || dialogAperto) return true;
                const float x = (float)GET_X_LPARAM(l);
                const float y = (float)GET_Y_LPARAM(l);
                const int z = dettagli::zonaIn(zone, x, y);
                if (z == Z_SLIDER_LUM) {
                    dragSlider = 0;
                    premuto = z;
                    SetCapture(v.fin);
                    aggiornaSlider(0, x);
                    disegna();
                } else if (z == Z_SLIDER_VEL) {
                    dragSlider = 1;
                    premuto = z;
                    SetCapture(v.fin);
                    aggiornaSlider(1, x);
                    disegna();
                } else {
                    premuto = z;
                    disegna();
                }
                return true;
            }
            case WM_LBUTTONUP: {
                if (dragSlider >= 0) {
                    dragSlider = -1;
                    premuto = -1;
                    ReleaseCapture();
                    disegna();
                    return true;
                }
                if (anim.morph || chiudendo || dialogAperto) {
                    premuto = -1;
                    return true;
                }
                const int p = premuto;
                premuto = -1;
                const int z = dettagli::zonaIn(zone, (float)GET_X_LPARAM(l), (float)GET_Y_LPARAM(l));
                if (z >= 0 && z == p) clic(z);
                else disegna();
                return true;
            }
            case WM_CAPTURECHANGED:
                if (dragSlider >= 0) {
                    dragSlider = -1;
                    premuto = -1;
                    if (apertaFlag) disegna();
                }
                return true;
            case WM_KEYDOWN:
                if (w == VK_ESCAPE) avviaChiusura();
                return true;
            case WM_CLOSE:
                avviaChiusura();
                return true;
            case WM_NCHITTEST: {
                if (anim.morph || chiudendo) return false;
                POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
                ScreenToClient(v.fin, &p);
                const float d = v.dpi;
                if (p.y >= 0 && p.y < 60 * d && dettagli::zonaIn(zone, (float)p.x, (float)p.y) < 0) {
                    r = HTCAPTION;
                    return true;
                }
                return false;
            }
        }
        return false;
    }

private:
    static constexpr float LARGO = 560.0f, ALTO = 510.0f;

    enum {
        Z_CHIUDI = 100,
        Z_MODO_BASE = 200,
        Z_SLIDER_LUM = 300,
        Z_SLIDER_VEL = 301,
        Z_COL_ARCOBALENO = 400,
        Z_COL_PRESET = 401,
        Z_COL_CUSTOM = 420,
        Z_BTN_AGGIORNA = 500,
        Z_BTN_GEARHUB = 501,
        Z_BTN_SOFTWARE = 502,
    };

    struct ModoVoce {
        int modo;
        const wchar_t* it;
        const wchar_t* en;
    };
    static constexpr ModoVoce MODI[] = {
        {0, L"Spento",       L"Off"},
        {1, L"Statico",      L"Static"},
        {2, L"Respiro",      L"Breathing"},
        {3, L"Spettro",      L"Spectrum"},
        {4, L"Onda",         L"Wave"},
        {5, L"Increspatura", L"Ripple"},
        {6, L"Pioggia",      L"Raindrop"},
        {8, L"Reattivo",     L"Reactive"},
    };
    static constexpr int N_MODI = (int)(sizeof(MODI) / sizeof(MODI[0]));

    struct ColorePreset {
        uint8_t r, g, b;
    };
    static constexpr ColorePreset COLORI[] = {
        {0xFA, 0xFA, 0xFA},   // White
        {0xFF, 0x00, 0xEA},   // Magenta
        {0xFF, 0x2D, 0x20},   // Red
        {0xFF, 0x9F, 0x0A},   // Orange
        {0xFF, 0xD6, 0x0A},   // Yellow
        {0x30, 0xD1, 0x58},   // Green
        {0x32, 0xD2, 0xFF},   // Cyan
        {0x0A, 0x84, 0xFF},   // Blue
        {0xBF, 0x5A, 0xF2},   // Purple
    };
    static constexpr int N_COLORI = (int)(sizeof(COLORI) / sizeof(COLORI[0]));

    vetro::Finestra v;
    dettagli::FormatiUi fmt;
    dettagli::MorphAnim anim;

    bool apertaFlag = false;
    bool chiudendo = false;
    bool dentro = false;
    bool dialogAperto = false;
    bool connesso = false;

    int sopra = -1;
    int premuto = -1;
    int dragSlider = -1;
    D2D1_RECT_F trackLum{}, trackVel{};
    std::vector<dettagli::Zona> zone;

    std::wstring statoConn;
    attackshark::LedStato led{};

    void avviaChiusura() {
        if (!apertaFlag) return;
        if (dragSlider >= 0) {
            dragSlider = -1;
            ReleaseCapture();
        }
        premuto = -1;
        if (chiudendo) return;
        if (anim.morph == 0 && v.fin) GetWindowRect(v.fin, &anim.rA);
        chiudendo = true;
        anim.gira(-1);
        vetro::battito(v.fin, 1);
    }

    void nascondi() {
        if (dragSlider >= 0) {
            dragSlider = -1;
            ReleaseCapture();
        }
        if (v.fin) {
            vetro::fermaBattito(v.fin, 1);
            ShowWindow(v.fin, SW_HIDE);
        }
        apertaFlag = false;
        chiudendo = false;
        anim.resetChiuso();
        sopra = -1;
        premuto = -1;
        dentro = false;
    }

    void passoAnimazione() {
        if (!apertaFlag) {
            vetro::fermaBattito(v.fin, 1);
            return;
        }
        bool animAttiva = true;
        if (anim.avanza(v, chiudendo, animAttiva)) {
            nascondi();
            return;
        }
        disegna();
        if (!animAttiva) vetro::fermaBattito(v.fin, 1);
    }

    void applicaLed() {
        led.valido = true;
        connesso = attackshark::impostaLedRK86(led);
        disegna();
    }

    void aggiornaSlider(int which, float mouseX) {
        const D2D1_RECT_F& tr = (which == 0) ? trackLum : trackVel;
        const float width = tr.right - tr.left;
        if (width <= 1.0f) return;
        const float frac = std::clamp((mouseX - tr.left) / width, 0.0f, 1.0f);
        const int val = std::clamp((int)std::lround(frac * 4.0f), 0, 4);
        if (which == 0) {
            if (val != led.luminosita) {
                led.luminosita = val;
                applicaLed();
            }
        } else {
            if (val != led.velocita) {
                led.velocita = val;
                applicaLed();
            }
        }
    }

    void scegliColoreCustom() {
        if (dialogAperto) return;
        dialogAperto = true;
        static COLORREF customColors[16] = {
            RGB(255, 0, 234), RGB(250, 250, 250), RGB(255, 45, 32), RGB(255, 159, 10),
            RGB(255, 214, 10), RGB(48, 209, 88), RGB(50, 210, 255), RGB(10, 132, 255),
            RGB(191, 90, 242)
        };
        CHOOSECOLORW cc{};
        cc.lStructSize = sizeof(cc);
        cc.hwndOwner = v.fin;
        cc.lpCustColors = customColors;
        cc.rgbResult = RGB(led.r, led.g, led.b);
        cc.Flags = CC_FULLOPEN | CC_RGBINIT;
        const BOOL ok = ChooseColorW(&cc);
        dialogAperto = false;
        if (!ok) return;
        uint8_t nr = GetRValue(cc.rgbResult);
        uint8_t ng = GetGValue(cc.rgbResult);
        uint8_t nb = GetBValue(cc.rgbResult);
        if (nr == 255 && ng == 255 && nb == 255) {
            nr = 250;
            ng = 250;
            nb = 250;
        }
        led.arcobaleno = false;
        led.r = nr;
        led.g = ng;
        led.b = nb;
        applicaLed();
    }

    void clic(int id) {
        if (id == Z_CHIUDI) {
            avviaChiusura();
            return;
        }
        if (id >= Z_MODO_BASE && id < Z_MODO_BASE + N_MODI) {
            led.modo = MODI[id - Z_MODO_BASE].modo;
            applicaLed();
            return;
        }
        if (id == Z_COL_ARCOBALENO) {
            led.arcobaleno = true;
            applicaLed();
            return;
        }
        if (id >= Z_COL_PRESET && id < Z_COL_PRESET + N_COLORI) {
            const ColorePreset& cp = COLORI[id - Z_COL_PRESET];
            led.arcobaleno = false;
            led.r = cp.r;
            led.g = cp.g;
            led.b = cp.b;
            applicaLed();
            return;
        }
        if (id == Z_COL_CUSTOM) {
            scegliColoreCustom();
            return;
        }
        if (id == Z_BTN_AGGIORNA) {
            attackshark::LedStato nuovo{};
            if (attackshark::leggiLedRK86(nuovo)) {
                led = nuovo;
                connesso = true;
            } else {
                connesso = false;
            }
            disegna();
            return;
        }
        if (id == Z_BTN_GEARHUB) {
            attackshark::apriGearHub();
            return;
        }
        if (id == Z_BTN_SOFTWARE) {
            attackshark::apriSoftware();
            return;
        }
    }

    static bool coloreCorrisponde(uint8_t r, uint8_t g, uint8_t b, const ColorePreset& p) {
        if (p.r == 0xFA && p.g == 0xFA && p.b == 0xFA && r == 0xFF && g == 0xFF && b == 0xFF) return true;
        return r == p.r && g == p.g && b == p.b;
    }

    void disegnaIconaTastiera(float cx, float cy, float d, D2D1_COLOR_F col) {
        v.pen->SetColor(col);
        v.rt->DrawRoundedRectangle({{cx - 11.0f * d, cy - 6.5f * d, cx + 11.0f * d, cy + 6.5f * d}, 2.5f * d, 2.5f * d},
                                   v.pen, 1.6f * d);
        for (int kDot = -2; kDot <= 2; kDot++) {
            v.rt->FillEllipse({{cx + kDot * 3.6f * d, cy - 2.0f * d}, 1.0f * d, 1.0f * d}, v.pen);
        }
        v.rt->DrawLine({cx - 5.2f * d, cy + 2.4f * d}, {cx + 5.2f * d, cy + 2.4f * d}, v.pen, 1.5f * d);
    }

    void disegna() {
        if (!apertaFlag || !v.inizia()) return;
        fmt.ricrea(v, v.dpi);
        float W = (float)v.W, H = (float)v.H;
        const bool strato = dettagli::iniziaLayerMorph(v, anim, W, H);

        const float d = v.dpi;
        const float L = 20.0f * d;
        const float R = W - 20.0f * d;
        const float rr = std::max(8.0f, raggio::vetro() - 10.0f) * d;
        const D2D1_COLOR_F ink = v.inchiostro();
        const D2D1_COLOR_F tenue = v.inchiostro(0.60f);
        const D2D1_COLOR_F iosBlu = v.scuro ? D2D1::ColorF(0.04f, 0.52f, 1.0f, 1.0f)
                                            : D2D1::ColorF(0.0f, 0.48f, 1.0f, 1.0f);
        const D2D1_COLOR_F cardBg = v.scuro ? D2D1::ColorF(1, 1, 1, 0.06f) : D2D1::ColorF(1, 1, 1, 0.55f);
        const D2D1_COLOR_F cardHover = v.scuro ? D2D1::ColorF(1, 1, 1, 0.10f) : D2D1::ColorF(1, 1, 1, 0.75f);

        zone.clear();

        // ---- Header ----
        disegnaIconaTastiera(L + 13.0f * d, 31.0f * d, d, ink);
        dettagli::disegnaHeaderComune(v, fmt, W, L, R, d, L"Attack Shark RK86", statoConn, connesso,
                                      Z_CHIUDI, sopra, premuto, zone, ink, tenue);

        float y = 68.0f * d;

        // ---- 1. Lighting Mode Card ----
        {
            const float cardH = 108.0f * d;
            const D2D1_RECT_F rm = {L, y, R, y + cardH};
            v.pen->SetColor(cardBg);
            v.rt->FillRoundedRectangle({rm, rr, rr}, v.pen);

            std::wstring modeLabel;
            for (int i = 0; i < N_MODI; i++) {
                if (MODI[i].modo == led.modo) {
                    modeLabel = testi::t(MODI[i].it, MODI[i].en);
                    break;
                }
            }
            if (modeLabel.empty()) {
                wchar_t mb[32];
                swprintf(mb, 32, testi::t(L"Modalità %d", L"Mode %d"), led.modo);
                modeLabel = mb;
            }
            if (!connesso) {
                modeLabel = testi::t(L"Tastiera non trovata", L"Keyboard not found");
            }
            const D2D1_COLOR_F badgeCol = connesso
                ? tenue
                : (v.scuro ? D2D1::ColorF(1.0f, 0.45f, 0.40f, 0.95f) : D2D1::ColorF(0.84f, 0.22f, 0.18f, 0.95f));
            v.testo(testi::t(L"Modalità illuminazione", L"Lighting Mode"), fmt.fVoce,
                    {rm.left + 16 * d, rm.top + 10 * d, rm.right - 170 * d, rm.top + 30 * d}, ink);
            v.testo(modeLabel, fmt.fBadge,
                    {rm.right - 180 * d, rm.top + 10 * d, rm.right - 16 * d, rm.top + 30 * d}, badgeCol);

            const float gx = rm.left + 16.0f * d;
            const float gy = rm.top + 36.0f * d;
            const float gw = rm.right - 16.0f * d - gx;
            const float rowH = 29.0f * d;
            const float trackR = 11.0f * d;
            v.pen->SetColor(v.scuro ? D2D1::ColorF(1, 1, 1, 0.08f) : D2D1::ColorF(0, 0, 0, 0.06f));
            v.rt->FillRoundedRectangle({{gx, gy, gx + gw, gy + rowH * 2.0f + 4.0f * d}, trackR, trackR}, v.pen);

            for (int i = 0; i < N_MODI; i++) {
                const int col = i % 4;
                const int row = i / 4;
                const float py = gy + 2.0f * d + (float)row * rowH;
                const D2D1_RECT_F rp = {
                    gx + gw * col / 4.0f + 3.0f * d,
                    py + 2.0f * d,
                    gx + gw * (col + 1) / 4.0f - 3.0f * d,
                    py + rowH - 2.0f * d
                };
                const int zid = Z_MODO_BASE + i;
                zone.push_back({rp, zid});
                const bool attivo = (led.modo == MODI[i].modo);
                const float pillR = 8.0f * d;
                if (attivo) {
                    v.pen->SetColor(v.scuro ? D2D1::ColorF(1, 1, 1, 0.22f) : D2D1::ColorF(1, 1, 1, 0.95f));
                    v.rt->FillRoundedRectangle({rp, pillR, pillR}, v.pen);
                    v.pen->SetColor(D2D1::ColorF(iosBlu.r, iosBlu.g, iosBlu.b, v.scuro ? 0.55f : 0.45f));
                    v.rt->DrawRoundedRectangle({rp, pillR, pillR}, v.pen, 1.2f * d);
                } else if (sopra == zid || premuto == zid) {
                    v.pen->SetColor(v.scuro ? D2D1::ColorF(1, 1, 1, 0.08f) : D2D1::ColorF(0, 0, 0, 0.05f));
                    v.rt->FillRoundedRectangle({rp, pillR, pillR}, v.pen);
                }
                v.testo(testi::t(MODI[i].it, MODI[i].en), fmt.fCentro, rp, ink);
            }

            y += cardH + 8 * d;
        }

        // ---- 2. Brightness Card ----
        {
            const int lum = std::clamp(led.luminosita, 0, 4);
            wchar_t bBuf[16];
            swprintf(bBuf, 16, L"%d%%", lum * 25);
            dettagli::disegnaSliderIosCard(
                v, fmt, L, R, y, 72.0f * d, 8.0f * d, 20.0f * d, d, rr,
                testi::t(L"Luminosità", L"Brightness"), bBuf, lum / 4.0f,
                Z_SLIDER_LUM, dragSlider == 0, sopra == Z_SLIDER_LUM, trackLum,
                zone, ink, iosBlu, cardBg, cardHover);
        }

        // ---- 3. Effect Speed Card ----
        {
            const int vel = std::clamp(led.velocita, 0, 4);
            wchar_t sBuf[16];
            swprintf(sBuf, 16, L"%d", vel + 1);
            dettagli::disegnaSliderIosCard(
                v, fmt, L, R, y, 72.0f * d, 8.0f * d, 20.0f * d, d, rr,
                testi::t(L"Velocità effetto", L"Effect Speed"), sBuf, vel / 4.0f,
                Z_SLIDER_VEL, dragSlider == 1, sopra == Z_SLIDER_VEL, trackVel,
                zone, ink, iosBlu, cardBg, cardHover);
        }

        // ---- 4. RGB Color Card ----
        {
            const float cardH = 92.0f * d;
            const D2D1_RECT_F rc = {L, y, R, y + cardH};
            const bool hoverSwatch = (sopra == Z_COL_ARCOBALENO || sopra == Z_COL_CUSTOM ||
                                      (sopra >= Z_COL_PRESET && sopra < Z_COL_PRESET + N_COLORI));
            v.pen->SetColor(hoverSwatch ? cardHover : cardBg);
            v.rt->FillRoundedRectangle({rc, rr, rr}, v.pen);

            std::wstring colText;
            if (led.arcobaleno) {
                colText = testi::t(L"Arcobaleno", L"Rainbow");
            } else {
                wchar_t hx[16];
                swprintf(hx, 16, L"#%02X%02X%02X", (unsigned)led.r, (unsigned)led.g, (unsigned)led.b);
                colText = hx;
            }
            v.testo(testi::t(L"Colore RGB", L"RGB Color"), fmt.fVoce,
                    {rc.left + 16 * d, rc.top + 10 * d, rc.right - 140 * d, rc.top + 30 * d}, ink);
            v.testo(testi::isola(colText), fmt.fBadge,
                    {rc.right - 150 * d, rc.top + 10 * d, rc.right - 16 * d, rc.top + 30 * d}, tenue);

            const float rBase = 10.5f * d;
            const float passo = 33.0f * d;
            const float xStart = rc.left + 16.0f * d + rBase;
            const float cy = rc.bottom - 28.0f * d;
            const float zTop = std::max(rc.top + 32.0f * d, cy - 17.0f * d);
            const float zBot = std::min(rc.bottom, cy + 17.0f * d);

            bool matchPreset = false;

            // Swatch 0: Rainbow
            {
                const float cx = xStart;
                zone.push_back({{cx - passo * 0.5f, zTop, cx + passo * 0.5f, zBot}, Z_COL_ARCOBALENO});
                const bool hov = (sopra == Z_COL_ARCOBALENO);
                const float rad = hov ? (rBase + 1.0f * d) : rBase;
                if (hov) {
                    v.pen->SetColor(D2D1::ColorF(0.35f, 0.78f, 1.0f, 0.26f));
                    v.rt->FillEllipse({{cx, cy}, rad + 4.5f * d, rad + 4.5f * d}, v.pen);
                }
                const D2D1_ELLIPSE el = {{cx, cy}, rad, rad};
                D2D1_GRADIENT_STOP stops[6] = {
                    {0.00f, D2D1::ColorF(1.00f, 0.22f, 0.24f, 1.0f)},
                    {0.20f, D2D1::ColorF(1.00f, 0.62f, 0.08f, 1.0f)},
                    {0.40f, D2D1::ColorF(1.00f, 0.86f, 0.10f, 1.0f)},
                    {0.60f, D2D1::ColorF(0.18f, 0.84f, 0.42f, 1.0f)},
                    {0.80f, D2D1::ColorF(0.10f, 0.58f, 1.00f, 1.0f)},
                    {1.00f, D2D1::ColorF(0.84f, 0.24f, 0.96f, 1.0f)},
                };
                ID2D1GradientStopCollection* gsc = nullptr;
                ID2D1LinearGradientBrush* lgb = nullptr;
                bool drewGrad = false;
                if (SUCCEEDED(v.rt->CreateGradientStopCollection(stops, 6, &gsc)) && gsc) {
                    if (SUCCEEDED(v.rt->CreateLinearGradientBrush(
                            D2D1::LinearGradientBrushProperties({cx - rad, cy - rad}, {cx + rad, cy + rad}),
                            gsc, &lgb)) && lgb) {
                        v.rt->FillEllipse(el, lgb);
                        lgb->Release();
                        drewGrad = true;
                    }
                    gsc->Release();
                }
                if (!drewGrad) {
                    v.pen->SetColor(D2D1::ColorF(0.20f, 0.85f, 1.0f, 1.0f));
                    v.rt->FillEllipse(el, v.pen);
                }
                v.pen->SetColor(v.scuro ? D2D1::ColorF(1, 1, 1, 0.24f) : D2D1::ColorF(0, 0, 0, 0.18f));
                v.rt->DrawEllipse(el, v.pen, 1.0f);
                if (led.arcobaleno) {
                    v.pen->SetColor(v.scuro ? D2D1::ColorF(1, 1, 1, 0.96f) : D2D1::ColorF(0.10f, 0.12f, 0.16f, 0.90f));
                    v.rt->DrawEllipse({{cx, cy}, rad + 3.0f * d, rad + 3.0f * d}, v.pen, 2.0f * d);
                }
            }

            // Swatches 1..9: Solid presets
            for (int j = 0; j < N_COLORI; j++) {
                const ColorePreset& cp = COLORI[j];
                const int zid = Z_COL_PRESET + j;
                const float cx = xStart + passo * (float)(j + 1);
                zone.push_back({{cx - passo * 0.5f, zTop, cx + passo * 0.5f, zBot}, zid});

                const D2D1_COLOR_F c = D2D1::ColorF(cp.r / 255.0f, cp.g / 255.0f, cp.b / 255.0f, 1.0f);
                const bool hov = (sopra == zid);
                const bool attiva = !led.arcobaleno && coloreCorrisponde(led.r, led.g, led.b, cp);
                if (attiva) matchPreset = true;

                const float rad = hov ? (rBase + 1.0f * d) : rBase;
                if (hov) {
                    v.pen->SetColor(D2D1::ColorF(c.r, c.g, c.b, 0.26f));
                    v.rt->FillEllipse({{cx, cy}, rad + 4.5f * d, rad + 4.5f * d}, v.pen);
                }
                const D2D1_ELLIPSE el = {{cx, cy}, rad, rad};
                v.pen->SetColor(c);
                v.rt->FillEllipse(el, v.pen);
                v.pen->SetColor(v.scuro ? D2D1::ColorF(1, 1, 1, 0.22f) : D2D1::ColorF(0, 0, 0, 0.18f));
                v.rt->DrawEllipse(el, v.pen, 1.0f);
                if (attiva) {
                    v.pen->SetColor(v.scuro ? D2D1::ColorF(1, 1, 1, 0.96f) : D2D1::ColorF(0.10f, 0.12f, 0.16f, 0.90f));
                    v.rt->DrawEllipse({{cx, cy}, rad + 3.0f * d, rad + 3.0f * d}, v.pen, 2.0f * d);
                }
            }

            // Custom... button
            {
                const bool customAttivo = !led.arcobaleno && !matchPreset;
                const std::wstring customTxt = testi::t(L"Personalizzato…", L"Custom…");
                const float dotSpace = customAttivo ? 18.0f * d : 0.0f;
                const float minLeft = xStart + passo * (float)(N_COLORI + 1) - 4.0f * d;
                const float btnW = std::min(rc.right - 16.0f * d - minLeft,
                                            v.larghezza(customTxt, fmt.fCentro) + 24.0f * d + dotSpace);
                const D2D1_RECT_F rb = {rc.right - 16.0f * d - btnW, cy - 15.0f * d, rc.right - 16.0f * d, cy + 15.0f * d};
                zone.push_back({rb, Z_COL_CUSTOM});
                const float bAlpha = (premuto == Z_COL_CUSTOM) ? 0.20f
                                   : (sopra == Z_COL_CUSTOM)   ? (v.scuro ? 0.16f : 0.11f)
                                                               : (v.scuro ? 0.09f : 0.06f);
                v.pen->SetColor(v.scuro ? D2D1::ColorF(1, 1, 1, bAlpha) : D2D1::ColorF(0, 0, 0, bAlpha));
                v.rt->FillRoundedRectangle({rb, 15.0f * d, 15.0f * d}, v.pen);
                if (customAttivo) {
                    v.pen->SetColor(v.scuro ? D2D1::ColorF(1, 1, 1, 0.70f) : D2D1::ColorF(0.10f, 0.12f, 0.16f, 0.65f));
                    v.rt->DrawRoundedRectangle({rb, 15.0f * d, 15.0f * d}, v.pen, 1.4f * d);
                    const float dcx = rb.left + 14.0f * d;
                    v.pen->SetColor(D2D1::ColorF(led.r / 255.0f, led.g / 255.0f, led.b / 255.0f, 1.0f));
                    v.rt->FillEllipse({{dcx, cy}, 5.5f * d, 5.5f * d}, v.pen);
                    v.pen->SetColor(v.scuro ? D2D1::ColorF(1, 1, 1, 0.35f) : D2D1::ColorF(0, 0, 0, 0.25f));
                    v.rt->DrawEllipse({{dcx, cy}, 5.5f * d, 5.5f * d}, v.pen, 1.0f);
                }
                v.testo(customTxt, fmt.fCentro, {rb.left + (customAttivo ? 18.0f * d : 0.0f), rb.top, rb.right, rb.bottom}, ink);
            }

            y += cardH + 12 * d;
        }

        // ---- 5. Footer Actions ----
        {
            const float btnH = 34.0f * d;
            const float yb = y + 4.0f * d;
            const float gap = 8.0f * d;

            const std::wstring tRef = testi::t(L"Aggiorna", L"Refresh");
            const std::wstring tGear = testi::t(L"Apri GearHub (WebHID)", L"Open GearHub (WebHID)");
            const std::wstring tSoft = L"Attack Shark Software";

            float wRef = v.larghezza(tRef, fmt.fCentro) + 26.0f * d;
            float wGear = v.larghezza(tGear, fmt.fCentro) + 26.0f * d;
            float wSoft = v.larghezza(tSoft, fmt.fCentro) + 26.0f * d;
            const float avail = R - L - 2.0f * gap;
            const float tot = wRef + wGear + wSoft;
            if (tot > avail && tot > 1.0f) {
                const float scale = avail / tot;
                wRef *= scale;
                wGear *= scale;
                wSoft *= scale;
            }

            const D2D1_RECT_F rRef = {L, yb, L + wRef, yb + btnH};
            const D2D1_RECT_F rSoft = {R - wSoft, yb, R, yb + btnH};
            const D2D1_RECT_F rGear = {rSoft.left - gap - wGear, yb, rSoft.left - gap, yb + btnH};

            dettagli::disegnaPillolaFooter(v, fmt, rRef, btnH, Z_BTN_AGGIORNA, tRef, false, sopra, premuto, ink, iosBlu, zone);
            dettagli::disegnaPillolaFooter(v, fmt, rGear, btnH, Z_BTN_GEARHUB, tGear, true, sopra, premuto, ink, iosBlu, zone);
            dettagli::disegnaPillolaFooter(v, fmt, rSoft, btnH, Z_BTN_SOFTWARE, tSoft, false, sopra, premuto, ink, iosBlu, zone);
        }

        if (strato) {
            v.rt->PopLayer();
            v.rt->SetTransform(D2D1::Matrix3x2F::Identity());
        }
        v.finisci();
    }
};

class FinestraMouse {
public:
    ~FinestraMouse() { fmt.libera(); }

    void apri(const std::wstring& statoConnessione) {
        if (!v.fin && !v.crea(L"FluidWin.AttackSharkV6", vetro::procedura<FinestraMouse>, this, true, 2, true, false)) {
            return;
        }
        SetWindowTextW(v.fin, L"Attack Shark V6");
        statoConn = statoConnessione;
        mouse = attackshark::MouseStato{};
        connesso = attackshark::leggiMouseV6(mouse);
        v.aggiornaTema();

        if (apertaFlag && !chiudendo) {
            if (anim.morph < 0) {
                anim.gira(1);
                vetro::battito(v.fin, 1);
            }
            SetForegroundWindow(v.fin);
            disegna();
            return;
        }

        anim.preparaApertura(v, LARGO, ALTO);
        chiudendo = false;
        sopra = -1;
        premuto = -1;
        draggingDpi = false;
        dentro = false;
        apertaFlag = true;

        fmt.ricrea(v, v.dpi);
        v.posiziona(anim.rDa.left, anim.rDa.top,
                    anim.rDa.right - anim.rDa.left, anim.rDa.bottom - anim.rDa.top,
                    HWND_TOP, SWP_SHOWWINDOW);
        v.alfa(0);
        vetro::battito(v.fin, 1);
        SetForegroundWindow(v.fin);
        disegna();
    }

    void chiudi() {
        nascondi();
        fmt.libera();
        v.chiudi();
    }

    bool aperta() const { return apertaFlag; }

    void aggiornaTema() {
        if (!v.fin) return;
        v.aggiornaTema();
        if (apertaFlag) disegna();
    }

    bool messaggio(HWND, UINT m, WPARAM w, LPARAM l, LRESULT& r) {
        r = 0;
        switch (m) {
            case WM_TIMER:
                if (w == 1) passoAnimazione();
                return true;
            case WM_PAINT:
                disegna();
                return true;
            case WM_MOUSEMOVE: {
                if (anim.morph || chiudendo) return true;
                const float x = (float)GET_X_LPARAM(l);
                const float y = (float)GET_Y_LPARAM(l);
                if (draggingDpi) {
                    aggiornaSliderDpi(x, false);
                    return true;
                }
                if (!dentro) {
                    TRACKMOUSEEVENT tm{sizeof(tm), TME_LEAVE, v.fin, 0};
                    TrackMouseEvent(&tm);
                    dentro = true;
                }
                const int s = dettagli::zonaIn(zone, x, y);
                if (s != sopra) {
                    sopra = s;
                    disegna();
                }
                return true;
            }
            case WM_MOUSELEAVE:
                dentro = false;
                if (!draggingDpi) {
                    sopra = -1;
                    if (!chiudendo) disegna();
                }
                return true;
            case WM_LBUTTONDOWN: {
                if (anim.morph || chiudendo) return true;
                const float x = (float)GET_X_LPARAM(l);
                const float y = (float)GET_Y_LPARAM(l);
                const int z = dettagli::zonaIn(zone, x, y);
                if (z == Z_SLIDER_DPI) {
                    draggingDpi = true;
                    premuto = z;
                    SetCapture(v.fin);
                    aggiornaSliderDpi(x, true);
                } else {
                    premuto = z;
                    disegna();
                }
                return true;
            }
            case WM_LBUTTONUP: {
                if (draggingDpi) {
                    aggiornaSliderDpi((float)GET_X_LPARAM(l), false);
                    draggingDpi = false;
                    premuto = -1;
                    ReleaseCapture();
                    applicaDpi();
                    return true;
                }
                if (anim.morph || chiudendo) {
                    premuto = -1;
                    return true;
                }
                const int p = premuto;
                premuto = -1;
                const int z = dettagli::zonaIn(zone, (float)GET_X_LPARAM(l), (float)GET_Y_LPARAM(l));
                if (z >= 0 && z == p) clic(z);
                else disegna();
                return true;
            }
            case WM_CAPTURECHANGED:
                if (draggingDpi) {
                    draggingDpi = false;
                    premuto = -1;
                    if (apertaFlag) disegna();
                }
                return true;
            case WM_KEYDOWN:
                if (w == VK_ESCAPE) avviaChiusura();
                return true;
            case WM_CLOSE:
                avviaChiusura();
                return true;
            case WM_NCHITTEST: {
                if (anim.morph || chiudendo) return false;
                POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
                ScreenToClient(v.fin, &p);
                const float d = v.dpi;
                if (p.y >= 0 && p.y < 60 * d && dettagli::zonaIn(zone, (float)p.x, (float)p.y) < 0) {
                    r = HTCAPTION;
                    return true;
                }
                return false;
            }
        }
        return false;
    }

private:
    static constexpr float LARGO = 560.0f, ALTO = 500.0f;

    enum {
        Z_CHIUDI = 100,
        Z_POLL_BASE = 200,
        Z_STAGE_BASE = 300,
        Z_SLIDER_DPI = 400,
        Z_BTN_AGGIORNA = 500,
        Z_BTN_SOFTWARE = 501,
    };

    static constexpr int POLLING_HZ[4] = {125, 250, 500, 1000};
    static constexpr uint32_t STAGE_COL[6] = {
        0xFF453A,  // 1: Red
        0x30D158,  // 2: Green
        0x0A84FF,  // 3: Blue
        0xFFD60A,  // 4: Yellow
        0x32D2FF,  // 5: Cyan
        0xBF5AF2,  // 6: Purple
    };

    vetro::Finestra v;
    dettagli::FormatiUi fmt;
    dettagli::MorphAnim anim;

    bool apertaFlag = false;
    bool chiudendo = false;
    bool dentro = false;
    bool draggingDpi = false;
    bool connesso = false;

    int sopra = -1;
    int premuto = -1;
    D2D1_RECT_F trackDpi{};
    std::vector<dettagli::Zona> zone;

    std::wstring statoConn;
    attackshark::MouseStato mouse{};

    void avviaChiusura() {
        if (!apertaFlag) return;
        if (draggingDpi) {
            draggingDpi = false;
            ReleaseCapture();
        }
        premuto = -1;
        if (chiudendo) return;
        if (anim.morph == 0 && v.fin) GetWindowRect(v.fin, &anim.rA);
        chiudendo = true;
        anim.gira(-1);
        vetro::battito(v.fin, 1);
    }

    void nascondi() {
        if (draggingDpi) {
            draggingDpi = false;
            ReleaseCapture();
        }
        if (v.fin) {
            vetro::fermaBattito(v.fin, 1);
            ShowWindow(v.fin, SW_HIDE);
        }
        apertaFlag = false;
        chiudendo = false;
        anim.resetChiuso();
        sopra = -1;
        premuto = -1;
        dentro = false;
    }

    void passoAnimazione() {
        if (!apertaFlag) {
            vetro::fermaBattito(v.fin, 1);
            return;
        }
        bool animAttiva = true;
        if (anim.avanza(v, chiudendo, animAttiva)) {
            nascondi();
            return;
        }
        disegna();
        if (!animAttiva) vetro::fermaBattito(v.fin, 1);
    }

    void applicaDpi() {
        connesso = attackshark::impostaDpiV6(mouse);
        disegna();
    }

    void aggiornaSliderDpi(float mouseX, bool forzaDisegno) {
        const float width = trackDpi.right - trackDpi.left;
        if (width <= 1.0f) return;
        const float frac = std::clamp((mouseX - trackDpi.left) / width, 0.0f, 1.0f);
        const int step = std::clamp((int)std::lround(frac * 96.0f), 0, 96);
        const int val = 400 + step * 100;
        const int idx = std::clamp(mouse.dpiAttivo, 0, 5);
        if (val != mouse.dpi[idx] || forzaDisegno) {
            mouse.dpi[idx] = val;
            disegna();
        }
    }

    void clic(int id) {
        if (id == Z_CHIUDI) {
            avviaChiusura();
            return;
        }
        if (id >= Z_POLL_BASE && id < Z_POLL_BASE + 4) {
            const int hz = POLLING_HZ[id - Z_POLL_BASE];
            const bool ok = attackshark::impostaPollingV6(hz);
            connesso = ok;
            if (ok) {
                mouse.pollingHz = hz;
                if (mouse.haRaw04) {
                    mouse.raw04[14] = attackshark::dettagli::codicePollingV6(hz);
                }
            }
            disegna();
            return;
        }
        if (id >= Z_STAGE_BASE && id < Z_STAGE_BASE + 6) {
            mouse.dpiAttivo = id - Z_STAGE_BASE;
            applicaDpi();
            return;
        }
        if (id == Z_BTN_AGGIORNA) {
            attackshark::MouseStato nuovo{};
            if (attackshark::leggiMouseV6(nuovo)) {
                mouse = nuovo;
                connesso = true;
                if (statoConn == testi::t(L"Non connesso", L"Offline")) {
                    statoConn.clear();
                }
            } else {
                connesso = false;
                statoConn.clear();
            }
            disegna();
            return;
        }
        if (id == Z_BTN_SOFTWARE) {
            attackshark::apriSoftware();
            return;
        }
    }

    void disegnaIconaMouse(float cx, float cy, float d, D2D1_COLOR_F col) {
        v.pen->SetColor(col);
        v.rt->DrawRoundedRectangle({{cx - 6.5f * d, cy - 10.0f * d, cx + 6.5f * d, cy + 10.0f * d}, 6.5f * d, 6.5f * d},
                                   v.pen, 1.6f * d);
        v.rt->DrawLine({cx, cy - 6.0f * d}, {cx, cy - 2.0f * d}, v.pen, 1.8f * d);
    }

    void disegna() {
        if (!apertaFlag || !v.inizia()) return;
        fmt.ricrea(v, v.dpi);
        float W = (float)v.W, H = (float)v.H;
        const bool strato = dettagli::iniziaLayerMorph(v, anim, W, H);

        const float d = v.dpi;
        const float L = 20.0f * d;
        const float R = W - 20.0f * d;
        const float rr = std::max(8.0f, raggio::vetro() - 10.0f) * d;
        const D2D1_COLOR_F ink = v.inchiostro();
        const D2D1_COLOR_F tenue = v.inchiostro(0.60f);
        const D2D1_COLOR_F iosBlu = v.scuro ? D2D1::ColorF(0.04f, 0.52f, 1.0f, 1.0f)
                                            : D2D1::ColorF(0.0f, 0.48f, 1.0f, 1.0f);
        const D2D1_COLOR_F cardBg = v.scuro ? D2D1::ColorF(1, 1, 1, 0.06f) : D2D1::ColorF(1, 1, 1, 0.55f);
        const D2D1_COLOR_F cardHover = v.scuro ? D2D1::ColorF(1, 1, 1, 0.10f) : D2D1::ColorF(1, 1, 1, 0.75f);
        const D2D1_COLOR_F errCol = v.scuro ? D2D1::ColorF(1.0f, 0.45f, 0.40f, 0.95f)
                                            : D2D1::ColorF(0.84f, 0.22f, 0.18f, 0.95f);

        zone.clear();

        // ---- Header ----
        disegnaIconaMouse(L + 13.0f * d, 31.0f * d, d, ink);
        dettagli::disegnaHeaderComune(v, fmt, W, L, R, d, L"Attack Shark V6", statoConn, connesso,
                                      Z_CHIUDI, sopra, premuto, zone, ink, tenue);

        float y = 68.0f * d;

        // ---- 1. Polling Rate Card ----
        {
            const float cardH = 94.0f * d;
            const D2D1_RECT_F rm = {L, y, R, y + cardH};
            v.pen->SetColor(cardBg);
            v.rt->FillRoundedRectangle({rm, rr, rr}, v.pen);

            wchar_t pBuf[32];
            swprintf(pBuf, 32, L"%d Hz", mouse.pollingHz);
            const std::wstring badge = connesso ? std::wstring(pBuf)
                                                : std::wstring(testi::t(L"Mouse non trovato", L"Mouse not found"));
            v.testo(testi::t(L"Frequenza di polling", L"Polling Rate"), fmt.fVoce,
                    {rm.left + 16 * d, rm.top + 10 * d, rm.right - 170 * d, rm.top + 32 * d}, ink);
            v.testo(testi::isola(badge), fmt.fBadge,
                    {rm.right - 180 * d, rm.top + 10 * d, rm.right - 16 * d, rm.top + 32 * d},
                    connesso ? tenue : errCol);

            const float gx = rm.left + 16.0f * d;
            const float gy = rm.top + 38.0f * d;
            const float gw = rm.right - 16.0f * d - gx;
            const float rowH = 40.0f * d;
            const float trackR = 11.0f * d;
            v.pen->SetColor(v.scuro ? D2D1::ColorF(1, 1, 1, 0.08f) : D2D1::ColorF(0, 0, 0, 0.06f));
            v.rt->FillRoundedRectangle({{gx, gy, gx + gw, gy + rowH + 4.0f * d}, trackR, trackR}, v.pen);

            for (int i = 0; i < 4; i++) {
                const float py = gy + 2.0f * d;
                const D2D1_RECT_F rp = {
                    gx + gw * (float)i / 4.0f + 3.0f * d,
                    py + 2.0f * d,
                    gx + gw * (float)(i + 1) / 4.0f - 3.0f * d,
                    py + rowH - 2.0f * d
                };
                const int zid = Z_POLL_BASE + i;
                zone.push_back({rp, zid});
                const bool attivo = (mouse.pollingHz == POLLING_HZ[i]);
                const float pillR = 8.0f * d;
                if (attivo) {
                    v.pen->SetColor(v.scuro ? D2D1::ColorF(1, 1, 1, 0.22f) : D2D1::ColorF(1, 1, 1, 0.95f));
                    v.rt->FillRoundedRectangle({rp, pillR, pillR}, v.pen);
                    v.pen->SetColor(D2D1::ColorF(iosBlu.r, iosBlu.g, iosBlu.b, v.scuro ? 0.55f : 0.45f));
                    v.rt->DrawRoundedRectangle({rp, pillR, pillR}, v.pen, 1.2f * d);
                } else if (sopra == zid || premuto == zid) {
                    v.pen->SetColor(v.scuro ? D2D1::ColorF(1, 1, 1, 0.08f) : D2D1::ColorF(0, 0, 0, 0.05f));
                    v.rt->FillRoundedRectangle({rp, pillR, pillR}, v.pen);
                }
                wchar_t hzTxt[16];
                swprintf(hzTxt, 16, L"%d Hz", POLLING_HZ[i]);
                v.testo(testi::isola(hzTxt), fmt.fCentro, rp, ink);
            }

            y += cardH + 12 * d;
        }

        const int idxAttivo = std::clamp(mouse.dpiAttivo, 0, 5);

        // ---- 2. DPI Stages Card (2x3 grid) ----
        {
            const float cardH = 162.0f * d;
            const D2D1_RECT_F rm = {L, y, R, y + cardH};
            v.pen->SetColor(cardBg);
            v.rt->FillRoundedRectangle({rm, rr, rr}, v.pen);

            wchar_t sBuf[48];
            swprintf(sBuf, 48, testi::t(L"Livello %d \u00b7 %d DPI", L"Stage %d \u00b7 %d DPI"),
                     idxAttivo + 1, mouse.dpi[idxAttivo]);
            v.testo(testi::t(L"Livelli DPI", L"DPI Stages"), fmt.fVoce,
                    {rm.left + 16 * d, rm.top + 10 * d, rm.right - 190 * d, rm.top + 32 * d}, ink);
            v.testo(testi::isola(sBuf), fmt.fBadge,
                    {rm.right - 200 * d, rm.top + 10 * d, rm.right - 16 * d, rm.top + 32 * d}, tenue);

            const float gx = rm.left + 16.0f * d;
            const float gy = rm.top + 38.0f * d;
            const float gw = rm.right - 16.0f * d - gx;
            const float rowH = 54.0f * d;
            const float trackR = 11.0f * d;
            v.pen->SetColor(v.scuro ? D2D1::ColorF(1, 1, 1, 0.08f) : D2D1::ColorF(0, 0, 0, 0.06f));
            v.rt->FillRoundedRectangle({{gx, gy, gx + gw, gy + rowH * 2.0f + 4.0f * d}, trackR, trackR}, v.pen);

            for (int i = 0; i < 6; i++) {
                const int col = i % 3;
                const int row = i / 3;
                const float py = gy + 2.0f * d + (float)row * rowH;
                const D2D1_RECT_F rp = {
                    gx + gw * (float)col / 3.0f + 3.0f * d,
                    py + 2.0f * d,
                    gx + gw * (float)(col + 1) / 3.0f - 3.0f * d,
                    py + rowH - 2.0f * d
                };
                const int zid = Z_STAGE_BASE + i;
                zone.push_back({rp, zid});
                const bool attivo = (idxAttivo == i);
                const float pillR = 8.0f * d;
                if (attivo) {
                    v.pen->SetColor(v.scuro ? D2D1::ColorF(1, 1, 1, 0.22f) : D2D1::ColorF(1, 1, 1, 0.95f));
                    v.rt->FillRoundedRectangle({rp, pillR, pillR}, v.pen);
                    v.pen->SetColor(D2D1::ColorF(iosBlu.r, iosBlu.g, iosBlu.b, v.scuro ? 0.55f : 0.45f));
                    v.rt->DrawRoundedRectangle({rp, pillR, pillR}, v.pen, 1.2f * d);
                } else if (sopra == zid || premuto == zid) {
                    v.pen->SetColor(v.scuro ? D2D1::ColorF(1, 1, 1, 0.08f) : D2D1::ColorF(0, 0, 0, 0.05f));
                    v.rt->FillRoundedRectangle({rp, pillR, pillR}, v.pen);
                }

                const uint32_t hex = STAGE_COL[i];
                const D2D1_COLOR_F dotCol = D2D1::ColorF(
                    ((hex >> 16) & 0xFF) / 255.0f,
                    ((hex >> 8) & 0xFF) / 255.0f,
                    (hex & 0xFF) / 255.0f,
                    1.0f);
                const float dotR = 5.0f * d;
                const float dotCx = rp.left + 16.0f * d;
                const float dotCy = (rp.top + rp.bottom) * 0.5f;
                v.pen->SetColor(dotCol);
                v.rt->FillEllipse({{dotCx, dotCy}, dotR, dotR}, v.pen);
                v.pen->SetColor(v.scuro ? D2D1::ColorF(1, 1, 1, 0.28f) : D2D1::ColorF(0, 0, 0, 0.18f));
                v.rt->DrawEllipse({{dotCx, dotCy}, dotR, dotR}, v.pen, 1.0f);

                wchar_t stLbl[24], dpiLbl[24];
                swprintf(stLbl, 24, testi::t(L"Livello %d", L"Stage %d"), i + 1);
                swprintf(dpiLbl, 24, L"%d DPI", mouse.dpi[i]);
                const float txL = dotCx + 14.0f * d;
                const float txR = rp.right - 10.0f * d;
                v.testo(stLbl, fmt.fSotto, {txL, rp.top + 6.0f * d, txR, rp.top + 23.0f * d}, tenue);
                v.testo(testi::isola(dpiLbl), fmt.fVoce, {txL, rp.top + 22.0f * d, txR, rp.bottom - 6.0f * d}, ink);
            }

            y += cardH + 12 * d;
        }

        // ---- 3. Selected Stage DPI Card ----
        {
            const int curDpi = std::clamp(mouse.dpi[idxAttivo], 400, 10000);
            wchar_t dBuf[24];
            swprintf(dBuf, 24, L"%d DPI", curDpi);
            dettagli::disegnaSliderIosCard(
                v, fmt, L, R, y, 86.0f * d, 12.0f * d, 24.0f * d, d, rr,
                testi::t(L"DPI livello selezionato", L"Selected Stage DPI"), dBuf,
                (float)(curDpi - 400) / 9600.0f,
                Z_SLIDER_DPI, draggingDpi, sopra == Z_SLIDER_DPI, trackDpi,
                zone, ink, iosBlu, cardBg, cardHover);
        }

        // ---- 4. Footer Actions ----
        {
            const float btnH = 34.0f * d;
            const float yb = y;
            const float gap = 10.0f * d;

            const std::wstring tRef = testi::t(L"Aggiorna", L"Refresh");
            const std::wstring tSoft = L"Attack Shark Software";

            float wRef = v.larghezza(tRef, fmt.fCentro) + 28.0f * d;
            float wSoft = v.larghezza(tSoft, fmt.fCentro) + 28.0f * d;
            const float avail = R - L - gap;
            const float tot = wRef + wSoft;
            if (tot > avail && tot > 1.0f) {
                const float scale = avail / tot;
                wRef *= scale;
                wSoft *= scale;
            }

            const D2D1_RECT_F rRef = {L, yb, L + wRef, yb + btnH};
            const D2D1_RECT_F rSoft = {R - wSoft, yb, R, yb + btnH};

            dettagli::disegnaPillolaFooter(v, fmt, rRef, btnH, Z_BTN_AGGIORNA, tRef, false, sopra, premuto, ink, iosBlu, zone);
            dettagli::disegnaPillolaFooter(v, fmt, rSoft, btnH, Z_BTN_SOFTWARE, tSoft, false, sopra, premuto, ink, iosBlu, zone);
        }

        if (strato) {
            v.rt->PopLayer();
            v.rt->SetTransform(D2D1::Matrix3x2F::Identity());
        }
        v.finisci();
    }
};

}  // namespace attackshark_ui
