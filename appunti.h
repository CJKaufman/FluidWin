// Cronologia degli appunti: memoria condivisa fra il centro di controllo e la finestra di vetro (Win+V).
// Le voci non fissate restano solo in memoria; le fissate si salvano in %LOCALAPPDATA%\FluidWin\appunti_fissati.bin.
// Rispetta i segni di riservatezza dei gestori di password.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <iterator>
#include <cwctype>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include "vetro.h"
#include "testi.h"
#include "menu.h"

namespace appunti {

struct Voce {
    UINT id = 0;
    bool immagine = false;
    bool fissata = false;
    std::wstring testo;            // testo intero (al massimo 256 KB)
    std::wstring anteprimaTesto;   // testo normalizzato su una riga/due per la scheda
    std::vector<BYTE> dib;         // immagine: CF_DIB originale per incollare senza perdita
    int w = 0, h = 0;              // misure reali dell'immagine
    std::vector<UINT> anteprima;   // pixel BGRA premoltiplicati aw x ah (al massimo 320 px di larghezza)
    int aw = 0, ah = 0;
    std::wstring sorgente;         // nome dell'app che ha copiato (senza .exe)
    INT64 quando = 0;              // FILETIME (usato dal centro di controllo)
    ULONGLONG tick = 0;            // GetTickCount64() per il tempo relativo
};

class Cronologia {
public:
    static Cronologia& io() { static Cronologia c; return c; }
    std::vector<Voce> voci;   // fissate in cima, poi le piu' recenti
    UINT versione = 0;

    void avvia(HWND h) {
        if (finestra && finestra != h) RemoveClipboardFormatListener(finestra);
        finestra = h;
        if (finestra) AddClipboardFormatListener(finestra);
        if (!fissatiCaricati) {
            fissatiCaricati = true;
            caricaFissati();
        }
    }

    void ferma() {
        if (finestra) RemoveClipboardFormatListener(finestra);
        finestra = nullptr;
        if (!voci.empty()) {
            voci.clear();
            versione++;
        }
        fissatiCaricati = false;
    }

    void svuota() {
        if (!voci.empty()) {
            voci.clear();
            versione++;
        }
        salvaFissati();
    }

    void svuotaNonFissate() {
        const size_t prima = voci.size();
        voci.erase(std::remove_if(voci.begin(), voci.end(), [](const Voce& v) { return !v.fissata; }), voci.end());
        if (voci.size() != prima) versione++;
    }

    void togli(UINT id) {
        auto it = std::find_if(voci.begin(), voci.end(), [id](const Voce& v) { return v.id == id; });
        if (it == voci.end()) return;
        const bool eraFissata = it->fissata;
        voci.erase(it);
        versione++;
        if (eraFissata) salvaFissati();
    }

    void fissa(UINT id) {
        auto it = std::find_if(voci.begin(), voci.end(), [id](const Voce& v) { return v.id == id; });
        if (it == voci.end()) return;
        Voce v = std::move(*it);
        voci.erase(it);
        v.fissata = !v.fissata;
        inserisciNelGruppo(std::move(v));
        pulisciLimite();
        versione++;
        salvaFissati();
    }

    // WM_CLIPBOARDUPDATE
    bool cambiato() {
        const DWORD seq = GetClipboardSequenceNumber();
        if (haSequenzaInterna && seq == ultimaSequenzaInterna) return false;

        const std::wstring app = nomeSorgente();
        if (!apri()) return false;

        static const UINT ESCLUDI = RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing");
        static const UINT IGNORA = RegisterClipboardFormatW(L"Clipboard Viewer Ignore");
        static const UINT CRONO = RegisterClipboardFormatW(L"CanIncludeInClipboardHistory");

        bool vietato = (ESCLUDI && IsClipboardFormatAvailable(ESCLUDI) != FALSE) ||
                       (IGNORA && IsClipboardFormatAvailable(IGNORA) != FALSE);
        // col segno presente si archivia solo se si legge un DWORD diverso da 0: illeggibile vale come vietato
        if (!vietato && CRONO && IsClipboardFormatAvailable(CRONO)) {
            vietato = true;
            if (HANDLE hc = GetClipboardData(CRONO)) {
                if (GlobalSize(hc) >= sizeof(DWORD)) {
                    if (const DWORD* p = (const DWORD*)GlobalLock(hc)) {
                        vietato = (*p == 0);
                        GlobalUnlock(hc);
                    }
                }
            }
        }

        Voce v;
        bool ok = false;
        if (!vietato && IsClipboardFormatAvailable(CF_UNICODETEXT)) {
            if (HANDLE ht = GetClipboardData(CF_UNICODETEXT)) {
                if (const wchar_t* p = (const wchar_t*)GlobalLock(ht)) {
                    constexpr size_t MAX_CARATTERI = (256 * 1024) / sizeof(wchar_t);
                    const size_t n = wcsnlen(p, GlobalSize(ht) / sizeof(wchar_t));
                    v.testo.assign(p, std::min<size_t>(n, MAX_CARATTERI));
                    GlobalUnlock(ht);
                }
            }
            ok = std::any_of(v.testo.begin(), v.testo.end(), [](wchar_t ch) { return !iswspace(ch); });
            if (ok) v.anteprimaTesto = normalizzaAnteprima(v.testo);
        } else if (!vietato && IsClipboardFormatAvailable(CF_DIB)) {
            if (HANDLE hd = GetClipboardData(CF_DIB)) {
                constexpr SIZE_T MAX_DIB = 16 * 1024 * 1024;
                const SIZE_T n = GlobalSize(hd);
                if (n > sizeof(BITMAPINFOHEADER) && n <= MAX_DIB) {
                    if (const BYTE* p = (const BYTE*)GlobalLock(hd)) {
                        v.dib.assign(p, p + n);
                        GlobalUnlock(hd);
                    }
                }
            }
            v.immagine = true;
            ok = !v.dib.empty() && creaAnteprima(v);
        }
        CloseClipboard();
        if (!ok) return false;

        FILETIME ft{};
        GetSystemTimeAsFileTime(&ft);
        v.quando = ((INT64)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
        v.tick = GetTickCount64();
        v.sorgente = app;

        bool eraFissata = false;
        for (size_t i = 0; i < voci.size(); i++) {
            if (voci[i].immagine == v.immagine && (v.immagine ? voci[i].dib == v.dib : voci[i].testo == v.testo)) {
                eraFissata = voci[i].fissata;
                voci.erase(voci.begin() + (ptrdiff_t)i);
                break;
            }
        }
        v.fissata = eraFissata;
        v.id = ++ultimoId;
        inserisciNelGruppo(std::move(v));
        pulisciLimite();
        versione++;
        if (eraFissata) salvaFissati();
        return true;
    }

    // rimette negli appunti la voce scelta (e la porta in cima al suo gruppo)
    bool ricopia(UINT id, bool spostaInCima = true) {
        auto it = std::find_if(voci.begin(), voci.end(), [id](const Voce& v) { return v.id == id; });
        if (it == voci.end()) return false;
        const void* dati = it->immagine ? (const void*)it->dib.data() : (const void*)it->testo.c_str();
        const size_t n = it->immagine ? it->dib.size() : (it->testo.size() + 1) * sizeof(wchar_t);
        HGLOBAL g = n ? GlobalAlloc(GMEM_MOVEABLE, n) : nullptr;
        if (!g) return false;
        void* p = GlobalLock(g);
        if (!p) { GlobalFree(g); return false; }
        memcpy(p, dati, n);
        GlobalUnlock(g);
        if (!apri()) { GlobalFree(g); return false; }
        bool ok = false;
        if (EmptyClipboard()) ok = SetClipboardData(it->immagine ? CF_DIB : CF_UNICODETEXT, g) != nullptr;
        if (!ok) GlobalFree(g);
        CloseClipboard();
        if (ok) {
            ultimaSequenzaInterna = GetClipboardSequenceNumber();
            haSequenzaInterna = true;
            if (spostaInCima) {
                Voce v = std::move(*it);
                const bool eraFissata = v.fissata;
                voci.erase(it);
                FILETIME ft{};
                GetSystemTimeAsFileTime(&ft);
                v.quando = ((INT64)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
                v.tick = GetTickCount64();
                inserisciNelGruppo(std::move(v));
                versione++;
                if (eraFissata) salvaFissati();
            }
        }
        return ok;
    }

    static size_t inizioPixelDib(const BYTE* p, size_t n) {
        if (!p || n < sizeof(BITMAPINFOHEADER)) return 0;
        BITMAPINFOHEADER bh{};
        memcpy(&bh, p, sizeof(bh));
        if (bh.biSize < sizeof(BITMAPINFOHEADER) || bh.biSize > n) return 0;
        if (bh.biWidth <= 0 || bh.biWidth > 32768 || bh.biHeight == 0 || bh.biHeight < -32768 || bh.biHeight > 32768) return 0;
        const unsigned bpp = bh.biBitCount;
        if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) return 0;
        uint64_t off = bh.biSize;
        if (bh.biCompression == BI_BITFIELDS) {
            if (bpp != 16 && bpp != 32) return 0;
            if (bh.biSize == sizeof(BITMAPINFOHEADER)) off += 12;
        } else if (bh.biCompression != BI_RGB) return 0;
        const uint64_t massimo = bpp <= 8 ? (1u << bpp) : 256;
        const uint64_t colori = bh.biClrUsed ? bh.biClrUsed : (bpp <= 8 ? massimo : 0);
        if (colori > massimo) return 0;
        off += colori * 4;
        const uint64_t riga = ((uint64_t)bh.biWidth * bpp + 31) / 32 * 4;
        const uint64_t alta = (uint64_t)(bh.biHeight < 0 ? -(int64_t)bh.biHeight : bh.biHeight);
        if (off + riga * alta > n) return 0;
        return (size_t)off;
    }

private:
    static constexpr uint32_t VERSIONE_FISSATI = 1;
    static constexpr uint32_t MAX_FISSATI = 50;
    static constexpr uint32_t MAX_SORG_BYTES = 4096;
    static constexpr uint32_t MAX_TESTO_BYTES = 512 * 1024;
    static constexpr uint32_t MAX_DIB_BYTES = 16 * 1024 * 1024;
    static constexpr uint32_t MAX_ANT_BYTES = 4 * 1024 * 1024;

    HWND finestra = nullptr;
    UINT ultimoId = 0;
    DWORD ultimaSequenzaInterna = 0;
    bool haSequenzaInterna = false;
    bool fissatiCaricati = false;

    static std::wstring percorsoFissati(bool creaDir = false) {
        wchar_t base[MAX_PATH] = L"";
        if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, base)) || !base[0]) return {};
        const std::wstring dir = std::wstring(base) + L"\\FluidWin";
        if (creaDir) CreateDirectoryW(dir.c_str(), nullptr);
        return dir + L"\\appunti_fissati.bin";
    }

    static bool scriviTutto(HANDLE h, const void* dati, size_t n) {
        const BYTE* p = (const BYTE*)dati;
        while (n > 0) {
            const DWORD chunk = (DWORD)std::min<size_t>(n, 0x40000000u);
            DWORD scritti = 0;
            if (!WriteFile(h, p, chunk, &scritti, nullptr) || scritti == 0) return false;
            p += scritti;
            n -= scritti;
        }
        return true;
    }

    static bool leggiTutto(HANDLE h, void* dati, size_t n) {
        BYTE* p = (BYTE*)dati;
        while (n > 0) {
            const DWORD chunk = (DWORD)std::min<size_t>(n, 0x40000000u);
            DWORD letti = 0;
            if (!ReadFile(h, p, chunk, &letti, nullptr) || letti == 0) return false;
            p += letti;
            n -= letti;
        }
        return true;
    }

    template <typename T>
    static bool scriviVal(HANDLE h, const T& val) {
        return scriviTutto(h, &val, sizeof(T));
    }

    template <typename T>
    static bool leggiVal(HANDLE h, T& val) {
        return leggiTutto(h, &val, sizeof(T));
    }

    void salvaFissati() const {
        std::vector<const Voce*> fissate;
        for (const Voce& v : voci) {
            if (v.fissata) fissate.push_back(&v);
        }
        if (fissate.empty()) {
            const std::wstring path = percorsoFissati(false);
            if (!path.empty()) DeleteFileW(path.c_str());
            return;
        }
        const std::wstring path = percorsoFissati(true);
        if (path.empty()) return;
        const std::wstring tmp = path + L".tmp";
        HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;

        static constexpr char MAGIA[4] = {'F', 'W', 'C', 'B'};
        const uint32_t ver = VERSIONE_FISSATI;
        const uint32_t count = (uint32_t)std::min<size_t>(fissate.size(), MAX_FISSATI);
        bool ok = scriviTutto(h, MAGIA, 4) && scriviVal(h, ver) && scriviVal(h, count);

        for (uint32_t i = 0; i < count && ok; i++) {
            const Voce& v = *fissate[i];
            const uint8_t img = v.immagine ? 1u : 0u;
            const uint64_t quando = (uint64_t)v.quando;
            const uint32_t sorgLen = (uint32_t)std::min<size_t>(v.sorgente.size() * sizeof(wchar_t), MAX_SORG_BYTES);
            ok = scriviVal(h, img) && scriviVal(h, quando) && scriviVal(h, sorgLen);
            if (ok && sorgLen > 0) ok = scriviTutto(h, v.sorgente.data(), sorgLen);

            if (!ok) break;
            if (!v.immagine) {
                const uint32_t testoLen = (uint32_t)std::min<size_t>(v.testo.size() * sizeof(wchar_t), MAX_TESTO_BYTES);
                ok = scriviVal(h, testoLen);
                if (ok && testoLen > 0) ok = scriviTutto(h, v.testo.data(), testoLen);
            } else {
                const int32_t w = (int32_t)v.w;
                const int32_t hImg = (int32_t)v.h;
                const int32_t aw = (int32_t)v.aw;
                const int32_t ah = (int32_t)v.ah;
                const uint32_t dibLen = (uint32_t)v.dib.size();
                const uint32_t antLen = (uint32_t)(v.anteprima.size() * sizeof(UINT));
                ok = scriviVal(h, w) && scriviVal(h, hImg) && scriviVal(h, aw) && scriviVal(h, ah)
                  && scriviVal(h, dibLen);
                if (ok && dibLen > 0) ok = scriviTutto(h, v.dib.data(), dibLen);
                if (ok) ok = scriviVal(h, antLen);
                if (ok && antLen > 0) ok = scriviTutto(h, v.anteprima.data(), antLen);
            }
        }

        CloseHandle(h);
        if (ok) {
            if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) DeleteFileW(tmp.c_str());
        } else {
            DeleteFileW(tmp.c_str());
        }
    }

    void caricaFissati() {
        const std::wstring path = percorsoFissati(false);
        if (path.empty()) return;
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;

        char magic[4]{};
        uint32_t ver = 0, count = 0;
        bool ok = leggiTutto(h, magic, 4) && leggiVal(h, ver) && leggiVal(h, count);
        if (!ok || memcmp(magic, "FWCB", 4) != 0 || ver != VERSIONE_FISSATI || count > MAX_FISSATI) {
            CloseHandle(h);
            return;
        }

        FILETIME ftOra{};
        GetSystemTimeAsFileTime(&ftOra);
        const INT64 oraFt = (INT64)(((uint64_t)ftOra.dwHighDateTime << 32) | (uint64_t)ftOra.dwLowDateTime);
        const ULONGLONG oraTick = GetTickCount64();

        std::vector<Voce> caricate;
        caricate.reserve(count);

        for (uint32_t i = 0; i < count; i++) {
            uint8_t img = 0;
            uint64_t quando = 0;
            uint32_t sorgLen = 0;
            if (!leggiVal(h, img) || !leggiVal(h, quando) || !leggiVal(h, sorgLen)) { ok = false; break; }
            if (img > 1u || sorgLen > MAX_SORG_BYTES || (sorgLen % sizeof(wchar_t)) != 0) { ok = false; break; }

            Voce v;
            v.immagine = (img != 0);
            v.fissata = true;
            v.quando = (INT64)quando;
            const ULONGLONG diffMs = (oraFt > v.quando && v.quando > 0) ? (ULONGLONG)((oraFt - v.quando) / 10000LL) : 0ULL;
            v.tick = (oraTick > diffMs) ? (oraTick - diffMs) : 0ULL;

            if (sorgLen > 0) {
                v.sorgente.resize(sorgLen / sizeof(wchar_t));
                if (!leggiTutto(h, &v.sorgente[0], sorgLen)) { ok = false; break; }
            }

            if (!v.immagine) {
                uint32_t testoLen = 0;
                if (!leggiVal(h, testoLen) || testoLen > MAX_TESTO_BYTES || (testoLen % sizeof(wchar_t)) != 0) { ok = false; break; }
                if (testoLen > 0) {
                    v.testo.resize(testoLen / sizeof(wchar_t));
                    if (!leggiTutto(h, &v.testo[0], testoLen)) { ok = false; break; }
                }
                v.anteprimaTesto = normalizzaAnteprima(v.testo);
            } else {
                int32_t w = 0, hImg = 0, aw = 0, ah = 0;
                uint32_t dibLen = 0, antLen = 0;
                if (!leggiVal(h, w) || !leggiVal(h, hImg) || !leggiVal(h, aw) || !leggiVal(h, ah) || !leggiVal(h, dibLen)) { ok = false; break; }
                if (w <= 0 || w > 32768 || hImg <= 0 || hImg > 32768 || aw <= 0 || aw > 1024 || ah <= 0 || ah > 1024) { ok = false; break; }
                if (dibLen <= sizeof(BITMAPINFOHEADER) || dibLen > MAX_DIB_BYTES) { ok = false; break; }
                v.dib.resize(dibLen);
                if (!leggiTutto(h, v.dib.data(), dibLen)) { ok = false; break; }
                if (inizioPixelDib(v.dib.data(), v.dib.size()) == 0) { ok = false; break; }

                if (!leggiVal(h, antLen) || antLen > MAX_ANT_BYTES || (antLen % sizeof(UINT)) != 0) { ok = false; break; }
                if ((uint64_t)aw * (uint64_t)ah * sizeof(UINT) != (uint64_t)antLen) { ok = false; break; }
                v.anteprima.resize(antLen / sizeof(UINT));
                if (antLen > 0 && !leggiTutto(h, v.anteprima.data(), antLen)) { ok = false; break; }
                v.w = (int)w;
                v.h = (int)hImg;
                v.aw = (int)aw;
                v.ah = (int)ah;
            }

            v.id = ++ultimoId;
            caricate.push_back(std::move(v));
        }

        CloseHandle(h);
        if (!ok || caricate.empty()) return;

        voci.insert(voci.begin(), std::make_move_iterator(caricate.begin()), std::make_move_iterator(caricate.end()));
        versione++;
    }

    bool apri() {
        for (int i = 0; i < 5; i++) {
            if (OpenClipboard(finestra)) return true;
            Sleep(15);
        }
        return false;
    }

    void inserisciNelGruppo(Voce&& v) {
        if (v.fissata) {
            voci.insert(voci.begin(), std::move(v));
        } else {
            auto pos = std::find_if(voci.begin(), voci.end(), [](const Voce& x) { return !x.fissata; });
            voci.insert(pos, std::move(v));
        }
    }

    void pulisciLimite() {
        int nonFissate = 0;
        for (size_t i = 0; i < voci.size();) {
            if (!voci[i].fissata) {
                if (++nonFissate > 20) {
                    voci.erase(voci.begin() + (ptrdiff_t)i);
                    continue;
                }
            }
            i++;
        }
    }

    static std::wstring normalizzaAnteprima(const std::wstring& s) {
        std::wstring out;
        out.reserve(std::min<size_t>(s.size(), 260));
        bool spazio = false;
        for (wchar_t c : s) {
            if (out.size() >= 260) break;
            if (c == L'\r' || c == L'\n' || c == L'\t' || iswspace(c)) {
                if (!spazio && !out.empty()) out.push_back(L' ');
                spazio = true;
            } else {
                out.push_back(c);
                spazio = false;
            }
        }
        while (!out.empty() && out.back() == L' ') out.pop_back();
        return out;
    }

    static std::wstring nomeExeDaFinestra(HWND hw) {
        if (!hw) return {};
        DWORD pid = 0;
        GetWindowThreadProcessId(hw, &pid);
        if (!pid) return {};
        HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!hp) return {};
        wchar_t buf[MAX_PATH] = L"";
        DWORD len = MAX_PATH;
        std::wstring nome;
        if (QueryFullProcessImageNameW(hp, 0, buf, &len) && len > 0) {
            const wchar_t* base = wcsrchr(buf, L'\\');
            nome = base ? (base + 1) : buf;
            if (nome.size() > 4) {
                std::wstring ext = nome.substr(nome.size() - 4);
                for (wchar_t& ch : ext) ch = (wchar_t)towlower(ch);
                if (ext == L".exe") nome.resize(nome.size() - 4);
            }
            if (!nome.empty()) nome[0] = (wchar_t)towupper(nome[0]);
        }
        CloseHandle(hp);
        return nome;
    }

    static std::wstring nomeSorgente() {
        std::wstring s = nomeExeDaFinestra(GetClipboardOwner());
        if (s.empty()) s = nomeExeDaFinestra(GetForegroundWindow());
        return s;
    }

    static bool creaAnteprima(Voce& v) {
        const size_t off = inizioPixelDib(v.dib.data(), v.dib.size());
        if (!off) return false;
        const BITMAPINFOHEADER* bh = (const BITMAPINFOHEADER*)v.dib.data();
        v.w = bh->biWidth;
        v.h = std::abs(bh->biHeight);
        const double k = std::min(1.0, std::min(180.0 / v.h, 320.0 / v.w));
        v.aw = std::max(1, (int)std::lround(v.w * k));
        v.ah = std::max(1, (int)std::lround(v.h * k));
        BITMAPINFO bi{};
        bi.bmiHeader = {sizeof(BITMAPINFOHEADER), v.aw, -v.ah, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
        void* bits = nullptr;
        HDC scr = GetDC(nullptr), mem = CreateCompatibleDC(scr);
        HBITMAP b = CreateDIBSection(scr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        bool ok = false;
        if (b && bits) {
            HGDIOBJ vec = SelectObject(mem, b);
            SetStretchBltMode(mem, HALFTONE);
            SetBrushOrgEx(mem, 0, 0, nullptr);
            ok = StretchDIBits(mem, 0, 0, v.aw, v.ah, 0, 0, v.w, v.h,
                               v.dib.data() + off, (const BITMAPINFO*)bh, DIB_RGB_COLORS, SRCCOPY) > 0;
            GdiFlush();
            if (ok) {
                v.anteprima.assign((UINT*)bits, (UINT*)bits + (size_t)v.aw * v.ah);
                for (UINT& px : v.anteprima) px |= 0xFF000000u;
            }
            SelectObject(mem, vec);
        }
        if (b) DeleteObject(b);
        DeleteDC(mem);
        ReleaseDC(nullptr, scr);
        return ok;
    }
};

// ---- Finestra di vetro degli appunti (Win+V) ----
class Appunti {
public:
    ~Appunti() { distruggi(); }

    void cattura() {
        if (Cronologia::io().cambiato() && aperta()) {
            aggiornaFiltro();
            disegna();
        }
    }

    bool aperta() const { return stato == 1 || stato == 2; }

    void alterna() {
        if (aperta()) chiudi();
        else apri();
    }

    void apri() {
        if (!v.fin && !v.crea(L"FluidWin.Appunti", vetro::procedura<Appunti>, this, true, 2, true, true)) return;
        HWND fg = GetForegroundWindow();
        if (fg && fg != v.fin) precedente = fg;

        v.aggiornaTema();
        POINT pt{};
        GetCursorPos(&pt);
        HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoW(mon, &mi)) {
            mi.rcWork = vetro::areaPrincipale(true);
        }
        v.dpi = vetro::dpiMonitor(mon);
        const float d = v.dpi;
        assicuraFormati(d);

        const int margine = (int)std::lround(12 * d);
        const int workW = (int)(mi.rcWork.right - mi.rcWork.left);
        const int workH = (int)(mi.rcWork.bottom - mi.rcWork.top);
        destW = std::min((int)std::lround(384 * d), std::max(240, workW - margine * 2));
        destH = std::min((int)std::lround(484 * d), std::max(260, workH - margine * 2));

        int x = pt.x - destW / 2;
        int y = pt.y - (int)std::lround(28 * d);
        const int minX = (int)mi.rcWork.left + margine;
        const int maxX = std::max(minX, (int)mi.rcWork.right - margine - destW);
        const int minY = (int)mi.rcWork.top + margine;
        const int maxY = std::max(minY, (int)mi.rcWork.bottom - margine - destH);
        destX = std::clamp(x, minX, maxX);
        destY = std::clamp(y, minY, maxY);

        ricerca.clear();
        scrollY = 0;
        sel = 0;
        sopra = -1;
        sopraAzione = 0;
        sopraSvuota = false;
        menuAperto = false;
        aggiornaFiltro();

        KillTimer(v.fin, TIMER_INCOLLA);
        stato = 1;
        tAnim = vetro::ms();
        const int offsetY = (int)std::lround(10 * d);
        v.posiziona(destX, destY + offsetY, destW, destH, HWND_TOPMOST, SWP_SHOWWINDOW);
        v.alfa(0);
        prendiFuoco();
        vetro::battito(v.fin, 1);
        disegna();
    }

    void chiudi(bool anima = true) {
        if (stato == 0 || (stato == 3 && anima)) return;
        if (!anima) {
            vetro::fermaBattito(v.fin, 1);
            stato = 0;
            if (v.fin) ShowWindow(v.fin, SW_HIDE);
            return;
        }
        stato = 3;
        tAnim = vetro::ms();
        vetro::battito(v.fin, 1);
    }

    void distruggi() {
        if (v.fin) {
            vetro::fermaBattito(v.fin, 1);
            KillTimer(v.fin, TIMER_INCOLLA);
        }
        liberaBitmap();
        liberaFormati();
        v.chiudi();
        stato = 0;
    }

    bool messaggio(HWND hw, UINT m, WPARAM w, LPARAM l, LRESULT& r) {
        r = 0;
        switch (m) {
            case WM_TIMER:
                if (w == 1) { passoAnimazione(); return true; }
                if (w == TIMER_INCOLLA) {
                    KillTimer(hw, TIMER_INCOLLA);
                    inviaCtrlV();
                    return true;
                }
                return false;

            case WM_PAINT:
                disegna();
                return true;

            case WM_ACTIVATE:
                if (LOWORD(w) == WA_INACTIVE && aperta() && !menuAperto) {
                    chiudi(true);
                }
                return true;

            case WM_CLOSE:
                chiudi(true);
                return true;

            case WM_KEYDOWN:
                if (!aperta()) return true;
                if (w == VK_ESCAPE) {
                    chiudi(true);
                    return true;
                }
                if (w == VK_UP) {
                    if (!filtrate.empty()) {
                        sel = std::max(0, sel - 1);
                        assicuraVisibile(sel);
                        disegna();
                    }
                    return true;
                }
                if (w == VK_DOWN) {
                    if (!filtrate.empty()) {
                        sel = std::min((int)filtrate.size() - 1, sel + 1);
                        assicuraVisibile(sel);
                        disegna();
                    }
                    return true;
                }
                if (w == VK_RETURN) {
                    if (sel >= 0 && sel < (int)filtrate.size()) {
                        incolla(filtrate[sel], false);
                    }
                    return true;
                }
                if (w == VK_BACK) {
                    if (!ricerca.empty()) {
                        if (GetKeyState(VK_CONTROL) & 0x8000) ricerca.clear();
                        else ricerca.pop_back();
                        aggiornaFiltro();
                        disegna();
                    }
                    return true;
                }
                return false;

            case WM_CHAR: {
                if (!aperta()) return true;
                if (w < 32 || w == 127 || (GetKeyState(VK_CONTROL) & 0x8000)) return true;
                if (ricerca.size() < 128) {
                    ricerca.push_back((wchar_t)w);
                    aggiornaFiltro();
                    disegna();
                }
                return true;
            }

            case WM_MOUSEWHEEL: {
                if (!aperta()) return true;
                const int delta = GET_WHEEL_DELTA_WPARAM(w);
                const float passo = 42.0f * v.dpi;
                scrollY = std::clamp(scrollY - (delta / 120.0f) * passo, 0.0f, maxScroll());
                aggiornaHover(ultimoMouseX, ultimoMouseY);
                disegna();
                return true;
            }

            case WM_MOUSEMOVE: {
                if (!aperta()) return true;
                ultimoMouseX = (float)GET_X_LPARAM(l);
                ultimoMouseY = (float)GET_Y_LPARAM(l);
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hw, 0};
                TrackMouseEvent(&tme);
                if (aggiornaHover(ultimoMouseX, ultimoMouseY)) disegna();
                return true;
            }

            case WM_MOUSELEAVE:
                if (sopra != -1 || sopraAzione != 0 || sopraSvuota) {
                    sopra = -1;
                    sopraAzione = 0;
                    sopraSvuota = false;
                    disegna();
                }
                return true;

            case WM_LBUTTONUP: {
                if (!aperta()) return true;
                const float mx = (float)GET_X_LPARAM(l);
                const float my = (float)GET_Y_LPARAM(l);
                if (puntoIn(rettSvuota(), mx, my) && haNonFissate()) {
                    Cronologia::io().svuotaNonFissate();
                    aggiornaFiltro();
                    disegna();
                    return true;
                }
                int idx = -1, az = 0;
                if (colpisceLista(mx, my, idx, az) && idx >= 0 && idx < (int)filtrate.size()) {
                    const UINT id = filtrate[idx];
                    if (az == 1) {
                        Cronologia::io().fissa(id);
                        aggiornaFiltro();
                        disegna();
                    } else if (az == 2) {
                        Cronologia::io().togli(id);
                        aggiornaFiltro();
                        disegna();
                    } else {
                        incolla(id, false);
                    }
                }
                return true;
            }

            case WM_RBUTTONUP: {
                if (!aperta()) return true;
                const float mx = (float)GET_X_LPARAM(l);
                const float my = (float)GET_Y_LPARAM(l);
                int idx = -1, az = 0;
                if (!colpisceLista(mx, my, idx, az) || idx < 0 || idx >= (int)filtrate.size()) return true;
                const UINT id = filtrate[idx];
                const Voce* voce = trovaVoce(id);
                if (!voce) return true;

                sel = idx;
                disegna();

                POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
                ClientToScreen(hw, &pt);
                const std::vector<menu::Voce> vociMenu = {
                    menu::voce(testi::t(L"Incolla", L"Paste"), 1),
                    menu::voce(testi::t(L"Copia soltanto", L"Copy only"), 2),
                    menu::voce(voce->fissata ? testi::t(L"Sblocca", L"Unpin") : testi::t(L"Fissa", L"Pin"), 3),
                    menu::separatore(),
                    menu::voce(testi::t(L"Elimina", L"Delete"), 4),
                };

                menuAperto = true;
                const UINT scelta = menu::mostra(vociMenu, pt, hw);
                menuAperto = false;

                if (scelta == 1) {
                    incolla(id, false);
                } else if (scelta == 2) {
                    incolla(id, true);
                } else if (scelta == 3) {
                    Cronologia::io().fissa(id);
                    aggiornaFiltro();
                    SetForegroundWindow(hw);
                    disegna();
                } else if (scelta == 4) {
                    Cronologia::io().togli(id);
                    aggiornaFiltro();
                    SetForegroundWindow(hw);
                    disegna();
                } else if (aperta() && GetForegroundWindow() != hw) {
                    chiudi(true);
                }
                return true;
            }
        }
        return false;
    }

private:
    static constexpr UINT TIMER_INCOLLA = 2;
    static constexpr double DURATA_APRI_MS = 160.0;
    static constexpr double DURATA_CHIUDI_MS = 120.0;

    vetro::Finestra v;
    IDWriteTextFormat *fTitolo = nullptr, *fTesto = nullptr, *fMeta = nullptr, *fPiccolo = nullptr, *fVuoto = nullptr;
    float fDpi = 0;

    std::unordered_map<UINT, ID2D1Bitmap*> bmpCache;
    unsigned bmpGiro = 0;

    HWND precedente = nullptr;
    int stato = 0;   // 0 chiusa, 1 apertura, 2 aperta, 3 chiusura
    ULONGLONG tAnim = 0;
    int destX = 0, destY = 0, destW = 384, destH = 484;

    std::wstring ricerca;
    std::vector<UINT> filtrate;
    float scrollY = 0;
    int sel = 0;
    int sopra = -1;
    int sopraAzione = 0;   // 0 scheda, 1 fissa, 2 elimina
    bool sopraSvuota = false;
    bool menuAperto = false;
    float ultimoMouseX = -1, ultimoMouseY = -1;

    void liberaBitmap() {
        for (auto& kv : bmpCache) {
            if (kv.second) kv.second->Release();
        }
        bmpCache.clear();
    }

    void liberaFormati() {
        for (IDWriteTextFormat** f : {&fTitolo, &fTesto, &fMeta, &fPiccolo, &fVuoto}) {
            if (*f) {
                (*f)->Release();
                *f = nullptr;
            }
        }
        fDpi = 0;
    }

    void assicuraFormati(float d) {
        if (fTitolo && std::abs(fDpi - d) < 0.01f) return;
        liberaFormati();
        fDpi = d;
        fTitolo = v.formato(16.0f * d, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        fTesto = v.formato(13.0f * d, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, true);
        fMeta = v.formato(11.0f * d, DWRITE_FONT_WEIGHT_NORMAL);
        fPiccolo = v.formato(12.0f * d, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_TEXT_ALIGNMENT_CENTER);
        fVuoto = v.formato(13.0f * d, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, true);
    }

    void prendiFuoco() {
        HWND fg = GetForegroundWindow();
        DWORD cur = GetCurrentThreadId();
        DWORD tid = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
        bool att = (tid && tid != cur) ? (AttachThreadInput(cur, tid, TRUE) != FALSE) : false;
        SetForegroundWindow(v.fin);
        SetActiveWindow(v.fin);
        SetFocus(v.fin);
        if (att) AttachThreadInput(cur, tid, FALSE);
    }

    void passoAnimazione() {
        const double dt = (double)(vetro::ms() - tAnim);
        const float d = v.dpi;
        if (stato == 1) {
            const double p = std::clamp(dt / DURATA_APRI_MS, 0.0, 1.0);
            const double k = vetro::uscita(p);
            const int oy = (int)std::lround((1.0 - k) * 10.0 * d);
            SetWindowPos(v.fin, HWND_TOPMOST, destX, destY + oy, destW, destH, SWP_NOACTIVATE);
            v.alfa(k);
            disegna();
            if (p >= 1.0) {
                stato = 2;
                v.alfa(1.0);
                vetro::fermaBattito(v.fin, 1);
            }
        } else if (stato == 3) {
            const double p = std::clamp(dt / DURATA_CHIUDI_MS, 0.0, 1.0);
            const double k = vetro::uscita(p);
            const int oy = (int)std::lround(k * 8.0 * d);
            SetWindowPos(v.fin, HWND_TOPMOST, destX, destY + oy, destW, destH, SWP_NOACTIVATE);
            v.alfa(1.0 - k);
            if (p >= 1.0) {
                stato = 0;
                vetro::fermaBattito(v.fin, 1);
                ShowWindow(v.fin, SW_HIDE);
            }
        } else {
            vetro::fermaBattito(v.fin, 1);
        }
    }

    static bool contieneSenzaMaiusc(const std::wstring& testo, const std::wstring& ago) {
        if (ago.empty()) return true;
        auto it = std::search(
            testo.begin(), testo.end(),
            ago.begin(), ago.end(),
            [](wchar_t a, wchar_t b) { return towlower(a) == towlower(b); }
        );
        return it != testo.end();
    }

    static const Voce* trovaVoce(UINT id) {
        const auto& voci = Cronologia::io().voci;
        for (const auto& vc : voci) {
            if (vc.id == id) return &vc;
        }
        return nullptr;
    }

    static bool haNonFissate() {
        for (const auto& vc : Cronologia::io().voci) {
            if (!vc.fissata) return true;
        }
        return false;
    }

    void aggiornaFiltro() {
        filtrate.clear();
        const auto& voci = Cronologia::io().voci;
        for (const auto& vc : voci) {
            if (ricerca.empty()) {
                filtrate.push_back(vc.id);
            } else {
                const bool matchTesto = !vc.immagine && contieneSenzaMaiusc(vc.testo, ricerca);
                const bool matchApp = contieneSenzaMaiusc(vc.sorgente, ricerca);
                const bool matchImg = vc.immagine && (contieneSenzaMaiusc(L"image", ricerca) || contieneSenzaMaiusc(L"immagine", ricerca));
                if (matchTesto || matchApp || matchImg) filtrate.push_back(vc.id);
            }
        }
        if (filtrate.empty()) sel = 0;
        else sel = std::clamp(sel, 0, (int)filtrate.size() - 1);
        scrollY = std::clamp(scrollY, 0.0f, maxScroll());
    }

    D2D1_RECT_F rettSvuota() const {
        const float d = v.dpi;
        const float pw = 66.0f * d, ph = 26.0f * d;
        const float right = (float)v.W - 14.0f * d;
        const float top = (48.0f * d - ph) * 0.5f;
        return {right - pw, top, right, top + ph};
    }

    D2D1_RECT_F rettRicerca() const {
        const float d = v.dpi;
        return {12.0f * d, 48.0f * d, (float)v.W - 12.0f * d, 84.0f * d};
    }

    D2D1_RECT_F rettLista() const {
        const float d = v.dpi;
        return {0.0f, 92.0f * d, (float)v.W, (float)v.H - 8.0f * d};
    }

    float altezzaScheda(const Voce& vc) const {
        return (vc.immagine ? 108.0f : 78.0f) * v.dpi;
    }

    float altezzaTotaleLista() const {
        const float gap = 6.0f * v.dpi;
        float tot = 0.0f;
        for (size_t i = 0; i < filtrate.size(); i++) {
            const Voce* vc = trovaVoce(filtrate[i]);
            if (!vc) continue;
            tot += altezzaScheda(*vc);
            if (i + 1 < filtrate.size()) tot += gap;
        }
        return tot;
    }

    float maxScroll() const {
        const D2D1_RECT_F area = rettLista();
        const float vis = std::max(0.0f, area.bottom - area.top);
        return std::max(0.0f, altezzaTotaleLista() - vis);
    }

    void assicuraVisibile(int idx) {
        if (idx < 0 || idx >= (int)filtrate.size()) return;
        const float gap = 6.0f * v.dpi;
        float y = 0.0f, h = 0.0f;
        for (int i = 0; i <= idx; i++) {
            const Voce* vc = trovaVoce(filtrate[i]);
            h = vc ? altezzaScheda(*vc) : 78.0f * v.dpi;
            if (i < idx) y += h + gap;
        }
        const D2D1_RECT_F area = rettLista();
        const float vis = std::max(1.0f, area.bottom - area.top);
        if (y < scrollY) scrollY = y;
        else if (y + h > scrollY + vis) scrollY = y + h - vis;
        scrollY = std::clamp(scrollY, 0.0f, maxScroll());
    }

    static bool puntoIn(const D2D1_RECT_F& r, float x, float y) {
        return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
    }

    void rettPulsantiScheda(const D2D1_RECT_F& card, D2D1_RECT_F& rPin, D2D1_RECT_F& rDel) const {
        const float d = v.dpi;
        const float s = 24.0f * d;
        const float top = card.top + 8.0f * d;
        rDel = {card.right - 8.0f * d - s, top, card.right - 8.0f * d, top + s};
        rPin = {rDel.left - 4.0f * d - s, top, rDel.left - 4.0f * d, top + s};
    }

    bool colpisceLista(float mx, float my, int& outIdx, int& outAzione) const {
        outIdx = -1;
        outAzione = 0;
        const D2D1_RECT_F area = rettLista();
        if (!puntoIn(area, mx, my)) return false;

        const float d = v.dpi;
        const float gap = 6.0f * d;
        float y = area.top - scrollY;
        for (int i = 0; i < (int)filtrate.size(); i++) {
            const Voce* vc = trovaVoce(filtrate[i]);
            if (!vc) continue;
            const float h = altezzaScheda(*vc);
            const D2D1_RECT_F card = {12.0f * d, y, (float)v.W - 12.0f * d, y + h};
            if (puntoIn(card, mx, my)) {
                outIdx = i;
                D2D1_RECT_F rPin{}, rDel{};
                rettPulsantiScheda(card, rPin, rDel);
                if (puntoIn(rPin, mx, my)) outAzione = 1;
                else if (puntoIn(rDel, mx, my)) outAzione = 2;
                return true;
            }
            y += h + gap;
        }
        return false;
    }

    bool aggiornaHover(float mx, float my) {
        const bool sv = puntoIn(rettSvuota(), mx, my);
        int idx = -1, az = 0;
        colpisceLista(mx, my, idx, az);
        const bool cambiato = (sv != sopraSvuota) || (idx != sopra) || (az != sopraAzione);
        sopraSvuota = sv;
        sopra = idx;
        sopraAzione = az;
        return cambiato;
    }

    void incolla(UINT id, bool soloCopia = false) {
        if (!Cronologia::io().ricopia(id, true)) return;
        chiudi(false);
        if (!soloCopia && !(GetKeyState(VK_SHIFT) & 0x8000) && precedente && IsWindow(precedente)) {
            SetForegroundWindow(precedente);
            SetTimer(v.fin, TIMER_INCOLLA, 45, nullptr);
        }
    }

    static void inviaCtrlV() {
        INPUT in[4]{};
        in[0].type = INPUT_KEYBOARD;
        in[0].ki.wVk = VK_CONTROL;
        in[1].type = INPUT_KEYBOARD;
        in[1].ki.wVk = 'V';
        in[2].type = INPUT_KEYBOARD;
        in[2].ki.wVk = 'V';
        in[2].ki.dwFlags = KEYEVENTF_KEYUP;
        in[3].type = INPUT_KEYBOARD;
        in[3].ki.wVk = VK_CONTROL;
        in[3].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(4, in, sizeof(INPUT));
    }

    static std::wstring tempoRelativo(INT64 quando) {
        FILETIME ft{};
        GetSystemTimeAsFileTime(&ft);
        const INT64 ora = (INT64)(((uint64_t)ft.dwHighDateTime << 32) | (uint64_t)ft.dwLowDateTime);
        const ULONGLONG sec = (ora > quando && quando > 0) ? (ULONGLONG)((ora - quando) / 10000000LL) : 0ULL;
        if (sec < 60) return testi::t(L"Adesso", L"Just now");
        wchar_t buf[32]{};
        if (sec < 3600) {
            swprintf(buf, 32, testi::t(L"%llu min fa", L"%llum ago"), (unsigned long long)(sec / 60));
            return buf;
        }
        if (sec < 86400) {
            swprintf(buf, 32, testi::t(L"%llu h fa", L"%lluh ago"), (unsigned long long)(sec / 3600));
            return buf;
        }
        swprintf(buf, 32, testi::t(L"%llu g fa", L"%llud ago"), (unsigned long long)(sec / 86400));
        return buf;
    }

    ID2D1Bitmap* ottieniBitmap(const Voce& vc) {
        if (!v.rt || vc.anteprima.empty() || vc.aw <= 0 || vc.ah <= 0) return nullptr;
        auto it = bmpCache.find(vc.id);
        if (it != bmpCache.end()) return it->second;
        ID2D1Bitmap* bmp = nullptr;
        const HRESULT hr = v.rt->CreateBitmap(
            D2D1::SizeU((UINT32)vc.aw, (UINT32)vc.ah),
            vc.anteprima.data(),
            (UINT32)(vc.aw * 4),
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
            &bmp
        );
        if (SUCCEEDED(hr) && bmp) {
            bmpCache[vc.id] = bmp;
            return bmp;
        }
        return nullptr;
    }

    void disegnaPuntina(float cx, float cy, float d, bool piena, D2D1_COLOR_F col) {
        if (!v.f2d || !v.rt) return;
        ID2D1PathGeometry* geo = nullptr;
        if (FAILED(v.f2d->CreatePathGeometry(&geo)) || !geo) return;
        ID2D1GeometrySink* sink = nullptr;
        if (SUCCEEDED(geo->Open(&sink)) && sink) {
            // piccola puntina inclinata di 45 gradi
            sink->BeginFigure({cx - 1.5f * d, cy - 5.0f * d}, piena ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddLine({cx + 4.5f * d, cy + 1.0f * d});
            sink->AddLine({cx + 1.5f * d, cy + 2.5f * d});
            sink->AddLine({cx - 0.5f * d, cy + 0.5f * d});
            sink->AddLine({cx - 3.0f * d, cy - 2.0f * d});
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            sink->Release();
            v.pen->SetColor(col);
            if (piena) v.rt->FillGeometry(geo, v.pen);
            else v.rt->DrawGeometry(geo, v.pen, 1.3f * d);
            v.rt->DrawLine({cx - 1.0f * d, cy + 1.0f * d}, {cx - 4.5f * d, cy + 4.5f * d}, v.pen, 1.4f * d);
        }
        geo->Release();
    }

    void disegna() {
        if (!v.rt || !v.fin) return;
        if (v.giroRt != bmpGiro) {
            liberaBitmap();
            bmpGiro = v.giroRt;
        }
        // pulizia delle bitmap di voci eliminate
        const auto& tutte = Cronologia::io().voci;
        for (auto it = bmpCache.begin(); it != bmpCache.end();) {
            const bool esiste = std::any_of(tutte.begin(), tutte.end(), [&](const Voce& vc) { return vc.id == it->first; });
            if (!esiste) {
                if (it->second) it->second->Release();
                it = bmpCache.erase(it);
            } else {
                ++it;
            }
        }

        if (!v.inizia(1.0f)) return;
        const float d = v.dpi;
        const bool sc = v.scuro;
        const D2D1_COLOR_F ink = v.inchiostro(1.0f);
        const D2D1_COLOR_F muted = v.inchiostro(0.55f);

        // riflesso speculare in cima (Liquid Glass)
        {
            D2D1_GRADIENT_STOP stops[2] = {
                {0.0f, sc ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.10f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.42f)},
                {1.0f, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.0f)}
            };
            ID2D1GradientStopCollection* gsc = nullptr;
            if (SUCCEEDED(v.rt->CreateGradientStopCollection(stops, 2, &gsc)) && gsc) {
                ID2D1LinearGradientBrush* sheen = nullptr;
                if (SUCCEEDED(v.rt->CreateLinearGradientBrush(
                        D2D1::LinearGradientBrushProperties({0.0f, 0.0f}, {0.0f, 56.0f * d}), gsc, &sheen)) && sheen) {
                    v.rt->FillRectangle({0.0f, 0.0f, (float)v.W, 56.0f * d}, sheen);
                    sheen->Release();
                }
                gsc->Release();
            }
        }

        // intestazione (48 * dpi)
        {
            const std::wstring titolo = testi::t(L"Appunti", L"Clipboard");
            const D2D1_RECT_F rTit = {16.0f * d, 0.0f, (float)v.W - 100.0f * d, 48.0f * d};
            v.testo(titolo, fTitolo, rTit, ink);

            const float tw = v.larghezza(titolo, fTitolo);
            wchar_t cnt[24]{};
            swprintf(cnt, 24, L"%zu", filtrate.size());
            const D2D1_RECT_F rCnt = {16.0f * d + tw + 8.0f * d, 2.0f * d, (float)v.W - 92.0f * d, 48.0f * d};
            v.testo(cnt, fMeta, rCnt, muted);

            const D2D1_RECT_F rSv = rettSvuota();
            const bool abil = haNonFissate();
            const D2D1_COLOR_F bgSv = !abil
                ? D2D1::ColorF(sc ? 1.0f : 0.0f, sc ? 1.0f : 0.0f, sc ? 1.0f : 0.0f, 0.04f)
                : sopraSvuota
                    ? (sc ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.16f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.10f))
                    : (sc ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.09f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.05f));
            v.pen->SetColor(bgSv);
            v.rt->FillRoundedRectangle({rSv, 13.0f * d, 13.0f * d}, v.pen);
            v.testo(testi::t(L"Svuota", L"Clear"), fPiccolo, rSv, abil ? ink : v.inchiostro(0.32f));
        }

        // barra di ricerca (36 * dpi)
        {
            const D2D1_RECT_F rRic = rettRicerca();
            v.pen->SetColor(sc ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.08f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.05f));
            v.rt->FillRoundedRectangle({rRic, 8.0f * d, 8.0f * d}, v.pen);
            v.pen->SetColor(sc ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.12f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.09f));
            v.rt->DrawRoundedRectangle({rRic, 8.0f * d, 8.0f * d}, v.pen, 1.0f);

            // lente di ricerca
            const float lx = rRic.left + 15.0f * d;
            const float ly = (rRic.top + rRic.bottom) * 0.5f - 1.0f * d;
            v.pen->SetColor(muted);
            v.rt->DrawEllipse({{lx, ly}, 4.8f * d, 4.8f * d}, v.pen, 1.4f * d);
            v.rt->DrawLine({lx + 3.4f * d, ly + 3.4f * d}, {lx + 6.8f * d, ly + 6.8f * d}, v.pen, 1.5f * d);

            const D2D1_RECT_F rTesto = {rRic.left + 30.0f * d, rRic.top, rRic.right - 10.0f * d, rRic.bottom};
            if (ricerca.empty()) {
                v.testo(testi::t(L"Cerca negli appunti…", L"Search clipboard…"), fMeta, rTesto, muted);
            } else {
                v.testo(ricerca, fMeta, rTesto, ink);
            }
        }

        // lista scorrevole delle schede
        const D2D1_RECT_F area = rettLista();
        v.rt->PushAxisAlignedClip(area, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        if (filtrate.empty()) {
            const std::wstring msg = Cronologia::io().voci.empty()
                ? testi::t(L"Nessun elemento negli appunti", L"Clipboard history is empty")
                : testi::t(L"Nessun risultato", L"No matches");
            const D2D1_RECT_F rMsg = {24.0f * d, area.top + (area.bottom - area.top) * 0.38f, (float)v.W - 24.0f * d, area.bottom};
            v.testo(msg, fVuoto, rMsg, muted);
        } else {
            const float gap = 6.0f * d;
            float y = area.top - scrollY;
            for (int i = 0; i < (int)filtrate.size(); i++) {
                const Voce* vc = trovaVoce(filtrate[i]);
                if (!vc) continue;
                const float h = altezzaScheda(*vc);
                const D2D1_RECT_F card = {12.0f * d, y, (float)v.W - 12.0f * d, y + h};

                if (card.bottom >= area.top && card.top <= area.bottom) {
                    const bool isSel = (i == sel);
                    const bool isHov = (i == sopra);
                    const D2D1_COLOR_F fill = isHov
                        ? (sc ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.13f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.075f))
                        : isSel
                            ? (sc ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.095f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.055f))
                            : (sc ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.055f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.42f));
                    v.pen->SetColor(fill);
                    v.rt->FillRoundedRectangle({card, 10.0f * d, 10.0f * d}, v.pen);

                    const D2D1_COLOR_F bordo = isSel
                        ? (sc ? D2D1::ColorF(0.38f, 0.68f, 1.0f, 0.55f) : D2D1::ColorF(0.0f, 0.45f, 0.88f, 0.45f))
                        : (sc ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.09f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.07f));
                    v.pen->SetColor(bordo);
                    v.rt->DrawRoundedRectangle({card, 10.0f * d, 10.0f * d}, v.pen, 1.0f);

                    const float pad = 12.0f * d;
                    const float rightLimit = (isHov || vc->fissata) ? (card.right - 66.0f * d) : (card.right - pad);

                    std::wstring meta;
                    if (!vc->sorgente.empty()) meta = vc->sorgente + L" · ";
                    meta += tempoRelativo(vc->quando);

                    if (vc->immagine) {
                        const float maxW = 96.0f * d, maxH = 84.0f * d;
                        const float scala = std::min(maxW / std::max(1, vc->aw), maxH / std::max(1, vc->ah));
                        const float iw = std::max(12.0f * d, vc->aw * scala);
                        const float ih = std::max(12.0f * d, vc->ah * scala);
                        const float ix = card.left + pad;
                        const float iy = card.top + (h - ih) * 0.5f;
                        const D2D1_RECT_F rImg = {ix, iy, ix + iw, iy + ih};
                        if (ID2D1Bitmap* bmp = ottieniBitmap(*vc)) {
                            v.rt->DrawBitmap(bmp, rImg, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                        }
                        v.pen->SetColor(sc ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.14f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.12f));
                        v.rt->DrawRoundedRectangle({rImg, 5.0f * d, 5.0f * d}, v.pen, 1.0f);

                        const float tx = ix + iw + 12.0f * d;
                        wchar_t dim[48]{};
                        swprintf(dim, 48, L"%d × %d", vc->w, vc->h);
                        v.testo(dim, fTesto, {tx, card.top + 22.0f * d, rightLimit, card.top + 46.0f * d}, ink);
                        v.testo(meta, fMeta, {tx, card.bottom - 30.0f * d, card.right - pad, card.bottom - 10.0f * d}, muted);
                    } else {
                        const std::wstring& snippet = vc->anteprimaTesto.empty() ? vc->testo : vc->anteprimaTesto;
                        v.testo(snippet, fTesto, {card.left + pad, card.top + 10.0f * d, rightLimit, card.top + 48.0f * d}, ink);
                        v.testo(meta, fMeta, {card.left + pad, card.bottom - 26.0f * d, card.right - pad, card.bottom - 8.0f * d}, muted);
                    }

                    // pulsanti azione (Pin e Elimina) in alto a destra
                    D2D1_RECT_F rPin{}, rDel{};
                    rettPulsantiScheda(card, rPin, rDel);
                    const D2D1_COLOR_F oro = sc ? D2D1::ColorF(1.0f, 0.78f, 0.22f, 0.95f) : D2D1::ColorF(0.85f, 0.50f, 0.0f, 0.95f);

                    if (isHov || vc->fissata) {
                        if (isHov) {
                            const bool hovPin = (sopraAzione == 1);
                            v.pen->SetColor(hovPin
                                ? (sc ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.18f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.11f))
                                : (sc ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.08f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.05f)));
                            v.rt->FillRoundedRectangle({rPin, 6.0f * d, 6.0f * d}, v.pen);
                        }
                        const float pcx = (rPin.left + rPin.right) * 0.5f;
                        const float pcy = (rPin.top + rPin.bottom) * 0.5f;
                        disegnaPuntina(pcx, pcy, d, vc->fissata, vc->fissata ? oro : v.inchiostro(0.75f));
                    }

                    if (isHov) {
                        const bool hovDel = (sopraAzione == 2);
                        v.pen->SetColor(hovDel
                            ? D2D1::ColorF(0.95f, 0.26f, 0.22f, sc ? 0.35f : 0.22f)
                            : (sc ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.08f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.05f)));
                        v.rt->FillRoundedRectangle({rDel, 6.0f * d, 6.0f * d}, v.pen);
                        const float dcx = (rDel.left + rDel.right) * 0.5f;
                        const float dcy = (rDel.top + rDel.bottom) * 0.5f;
                        const float arm = 3.8f * d;
                        v.pen->SetColor(hovDel ? D2D1::ColorF(1.0f, 0.36f, 0.32f, 1.0f) : v.inchiostro(0.80f));
                        v.rt->DrawLine({dcx - arm, dcy - arm}, {dcx + arm, dcy + arm}, v.pen, 1.4f * d);
                        v.rt->DrawLine({dcx + arm, dcy - arm}, {dcx - arm, dcy + arm}, v.pen, 1.4f * d);
                    }
                }
                y += h + gap;
            }
        }

        // barra di scorrimento sottile
        const float ms = maxScroll();
        if (ms > 0.5f) {
            const float vis = area.bottom - area.top;
            const float tot = vis + ms;
            const float thumbH = std::max(28.0f * d, vis * (vis / tot));
            const float thumbY = area.top + (scrollY / ms) * (vis - thumbH);
            const D2D1_RECT_F thumb = {(float)v.W - 5.5f * d, thumbY, (float)v.W - 2.5f * d, thumbY + thumbH};
            v.pen->SetColor(sc ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.24f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.22f));
            v.rt->FillRoundedRectangle({thumb, 1.5f * d, 1.5f * d}, v.pen);
        }

        v.rt->PopAxisAlignedClip();

        // bordo interno 1 px
        const float rag = v.raggio();
        v.pen->SetColor(sc ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.14f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.55f));
        v.rt->DrawRoundedRectangle({{0.5f, 0.5f, (float)v.W - 0.5f, (float)v.H - 0.5f}, rag, rag}, v.pen, 1.0f);

        v.finisci();
    }
};

}  // namespace appunti
