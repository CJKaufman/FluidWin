// Ricerca rapida (come Spotlight): Win+Tab apre una barra di vetro al centro dello schermo. Mentre si scrive mostra
// app, pagine delle impostazioni, file (dall'indice di ricerca di Windows, in un thread a parte), il risultato dei
// calcoli e la ricerca sul web. Frecce e Invio per scegliere, Esc per chiudere.
#pragma once
#include "vetro.h"
#include "onda.h"
#include "indice.h"
#include <windowsx.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <structuredquerycondition.h>
#include <propsys.h>
#include <winhttp.h>
#include <powrprof.h>
#include <iphlpapi.h>
#include <vector>
#include <deque>
#include <unordered_map>
#include <functional>
#include <cwctype>
#include <atomic>
#include <mutex>
#include <thread>
#include <ctime>

namespace ricerca {

enum Tipo { T_CALCOLO, T_AZIONE, T_APP, T_IMPOSTAZIONE, T_FILE, T_WEB };

struct Risultato {
    Tipo tipo = T_APP;
    std::wstring titolo, sotto, azione, icona;   // icona: percorso per la shell ("" = simbolo disegnato)
};

// ---- calcolatrice: + - * / ^ % e parentesi, virgola o punto per i decimali ----
class Calcolo {
public:
    // Limiti (audit FW-AUDIT-027): il parser e' ricorsivo e il testo puo' arrivare incollato; qualche migliaio di
    // parentesi esauriva lo stack del thread principale (provato: 100.000 parentesi, uscita C00000FD). Un calcolo vero
    // sta comodo in 200 caratteri; ogni parentesi costa tre livelli (somma, potenza, segno): 150 = circa 50 parentesi
    static constexpr size_t LUNGO_MAX = 200;
    static constexpr int PROFONDO_MAX = 150;
    static bool valuta(const std::wstring& s, double& out) {
        if (s.size() > LUNGO_MAX) return false;
        bool cifra = false, operatore = false;
        for (wchar_t c : s) {
            if (iswdigit(c)) cifra = true;
            else if (wcschr(L"+-*/^%x()", c)) operatore = true;
            else if (c != L' ' && c != L',' && c != L'.') return false;
        }
        if (!cifra || !operatore) return false;
        Calcolo c(s);
        out = c.somma();
        c.spazi();
        return c.ok && c.p == c.s.size() && std::isfinite(out);
    }
private:
    std::wstring s; size_t p = 0; bool ok = true; int prof = 0;
    explicit Calcolo(const std::wstring& t) : s(t) {}
    void spazi() { while (p < s.size() && s[p] == L' ') p++; }
    // un livello di ricorsione in piu' (parentesi, segno, potenza): oltre il limite il calcolo non vale
    struct Livello {
        Calcolo& c; bool troppo;
        explicit Livello(Calcolo& k) : c(k), troppo(++k.prof > PROFONDO_MAX) { if (troppo) c.ok = false; }
        ~Livello() { c.prof--; }
        Livello(const Livello&) = delete; Livello& operator=(const Livello&) = delete;
    };
    double somma() {
        const Livello l(*this); if (l.troppo) return 0;
        double v = prodotto();
        for (;;) { spazi(); if (p < s.size() && (s[p] == L'+' || s[p] == L'-')) { const wchar_t o = s[p++]; const double b = prodotto(); v = o == L'+' ? v + b : v - b; } else return v; }
    }
    double prodotto() {
        double v = potenza();
        for (;;) {
            spazi();
            if (p < s.size() && (s[p] == L'*' || s[p] == L'x' || s[p] == L'/')) { const wchar_t o = s[p++]; const double b = potenza(); v = o == L'/' ? v / b : v * b; }
            else if (p < s.size() && s[p] == L'%') { p++; v /= 100; }
            else return v;
        }
    }
    double potenza() {
        const Livello l(*this); if (l.troppo) return 0;
        const double b = unario(); spazi(); if (p < s.size() && s[p] == L'^') { p++; return std::pow(b, potenza()); } return b;
    }
    double unario() {
        const Livello l(*this); if (l.troppo) return 0;
        spazi(); if (p < s.size() && s[p] == L'-') { p++; return -unario(); } if (p < s.size() && s[p] == L'+') { p++; return unario(); } return atomo();
    }
    double atomo() {
        spazi();
        if (p < s.size() && s[p] == L'(') { p++; const double v = somma(); spazi(); if (p < s.size() && s[p] == L')') p++; else ok = false; return v; }
        std::wstring n;
        while (p < s.size() && (iswdigit(s[p]) || s[p] == L'.' || s[p] == L',')) n += s[p] == L',' ? L'.' : s[p], p++;
        if (n.empty()) { ok = false; return 0; }
        // il numero va letto tutto: "1.2.3" non e' un numero (wcstod leggeva "1.2" e il resto spariva, FW-AUDIT-028)
        wchar_t* fine = nullptr;
        const double v = wcstod(n.c_str(), &fine);
        if (!fine || *fine) ok = false;
        return v;
    }
};

namespace dettagli {

inline std::wstring minuscolo(std::wstring s) { for (auto& c : s) c = (wchar_t)towlower(c); return s; }
inline std::wstring maiuscolo(std::wstring s) { for (auto& c : s) c = (wchar_t)towupper(c); return s; }

inline bool leggiNumero(const std::wstring& s, size_t& p, bool consentiSegno, double& out) {
    size_t i = p;
    if (consentiSegno && i < s.size() && (s[i] == L'-' || s[i] == L'+')) i++;
    std::wstring buf;
    if (i > p) buf += s[p];
    bool cifra = false, punto = false;
    while (i < s.size()) {
        const wchar_t c = s[i];
        if (c >= L'0' && c <= L'9') { buf += c; cifra = true; i++; }
        else if ((c == L'.' || c == L',') && !punto) { buf += L'.'; punto = true; i++; }
        else break;
    }
    if (!cifra) return false;
    wchar_t* fine = nullptr;
    const double v = wcstod(buf.c_str(), &fine);
    if (!fine || *fine || !std::isfinite(v)) return false;
    out = v;
    p = i;
    return true;
}

inline bool leggiSeparatore(const std::wstring& s, size_t& p, bool spazioPrima) {
    if (p + 1 < s.size() && s[p] == L'-' && s[p + 1] == L'>') { p += 2; return true; }
    if (p < s.size() && s[p] == L'=') { p += 1; return true; }
    if (spazioPrima && p + 2 < s.size()) {
        const wchar_t a = (wchar_t)towlower(s[p]), b = (wchar_t)towlower(s[p + 1]);
        if (((a == L't' && b == L'o') || (a == L'i' && b == L'n')) && s[p + 2] == L' ') {
            p += 3;
            return true;
        }
    }
    return false;
}

inline std::wstring formattaNumero(double v, const wchar_t* fmt) {
    if (std::abs(v) < 1e-12) v = 0.0;
    wchar_t b[64];
    swprintf(b, 64, fmt, v);
    std::wstring s = b;
    const wchar_t vg = testi::virgola();
    for (auto& c : s) if (c == L'.') c = vg;
    return s;
}

struct WinHttpHandle {
    HINTERNET h = nullptr;
    ~WinHttpHandle() { if (h) WinHttpCloseHandle(h); }
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;
    explicit WinHttpHandle(HINTERNET x = nullptr) : h(x) {}
};

struct FileHandle {
    HANDLE h = INVALID_HANDLE_VALUE;
    ~FileHandle() { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    explicit FileHandle(HANDLE x = INVALID_HANDLE_VALUE) : h(x) {}
};

}  // namespace dettagli

inline bool convertiUnita(const std::wstring& q, std::wstring& outTitolo, std::wstring& outCopia) {
    if (q.size() > 96) return false;
    size_t p = 0;
    double val = 0;
    if (!dettagli::leggiNumero(q, p, true, val)) return false;
    while (p < q.size() && q[p] == L' ') p++;
    const size_t f0 = p;
    while (p < q.size() && q[p] != L' ' && q[p] != L'=' && !(q[p] == L'-' && p + 1 < q.size() && q[p + 1] == L'>')) p++;
    if (p == f0) return false;
    const std::wstring da = dettagli::minuscolo(q.substr(f0, p - f0));
    const size_t sp0 = p;
    while (p < q.size() && q[p] == L' ') p++;
    if (!dettagli::leggiSeparatore(q, p, p > sp0)) return false;
    while (p < q.size() && q[p] == L' ') p++;
    if (p >= q.size()) return false;
    const std::wstring a = dettagli::minuscolo(q.substr(p));
    if (a.find(L' ') != std::wstring::npos) return false;

    auto tempId = [](const std::wstring& u) -> int {
        if (u == L"c" || u == L"celsius" || u == L"\u00B0c") return 0;
        if (u == L"f" || u == L"fahrenheit" || u == L"\u00B0f") return 1;
        if (u == L"k" || u == L"kelvin") return 2;
        return -1;
    };
    const int tDa = tempId(da), tA = tempId(a);
    if (tDa >= 0 && tA >= 0) {
        double c = val;
        if (tDa == 1) c = (val - 32.0) * 5.0 / 9.0;
        else if (tDa == 2) c = val - 273.15;
        double res = c;
        const wchar_t* et = L"\u00B0C";
        if (tA == 1) { res = c * 9.0 / 5.0 + 32.0; et = L"\u00B0F"; }
        else if (tA == 2) { res = c + 273.15; et = L"K"; }
        outCopia = dettagli::formattaNumero(res, L"%.8g");
        outTitolo = L"= " + outCopia + L" " + et;
        return true;
    }

    enum Cat { C_LEN, C_MASS, C_VOL, C_DATA, C_SPEED, C_TIME, C_AREA };
    struct Unita { const wchar_t* chiave; Cat cat; double fattore; const wchar_t* etichetta; };
    static const Unita tabella[] = {
        {L"mm", C_LEN, 0.001, L"mm"}, {L"cm", C_LEN, 0.01, L"cm"},
        {L"m", C_LEN, 1.0, L"m"}, {L"meter", C_LEN, 1.0, L"m"}, {L"meters", C_LEN, 1.0, L"m"},
        {L"km", C_LEN, 1000.0, L"km"},
        {L"in", C_LEN, 0.0254, L"in"}, {L"inch", C_LEN, 0.0254, L"in"}, {L"inches", C_LEN, 0.0254, L"in"},
        {L"ft", C_LEN, 0.3048, L"ft"}, {L"foot", C_LEN, 0.3048, L"ft"}, {L"feet", C_LEN, 0.3048, L"ft"},
        {L"yd", C_LEN, 0.9144, L"yd"}, {L"yard", C_LEN, 0.9144, L"yd"}, {L"yards", C_LEN, 0.9144, L"yd"},
        {L"mi", C_LEN, 1609.344, L"mi"}, {L"mile", C_LEN, 1609.344, L"mi"}, {L"miles", C_LEN, 1609.344, L"mi"},

        {L"mg", C_MASS, 0.001, L"mg"},
        {L"g", C_MASS, 1.0, L"g"}, {L"gram", C_MASS, 1.0, L"g"}, {L"grams", C_MASS, 1.0, L"g"},
        {L"kg", C_MASS, 1000.0, L"kg"},
        {L"oz", C_MASS, 28.349523125, L"oz"},
        {L"lb", C_MASS, 453.59237, L"lb"}, {L"lbs", C_MASS, 453.59237, L"lb"},
        {L"st", C_MASS, 6350.29318, L"st"}, {L"stone", C_MASS, 6350.29318, L"st"},
        {L"t", C_MASS, 1000000.0, L"t"}, {L"ton", C_MASS, 1000000.0, L"t"}, {L"tonne", C_MASS, 1000000.0, L"t"},

        {L"ml", C_VOL, 0.001, L"mL"},
        {L"l", C_VOL, 1.0, L"L"}, {L"liter", C_VOL, 1.0, L"L"}, {L"liters", C_VOL, 1.0, L"L"},
        {L"litre", C_VOL, 1.0, L"L"}, {L"litres", C_VOL, 1.0, L"L"},
        {L"tsp", C_VOL, 0.00492892159375, L"tsp"},
        {L"tbsp", C_VOL, 0.01478676478125, L"tbsp"},
        {L"floz", C_VOL, 0.0295735295625, L"fl oz"},
        {L"cup", C_VOL, 0.2365882365, L"cup"}, {L"cups", C_VOL, 0.2365882365, L"cup"},
        {L"pt", C_VOL, 0.473176473, L"pt"}, {L"pint", C_VOL, 0.473176473, L"pt"},
        {L"gal", C_VOL, 3.785411784, L"gal"}, {L"gallon", C_VOL, 3.785411784, L"gal"}, {L"gallons", C_VOL, 3.785411784, L"gal"},

        {L"b", C_DATA, 1.0, L"B"}, {L"byte", C_DATA, 1.0, L"B"}, {L"bytes", C_DATA, 1.0, L"B"},
        {L"kb", C_DATA, 1e3, L"KB"}, {L"mb", C_DATA, 1e6, L"MB"}, {L"gb", C_DATA, 1e9, L"GB"},
        {L"tb", C_DATA, 1e12, L"TB"}, {L"pb", C_DATA, 1e15, L"PB"},
        {L"kib", C_DATA, 1024.0, L"KiB"}, {L"mib", C_DATA, 1048576.0, L"MiB"},
        {L"gib", C_DATA, 1073741824.0, L"GiB"}, {L"tib", C_DATA, 1099511627776.0, L"TiB"},

        {L"m/s", C_SPEED, 1.0, L"m/s"}, {L"mps", C_SPEED, 1.0, L"m/s"},
        {L"km/h", C_SPEED, 1.0 / 3.6, L"km/h"}, {L"kph", C_SPEED, 1.0 / 3.6, L"km/h"}, {L"kmh", C_SPEED, 1.0 / 3.6, L"km/h"},
        {L"mph", C_SPEED, 0.44704, L"mph"},
        {L"kn", C_SPEED, 1852.0 / 3600.0, L"kn"}, {L"kt", C_SPEED, 1852.0 / 3600.0, L"kn"}, {L"knots", C_SPEED, 1852.0 / 3600.0, L"kn"},

        {L"ms", C_TIME, 0.001, L"ms"},
        {L"s", C_TIME, 1.0, L"s"}, {L"sec", C_TIME, 1.0, L"s"}, {L"secs", C_TIME, 1.0, L"s"},
        {L"second", C_TIME, 1.0, L"s"}, {L"seconds", C_TIME, 1.0, L"s"},
        {L"min", C_TIME, 60.0, L"min"}, {L"mins", C_TIME, 60.0, L"min"},
        {L"minute", C_TIME, 60.0, L"min"}, {L"minutes", C_TIME, 60.0, L"min"},
        {L"h", C_TIME, 3600.0, L"h"}, {L"hr", C_TIME, 3600.0, L"h"}, {L"hrs", C_TIME, 3600.0, L"h"},
        {L"hour", C_TIME, 3600.0, L"h"}, {L"hours", C_TIME, 3600.0, L"h"},
        {L"d", C_TIME, 86400.0, L"d"}, {L"day", C_TIME, 86400.0, L"d"}, {L"days", C_TIME, 86400.0, L"d"},
        {L"wk", C_TIME, 604800.0, L"wk"}, {L"week", C_TIME, 604800.0, L"wk"}, {L"weeks", C_TIME, 604800.0, L"wk"},

        {L"m2", C_AREA, 1.0, L"m\u00B2"}, {L"sqm", C_AREA, 1.0, L"m\u00B2"},
        {L"km2", C_AREA, 1e6, L"km\u00B2"}, {L"sqkm", C_AREA, 1e6, L"km\u00B2"},
        {L"ft2", C_AREA, 0.09290304, L"ft\u00B2"}, {L"sqft", C_AREA, 0.09290304, L"ft\u00B2"},
        {L"acre", C_AREA, 4046.8564224, L"acre"}, {L"acres", C_AREA, 4046.8564224, L"acre"},
        {L"ha", C_AREA, 10000.0, L"ha"}, {L"hectare", C_AREA, 10000.0, L"ha"}, {L"hectares", C_AREA, 10000.0, L"ha"},
    };
    const Unita *uDa = nullptr, *uA = nullptr;
    for (const auto& u : tabella) {
        if (!uDa && da == u.chiave) uDa = &u;
        if (!uA && a == u.chiave) uA = &u;
    }
    if (!uDa || !uA || uDa->cat != uA->cat) return false;
    const double res = val * uDa->fattore / uA->fattore;
    if (!std::isfinite(res)) return false;
    outCopia = dettagli::formattaNumero(res, L"%.8g");
    outTitolo = L"= " + outCopia + L" " + uA->etichetta;
    return true;
}

class Valuta {
public:
    static constexpr long long TTL = 6 * 3600;

    // 0 = non e' una valuta, 1 = convertita, 2 = tassi in caricamento
    int converti(const std::wstring& q, HWND notifica, std::wstring& outTitolo, std::wstring& outSotto, std::wstring& outCopia) {
        double importo = 0;
        std::wstring da, a;
        if (!analizza(q, importo, da, a)) return 0;

        bool serveAggiornare = false;
        double rDa = 0, rA = 0;
        bool trovati = false;
        {
            std::lock_guard<std::mutex> lk(mtx);
            if (!discoLetto) {
                discoLetto = true;
                leggiDiscoBloccato();
            }
            const long long ora = (long long)std::time(nullptr);
            if (tassi.empty() || ora - aggiornato >= TTL) serveAggiornare = true;
            auto itDa = tassi.find(da), itA = tassi.find(a);
            if (itDa != tassi.end() && itA != tassi.end() && itDa->second > 0 && itA->second > 0) {
                rDa = itDa->second; rA = itA->second; trovati = true;
            }
        }
        if (serveAggiornare) avviaAggiornamento(notifica);
        if (!trovati) return 2;

        const double tasso = rA / rDa;
        const double res = importo * tasso;
        if (!std::isfinite(res)) return 0;
        outCopia = dettagli::formattaNumero(res, L"%.2f");
        outTitolo = L"= " + outCopia + L" " + a;
        outSotto = L"1 " + da + L" = " + dettagli::formattaNumero(tasso, L"%.4g") + L" " + a
                   + testi::t(L" \u00B7 Invio per copiare", L" \u00B7 Enter to copy");
        return 1;
    }

private:
    std::mutex mtx;
    std::unordered_map<std::wstring, double> tassi;
    long long aggiornato = 0;
    bool discoLetto = false;
    std::atomic<bool> inCorso{false};

    static bool codiceValido(const std::wstring& c) {
        static const wchar_t* const codici[] = {
            L"ZAR", L"USD", L"EUR", L"GBP", L"AUD", L"CAD", L"JPY", L"CHF", L"CNY",
            L"INR", L"NZD", L"BRL", L"MXN", L"KRW", L"SGD", L"HKD", L"SEK", L"NOK",
            L"DKK", L"PLN", L"TRY", L"AED", L"SAR", L"THB", L"NAD", L"BWP"
        };
        for (const wchar_t* x : codici) if (c == x) return true;
        return false;
    }

    static bool analizza(const std::wstring& q, double& importo, std::wstring& da, std::wstring& a) {
        if (q.size() > 64 || q.empty()) return false;
        size_t p = 0;
        bool daSimbolo = false;
        if (q[0] == L'$') { da = L"USD"; p = 1; daSimbolo = true; }
        else if (q[0] == L'\u20AC') { da = L"EUR"; p = 1; daSimbolo = true; }
        else if (q[0] == L'\u00A3') { da = L"GBP"; p = 1; daSimbolo = true; }
        else if ((q[0] == L'R' || q[0] == L'r') && q.size() > 1 && q[1] >= L'0' && q[1] <= L'9') { da = L"ZAR"; p = 1; daSimbolo = true; }

        if (!dettagli::leggiNumero(q, p, false, importo)) return false;
        if (!daSimbolo) {
            while (p < q.size() && q[p] == L' ') p++;
            const size_t f0 = p;
            while (p < q.size() && iswalpha(q[p])) p++;
            if (p - f0 != 3) return false;
            da = dettagli::maiuscolo(q.substr(f0, 3));
            if (!codiceValido(da)) return false;
        }
        const size_t sp0 = p;
        while (p < q.size() && q[p] == L' ') p++;
        if (!dettagli::leggiSeparatore(q, p, p > sp0)) return false;
        while (p < q.size() && q[p] == L' ') p++;
        if (q.size() - p != 3) return false;
        a = dettagli::maiuscolo(q.substr(p));
        return codiceValido(a);
    }

    static std::wstring percorsoCache(bool creaCartella) {
        PWSTR base = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base)) || !base) return L"";
        std::wstring dir = std::wstring(base) + L"\\FluidWin";
        CoTaskMemFree(base);
        if (creaCartella) CreateDirectoryW(dir.c_str(), nullptr);
        return dir + L"\\tassi_valuta.json";
    }

    static bool leggiJson(const std::string& body, std::unordered_map<std::wstring, double>& out, long long* savedAt) {
        if (savedAt) {
            const size_t ps = body.find("\"saved_at\"");
            if (ps != std::string::npos) {
                const size_t col = body.find(':', ps + 10);
                if (col != std::string::npos) *savedAt = strtoll(body.c_str() + col + 1, nullptr, 10);
            }
        }
        const size_t pr = body.find("\"rates\"");
        if (pr == std::string::npos) return false;
        size_t i = body.find('{', pr + 7);
        if (i == std::string::npos) return false;
        i++;
        std::unordered_map<std::wstring, double> mappa;
        while (i < body.size()) {
            while (i < body.size() && (body[i] == ' ' || body[i] == '\t' || body[i] == '\r' || body[i] == '\n' || body[i] == ',')) i++;
            if (i >= body.size() || body[i] == '}') break;
            if (body[i] != '"') break;
            const size_t k0 = ++i;
            while (i < body.size() && body[i] != '"') i++;
            if (i >= body.size()) break;
            const std::string chiave = body.substr(k0, i - k0);
            i++;
            while (i < body.size() && (body[i] == ' ' || body[i] == '\t' || body[i] == ':')) i++;
            char* fine = nullptr;
            const double val = strtod(body.c_str() + i, &fine);
            if (!fine || fine == body.c_str() + i) break;
            i = (size_t)(fine - body.c_str());
            if (chiave.size() == 3 && std::isfinite(val) && val > 0) {
                std::wstring wk(3, L' ');
                for (size_t j = 0; j < 3; j++) wk[j] = (wchar_t)towupper((unsigned char)chiave[j]);
                if (codiceValido(wk)) mappa[wk] = val;
            }
        }
        if (mappa.size() < 5 || !mappa.count(L"USD")) return false;
        out = std::move(mappa);
        return true;
    }

    void leggiDiscoBloccato() {
        const std::wstring p = percorsoCache(false);
        if (p.empty()) return;
        dettagli::FileHandle fh(CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (fh.h == INVALID_HANDLE_VALUE) return;
        LARGE_INTEGER sz{};
        if (!GetFileSizeEx(fh.h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > 262144) return;
        std::string buf((size_t)sz.QuadPart, '\0');
        DWORD letti = 0;
        if (!ReadFile(fh.h, buf.data(), (DWORD)buf.size(), &letti, nullptr) || letti == 0) return;
        buf.resize(letti);
        long long ts = 0;
        std::unordered_map<std::wstring, double> mappa;
        if (leggiJson(buf, mappa, &ts)) {
            tassi = std::move(mappa);
            aggiornato = ts;
        }
    }

    static void scriviDisco(long long ts, const std::unordered_map<std::wstring, double>& mappa) {
        const std::wstring p = percorsoCache(true);
        if (p.empty()) return;
        std::string json = "{\"saved_at\":" + std::to_string(ts) + ",\"rates\":{";
        bool primo = true;
        for (const auto& [k, v] : mappa) {
            if (!primo) json += ',';
            primo = false;
            char riga[64];
            snprintf(riga, sizeof(riga), "\"%c%c%c\":%.8g", (char)k[0], (char)k[1], (char)k[2], v);
            json += riga;
        }
        json += "}}";
        dettagli::FileHandle fh(CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (fh.h == INVALID_HANDLE_VALUE) return;
        DWORD scritti = 0;
        WriteFile(fh.h, json.data(), (DWORD)json.size(), &scritti, nullptr);
    }

    void avviaAggiornamento(HWND notifica) {
        if (inCorso.exchange(true)) return;
        std::thread([this, notifica]() {
            struct Ripristina { std::atomic<bool>& f; ~Ripristina() { f.store(false); } } guard{inCorso};
            dettagli::WinHttpHandle sess(WinHttpOpen(L"FluidWin/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
            if (!sess.h) return;
            WinHttpSetTimeouts(sess.h, 5000, 5000, 5000, 5000);
            dettagli::WinHttpHandle conn(WinHttpConnect(sess.h, L"open.er-api.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
            if (!conn.h) return;
            dettagli::WinHttpHandle req(WinHttpOpenRequest(conn.h, L"GET", L"/v6/latest/USD", nullptr,
                                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
            if (!req.h) return;
            if (!WinHttpSendRequest(req.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) return;
            if (!WinHttpReceiveResponse(req.h, nullptr)) return;
            DWORD stato = 0, lunStato = sizeof(stato);
            if (!WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                     WINHTTP_HEADER_NAME_BY_INDEX, &stato, &lunStato, WINHTTP_NO_HEADER_INDEX) || stato != 200) return;
            std::string body;
            for (;;) {
                DWORD disp = 0;
                if (!WinHttpQueryDataAvailable(req.h, &disp) || disp == 0) break;
                if (body.size() + disp > 262144) return;
                const size_t prec = body.size();
                body.resize(prec + disp);
                DWORD letti = 0;
                if (!WinHttpReadData(req.h, body.data() + prec, disp, &letti)) return;
                body.resize(prec + letti);
                if (letti == 0) break;
            }
            std::unordered_map<std::wstring, double> mappa;
            if (!leggiJson(body, mappa, nullptr)) return;
            const long long ora = (long long)std::time(nullptr);
            scriviDisco(ora, mappa);
            {
                std::lock_guard<std::mutex> lk(mtx);
                tassi = std::move(mappa);
                aggiornato = ora;
            }
            if (notifica && IsWindow(notifica)) PostMessageW(notifica, WM_APP + 12, 0, 0);
        }).detach();
    }
};

inline bool parseColoreHex(const std::wstring& q, std::wstring& outTitolo, std::wstring& outCopia, std::wstring& outIcona) {
    if ((q.size() != 4 && q.size() != 7) || q[0] != L'#') return false;
    auto hex = [](wchar_t c) -> int {
        if (c >= L'0' && c <= L'9') return c - L'0';
        if (c >= L'a' && c <= L'f') return 10 + (c - L'a');
        if (c >= L'A' && c <= L'F') return 10 + (c - L'A');
        return -1;
    };
    int r = 0, g = 0, b = 0;
    if (q.size() == 4) {
        const int d0 = hex(q[1]), d1 = hex(q[2]), d2 = hex(q[3]);
        if (d0 < 0 || d1 < 0 || d2 < 0) return false;
        r = d0 * 17; g = d1 * 17; b = d2 * 17;
    } else {
        int v[6];
        for (int i = 0; i < 6; i++) { v[i] = hex(q[i + 1]); if (v[i] < 0) return false; }
        r = v[0] * 16 + v[1]; g = v[2] * 16 + v[3]; b = v[4] * 16 + v[5];
    }
    const double rf = r / 255.0, gf = g / 255.0, bf = b / 255.0;
    const double mx = std::max({rf, gf, bf}), mn = std::min({rf, gf, bf}), delta = mx - mn;
    const double l = (mx + mn) / 2.0;
    const double s = delta == 0.0 ? 0.0 : delta / (1.0 - std::abs(2.0 * l - 1.0));
    double h = 0.0;
    if (delta > 0.0) {
        if (mx == rf) h = 60.0 * std::fmod((gf - bf) / delta + 6.0, 6.0);
        else if (mx == gf) h = 60.0 * ((bf - rf) / delta + 2.0);
        else h = 60.0 * ((rf - gf) / delta + 4.0);
    }
    wchar_t rgb[40], hsl[40], ic[16];
    swprintf(rgb, 40, L"rgb(%d, %d, %d)", r, g, b);
    swprintf(hsl, 40, L"hsl(%d, %d%%, %d%%)", (int)std::lround(h) % 360, (int)std::lround(s * 100.0), (int)std::lround(l * 100.0));
    swprintf(ic, 16, L"*col:%02X%02X%02X", r, g, b);
    outCopia = rgb;
    outTitolo = std::wstring(rgb) + L" \u00B7 " + hsl;
    outIcona = ic;
    return true;
}

inline std::wstring nuovoUuid() {
    GUID g{};
    if (FAILED(CoCreateGuid(&g))) return L"";
    wchar_t b[48];
    swprintf(b, 48, L"%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             (unsigned long)g.Data1, (unsigned)g.Data2, (unsigned)g.Data3,
             (unsigned)g.Data4[0], (unsigned)g.Data4[1], (unsigned)g.Data4[2], (unsigned)g.Data4[3],
             (unsigned)g.Data4[4], (unsigned)g.Data4[5], (unsigned)g.Data4[6], (unsigned)g.Data4[7]);
    return b;
}

inline std::wstring ipLocale() {
    ULONG sz = 0;
    if (GetIpAddrTable(nullptr, &sz, TRUE) != ERROR_INSUFFICIENT_BUFFER || sz == 0) return L"";
    std::vector<BYTE> buf(sz);
    auto* tbl = reinterpret_cast<MIB_IPADDRTABLE*>(buf.data());
    if (GetIpAddrTable(tbl, &sz, TRUE) != NO_ERROR) return L"";
    DWORD migliore = 0;
    int prioritaMigliore = -1;
    for (DWORD i = 0; i < tbl->dwNumEntries; i++) {
        const DWORD a = tbl->table[i].dwAddr;
        const unsigned b0 = a & 0xFF, b1 = (a >> 8) & 0xFF;
        if (a == 0 || b0 == 127) continue;
        if (b0 == 169 && b1 == 254) continue;
        const bool lan = (b0 == 192 && b1 == 168) || (b0 == 10) || (b0 == 172 && b1 >= 16 && b1 <= 31);
        const int prio = lan ? 2 : 1;
        if (prio > prioritaMigliore) { migliore = a; prioritaMigliore = prio; }
    }
    if (!migliore) return L"";
    wchar_t s[32];
    swprintf(s, 32, L"%u.%u.%u.%u",
             (unsigned)(migliore & 0xFF), (unsigned)((migliore >> 8) & 0xFF),
             (unsigned)((migliore >> 16) & 0xFF), (unsigned)((migliore >> 24) & 0xFF));
    return s;
}

class Ricerca {
public:
    std::function<std::vector<std::pair<std::wstring, std::wstring>>()> elencoApp;   // nome, comando d'avvio
    std::function<void()> apriImpostazioni;                                          // impostazioni di FluidWin
    std::function<bool()> conOnda;                                                   // onda di luce accesa nelle impostazioni
    void (*registro)(const char*) = nullptr;

    bool aperta() const { return apertaOra; }

    void apri() {
        if (apertaOra) { chiudi(); return; }
        if (!v.fin) {
            if (!v.crea(L"FluidWin.Ricerca", vetro::procedura<Ricerca>, this, true)) return;
            InitializeCriticalSection(&cs);
            evento = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            CloseHandle(CreateThread(nullptr, 0, lavoratore, this, 0, nullptr));
        }
        v.aggiornaTema();
        const RECT a = vetro::areaPrincipale();
        v.dpi = vetro::dpiMonitor(MonitorFromPoint({a.left, a.top}, MONITOR_DEFAULTTOPRIMARY));
        const float d = v.dpi;
        if (!fTesto) {
            fTesto = v.formato(21 * d);
            fTitolo = v.formato(14 * d, DWRITE_FONT_WEIGHT_MEDIUM);
            fSotto = v.formato(11.5f * d);
            fSezione = v.formato(11 * d, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        }
        app = elencoApp ? elencoApp() : std::vector<std::pair<std::wstring, std::wstring>>{};
        testo.clear(); risultati.clear(); file.clear(); sel = 0; sopra = -1; selUtente = false; saltaBasso = false;
        // (il mouse di adesso: un primo WM_MOUSEMOVE della finestra che cresce sotto il puntatore fermo non e' una
        // scelta, R20261121-124830 su S1 del giro 2)
        { POINT pm{}; GetCursorPos(&pm); ultimoMouse = (DWORD)MAKELONG((short)pm.x, (short)pm.y); }
        // una ricerca della sessione di prima ancora in corso non vale piu' (prima i suoi file arrivavano nella sessione
        // nuova vuota, R20261121-002818-S15)
        InterlockedIncrement(&generazione); KillTimer(v.fin, 3);   // (3: la ricerca in attesa dopo l'ultimo tasto)
        // icone dei file: oltre 300 si ricomincia (prima la cache cresceva per tutta la sessione, S17). Sotto cs: il
        // thread delle icone la legge; quelle ancora in lavorazione restano segnate (nullptr) e si rifanno
        EnterCriticalSection(&cs);
        if (icone.size() > 300 && iconeDaFare.empty() && !iconeInCorso) { for (auto& [k, b] : icone) if (b) b->Release(); icone.clear(); }
        LeaveCriticalSection(&cs);
        larg = (int)std::lround(680 * d);
        x = (a.left + a.right - larg) / 2; y = a.top + (int)std::lround((a.bottom - a.top) * 0.22);
        primaDi = GetForegroundWindow();
        apertaOra = true; chiudendo = false; tAnim = adesso(); ripreso = false;
        scalaW = 0.80; spostaY = 16 * d;   // come al primo fotogramma dell'animazione
        v.alfa(0);
        ridimensiona();
        // onda (goccia d'acqua e luce sui bordi), appena sotto la ricerca; i suoi fotogrammi danno anche il ritmo al
        // riquadro. Prima di mostrare la ricerca: l'onda fotografa lo schermo com'e' adesso, senza di lei
        if (!conOnda || conOnda()) {
            const double t1 = adesso();
            onda.avvisa = v.fin; onda.battito = true;
            onda.apri(vetro::areaPrincipale(), {x + larg / 2, y + (int)std::lround(ALTO_BARRA * d / 2)}, d, v.fin);
            if (registro && adesso() - t1 > 0.008) { char b[80]; snprintf(b, sizeof(b), "ricerca: foto per l'onda %.1f ms", (adesso() - t1) * 1000); registro(b); }
        }
        ShowWindow(v.fin, SW_SHOW);
        prendiFuoco();
        vetro::battito(v.fin, 1);    // animazione del riquadro (se l'onda non c'e', o tra un battito e l'altro)
        SetTimer(v.fin, 2, 530, nullptr);   // cursore lampeggiante
        passoRiquadro();
    }

    void chiudi(bool restituisciFuoco = true) {
        if (!apertaOra) return;
        apertaOra = false; chiudendo = true; tAnim = adesso();
        KillTimer(v.fin, 2);
        onda.chiudi();
        // il riquadro sfuma salendo un poco e restringendosi (sotto nessun clic: e' gia' chiusa)
        vetro::battito(v.fin, 1);
        if (restituisciFuoco && primaDi && IsWindow(primaDi) && IsWindowVisible(primaDi)) SetForegroundWindow(primaDi);
        passoRiquadro();
    }

    bool messaggio(HWND hw, UINT m, WPARAM w, LPARAM l, LRESULT& r) {
        r = 0;
        if (m == onda.messaggio) {
            if (w == 1) { onda.battitoRicevuto(); passoRiquadro(); } else onda.controlla();
            return true;
        }
        // mentre sfuma e' gia' chiusa: niente clic ne' tasti
        if (chiudendo && ((m >= WM_MOUSEFIRST && m <= WM_MOUSELAST) || m == WM_KEYDOWN || m == WM_CHAR)) return true;
        switch (m) {
            case WM_TIMER:
                if (w == 1) { passoRiquadro(); return true; }
                if (w == 2) cursore = !cursore;
                if (w == 3) { KillTimer(v.fin, 3); chiediFile(); }
                if (w == 4) { raccogliIcone(); }
                disegna();
                return true;
            case WM_PAINT: disegna(); return true;
            case WM_APP + 1: {   // risultati dei file dal thread
                if (!consegna::riprendi(hw, m, (const void*)l)) return true;   // (non consegnato dal thread: ignorato)
                auto* lista = (std::vector<Risultato>*)l;
                if ((LONG)w == generazione && apertaOra) { file = std::move(*lista); componi(); }
                delete lista;
                return true;
            }
            case WM_CHAR: {
                // Tetto di 512 come l'incolla, ma un carattere fuori dal piano base (due unita') entra intero o non entra:
                // controllato sulla singola unita' lasciava mezza emoji nella query (R20261121-124830-S1)
                if (w < 32 || w == 127 || (GetKeyState(VK_CONTROL) & 0x8000)) return true;
                const wchar_t c = (wchar_t)w;
                if (IS_HIGH_SURROGATE(c)) { saltaBasso = testo.size() + 2 > 512; if (!saltaBasso) testo += c; return true; }
                if (IS_LOW_SURROGATE(c)) {
                    const bool ok = !saltaBasso && !testo.empty() && IS_HIGH_SURROGATE(testo.back());
                    saltaBasso = false;
                    if (ok) { testo += c; cambiato(); }
                    return true;
                }
                saltaBasso = false;
                if (!testo.empty() && IS_HIGH_SURROGATE(testo.back())) testo.pop_back();   // (meta' rimasta sola)
                if (testo.size() < 512) { testo += c; cambiato(); }
                return true;
            }
            case WM_KEYDOWN: tasto(w); return true;
            case WM_MOUSEMOVE: {
                const int s = rigaIn(GET_X_LPARAM(l), GET_Y_LPARAM(l));
                // (scelta dell'utente solo se il mouse si e' mosso davvero: la finestra che cresce sotto il mouse fermo
                // manda anche lei WM_MOUSEMOVE)
                const DWORD mp = GetMessagePos(); const bool mosso = mp != ultimoMouse; ultimoMouse = mp;
                // (un movimento vero dentro la riga gia' evidenziata e' una scelta anche lui, R20261121-124830-S2)
                if (s >= 0 && mosso && !selUtente) { sel = s; selUtente = true; }
                if (s != sopra) { sopra = s; if (s >= 0) { sel = s; if (mosso) selUtente = true; } disegna(); }
                return true;
            }
            case WM_LBUTTONUP: { const int s = rigaIn(GET_X_LPARAM(l), GET_Y_LPARAM(l)); if (s >= 0) esegui(s); return true; }
            case WM_ACTIVATE:
                if (LOWORD(w) == WA_INACTIVE && apertaOra) {
                    // Appena aperta, il primo piano preso agganciandosi all'app di prima a volte torna a lei subito
                    // dopo lo sgancio (una volta su due nelle prove): non e' l'utente che ha cliccato altrove, si
                    // riprende (una volta sola) invece di chiudersi
                    if (agganciata && !ripreso && adesso() - tAnim < 0.3 && GetForegroundWindow() == primaDi) {
                        ripreso = true;
                        if (registro) registro("ricerca: primo piano tornato all'app di prima, lo riprende");
                        PostMessageW(v.fin, WM_APP + 2, 0, 0);
                        return true;
                    }
                    if (registro) {
                        char b[200]; wchar_t c[64] = L"", c2[64] = L""; GetClassNameW((HWND)l, c, 64);
                        HWND fg = GetForegroundWindow(); GetClassNameW(fg, c2, 64);
                        snprintf(b, sizeof(b), "ricerca: chiusa, attivata %p (%ls), in primo piano %p (%ls), primaDi %p, aggancio %d",
                                 (void*)l, c, (void*)fg, c2, (void*)primaDi, (int)agganciata);
                        registro(b);
                    }
                    chiudi(false);
                }
                return true;
            case WM_CLOSE: chiudi(); return true;
            case WM_APP + 2: if (apertaOra) prendiFuoco(); return true;
            case WM_APP + 12: if (apertaOra) componi(); return true;
        }
        return false;
    }

private:
    vetro::Finestra v;
    IDWriteTextFormat *fTesto = nullptr, *fTitolo = nullptr, *fSotto = nullptr, *fSezione = nullptr;
    std::wstring testo;
    std::vector<std::pair<std::wstring, std::wstring>> app;
    std::vector<Risultato> risultati, file;
    Valuta valuta;
    std::wstring uuidCache;
    LONG genUuid = -1;
    int sel = 0, sopra = -1, larg = 0, x = 0, y = 0;
    DWORD ultimoMouse = 0;
    bool saltaBasso = false;   // WM_CHAR: la prima meta' di un carattere non entrata, si scarta anche la seconda
    bool selUtente = false;   // sel scelta con frecce o mouse (non la prima riga data da sola: vedi componi)
    bool apertaOra = false, cursore = true;
    HWND primaDi = nullptr;
    onda::Onda onda;
    // animazione del riquadro: all'apertura si allarga da una pillola piu' stretta con un piccolo rimbalzo, salendo
    // di poco e apparendo; alla chiusura sfuma salendo e restringendosi, poi si nasconde
    bool chiudendo = false;
    double tAnim = 0;
    double scalaW = 1, spostaY = 0;   // larghezza (frazione) e spostamento verticale (px) di adesso
    static constexpr double DURATA_APRI = 0.42, DURATA_CHIUDI = 0.16;

    static double adesso() {
        static LARGE_INTEGER f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x; }();
        LARGE_INTEGER c; QueryPerformanceCounter(&c); return (double)c.QuadPart / f.QuadPart;
    }

    void passoRiquadro() {
        if (!apertaOra && !chiudendo) return;
        const float d = v.dpi;
        const double e = adesso() - tAnim;
        double a;
        bool finita;
        if (apertaOra) {
            const double m = vetro::molla(e / DURATA_APRI);
            scalaW = 0.80 + 0.20 * m;
            spostaY = (1 - m) * 16 * d;
            a = vetro::uscita(e / 0.15);
            finita = e >= DURATA_APRI;
            if (finita) { scalaW = 1; spostaY = 0; a = 1; }
        } else {
            const double k = vetro::uscita(e / DURATA_CHIUDI);
            scalaW = 1 - 0.06 * k;
            spostaY = -8 * d * k;
            a = 1 - k;
            finita = e >= DURATA_CHIUDI;
        }
        ridimensiona();
        v.alfa(a);
        disegna();
        if (!finita) return;
        vetro::fermaBattito(v.fin, 1);
        onda.battito = false;
        if (chiudendo) { chiudendo = false; ShowWindow(v.fin, SW_HIDE); scalaW = 1; spostaY = 0; }
    }
    volatile LONG generazione = 0;
    // thread per i file e le icone
    CRITICAL_SECTION cs{};
    HANDLE evento = nullptr;
    std::wstring richiesta; LONG genRichiesta = -1;
    std::deque<std::wstring> iconeDaFare;
    int iconeInCorso = 0;   // icone gia' prese dal thread e non ancora consegnate (sotto cs, FW-AUDIT-018)
    struct Pixel { std::wstring chiave; int w = 0, h = 0; std::vector<UINT> px; };
    std::vector<Pixel> iconePronte;
    std::unordered_map<std::wstring, ID2D1Bitmap*> icone;   // nullptr = in arrivo o non disponibile

    static constexpr float ALTO_BARRA = 64, ALTO_RIGA = 48;

    bool agganciata = false, ripreso = false;
    void prendiFuoco() {
        if (!ripreso) agganciata = false;
        SetForegroundWindow(v.fin);
        if (GetForegroundWindow() == v.fin) return;
        agganciata = true;
        HWND fg = GetForegroundWindow();
        const DWORD a = fg ? GetWindowThreadProcessId(fg, nullptr) : 0, b = GetCurrentThreadId();
        // (mai agganciati a un'app che non risponde: il suo input bloccato bloccherebbe anche quello di FluidWin)
        if (a && a != b && !IsHungAppWindow(fg) && AttachThreadInput(b, a, TRUE)) { SetForegroundWindow(v.fin); SetFocus(v.fin); AttachThreadInput(b, a, FALSE); }
    }

    void ridimensiona() {
        const float d = v.dpi;
        const int righe = (int)risultati.size();
        const int h = (int)std::lround((ALTO_BARRA + (righe ? 10 + righe * ALTO_RIGA + 8 : 0)) * d);
        const int w = (int)std::lround(larg * scalaW);
        v.posiziona(x + (larg - w) / 2, y + (int)std::lround(spostaY), w, h, HWND_TOPMOST);
    }

    void tasto(WPARAM k) {
        const bool ctrl = GetKeyState(VK_CONTROL) & 0x8000;
        if (k == VK_ESCAPE) { if (testo.empty()) chiudi(); else { testo.clear(); cambiato(); } }
        else if (k == VK_BACK && !testo.empty()) {
            if (ctrl) { while (!testo.empty() && testo.back() == L' ') testo.pop_back(); while (!testo.empty() && testo.back() != L' ') testo.pop_back(); }
            else { testo.pop_back(); if (!testo.empty() && IS_HIGH_SURROGATE(testo.back())) testo.pop_back(); }   // (l'emoji intera)
            cambiato();
        }
        else if (k == VK_DOWN && !risultati.empty()) { sel = (sel + 1) % (int)risultati.size(); selUtente = true; disegna(); }
        else if (k == VK_UP && !risultati.empty()) { sel = (sel - 1 + (int)risultati.size()) % (int)risultati.size(); selUtente = true; disegna(); }
        else if (k == VK_RETURN) esegui(sel);
        else if (ctrl && k == 'V' && OpenClipboard(v.fin)) {
            const size_t primaIncolla = testo.size();
            // dentro il blocco e al massimo 512 caratteri in tutto: un incolla enorme fermava l'interfaccia (S11)
            if (HANDLE hd = GetClipboardData(CF_UNICODETEXT)) if (const wchar_t* p = (const wchar_t*)GlobalLock(hd)) {
                // Si esaminano al massimo 4096 unita' (un blocco di soli a capo non fa crescere il testo, e prima si
                // percorreva tutto). Una coppia surrogata entra intera o non entra: tagliata a 512 restava a meta'
                // (R20261121-120822-S4 e conferma di S11)
                const size_t n = std::min<size_t>(GlobalSize(hd) / sizeof(wchar_t), 4096);
                for (size_t i = 0; i < n && p[i] && testo.size() < 512; i++) {
                    if (p[i] < 32) continue;
                    if (IS_HIGH_SURROGATE(p[i])) {
                        if (i + 1 >= n || !IS_LOW_SURROGATE(p[i + 1]) || testo.size() + 2 > 512) break;
                        testo += p[i]; testo += p[++i];
                    } else if (!IS_LOW_SURROGATE(p[i])) testo += p[i];
                }
                GlobalUnlock(hd);
            }
            // (cambiato solo se e' entrato qualcosa: un incolla di un'immagine azzerava la riga scelta, R20261121-124830-S3)
            CloseClipboard(); if (testo.size() != primaIncolla) cambiato();
        }
    }

    void cambiato() {
        cursore = true; SetTimer(v.fin, 2, 530, nullptr);
        selUtente = false; sel = 0;   // testo nuovo: torna la prima riga (la scelta di prima era per l'altro testo)
        InterlockedIncrement(&generazione);
        file.clear();
        componi();
        // i file si cercano quando si smette di scrivere per un attimo
        KillTimer(v.fin, 3);
        if (testo.size() >= 2) SetTimer(v.fin, 3, 180, nullptr);
    }

    static std::wstring minuscolo(std::wstring s) { for (auto& c : s) c = (wchar_t)towlower(c); return s; }

    static int punteggio(const std::wstring& nome, const std::wstring& q) {
        const std::wstring n = minuscolo(nome);
        const size_t p = n.find(q);
        if (p == std::wstring::npos) return -1;
        if (p == 0) return 3;
        if (n[p - 1] == L' ' || n[p - 1] == L'-') return 2;
        return 1;
    }

    void componi() {
        // la voce scelta con le frecce si ritrova per tipo e azione, non per numero di riga: i file che arrivano dopo
        // entrano prima di "Cerca sul web", e Invio apriva un file al suo posto (R20261121-002818-S9)
        // Solo una scelta vera (frecce o mouse): la prima riga data da sola resta la prima. Prima anche quella si
        // ritrovava per tipo, e se in attesa dei file c'era solo "Cerca sul web", Invio mandava al browser il nome del
        // file appena trovato (R20261121-120822-S1)
        const bool scelta = selUtente && sel >= 0 && sel < (int)risultati.size();
        if (!selUtente) sel = 0;
        const int tipoScelto = scelta ? (int)risultati[sel].tipo : -1;
        const std::wstring azioneScelta = scelta ? risultati[sel].azione : L"";
        risultati.clear();
        const size_t a = testo.find_first_not_of(L' '), b = testo.find_last_not_of(L' ');
        std::wstring q = a == std::wstring::npos ? L"" : testo.substr(a, b - a + 1);   // (senza spazi ai lati, in un passo)
        if (!q.empty()) {
            const std::wstring ql = minuscolo(q);
            double val;
            std::wstring convTitolo, convSotto, convCopia, convIcona;
            if (Calcolo::valuta(q, val)) {
                wchar_t b[64]; swprintf(b, 64, L"%.10g", val);
                std::wstring s = b; const wchar_t vg = testi::virgola(); for (auto& c : s) if (c == L'.') c = vg;   // (virgola in italiano, spagnolo...)
                risultati.push_back({T_CALCOLO, L"= " + s, testi::t(L"Invio per copiare il risultato", L"Enter to copy the result"), s, L""});
            } else if (convertiUnita(q, convTitolo, convCopia)) {
                risultati.push_back({T_CALCOLO, convTitolo, testi::t(L"Invio per copiare il risultato", L"Enter to copy the result"), convCopia, L""});
            } else if (const int stVal = valuta.converti(q, v.fin, convTitolo, convSotto, convCopia); stVal > 0) {
                if (stVal == 1) risultati.push_back({T_CALCOLO, convTitolo, convSotto, convCopia, L""});
                else risultati.push_back({T_CALCOLO, testi::t(L"Caricamento tassi di cambio\u2026", L"Loading exchange rates\u2026"), L"", L"", L""});
            } else if (parseColoreHex(q, convTitolo, convCopia, convIcona)) {
                risultati.push_back({T_CALCOLO, convTitolo, testi::t(L"Invio per copiare rgb(...)", L"Enter to copy rgb(...)"), convCopia, convIcona});
            } else if (ql == L"uuid" || ql == L"guid") {
                if (genUuid != generazione || uuidCache.empty()) { uuidCache = nuovoUuid(); genUuid = generazione; }
                if (!uuidCache.empty())
                    risultati.push_back({T_CALCOLO, uuidCache, testi::t(L"UUID v4 \u00B7 Invio per copiare", L"UUID v4 \u00B7 Enter to copy"), uuidCache, L""});
            } else if (ql == L"ip" || ql == L"my ip") {
                const std::wstring ip = ipLocale();
                if (!ip.empty())
                    risultati.push_back({T_CALCOLO, ip, testi::t(L"Indirizzo IP locale \u00B7 Invio per copiare", L"Local IP address \u00B7 Enter to copy"), ip, L""});
            }
            if ((ql.rfind(L"https://", 0) == 0 || ql.rfind(L"http://", 0) == 0) && q.find(L' ') == std::wstring::npos && q.size() > 8) {
                risultati.push_back({T_AZIONE, testi::t(L"Apri ", L"Open ") + q,
                                     testi::t(L"Nel browser predefinito", L"In the default browser"), L"*url:" + q, L""});
            }
            {
                std::vector<std::pair<int, size_t>> azTrovate;
                const auto& elencoAz = azioni();
                for (size_t i = 0; i < elencoAz.size(); i++) {
                    const int p = std::max(punteggio(elencoAz[i].titolo, ql), punteggio(elencoAz[i].sotto, ql));
                    if (p > 0) azTrovate.push_back({p, i});
                }
                std::stable_sort(azTrovate.begin(), azTrovate.end(), [](auto& a, auto& b) { return a.first > b.first; });
                for (size_t i = 0; i < azTrovate.size() && i < 3; i++) {
                    Risultato r = elencoAz[azTrovate[i].second];
                    r.sotto = testi::t(L"Azione rapida", L"Quick action");
                    risultati.push_back(std::move(r));
                }
            }
            // app: prima chi inizia con il testo
            std::vector<std::pair<int, size_t>> trovate;
            for (size_t i = 0; i < app.size(); i++) { const int p = punteggio(app[i].first, ql); if (p > 0) trovate.push_back({p, i}); }
            std::stable_sort(trovate.begin(), trovate.end(), [](auto& a, auto& b) { return a.first > b.first; });
            for (size_t i = 0; i < trovate.size() && i < 5; i++) {
                const auto& a = app[trovate[i].second];
                risultati.push_back({T_APP, a.first, L"App", a.second, a.second});
            }
            int n = 0;
            for (const auto& s : pagine()) {
                if (n >= 3) break;
                if (punteggio(s.titolo, ql) > 0 || minuscolo(s.sotto).find(ql) != std::wstring::npos) { Risultato r = s; r.sotto = testi::t(L"Impostazioni", L"Settings"); risultati.push_back(r); n++; }
            }
            // i file che entrano: la barra resta dentro tre quarti dello schermo (con 8 file, piu' app, impostazioni e
            // web, su uno schermo basso usciva di sotto). Una riga resta per la ricerca sul web
            {
                MONITORINFO mi{}; mi.cbSize = sizeof(mi);
                const bool ok = GetMonitorInfoW(MonitorFromWindow(v.fin, MONITOR_DEFAULTTONEAREST), &mi);
                const float alto = ok ? (mi.rcWork.bottom - mi.rcWork.top) / v.dpi * 0.75f : 1000;
                const int righe = std::max(0, (int)((alto - ALTO_BARRA - 18) / ALTO_RIGA));
                const int posto = std::max(0, righe - (int)risultati.size() - 1);
                for (size_t i = 0; i < file.size() && (int)i < posto; i++) risultati.push_back(file[i]);
            }
            risultati.push_back({T_WEB, testi::t(L"Cerca “", L"Search “") + testi::isola(q) + testi::t(L"” sul web", L"” on the web"),
                                 testi::t(L"Nel browser predefinito", L"In the default browser"), q, L""});
        }
        sel = std::clamp(sel, 0, std::max(0, (int)risultati.size() - 1));
        if (scelta) for (int i = 0; i < (int)risultati.size(); i++) if ((int)risultati[i].tipo == tipoScelto && risultati[i].azione == azioneScelta) { sel = i; break; }
        if (risultati.empty()) sel = 0;
        sopra = -1;
        chiediIcone();
        ridimensiona();
        disegna();
    }

    // icone che mancano: al thread
    void chiediIcone() {
        bool nuove = false;
        EnterCriticalSection(&cs);
        for (const auto& r : risultati) if (!r.icona.empty() && r.icona[0] != L'*' && !icone.count(r.icona)) { icone[r.icona] = nullptr; iconeDaFare.push_back(r.icona); nuove = true; }
        LeaveCriticalSection(&cs);
        if (nuove) { SetEvent(evento); SetTimer(v.fin, 4, 50, nullptr); }
    }

    // render target ricreato (vetro::Finestra::giroRt): le icone del vecchio non valgono, si rifanno (FW-AUDIT-012)
    unsigned giroIcone = 0;
    void controllaTarget() {
        if (giroIcone == v.giroRt) return;
        giroIcone = v.giroRt;
        for (auto& [k, b] : icone) if (b) b->Release();
        icone.clear();
        chiediIcone();
    }

    void chiediFile() {
        if (!apertaOra || testo.size() < 2) return;
        EnterCriticalSection(&cs); richiesta = testo; genRichiesta = generazione; LeaveCriticalSection(&cs);
        SetEvent(evento);
    }

    void raccogliIcone() {
        std::vector<Pixel> arrivate;
        EnterCriticalSection(&cs); arrivate.swap(iconePronte); const bool finite = iconeDaFare.empty() && !iconeInCorso; LeaveCriticalSection(&cs);
        for (auto& p : arrivate) {
            if (p.px.empty() || !v.rt) continue;
            ID2D1Bitmap* b = nullptr;
            v.rt->CreateBitmap(D2D1::SizeU(p.w, p.h), p.px.data(), p.w * 4,
                               D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)), &b);
            // la stessa icona puo' arrivare due volte (chiesta di nuovo dopo la ricreazione del target mentre la prima
            // era in lavorazione): quella vecchia si rilascia, prima si perdeva (R20261113-010114-S4)
            ID2D1Bitmap*& posto = icone[p.chiave];
            if (posto) posto->Release();
            posto = b;
        }
        if (finite && arrivate.empty()) KillTimer(v.fin, 4);
    }

    void esegui(int i) {
        if (i < 0 || i >= (int)risultati.size()) return;
        const Risultato r = risultati[i];
        chiudi(false);
        switch (r.tipo) {
            case T_CALCOLO:
                if (!r.azione.empty() && OpenClipboard(v.fin)) {
                    EmptyClipboard();
                    const size_t n = (r.azione.size() + 1) * sizeof(wchar_t);
                    if (HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, n)) { memcpy(GlobalLock(g), r.azione.c_str(), n); GlobalUnlock(g); SetClipboardData(CF_UNICODETEXT, g); }
                    CloseClipboard();
                }
                if (primaDi && IsWindow(primaDi)) SetForegroundWindow(primaDi);
                break;
            case T_AZIONE:
                if (r.azione.rfind(L"*url:", 0) == 0) {
                    ShellExecuteW(nullptr, L"open", r.azione.c_str() + 5, nullptr, nullptr, SW_SHOWNORMAL);
                } else if (r.azione == L"*act:lock") {
                    LockWorkStation();
                } else if (r.azione == L"*act:sleep") {
                    SetSuspendState(FALSE, FALSE, FALSE);
                } else if (r.azione == L"*act:display") {
                    PostMessageW(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, 2);
                } else if (r.azione == L"*act:trash") {
                    SHEmptyRecycleBinW(nullptr, nullptr, 0);
                } else if (r.azione == L"*act:theme") {
                    DWORD cur = 1, sz = sizeof(cur);
                    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &cur, &sz);
                    const DWORD nuovo = cur == 0 ? 1 : 0;
                    HKEY hk = nullptr;
                    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                                        0, nullptr, 0, KEY_SET_VALUE, nullptr, &hk, nullptr) == ERROR_SUCCESS && hk) {
                        RegSetValueExW(hk, L"AppsUseLightTheme", 0, REG_DWORD, (const BYTE*)&nuovo, sizeof(nuovo));
                        RegSetValueExW(hk, L"SystemUsesLightTheme", 0, REG_DWORD, (const BYTE*)&nuovo, sizeof(nuovo));
                        RegCloseKey(hk);
                    }
                    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"ImmersiveColorSet", SMTO_ABORTIFHUNG, 500, nullptr);
                } else if (r.azione == L"*act:snip") {
                    ShellExecuteW(nullptr, L"open", L"ms-screenclip:", nullptr, nullptr, SW_SHOWNORMAL);
                } else if (r.azione == L"*act:taskmgr") {
                    ShellExecuteW(nullptr, L"open", L"taskmgr.exe", nullptr, nullptr, SW_SHOWNORMAL);
                } else if (r.azione == L"*act:mute") {
                    INPUT in[2]{};
                    in[0].type = INPUT_KEYBOARD; in[0].ki.wVk = VK_VOLUME_MUTE;
                    in[1].type = INPUT_KEYBOARD; in[1].ki.wVk = VK_VOLUME_MUTE; in[1].ki.dwFlags = KEYEVENTF_KEYUP;
                    SendInput(2, in, sizeof(INPUT));
                }
                break;
            case T_WEB: {
                // tutta la stringa in UTF-8 in una volta, poi in percentuale: prima un carattere alla volta, e le emoji o
                // gli ideogrammi fuori dal piano base (due wchar_t) diventavano caratteri sostitutivi (S20)
                std::wstring u = L"https://www.google.com/search?q=";
                const int n8 = WideCharToMultiByte(CP_UTF8, 0, r.azione.c_str(), (int)r.azione.size(), nullptr, 0, nullptr, nullptr);
                std::string b8(n8 > 0 ? n8 : 0, '\0');
                if (n8 > 0) WideCharToMultiByte(CP_UTF8, 0, r.azione.c_str(), (int)r.azione.size(), b8.data(), n8, nullptr, nullptr);
                for (unsigned char c : b8) {
                    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) u += (wchar_t)c;
                    else if (c == ' ') u += L'+';
                    else { wchar_t h[4]; swprintf(h, 4, L"%%%02X", c); u += h; }
                }
                ShellExecuteW(nullptr, L"open", u.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                break;
            }
            default:
                if (r.azione == L"*fluidwin") { if (apriImpostazioni) apriImpostazioni(); }
                else ShellExecuteW(nullptr, L"open", r.azione.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    }

    int rigaIn(int px, int py) const {
        const float d = v.dpi, y0 = (ALTO_BARRA + 10) * d;
        if (py < y0 || px < 8 * d || px > v.W - 8 * d) return -1;
        const int i = (int)((py - y0) / (ALTO_RIGA * d));
        return i < (int)risultati.size() ? i : -1;
    }

    void disegna() {
        if (!apertaOra && !chiudendo) return;
        controllaTarget();
        if (!v.inizia()) return;
        const float d = v.dpi, W = (float)v.W;
        const D2D1_COLOR_F ink = v.inchiostro(), tenue = v.inchiostro(0.55f);
        // lente
        v.pen->SetColor(tenue);
        const float lx = 30 * d, ly = ALTO_BARRA * d / 2;
        v.rt->DrawEllipse({{lx - 1.5f * d, ly - 1.5f * d}, 7.5f * d, 7.5f * d}, v.pen, 2.0f * d);
        v.rt->DrawLine({lx + 4 * d, ly + 4 * d}, {lx + 9 * d, ly + 9 * d}, v.pen, 2.2f * d);
        const D2D1_RECT_F rt = {54 * d, 0, W - 24 * d, ALTO_BARRA * d};
        if (testo.empty()) v.testo(testi::t(L"Cerca app, file, impostazioni…", L"Search apps, files, settings…"), fTesto, rt, tenue);
        else v.testo(testo, fTesto, rt, ink);
        if (cursore) {
            const float cx = rt.left + (testo.empty() ? 0 : std::min(rt.right - rt.left, v.larghezza(testo, fTesto))) + 1.5f * d;
            v.pen->SetColor(D2D1::ColorF(0.25f, 0.6f, 1.0f, 1));
            v.rt->FillRectangle({cx, ly - 13 * d, cx + 2 * d, ly + 13 * d}, v.pen);
        }
        if (!risultati.empty()) {
            v.pen->SetColor(v.scuro ? D2D1::ColorF(1, 1, 1, 0.10f) : D2D1::ColorF(0, 0, 0, 0.08f));
            v.rt->FillRectangle({0, ALTO_BARRA * d, W, ALTO_BARRA * d + 1}, v.pen);
        }
        const float rr = std::max(8.0f, raggio::vetro() - 10.0f) * d;
        for (int i = 0; i < (int)risultati.size(); i++) {
            const Risultato& r = risultati[i];
            const float y0 = (ALTO_BARRA + 10 + i * ALTO_RIGA) * d;
            const D2D1_RECT_F riga = {8 * d, y0, W - 8 * d, y0 + ALTO_RIGA * d};
            const bool s = i == sel;
            if (s) {
                v.pen->SetColor(D2D1::ColorF(0.04f, 0.52f, 1.0f, 1));
                v.rt->FillRoundedRectangle({riga, std::min(rr, ALTO_RIGA * d / 2), std::min(rr, ALTO_RIGA * d / 2)}, v.pen);
            }
            const D2D1_COLOR_F c = s ? D2D1::ColorF(1, 1, 1, 1) : ink, c2 = s ? D2D1::ColorF(1, 1, 1, 0.75f) : tenue;
            // icona
            const float ix = riga.left + 12 * d, iy = y0 + (ALTO_RIGA - 30) * d / 2, il = 30 * d;
            if (r.icona.size() == 11 && r.icona.rfind(L"*col:", 0) == 0) {
                const unsigned rgb = (unsigned)wcstoul(r.icona.c_str() + 5, nullptr, 16);
                const float cr = ((rgb >> 16) & 255) / 255.0f, cg = ((rgb >> 8) & 255) / 255.0f, cb = (rgb & 255) / 255.0f;
                const float cx = ix + il / 2, cy = iy + il / 2;
                v.pen->SetColor(D2D1::ColorF(cr, cg, cb, 1.0f));
                v.rt->FillEllipse({{cx, cy}, 11 * d, 11 * d}, v.pen);
                v.pen->SetColor(s ? D2D1::ColorF(1, 1, 1, 0.85f) : D2D1::ColorF(ink.r, ink.g, ink.b, 0.35f));
                v.rt->DrawEllipse({{cx, cy}, 11 * d, 11 * d}, v.pen, 1.5f * d);
            } else {
                auto it = icone.find(r.icona);
                if (!r.icona.empty() && it != icone.end() && it->second) v.rt->DrawBitmap(it->second, {ix, iy, ix + il, iy + il});
                else simbolo(r.tipo, ix + il / 2, iy + il / 2, c);
            }
            const float tx = ix + il + 12 * d;
            v.testo(r.titolo, fTitolo, {tx, y0 + 5 * d, riga.right - 12 * d, y0 + 26 * d}, c);
            v.testo(r.sotto, fSotto, {tx, y0 + 25 * d, riga.right - 12 * d, y0 + 43 * d}, c2);
        }
        v.finisci();
    }

    void simbolo(Tipo t, float cx, float cy, D2D1_COLOR_F c) {
        const float d = v.dpi;
        v.pen->SetColor({c.r, c.g, c.b, c.a * 0.85f});
        switch (t) {
            case T_CALCOLO:
                v.rt->DrawRoundedRectangle({{cx - 9 * d, cy - 11 * d, cx + 9 * d, cy + 11 * d}, 3 * d, 3 * d}, v.pen, 1.6f * d);
                v.rt->DrawLine({cx - 5 * d, cy - 5 * d}, {cx + 5 * d, cy - 5 * d}, v.pen, 1.6f * d);
                for (int i = 0; i < 2; i++) for (int j = 0; j < 3; j++) v.rt->FillEllipse({{cx - 4.5f * d + j * 4.5f * d, cy + 1 * d + i * 5 * d}, 1.2f * d, 1.2f * d}, v.pen);
                break;
            case T_AZIONE:
                v.pen->SetColor({c.r, c.g, c.b, c.a * 0.14f});
                v.rt->FillRoundedRectangle({{cx - 11 * d, cy - 11 * d, cx + 11 * d, cy + 11 * d}, 6 * d, 6 * d}, v.pen);
                v.pen->SetColor({c.r, c.g, c.b, c.a * 0.85f});
                v.rt->DrawRoundedRectangle({{cx - 11 * d, cy - 11 * d, cx + 11 * d, cy + 11 * d}, 6 * d, 6 * d}, v.pen, 1.5f * d);
                v.rt->DrawLine({cx + 1.5f * d, cy - 6.5f * d}, {cx - 4.0f * d, cy + 0.5f * d}, v.pen, 1.8f * d);
                v.rt->DrawLine({cx - 4.0f * d, cy + 0.5f * d}, {cx + 0.8f * d, cy + 0.5f * d}, v.pen, 1.8f * d);
                v.rt->DrawLine({cx + 0.8f * d, cy + 0.5f * d}, {cx - 1.5f * d, cy + 6.5f * d}, v.pen, 1.8f * d);
                break;
            case T_WEB:
                v.rt->DrawEllipse({{cx, cy}, 10 * d, 10 * d}, v.pen, 1.6f * d);
                v.rt->DrawEllipse({{cx, cy}, 4.5f * d, 10 * d}, v.pen, 1.4f * d);
                v.rt->DrawLine({cx - 10 * d, cy}, {cx + 10 * d, cy}, v.pen, 1.4f * d);
                break;
            case T_IMPOSTAZIONE:
                v.rt->DrawEllipse({{cx, cy}, 6 * d, 6 * d}, v.pen, 2.2f * d);
                for (int i = 0; i < 8; i++) {
                    const float a = i * 3.14159265f / 4 + 0.39f, co = std::cos(a), si = std::sin(a);
                    v.rt->DrawLine({cx + co * 7 * d, cy + si * 7 * d}, {cx + co * 9.5f * d, cy + si * 9.5f * d}, v.pen, 2.6f * d);
                }
                break;
            default:   // file o app senza icona
                v.rt->DrawRoundedRectangle({{cx - 8 * d, cy - 11 * d, cx + 8 * d, cy + 11 * d}, 2.5f * d, 2.5f * d}, v.pen, 1.6f * d);
        }
    }

    static const std::vector<Risultato>& azioni() {
        static const wchar_t* const t[][4] = {
                {L"Blocca schermo", L"Lock screen", L"lock lock screen blocca blocca schermo", L"*act:lock"},
                {L"Sospendi", L"Sleep", L"sleep sospendi standby", L"*act:sleep"},
                {L"Spegni schermo", L"Sleep display", L"display off screen off sleep display spegni schermo", L"*act:display"},
                {L"Svuota cestino", L"Empty Recycle Bin", L"empty trash empty recycle bin svuota cestino", L"*act:trash"},
                {L"Cambia tema scuro / chiaro", L"Toggle Dark / Light mode", L"dark mode light mode toggle theme tema scuro tema chiaro", L"*act:theme"},
                {L"Cattura schermo", L"Take screenshot", L"screenshot snip cattura schermo", L"*act:snip"},
                {L"Gestione attivit\u00E0", L"Task Manager", L"task manager gestione attivit\u00E0", L"*act:taskmgr"},
                {L"Muto / Riattiva audio", L"Mute / Unmute audio", L"mute unmute muto", L"*act:mute"},
        };
        static std::vector<Risultato> p[testi::N_LINGUE];
        std::vector<Risultato>& v = p[testi::attuale()];
        if (v.empty()) for (const auto& x : t) v.push_back({T_AZIONE, testi::t(x[0], x[1]), x[2], x[3], L""});
        return v;
    }

    // pagine delle impostazioni: nome in italiano e in inglese, parole chiave nelle due lingue (si trova in tutte e due)
    static const std::vector<Risultato>& pagine() {
        static const wchar_t* t[][4] = {
                {L"Schermo", L"Display", L"display risoluzione monitor scala resolution scale", L"ms-settings:display"},
                {L"Audio", L"Sound", L"suono volume microfono altoparlanti audio microphone speakers", L"ms-settings:sound"},
                {L"Notifiche", L"Notifications", L"avvisi non disturbare alerts do not disturb", L"ms-settings:notifications"},
                {L"Bluetooth e dispositivi", L"Bluetooth & devices", L"cuffie mouse tastiera headphones keyboard", L"ms-settings:bluetooth"},
                {L"Wi-Fi", L"Wi-Fi", L"wireless rete internet network", L"ms-settings:network-wifi"},
                {L"Rete e Internet", L"Network & internet", L"ethernet connessione proxy vpn connection", L"ms-settings:network-status"},
                {L"Personalizzazione", L"Personalization", L"aspetto appearance", L"ms-settings:personalization"},
                {L"Sfondo", L"Background", L"wallpaper desktop sfondo", L"ms-settings:personalization-background"},
                {L"Colori e tema scuro", L"Colors and dark mode", L"modalità scura chiara colore dark light theme", L"ms-settings:colors"},
                {L"App installate", L"Installed apps", L"disinstalla programmi uninstall programs", L"ms-settings:appsfeatures"},
                {L"App predefinite", L"Default apps", L"browser apri con open with", L"ms-settings:defaultapps"},
                {L"App all'avvio", L"Startup apps", L"esecuzione automatica autostart", L"ms-settings:startupapps"},
                {L"Account", L"Accounts", L"utente profilo user profile", L"ms-settings:yourinfo"},
                {L"Data e ora", L"Date & time", L"orologio fuso orario clock time zone", L"ms-settings:dateandtime"},
                {L"Lingua e area geografica", L"Language & region", L"italiano inglese formato english format", L"ms-settings:regionlanguage"},
                {L"Tastiera e digitazione", L"Keyboard & typing", L"layout tastiera keyboard", L"ms-settings:typing"},
                {L"Mouse", L"Mouse", L"puntatore velocità touchpad pointer speed", L"ms-settings:mousetouchpad"},
                {L"Energia", L"Power", L"batteria sospensione spegnimento battery sleep", L"ms-settings:powersleep"},
                {L"Archiviazione", L"Storage", L"spazio disco pulizia disk space cleanup", L"ms-settings:storagesense"},
                {L"Windows Update", L"Windows Update", L"aggiornamenti updates", L"ms-settings:windowsupdate"},
                {L"Sicurezza di Windows", L"Windows Security", L"antivirus defender firewall", L"windowsdefender:"},
                {L"Privacy e sicurezza", L"Privacy & security", L"autorizzazioni fotocamera posizione permissions camera location", L"ms-settings:privacy"},
                {L"Stampanti e scanner", L"Printers & scanners", L"stampa print", L"ms-settings:printers"},
                {L"Informazioni sul sistema", L"About", L"pc specifiche nome versione specs name version", L"ms-settings:about"},
                {L"Gioco", L"Gaming", L"game bar modalità gioco game mode", L"ms-settings:gaming-gamebar"},
                {L"Accessibilità", L"Accessibility", L"testo grande contrasto narratore large text contrast narrator", L"ms-settings:easeofaccess"},
                {L"Pannello di controllo", L"Control Panel", L"classico classic", L"control"},
                {L"Gestione attività", L"Task Manager", L"processi task manager processes", L"taskmgr"},
                {L"Impostazioni di FluidWin", L"FluidWin settings", L"dock gelatina genio panoramica jelly genie overview", L"*fluidwin"},
        };
        static std::vector<Risultato> p[testi::N_LINGUE];
        std::vector<Risultato>& v = p[testi::attuale()];
        if (v.empty()) for (auto& x : t) v.push_back({T_IMPOSTAZIONE, testi::t(x[0], x[1]), x[2], x[3], L""});
        return v;
    }

    // ---- thread: file dall'indice di ricerca di Windows e icone ----
    static DWORD WINAPI lavoratore(LPVOID p) {
        Ricerca* r = (Ricerca*)p;
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        for (;;) {
            WaitForSingleObject(r->evento, INFINITE);
            for (;;) {
                std::wstring q, ic; LONG g = -1;
                EnterCriticalSection(&r->cs);
                if (r->genRichiesta >= 0) { q = r->richiesta; g = r->genRichiesta; r->genRichiesta = -1; }
                else if (!r->iconeDaFare.empty()) { ic = r->iconeDaFare.front(); r->iconeDaFare.pop_front(); r->iconeInCorso++; }
                LeaveCriticalSection(&r->cs);
                if (g >= 0) {
                    // al massimo 8 (prima 5), dopo aver saltato quelli di sistema; quanti se ne vedono lo decide componi
                    auto* lista = new std::vector<Risultato>(cercaFile(q, 8));
                    if (g != r->generazione || !consegna::posta(r->v.fin, WM_APP + 1, (WPARAM)g, lista)) delete lista;
                } else if (!ic.empty()) {
                    Pixel px; px.chiave = ic;
                    pixelIcona(ic, 64, px.w, px.h, px.px);
                    EnterCriticalSection(&r->cs); r->iconePronte.push_back(std::move(px)); r->iconeInCorso--; LeaveCriticalSection(&r->cs);
                } else break;
            }
        }
        return 0;
    }

    // file e cartelle dall'indice di Windows (indice.h, condiviso con il launcher): tutti i luoghi indicizzati, senza
    // AppData, cartelle nascoste e menu Start. Prima solo la cartella dell'utente
    static std::vector<Risultato> cercaFile(const std::wstring& q, int quanti) {
        std::vector<Risultato> out;
        for (auto& f : indice::cerca(q, quanti, true)) out.push_back({T_FILE, f.nome, f.cartella, f.percorso, f.percorso});
        return out;
    }

    // pixel dell'icona (premoltiplicati) dalla shell
    static bool pixelIcona(const std::wstring& percorso, int lato, int& w, int& h, std::vector<UINT>& px) {
        IShellItemImageFactory* fab = nullptr;
        if (FAILED(SHCreateItemFromParsingName(percorso.c_str(), nullptr, IID_PPV_ARGS(&fab)))) return false;
        HBITMAP hb = nullptr;
        const HRESULT hr = fab->GetImage({lato, lato}, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &hb);
        fab->Release();
        if (FAILED(hr) || !hb) return false;
        BITMAP bm{}; GetObject(hb, sizeof(bm), &bm);
        w = bm.bmWidth; h = bm.bmHeight;
        px.assign((size_t)w * h, 0);
        BITMAPINFO bi{}; bi.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB};
        HDC scr = GetDC(nullptr); GetDIBits(scr, hb, 0, h, px.data(), &bi, DIB_RGB_COLORS); ReleaseDC(nullptr, scr);
        DeleteObject(hb);
        bool alfa = false;
        for (UINT p : px) if (p >> 24) { alfa = true; break; }
        for (UINT& p : px) {
            const UINT a = alfa ? (p >> 24) : 255;
            p = (a << 24) | ((((p >> 16) & 255) * a / 255) << 16) | ((((p >> 8) & 255) * a / 255) << 8) | ((p & 255) * a / 255);
        }
        return true;
    }
};

}  // namespace ricerca
