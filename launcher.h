// Launcher di FluidWin: tutte le app con la ricerca (tasto Win o icona "App" del dock). Metodi di dock::Motore,
// dichiarati in dock.h (che include questo file).
#pragma once

namespace dock {

// ---- launcher: tutte le app, con la ricerca (tasto Win o icona "App" del dock) ----

inline DWORD WINAPI Motore::threadIcone(LPVOID p) {
    Motore* m = (Motore*)p;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    while (!m->fineIcone) {
        WaitForSingleObject(m->eventoIcone, INFINITE);
        while (!m->fineIcone) {
            std::pair<std::wstring, int> r;
            EnterCriticalSection(&m->csIcone);
            const bool c = !m->iconeDaFare.empty();
            if (c) { r = m->iconeDaFare.front(); m->iconeDaFare.pop_front(); }
            LeaveCriticalSection(&m->csIcone);
            if (!c) break;
            IconaPronta ip; ip.percorso = r.first; ip.lato = r.second;
            ip.ok = pixelIcona(r.first, r.second, ip.w, ip.h, ip.px);
            EnterCriticalSection(&m->csIcone); m->iconePronte.push_back(std::move(ip)); LeaveCriticalSection(&m->csIcone);
            PostMessageW(m->icone, WM_ICONA_PRONTA, 0, 0);
        }
    }
    CoUninitialize();
    return 0;
}

inline auto Motore::chiaveIcona(const std::wstring& percorso, int lato) -> std::wstring { return std::to_wstring(lato) + L"|" + percorso; }

inline DWORD WINAPI Motore::threadSottofondo(LPVOID p) {
    Sottofondo* l = (Sottofondo*)p;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    l->fai();
    CoUninitialize();
    if (!PostMessageW(l->dest, WM_LAVORO_FATTO, 0, (LPARAM)l)) delete l;   // FluidWin si sta chiudendo
    return 0;
}

inline auto Motore::inSottofondo(std::function<void()> fai, std::function<void()> applica) -> void {
    Sottofondo* l = new Sottofondo{std::move(fai), std::move(applica), icone};
    // nel pool di thread (vedi pool.h); se non parte, subito qui, come prima
    if (!pool::lancia(threadSottofondo, l, THREAD_PRIORITY_BELOW_NORMAL, true)) { l->fai(); l->applica(); delete l; }
}

inline auto Motore::chiediIcona(const std::wstring& percorso, int lato) -> void {
    if (percorso.empty() || !iconeChieste.insert(chiaveIcona(percorso, lato)).second) return;
    if (!csIconeOk) {
        InitializeCriticalSection(&csIcone); csIconeOk = true;
        eventoIcone = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        threadIconeH = CreateThread(nullptr, 0, threadIcone, this, 0, nullptr);
        if (threadIconeH) SetThreadPriority(threadIconeH, THREAD_PRIORITY_BELOW_NORMAL);
    }
    EnterCriticalSection(&csIcone); iconeDaFare.push_back({percorso, lato}); LeaveCriticalSection(&csIcone);
    iconeAttese++;
    SetEvent(eventoIcone);
}

// all'uscita il thread si ferma (dopo l'icona che sta preparando) prima che il Motore sparisca
inline auto Motore::fermaIcone() -> void {
    if (!threadIconeH) return;
    fineIcone = 1; SetEvent(eventoIcone);
    WaitForSingleObject(threadIconeH, 2000);
    CloseHandle(threadIconeH); threadIconeH = nullptr;
}

inline auto Motore::sorgenteIcona(const Voce& v) -> std::wstring {
    if (v.avvio == AVVIO_PILA) return cartellaDownload();   // l'icona della cartella Download
    if (!v.pacchetto.empty()) return v.pacchetto;             // app di pacchetto fissata (vedi Voce::pacchetto)
    return v.fissata ? v.avvio : (v.avvio.rfind(L"shell:AppsFolder\\", 0) == 0 ? v.avvio : v.exe);
}

// le icone arrivate dal thread diventano bitmap (veloce) e vanno a chi le aspetta: true se ne e' arrivata qualcuna
inline auto Motore::raccogliIcone() -> bool {
    if (!csIconeOk) return false;
    std::vector<IconaPronta> arrivate;
    EnterCriticalSection(&csIcone); arrivate.swap(iconePronte); LeaveCriticalSection(&csIcone);
    for (auto& ip : arrivate) {
        iconeAttese--;
        ID2D1Bitmap1* b = ip.ok ? bitmapDaPixel(ip.w, ip.h, ip.px) : nullptr;
        bool presa = false;
        if (ip.lato == 256) {          // dock
            for (auto& v : voci) if (!v.icona && v.avvio != AVVIO_LAUNCHER && v.avvio != AVVIO_GIOCHI && v.avvio != AVVIO_CONTROLLO && sorgenteIcona(v) == ip.percorso) { v.icona = b; presa = true; dirty = true; break; }
            // (si potra' richiedere: l'app puo' chiudersi e riaprirsi; se e' fallita si riprova al prossimo aggiornamento)
            iconeChieste.erase(chiaveIcona(ip.percorso, ip.lato));
        } else if (ip.lato == 128) {   // app in background (se non c'e', la piccola foto salvata da Windows)
            if (!b) b = (ID2D1Bitmap1*)fotoIconaSfondo(ip.percorso);
            cacheIcone[ip.percorso] = b; presa = b != nullptr;
            for (auto& a : appSfondo) if (a.exe == ip.percorso) a.icona = b;
            pDirty = true; altra.pDirty = true;
        } else if (ip.lato == 96) {    // launcher
            auto assegna = [&](std::vector<AppL>& el) {
                for (auto& a : el) {
                    if (sorgenteIconaL(a) == ip.percorso && !a.icona) {
                        if (b && presa) b->AddRef();
                        a.icona = b; presa = true; lDirty = true;
                    }
                }
            };
            assegna(tutteApp);
            assegna(tuttiGiochi);
            iconeChieste.erase(chiaveIcona(ip.percorso, ip.lato));
        } else if (ip.lato == 64) {    // notifiche
            const std::wstring pre = L"shell:AppsFolder\\";
            if (ip.percorso.rfind(pre, 0) == 0) { iconeN[ip.percorso.substr(pre.size())] = b; presa = true; }
            else if (auto it = iconeNDaExe.find(ip.percorso); it != iconeNDaExe.end()) { iconeN[it->second] = b; presa = true; }
            pDirty = true; altra.pDirty = true;
        }
        if (b && !presa) b->Release();
    }
    return !arrivate.empty();
}

inline auto Motore::launcherDaDisegnare() const -> bool { return lAperto && (lDirty || lAnim); }

inline auto Motore::creaLauncher(HINSTANCE inst) -> bool {
    LW = std::ceil(760 * dpi); LH = LHMAX = std::ceil(580 * dpi); LEXTRA = std::ceil(160 * dpi);
    WNDCLASSW wl{}; wl.lpfnWndProc = procLauncher; wl.hInstance = inst; wl.lpszClassName = L"FluidWin.Launcher";
    wl.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wl);
    launcher = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, wl.lpszClassName, L"App",
                               WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, inst, nullptr);
    vetroLauncher = creaVetro(inst);
    BOOL niente = TRUE; DwmSetWindowAttribute(launcher, DWMWA_TRANSITIONS_FORCEDISABLED, &niente, sizeof(niente));
    // ogni passo puo' fallire: false (niente launcher), mai un puntatore nullo usato dopo (audit FW-AUDIT-004)
    IDXGIDevice* dd = nullptr; IDXGIAdapter* ad = nullptr; IDXGIFactory2* fab = nullptr;
    HRESULT hr = dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dd);
    if (SUCCEEDED(hr)) hr = dd->GetAdapter(&ad);
    if (SUCCEEDED(hr)) hr = ad->GetParent(__uuidof(IDXGIFactory2), (void**)&fab);
    if (SUCCEEDED(hr)) {
        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Width = (UINT)LW; sd.Height = (UINT)(LH + LEXTRA); sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 2;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL; sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        hr = fab->CreateSwapChainForComposition(dev, &sd, nullptr, &scL);
    }
    if (fab) fab->Release();
    if (ad) ad->Release();
    if (dd) dd->Release();
    if (FAILED(hr)) return false;
    if (FAILED(dcomp->CreateTargetForHwnd(launcher, TRUE, &targetL)) || FAILED(dcomp->CreateVisual(&visualL)) ||
        FAILED(dcomp->CreateEffectGroup(&effettoL))) return false;
    visualL->SetContent(scL); visualL->SetEffect(effettoL);
    targetL->SetRoot(visualL); dcomp->Commit();
    IDXGISurface* sup = nullptr;
    if (FAILED(scL->GetBuffer(0, __uuidof(IDXGISurface), (void**)&sup)) || !sup) return false;
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                                                         D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    dc->CreateBitmapFromDxgiSurface(sup, &bp, &bersaglioL); sup->Release();
    if (bersaglioL) DragAcceptFiles(launcher, TRUE);
    return bersaglioL != nullptr;
}

// solo le app vere: niente disinstallazioni, documenti, pagine web e guide
inline auto Motore::appUtile(const std::wstring& nome, const std::wstring& par) -> bool {
    const std::wstring n = minuscolo(nome), p = minuscolo(par);
    if (n.find(L"uninstall") != std::wstring::npos || n.find(L"disinstall") != std::wstring::npos) return false;
    if (p.rfind(L"http", 0) == 0) return false;
    const size_t barra = p.find_last_of(L"\\/"), punto = p.rfind(L'.');
    if (barra == std::wstring::npos) return true;   // AppUserModelID (app dello Store e molte app desktop)
    if (punto == std::wstring::npos || punto < barra) return true;
    const std::wstring est = p.substr(punto + 1);
    return est == L"exe" || est == L"msc" || est == L"lnk" || est == L"cpl";
}

inline auto Motore::leggiTutteApp(bool subito) -> void {
    lLetto = adesso();
    if (subito) { Cronometro cr(this, "leggiTutteApp"); applicaTutteApp(elencaTutteApp()); return; }
    if (lavoroTutte) return;
    lavoroTutte = true;
    auto ris = std::make_shared<std::vector<AppL>>();
    inSottofondo([ris] { *ris = elencaTutteApp(); }, [this, ris] { lavoroTutte = false; applicaTutteApp(std::move(*ris)); });
}

inline auto Motore::applicaTutteApp(std::vector<AppL> nuove) -> void {
    // le icone gia' caricate si conservano
    for (auto& a : nuove) for (auto& v : tutteApp) if (v.avvio == a.avvio) { a.icona = v.icona; a.provata = v.provata; v.icona = nullptr; break; }
    for (auto& v : tutteApp) if (v.icona) v.icona->Release();
    tutteApp = std::move(nuove);
    // launcher aperto (l'elenco nuovo arriva mentre si guarda): la scelta e lo scorrimento restano dove sono
    if (!lModoGiochi) {
        const int sel = lSel; const double scr = lScrollObj;
        filtra();
        if (lAperto) { lSel = std::clamp(sel, 0, std::max(0, (int)filtrate.size() - 1)); lScrollObj = std::clamp(scr, 0.0, lScrollMax()); }
    }
    for (auto& a : tutteApp) if (!a.icona && !a.provata && (!mostraGiochi || !appartieneAiGiochi(a))) { a.provata = true; chiediIcona(sorgenteIconaL(a), 96); }
    if (registro) { char b[64]; snprintf(b, 64, "launcher: %d app", (int)tutteApp.size()); scrivi(b); }
}

inline auto Motore::elencaTutteApp() -> std::vector<AppL> {
    std::vector<AppL> nuove;
    IShellItem* cart = nullptr;
    if (FAILED(SHGetKnownFolderItem(FOLDERID_AppsFolder, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&cart)))) return nuove;
    IEnumShellItems* en = nullptr;
    if (SUCCEEDED(cart->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&en)))) {
        IShellItem* it = nullptr;
        while (en->Next(1, &it, nullptr) == S_OK) {
            LPWSTR n = nullptr, par = nullptr;
            if (SUCCEEDED(it->GetDisplayName(SIGDN_NORMALDISPLAY, &n)) && SUCCEEDED(it->GetDisplayName(SIGDN_PARENTRELATIVEPARSING, &par)) &&
                n && par && n[0] && appUtile(n, par)) {
                AppL a; a.nome = n; a.avvio = std::wstring(L"shell:AppsFolder\\") + par; a.minuscolo = minuscolo(n);
                nuove.push_back(std::move(a));
            }
            CoTaskMemFree(n); CoTaskMemFree(par); it->Release();
        }
        en->Release();
    }
    cart->Release();
    std::sort(nuove.begin(), nuove.end(), [](const AppL& a, const AppL& b) { return lstrcmpiW(a.nome.c_str(), b.nome.c_str()) < 0; });
    nuove.erase(std::unique(nuove.begin(), nuove.end(), [](const AppL& a, const AppL& b) { return a.minuscolo == b.minuscolo; }), nuove.end());
    return nuove;
}

static inline bool finisceCon(const std::wstring& s, const wchar_t* suff) {
    const size_t n = wcslen(suff);
    return s.size() >= n && !_wcsicmp(s.c_str() + s.size() - n, suff);
}

static inline bool esisteFile(const std::wstring& p) {
    if (p.empty()) return false;
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static inline bool esisteCartella(const std::wstring& p) {
    if (p.empty()) return false;
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static inline std::wstring normalizzaPercorso(std::wstring p) {
    for (auto& c : p) if (c == L'/') c = L'\\';
    while (p.size() > 3 && p.back() == L'\\') p.pop_back();
    if (p.size() == 2 && p[1] == L':') p.push_back(L'\\');
    return p;
}

static inline std::wstring chiaveTitolo(const std::wstring& s) {
    std::wstring r;
    r.reserve(s.size());
    for (wchar_t c : s) if (iswalnum(c)) r.push_back((wchar_t)towlower(c));
    return r;
}

static inline std::wstring leggiValoreReg(HKEY k, const wchar_t* nome) {
    DWORD tipo = 0, cb = 0;
    if (RegQueryValueExW(k, nome, nullptr, &tipo, nullptr, &cb) != ERROR_SUCCESS || !cb) return L"";
    if (tipo != REG_SZ && tipo != REG_EXPAND_SZ) return L"";
    std::vector<wchar_t> buf(cb / sizeof(wchar_t) + 2, L'\0');
    if (RegQueryValueExW(k, nome, nullptr, &tipo, (BYTE*)buf.data(), &cb) != ERROR_SUCCESS) return L"";
    if (tipo == REG_EXPAND_SZ) {
        const DWORD n = ExpandEnvironmentStringsW(buf.data(), nullptr, 0);
        if (n > 0) {
            std::vector<wchar_t> esp(n + 1, L'\0');
            if (ExpandEnvironmentStringsW(buf.data(), esp.data(), n + 1)) return esp.data();
        }
    }
    return buf.data();
}

static inline std::wstring leggiFileUtf8(const std::wstring& percorso) {
    HANDLE h = CreateFileW(percorso.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return L"";
    LARGE_INTEGER sz{};
    std::string bytes;
    if (GetFileSizeEx(h, &sz) && sz.QuadPart > 0 && sz.QuadPart <= 4 * 1024 * 1024) {
        bytes.resize((size_t)sz.QuadPart);
        DWORD letto = 0;
        if (!ReadFile(h, bytes.data(), (DWORD)bytes.size(), &letto, nullptr)) bytes.clear();
        else bytes.resize(letto);
    }
    CloseHandle(h);
    if (bytes.empty()) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), out.data(), n);
    return out;
}

static inline bool coppiaVdf(const std::wstring& riga, std::wstring& chiave, std::wstring& val) {
    auto leggi = [&](size_t& i, std::wstring& out) -> bool {
        while (i < riga.size() && riga[i] != L'"') i++;
        if (i >= riga.size()) return false;
        i++;
        out.clear();
        while (i < riga.size() && riga[i] != L'"') {
            if (riga[i] == L'\\' && i + 1 < riga.size()) { out.push_back(riga[i + 1]); i += 2; }
            else out.push_back(riga[i++]);
        }
        if (i >= riga.size()) return false;
        i++;
        return true;
    };
    size_t i = 0;
    return leggi(i, chiave) && leggi(i, val);
}

static inline void cercaExeRic(const std::wstring& dir, int prof, std::wstring& migliore, ULONGLONG& maxMigliore, std::wstring& ripiego, ULONGLONG& maxRipiego) {
    static const wchar_t* const scarta[] = {
        L"crash", L"unins", L"setup", L"redist", L"dxsetup", L"vcredist",
        L"crs-", L"unitycrash", L"easyanticheat", L"battleye", L"ue4prereq", L"ueprereq"
    };
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (prof < 2) cercaExeRic(dir + L"\\" + fd.cFileName, prof + 1, migliore, maxMigliore, ripiego, maxRipiego);
            continue;
        }
        const std::wstring fn = minuscolo(fd.cFileName);
        if (fn.size() <= 4 || fn.compare(fn.size() - 4, 4, L".exe")) continue;
        bool salta = false;
        for (const wchar_t* s : scarta) if (fn.find(s) != std::wstring::npos) { salta = true; break; }
        if (salta) continue;
        const ULONGLONG sz = ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        const std::wstring p = dir + L"\\" + fd.cFileName;
        if (fn.find(L"launcher") == std::wstring::npos) {
            if (migliore.empty() || sz > maxMigliore) { maxMigliore = sz; migliore = p; }
        } else {
            if (ripiego.empty() || sz > maxRipiego) { maxRipiego = sz; ripiego = p; }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static inline std::wstring trovaExeGioco(const std::wstring& dir) {
    if (dir.empty() || !esisteCartella(dir)) return L"";
    std::wstring migliore, ripiego;
    ULONGLONG maxMigliore = 0, maxRipiego = 0;
    cercaExeRic(dir, 0, migliore, maxMigliore, ripiego, maxRipiego);
    return !migliore.empty() ? migliore : ripiego;
}

static inline std::wstring trovaIconaSteamCache(const std::wstring& steamPath, const std::wstring& appid) {
    if (steamPath.empty() || appid.empty()) return L"";
    const std::wstring dir = steamPath + L"\\appcache\\librarycache\\" + appid;
    if (esisteCartella(dir)) {
        std::wstring ico, hashImg, altraImg;
        WIN32_FIND_DATAW fd{};
        HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                const std::wstring fn = minuscolo(fd.cFileName);
                if (fn.find(L"header") != std::wstring::npos || fn.find(L"library_") != std::wstring::npos || fn.find(L"logo") != std::wstring::npos) continue;
                const std::wstring full = dir + L"\\" + fd.cFileName;
                if (finisceCon(fn, L".ico")) { ico = full; break; }
                if (finisceCon(fn, L".jpg") || finisceCon(fn, L".png")) {
                    const size_t dot = fn.rfind(L'.');
                    const std::wstring stem = dot == std::wstring::npos ? fn : fn.substr(0, dot);
                    bool esadec = stem.size() >= 16;
                    for (wchar_t c : stem) if (!iswxdigit(c)) { esadec = false; break; }
                    if (esadec && hashImg.empty()) hashImg = full;
                    else if (altraImg.empty()) altraImg = full;
                }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        if (!ico.empty()) return ico;
        if (!hashImg.empty()) return hashImg;
        if (!altraImg.empty()) return altraImg;
    }
    const std::wstring legacy = steamPath + L"\\appcache\\librarycache\\" + appid + L"_icon.jpg";
    return esisteFile(legacy) ? legacy : L"";
}

inline auto Motore::leggiGiochiNascosti() -> std::unordered_set<std::wstring> {
    std::unordered_set<std::wstring> s;
    DWORD cb = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, CHIAVE_APPBAR, L"GiochiNascosti", RRF_RT_REG_MULTI_SZ, nullptr, nullptr, &cb) != ERROR_SUCCESS || cb <= sizeof(wchar_t)) return s;
    std::vector<wchar_t> buf(cb / sizeof(wchar_t) + 2, L'\0');
    if (RegGetValueW(HKEY_CURRENT_USER, CHIAVE_APPBAR, L"GiochiNascosti", RRF_RT_REG_MULTI_SZ, nullptr, buf.data(), &cb) != ERROR_SUCCESS) return s;
    for (const wchar_t* p = buf.data(); *p; p += wcslen(p) + 1) s.insert(minuscolo(p));
    return s;
}

inline auto Motore::salvaGiochiNascosti(const std::unordered_set<std::wstring>& s) -> void {
    std::vector<wchar_t> buf;
    for (const auto& x : s) {
        if (x.empty()) continue;
        buf.insert(buf.end(), x.begin(), x.end());
        buf.push_back(L'\0');
    }
    if (buf.empty()) { RegDeleteKeyValueW(HKEY_CURRENT_USER, CHIAVE_APPBAR, L"GiochiNascosti"); return; }
    buf.push_back(L'\0');
    RegSetKeyValueW(HKEY_CURRENT_USER, CHIAVE_APPBAR, L"GiochiNascosti", REG_MULTI_SZ, buf.data(), (DWORD)(buf.size() * sizeof(wchar_t)));
}

inline auto Motore::leggiGiochiAggiunti() -> std::vector<std::pair<std::wstring, std::wstring>> {
    std::vector<std::pair<std::wstring, std::wstring>> el;
    DWORD cb = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, CHIAVE_APPBAR, L"GiochiAggiunti", RRF_RT_REG_MULTI_SZ, nullptr, nullptr, &cb) != ERROR_SUCCESS || cb <= sizeof(wchar_t)) return el;
    std::vector<wchar_t> buf(cb / sizeof(wchar_t) + 2, L'\0');
    if (RegGetValueW(HKEY_CURRENT_USER, CHIAVE_APPBAR, L"GiochiAggiunti", RRF_RT_REG_MULTI_SZ, nullptr, buf.data(), &cb) != ERROR_SUCCESS) return el;
    for (const wchar_t* p = buf.data(); *p; p += wcslen(p) + 1) {
        const std::wstring r = p;
        const size_t tab = r.find(L'\t');
        if (tab == std::wstring::npos || tab == 0 || tab + 1 >= r.size()) continue;
        el.push_back({r.substr(0, tab), r.substr(tab + 1)});
    }
    return el;
}

inline auto Motore::salvaGiochiAggiunti(const std::vector<std::pair<std::wstring, std::wstring>>& el) -> void {
    std::vector<wchar_t> buf;
    for (const auto& [nome, percorso] : el) {
        if (nome.empty() || percorso.empty()) continue;
        buf.insert(buf.end(), nome.begin(), nome.end());
        buf.push_back(L'\t');
        buf.insert(buf.end(), percorso.begin(), percorso.end());
        buf.push_back(L'\0');
    }
    if (buf.empty()) { RegDeleteKeyValueW(HKEY_CURRENT_USER, CHIAVE_APPBAR, L"GiochiAggiunti"); return; }
    buf.push_back(L'\0');
    RegSetKeyValueW(HKEY_CURRENT_USER, CHIAVE_APPBAR, L"GiochiAggiunti", REG_MULTI_SZ, buf.data(), (DWORD)(buf.size() * sizeof(wchar_t)));
}

inline auto Motore::leggiIconeGiochi() -> std::unordered_map<std::wstring, std::wstring> {
    std::unordered_map<std::wstring, std::wstring> m;
    DWORD cb = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, CHIAVE_APPBAR, L"IconeGiochi", RRF_RT_REG_MULTI_SZ, nullptr, nullptr, &cb) != ERROR_SUCCESS || cb <= sizeof(wchar_t)) return m;
    std::vector<wchar_t> buf(cb / sizeof(wchar_t) + 2, L'\0');
    if (RegGetValueW(HKEY_CURRENT_USER, CHIAVE_APPBAR, L"IconeGiochi", RRF_RT_REG_MULTI_SZ, nullptr, buf.data(), &cb) != ERROR_SUCCESS) return m;
    for (const wchar_t* p = buf.data(); *p; p += wcslen(p) + 1) {
        const std::wstring r = p;
        const size_t tab = r.find(L'\t');
        if (tab == std::wstring::npos || tab == 0 || tab + 1 >= r.size()) continue;
        m[minuscolo(r.substr(0, tab))] = r.substr(tab + 1);
    }
    return m;
}

inline auto Motore::salvaIconeGiochi(const std::unordered_map<std::wstring, std::wstring>& m) -> void {
    std::vector<wchar_t> buf;
    for (const auto& [chiave, percorso] : m) {
        if (chiave.empty() || percorso.empty()) continue;
        const std::wstring k = minuscolo(chiave);
        buf.insert(buf.end(), k.begin(), k.end());
        buf.push_back(L'\t');
        buf.insert(buf.end(), percorso.begin(), percorso.end());
        buf.push_back(L'\0');
    }
    if (buf.empty()) { RegDeleteKeyValueW(HKEY_CURRENT_USER, CHIAVE_APPBAR, L"IconeGiochi"); return; }
    buf.push_back(L'\0');
    RegSetKeyValueW(HKEY_CURRENT_USER, CHIAVE_APPBAR, L"IconeGiochi", REG_MULTI_SZ, buf.data(), (DWORD)(buf.size() * sizeof(wchar_t)));
}

inline auto Motore::leggiTuttiGiochi(bool subito) -> void {
    gLetto = adesso();
    if (subito) { Cronometro cr(this, "leggiTuttiGiochi"); applicaTuttiGiochi(elencaTuttiGiochi()); return; }
    if (lavoroGiochi) return;
    lavoroGiochi = true;
    auto ris = std::make_shared<std::vector<AppL>>();
    inSottofondo([ris] { *ris = elencaTuttiGiochi(); }, [this, ris] { lavoroGiochi = false; applicaTuttiGiochi(std::move(*ris)); });
}

inline auto Motore::applicaTuttiGiochi(std::vector<AppL> nuovi) -> void {
    for (auto& a : nuovi) {
        const std::wstring s = sorgenteIconaL(a);
        for (auto& v : tuttiGiochi) {
            if (v.avvio == a.avvio && sorgenteIconaL(v) == s) {
                a.icona = v.icona; a.provata = v.provata; v.icona = nullptr; break;
            }
        }
    }
    for (auto& v : tuttiGiochi) if (v.icona) v.icona->Release();
    tuttiGiochi = std::move(nuovi);
    {
        const int sel = lSel; const double scr = lScrollObj;
        filtra();
        if (lAperto) { lSel = std::clamp(sel, 0, std::max(0, (int)filtrate.size() - 1)); lScrollObj = std::clamp(scr, 0.0, lScrollMax()); }
    }
    for (auto& a : tuttiGiochi) if (!a.icona && !a.provata) { a.provata = true; chiediIcona(sorgenteIconaL(a), 96); }
    if (!lModoGiochi) for (auto& a : tutteApp) if (!a.icona && !a.provata && (!mostraGiochi || !appartieneAiGiochi(a))) { a.provata = true; chiediIcona(sorgenteIconaL(a), 96); }
    if (registro) { char b[64]; snprintf(b, 64, "launcher giochi: %d", (int)tuttiGiochi.size()); scrivi(b); }
}

inline auto Motore::elencaTuttiGiochi() -> std::vector<AppL> {
    std::unordered_map<std::wstring, AppL> steam;
    auto pulisciIcona = [](std::wstring s) -> std::wstring {
        while (s.size() >= 2 && s.front() == L'"' && s.back() == L'"') s = s.substr(1, s.size() - 2);
        const size_t virg = s.rfind(L',');
        if (virg != std::wstring::npos && virg + 1 < s.size()) {
            bool cifre = true;
            for (size_t k = virg + 1; k < s.size(); k++) {
                if (k == virg + 1 && s[k] == L'-') continue;
                if (!iswdigit(s[k])) { cifre = false; break; }
            }
            if (cifre) {
                std::wstring prima = s.substr(0, virg);
                while (!prima.empty() && prima.back() == L' ') prima.pop_back();
                while (prima.size() >= 2 && prima.front() == L'"' && prima.back() == L'"') prima = prima.substr(1, prima.size() - 2);
                if (esisteFile(prima) || finisceCon(prima, L".ico") || finisceCon(prima, L".exe")) s = std::move(prima);
            }
        }
        return s;
    };

    const wchar_t* const radiciReg[] = {
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"
    };
    for (const wchar_t* rad : radiciReg) {
        for (REGSAM vista : {(REGSAM)KEY_WOW64_64KEY, (REGSAM)KEY_WOW64_32KEY}) {
            HKEY hRad = nullptr;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, rad, 0, KEY_READ | vista, &hRad) != ERROR_SUCCESS) continue;
            wchar_t nomeSub[256];
            for (DWORD idx = 0; ; idx++) {
                DWORD len = 256;
                if (RegEnumKeyExW(hRad, idx, nomeSub, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
                if (wcsncmp(nomeSub, L"Steam App ", 10) != 0) continue;
                const std::wstring appid = nomeSub + 10;
                if (appid.empty() || appid == L"228980") continue;
                HKEY hSub = nullptr;
                if (RegOpenKeyExW(hRad, nomeSub, 0, KEY_READ | vista, &hSub) != ERROR_SUCCESS) continue;
                const std::wstring dName = leggiValoreReg(hSub, L"DisplayName");
                const std::wstring dIcon = pulisciIcona(leggiValoreReg(hSub, L"DisplayIcon"));
                std::wstring dLoc = leggiValoreReg(hSub, L"InstallLocation");
                while (dLoc.size() >= 2 && dLoc.front() == L'"' && dLoc.back() == L'"') dLoc = dLoc.substr(1, dLoc.size() - 2);
                dLoc = normalizzaPercorso(std::move(dLoc));
                RegCloseKey(hSub);
                AppL& cur = steam[minuscolo(appid)];
                if (cur.avvio.empty()) cur.avvio = L"steam://rungameid/" + appid;
                if (cur.nome.empty() && !dName.empty()) cur.nome = dName;
                if (cur.cartella.empty() && !dLoc.empty()) cur.cartella = dLoc;
                if (cur.iconaFile.empty() && esisteFile(dIcon)) cur.iconaFile = dIcon;
            }
            RegCloseKey(hRad);
        }
    }

    std::wstring steamPath;
    {
        HKEY hSt = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_READ, &hSt) == ERROR_SUCCESS) {
            steamPath = normalizzaPercorso(leggiValoreReg(hSt, L"SteamPath"));
            RegCloseKey(hSt);
        }
    }

    std::vector<std::wstring> librerie, xboxRoots;
    std::unordered_set<std::wstring> libViste;
    auto aggiungiLib = [&](std::wstring p) {
        p = normalizzaPercorso(std::move(p));
        if (p.empty() || !esisteCartella(p)) return;
        if (libViste.insert(minuscolo(p)).second) librerie.push_back(std::move(p));
    };
    if (!steamPath.empty()) {
        aggiungiLib(steamPath);
        const std::wstring vdf = leggiFileUtf8(steamPath + L"\\steamapps\\libraryfolders.vdf");
        size_t pos = 0;
        while (pos < vdf.size()) {
            size_t fine = vdf.find(L'\n', pos);
            if (fine == std::wstring::npos) fine = vdf.size();
            std::wstring riga = vdf.substr(pos, fine - pos);
            if (!riga.empty() && riga.back() == L'\r') riga.pop_back();
            pos = fine + 1;
            std::wstring k, v;
            if (coppiaVdf(riga, k, v) && !_wcsicmp(k.c_str(), L"path")) aggiungiLib(std::move(v));
        }
    }

    const DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; i++) {
        if (!(drives & (1u << i))) continue;
        const wchar_t rad[4] = {(wchar_t)(L'A' + i), L':', L'\\', L'\0'};
        if (GetDriveTypeW(rad) != DRIVE_FIXED) continue;
        const std::wstring sl = std::wstring(rad) + L"SteamLibrary";
        if (esisteCartella(sl)) aggiungiLib(sl);
        const std::wstring xb = std::wstring(rad) + L"Xbox Games";
        if (esisteCartella(xb)) xboxRoots.push_back(xb);
    }

    for (const auto& lib : librerie) {
        const std::wstring apps = lib + L"\\steamapps";
        WIN32_FIND_DATAW fd{};
        HANDLE h = FindFirstFileW((apps + L"\\appmanifest_*.acf").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            const std::wstring txt = leggiFileUtf8(apps + L"\\" + fd.cFileName);
            std::wstring appid, name, installdir;
            size_t pos = 0;
            while (pos < txt.size()) {
                size_t fine = txt.find(L'\n', pos);
                if (fine == std::wstring::npos) fine = txt.size();
                std::wstring riga = txt.substr(pos, fine - pos);
                if (!riga.empty() && riga.back() == L'\r') riga.pop_back();
                pos = fine + 1;
                std::wstring k, v;
                if (!coppiaVdf(riga, k, v)) continue;
                if (!_wcsicmp(k.c_str(), L"appid")) appid = std::move(v);
                else if (!_wcsicmp(k.c_str(), L"name")) name = std::move(v);
                else if (!_wcsicmp(k.c_str(), L"installdir")) installdir = std::move(v);
            }
            if (appid.empty() || appid == L"228980") continue;
            const std::wstring cart = installdir.empty() ? L"" : normalizzaPercorso(apps + L"\\common\\" + installdir);
            AppL& cur = steam[minuscolo(appid)];
            if (cur.avvio.empty()) cur.avvio = L"steam://rungameid/" + appid;
            if (cur.nome.empty()) cur.nome = !name.empty() ? name : installdir;
            if ((cur.cartella.empty() || !esisteCartella(cur.cartella)) && !cart.empty() && esisteCartella(cart)) cur.cartella = cart;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    std::vector<AppL> nuove;
    std::unordered_set<std::wstring> coperte;
    for (auto& [idMin, a] : steam) {
        if (a.nome.empty()) continue;
        if (!a.cartella.empty()) coperte.insert(minuscolo(normalizzaPercorso(a.cartella)));
        if (a.iconaFile.empty()) {
            a.iconaFile = trovaIconaSteamCache(steamPath, idMin);
            if (a.iconaFile.empty() && !a.cartella.empty()) a.iconaFile = trovaExeGioco(a.cartella);
        }
        a.minuscolo = minuscolo(a.nome);
        nuove.push_back(std::move(a));
    }

    auto scansionaCartelle = [&](const std::wstring& radice, bool filtraSteam) {
        WIN32_FIND_DATAW fd{};
        HANDLE h = FindFirstFileW((radice + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            if (filtraSteam && (!_wcsicmp(fd.cFileName, L"Steamworks Shared") || !_wcsicmp(fd.cFileName, L"Steam Controller Configs"))) continue;
            const std::wstring dir = normalizzaPercorso(radice + L"\\" + fd.cFileName);
            const std::wstring dirMin = minuscolo(dir);
            if (coperte.count(dirMin)) continue;
            const std::wstring exe = trovaExeGioco(dir);
            if (exe.empty()) continue;
            coperte.insert(dirMin);
            AppL a;
            a.nome = fd.cFileName;
            a.avvio = exe;
            a.iconaFile = exe;
            a.cartella = dir;
            a.minuscolo = minuscolo(a.nome);
            nuove.push_back(std::move(a));
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    };

    for (const auto& lib : librerie) scansionaCartelle(lib + L"\\steamapps\\common", true);
    for (const auto& xb : xboxRoots) scansionaCartelle(xb, false);

    for (const auto& [nome, percorso] : leggiGiochiAggiunti()) {
        AppL a;
        a.nome = nome;
        a.manuale = true;
        a.minuscolo = minuscolo(nome);
        if (finisceCon(percorso, L".url")) {
            wchar_t url[2048] = L"", ico[MAX_PATH * 2] = L"";
            GetPrivateProfileStringW(L"InternetShortcut", L"URL", L"", url, 2048, percorso.c_str());
            GetPrivateProfileStringW(L"InternetShortcut", L"IconFile", L"", ico, MAX_PATH * 2, percorso.c_str());
            a.avvio = url[0] ? std::wstring(url) : percorso;
            a.iconaFile = (ico[0] && esisteFile(ico)) ? std::wstring(ico) : percorso;
        } else {
            a.avvio = percorso;
            a.iconaFile = percorso;
            if (finisceCon(percorso, L".exe") && esisteFile(percorso)) {
                const size_t bar = percorso.find_last_of(L"\\/");
                if (bar != std::wstring::npos) a.cartella = percorso.substr(0, bar);
            }
        }
        nuove.push_back(std::move(a));
    }

    const auto nascosti = leggiGiochiNascosti();
    nuove.erase(std::remove_if(nuove.begin(), nuove.end(), [&](const AppL& a) {
        return !a.manuale && nascosti.count(minuscolo(a.avvio)) != 0;
    }), nuove.end());

    const auto iconePers = leggiIconeGiochi();
    for (AppL& a : nuove) {
        auto it = iconePers.find(minuscolo(a.avvio));
        if (it == iconePers.end()) it = iconePers.find(a.minuscolo);
        if (it != iconePers.end() && esisteFile(it->second)) {
            a.iconaFile = it->second;
            a.iconaPers = true;
        }
    }

    std::sort(nuove.begin(), nuove.end(), [](const AppL& a, const AppL& b) {
        const int c = lstrcmpiW(a.nome.c_str(), b.nome.c_str());
        if (c != 0) return c < 0;
        if (a.manuale != b.manuale) return a.manuale > b.manuale;
        if (a.iconaPers != b.iconaPers) return a.iconaPers > b.iconaPers;
        const bool ia = !a.iconaFile.empty(), ib = !b.iconaFile.empty();
        if (ia != ib) return ia > ib;
        return a.avvio < b.avvio;
    });
    nuove.erase(std::unique(nuove.begin(), nuove.end(), [](const AppL& a, const AppL& b) {
        return a.minuscolo == b.minuscolo;
    }), nuove.end());
    return nuove;
}

// con la ricerca: prima i nomi che iniziano con il testo, poi quelli che lo contengono
// Ricerca: prima i nomi che iniziano col testo (o una parola che inizia col testo), poi le iniziali delle parole
// ("vsc" -> Visual Studio Code), poi i nomi che lo contengono, infine quelli che ne contengono le lettere in
// ordine ("clcl" -> Calcolatrice)
inline auto Motore::iniziali(const std::wstring& nome, const std::wstring& q) -> bool {
    size_t k = 0;
    for (size_t i = 0; i < nome.size() && k < q.size(); i++)
        if ((i == 0 || nome[i - 1] == L' ' || nome[i - 1] == L'-') && nome[i] == q[k]) k++;
    return k == q.size();
}

inline auto Motore::sottosequenza(const std::wstring& nome, const std::wstring& q) -> bool {
    size_t k = 0;
    for (size_t i = 0; i < nome.size() && k < q.size(); i++) if (nome[i] == q[k]) k++;
    return k == q.size();
}

inline auto Motore::appartieneAiGiochi(const AppL& a) const -> bool {
    std::wstring par = a.avvio;
    if (par.rfind(L"shell:AppsFolder\\", 0) == 0) par.erase(0, 17);
    const std::wstring parMin = minuscolo(normalizzaPercorso(std::move(par)));
    const std::wstring ta = chiaveTitolo(a.nome);
    for (const auto& g : tuttiGiochi) {
        const std::wstring avvG = minuscolo(normalizzaPercorso(g.avvio));
        const std::wstring icoG = minuscolo(normalizzaPercorso(g.iconaFile));
        if (!parMin.empty() && (parMin == avvG || (!icoG.empty() && parMin == icoG))) return true;
        if (!g.cartella.empty()) {
            const std::wstring cartG = minuscolo(normalizzaPercorso(g.cartella)) + L"\\";
            if (parMin.size() > cartG.size() && parMin.compare(0, cartG.size(), cartG) == 0) return true;
        }
        if (a.minuscolo == g.minuscolo || (!ta.empty() && ta == chiaveTitolo(g.nome))) return true;
    }
    return false;
}

inline auto Motore::filtra() -> void {
    filtrate.clear();
    const std::wstring q = minuscolo(query);
    const auto& el = elencoAttivo();
    std::vector<int> livelli[3];   // iniziali, contiene, lettere in ordine
    for (int i = 0; i < (int)el.size(); i++) {
        if (!lModoGiochi && mostraGiochi && appartieneAiGiochi(el[i])) continue;
        if (q.empty()) { filtrate.push_back(i); continue; }
        const std::wstring& n = el[i].minuscolo;
        const size_t p = n.find(q);
        if (p == 0 || (p != std::wstring::npos && n[p - 1] == L' ')) filtrate.push_back(i);
        else if (q.size() >= 2 && iniziali(n, q)) livelli[0].push_back(i);
        else if (p != std::wstring::npos) livelli[1].push_back(i);
        else if (q.size() >= 3 && sottosequenza(n, q)) livelli[2].push_back(i);
    }
    for (auto& l : livelli) filtrate.insert(filtrate.end(), l.begin(), l.end());
    lSel = 0; lScrollObj = 0; lDirty = true;
}

// misure della griglia (coordinate del launcher)
inline auto Motore::lColonne() const -> float { return 6; }

inline auto Motore::lCellaW() const -> float { return (LW - 48 * dpi) / lColonne(); }

inline auto Motore::lCellaH() const -> float { return 104 * dpi; }

// zona della barra di scorrimento che si puo' prendere col mouse (larga 16 punti: la barra disegnata e' sottile) e
// il suo cursore adesso
inline auto Motore::lRettBarra() const -> D2D1_RECT_F { return {LW - 18 * dpi, lGrigliaTop(), LW, lGrigliaBottom()}; }

inline auto Motore::lCursoreBarra() const -> D2D1_RECT_F {
    const double mx = lScrollMax();
    const float alt = lGrigliaBottom() - lGrigliaTop(), tot = alt + (float)mx;
    const float h = std::max(30 * dpi, alt * alt / tot), y = lGrigliaTop() + (mx > 0 ? (alt - h) * (float)(lScroll / mx) : 0);
    return {LW - 18 * dpi, y, LW, y + h};
}

inline auto Motore::lGrigliaTop() const -> float { return 84 * dpi; }

inline auto Motore::lGrigliaBottom() const -> float { return LH - 14 * dpi; }

inline auto Motore::lScrollMax() const -> double {
    const int righe = ((int)filtrate.size() + 5) / 6;
    return std::max(0.0, (double)(righe * lCellaH() - (lGrigliaBottom() - lGrigliaTop())));
}

inline auto Motore::lCella(int i) const -> D2D1_RECT_F {
    const float x = 24 * dpi + (i % 6) * lCellaW(), y = lGrigliaTop() + (i / 6) * lCellaH() - (float)lScroll;
    return {x, y, x + lCellaW(), y + lCellaH()};
}

inline auto Motore::lCellaIn(float x, float y) const -> int {
    if (y < lGrigliaTop() || y > lGrigliaBottom()) return -1;
    for (int i = 0; i < (int)filtrate.size(); i++) {
        const auto r = lCella(i);
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return i;
    }
    return -1;
}

// prende il primo piano anche quando lo apre il tasto Win (nessun clic arrivato al nostro processo)
inline auto Motore::prendiFuoco(HWND w) -> void {
    SetForegroundWindow(w);
    if (GetForegroundWindow() == w) return;
    HWND fg = GetForegroundWindow();
    const DWORD a = fg ? GetWindowThreadProcessId(fg, nullptr) : 0, b = GetCurrentThreadId();
    // (mai agganciati a un'app che non risponde: il suo input bloccato bloccherebbe anche quello di FluidWin)
    if (a && a != b && !IsHungAppWindow(fg) && AttachThreadInput(b, a, TRUE)) { SetForegroundWindow(w); SetFocus(w); AttachThreadInput(b, a, FALSE); }
}

inline auto Motore::apriLauncher(bool giochi) -> void {
    if (!attivo) return;
    if (nascostoSchermoIntero) schermoIntero(false);
    auto caricaElenco = [&](bool g) {
        if (g) {
            if (tuttiGiochi.empty()) leggiTuttiGiochi(true);
            else if (adesso() - gLetto > 600) leggiTuttiGiochi();
        } else {
            if (mostraGiochi && tuttiGiochi.empty()) leggiTuttiGiochi(true);
            if (tutteApp.empty()) leggiTutteApp(true);
            else if (adesso() - lLetto > 600) leggiTutteApp();
        }
    };
    auto aggiornaAncora = [&](bool g) {
        float ax = W / 2.0f;
        const auto pos = posizioniRiposo();
        const wchar_t* chiave = g ? AVVIO_GIOCHI : AVVIO_LAUNCHER;
        for (size_t i = 0; i < voci.size() && i < pos.size(); i++) if (voci[i].avvio == chiave) ax = pos[i] + S / 2;
        ancora = {(LONG)std::clamp(schermo.left + ax - rettLauncher.left, 40 * dpi, LW - 40 * dpi),
                  (LONG)std::min(LH + LEXTRA - 4, (float)(schermo.bottom - H + yBaseRiposo() - S / 2 - rettLauncher.top))};
    };
    if (lAperto && !lChiusura && lModoGiochi == giochi) { chiudiLauncher(); return; }
    if (lAperto && !lChiusura && lModoGiochi != giochi) {
        lModoGiochi = giochi;
        aggiornaAncora(giochi);
        caricaElenco(giochi);
        query.clear(); filtra(); lSopra = -1; lScroll = lScrollObj = 0;
        prendiFuoco(launcher);
        lDirty = true; dirty = true;
        return;
    }
    if (lAperto && lChiusura) {
        if (lModoGiochi != giochi) {
            lModoGiochi = giochi;
            aggiornaAncora(giochi);
            caricaElenco(giochi);
            query.clear(); filtra(); lSopra = -1; lScroll = lScrollObj = 0;
            dirty = true;
        }
        lChiusura = false; lDirty = true; lRipreso = true;
        prendiFuoco(launcher);
        scrivi("launcher: riaperto mentre si chiudeva");
        return;
    }
    lModoGiochi = giochi;
    caricaElenco(giochi);
    if (!nessunaScheda()) { if (dopo != 1) { chiudiTutte(); apriDopo(1, giochi ? 1 : 0); } return; }
    primaDelLauncher = GetForegroundWindow();
    query.clear(); filtra(); lSopra = -1; lScroll = 0;
    lAperto = true; lChiusura = false; lScala = 0.12; lVel = 0; lAlfa = 0; lT0 = adesso(); lRegione = false; lRipreso = false;
    const int x0 = schermo.left + (int)std::round((W - LW) / 2);
    const int y1 = schermo.bottom - H + (int)std::round(yBaseRiposo() - S - PAD - 18 * dpi);
    LH = std::clamp((float)(y1 - schermo.top) - 12 * dpi, 300 * dpi, LHMAX);
    rettLauncher = {x0, y1 - (int)LH, x0 + (int)LW, y1};
    aggiornaAncora(giochi);
    SetWindowRgn(launcher, nullptr, FALSE);
    SetWindowPos(launcher, HWND_TOPMOST, x0, rettLauncher.top, (int)LW, (int)(LH + LEXTRA), SWP_NOACTIVATE);
    posizionaLauncher();
    ShowWindow(vetroLauncher, SW_SHOWNOACTIVATE);
    ShowWindow(launcher, SW_SHOW);
    prendiFuoco(launcher);
    lDirty = true; dirty = true;
    scrivi(GetForegroundWindow() == launcher ? "launcher aperto" : "launcher aperto senza fuoco");
}

inline auto Motore::chiudiLauncher() -> void {
    if (!lAperto || lChiusura) return;
    lChiusura = true; lDirty = true;
    if (lRegione) { SetWindowRgn(launcher, nullptr, FALSE); lRegione = false; }   // per rientrare nell'icona serve tutta la finestra
}

inline auto Motore::nascondiLauncher() -> void {
    // chiuso senza aprire un'app (Esc, Win, clic sul dock): il primo piano torna alla finestra di prima
    if (lAperto && GetForegroundWindow() == launcher && primaDelLauncher && IsWindow(primaDelLauncher) && IsWindowVisible(primaDelLauncher))
        SetForegroundWindow(primaDelLauncher);
    primaDelLauncher = nullptr;
    if (launcher) ShowWindow(launcher, SW_HIDE);
    if (vetroLauncher) ShowWindow(vetroLauncher, SW_HIDE);
    if (lAperto) lChiusoT = adesso();
    lAperto = false; lChiusura = false; lAnim = false; lDirty = false;
    dirty = true;   // il dock puo' tornare a nascondersi
}

// il contenuto (DirectComposition) e il vetro crescono attorno all'icona "App"
inline auto Motore::posizionaLauncher() -> void {
    const double s = lScala;
    const float ax = (float)ancora.x, ay = (float)ancora.y;
    visualL->SetTransform(D2D1::Matrix3x2F::Scale((float)s, (float)s, {ax, ay}));
    effettoL->SetOpacity((float)std::clamp(lAlfa, 0.0, 1.0));
    const RECT& r = rettLauncher;
    const double gx = r.left + ax, gy = r.top + ay;
    const int L = (int)std::lround(gx + (r.left - gx) * s), T = (int)std::lround(gy + (r.top - gy) * s);
    const int R = (int)std::lround(gx + (r.right - gx) * s), B = (int)std::lround(gy + (r.bottom - gy) * s);
    SetWindowPos(vetroLauncher, launcher, L, T, std::max(1, R - L), std::max(1, B - T), SWP_NOACTIVATE);
    SetLayeredWindowAttributes(vetroLauncher, 0, (BYTE)std::lround(std::clamp(lAlfa, 0.0, 1.0) * 255), LWA_ALPHA);
    dcomp->Commit();
}

inline auto Motore::avviaApp(int i) -> void {
    if (i < 0 || i >= (int)filtrate.size()) return;
    const AppL a = elencoAttivo()[filtrate[i]];
    primaDelLauncher = nullptr;   // il primo piano va all'app che si apre
    chiudiLauncher();
    ShellExecuteW(nullptr, L"open", a.avvio.c_str(), nullptr, a.cartella.empty() ? nullptr : a.cartella.c_str(), SW_SHOWNORMAL);
}

inline auto Motore::rettEnergia() const -> D2D1_RECT_F { return {LW - 64 * dpi, 22 * dpi, LW - 24 * dpi, 62 * dpi}; }

// Menu di Windows per l'app (lo stesso dello Start), con in cima "Aggiungi al dock"
inline auto Motore::menuApp(int i, POINT pt) -> void {
    if (i < 0 || i >= (int)filtrate.size()) return;
    const AppL a = tutteApp[filtrate[i]];
    IShellItem* it = nullptr;
    if (FAILED(SHCreateItemFromParsingName(a.avvio.c_str(), nullptr, IID_PPV_ARGS(&it)))) return;
    IContextMenu* cm = nullptr;
    it->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(&cm));
    // eseguibile e AppUserModelID dell'app, per il collegamento nel dock
    std::wstring exe, id;
    IShellItem2* it2 = nullptr;
    if (SUCCEEDED(it->QueryInterface(IID_PPV_ARGS(&it2)))) {
        LPWSTR s = nullptr;
        if (SUCCEEDED(it2->GetString(PKEY_Link_TargetParsingPath, &s)) && s) { exe = s; CoTaskMemFree(s); }
        s = nullptr;
        if (SUCCEEDED(it2->GetString(PKEY_AppUserModel_ID, &s)) && s) { id = s; CoTaskMemFree(s); }
        it2->Release();
    }
    it->Release();
    if (!cm) return;
    const UINT ID_DOCK = 0x7F00, ID_GIOCHI = 0x7EFF;
    HMENU m = CreatePopupMenu();
    if (SUCCEEDED(cm->QueryContextMenu(m, 0, 1, ID_GIOCHI - 1, CMF_NORMAL))) {
        cm->QueryInterface(IID_PPV_ARGS(&menuShell2)); cm->QueryInterface(IID_PPV_ARGS(&menuShell3));
        // le voci di Windows, disegnate da noi; in cima il nome dell'app e "Aggiungi al dock"
        std::vector<menu::Voce> voci = {menu::titolo(a.nome), menu::voce(testi::t(L"Aggiungi al dock", L"Add to dock"), ID_DOCK)};
        if (mostraGiochi) voci.push_back(menu::voce(testi::t(L"Sposta nei giochi", L"Move to Games"), ID_GIOCHI));
        // "Disinstalla" (la voce di Windows, per le app dello Store e per le altre) in cima, subito visibile
        UINT idDisinstalla = 0;
        for (int i = 0; i < GetMenuItemCount(m) && !idDisinstalla; i++) {
            const UINT id = GetMenuItemID(m, i);
            if (id == (UINT)-1 || id == 0) continue;
            wchar_t v[64] = L"";
            if (SUCCEEDED(cm->GetCommandString(id - 1, GCS_VERBW, nullptr, (LPSTR)v, 64)) && !_wcsicmp(v, L"Uninstall")) idDisinstalla = id;
        }
        if (idDisinstalla) voci.push_back(menu::voce(testi::t(L"Disinstalla…", L"Uninstall…"), idDisinstalla));
        voci.push_back(menu::separatore());
        for (auto& v : menu::daHmenu(m)) if (v.id != idDisinstalla || v.separatore) voci.push_back(v);
        // i sottomenu ("Invia a", "Apri con"...) la shell li riempie solo quando stanno per aprirsi
        auto prepara = [this](HMENU s) {
            if (menuShell3) { LRESULT r = 0; menuShell3->HandleMenuMsg2(WM_INITMENUPOPUP, (WPARAM)s, 0, &r); }
            else if (menuShell2) menuShell2->HandleMenuMsg(WM_INITMENUPOPUP, (WPARAM)s, 0);
        };
        const UINT cmd = menu::mostra(voci, pt, launcher, false, prepara);
        if (menuShell3) { menuShell3->Release(); menuShell3 = nullptr; }
        if (menuShell2) { menuShell2->Release(); menuShell2 = nullptr; }
        if (cmd == ID_DOCK) {
            aggiungiAlDock(a.nome, exe, id, a.avvio);
        } else if (cmd == ID_GIOCHI) {
            auto el = leggiGiochiAggiunti();
            const std::wstring target = (!exe.empty() && esisteFile(exe)) ? exe : a.avvio;
            const std::wstring tMin = minuscolo(target);
            bool gia = false;
            for (const auto& kv : el) if (minuscolo(kv.second) == tMin) { gia = true; break; }
            if (!gia) {
                el.push_back({a.nome, target});
                salvaGiochiAggiunti(el);
            }
            leggiTuttiGiochi(true);
        } else if (cmd > 0) {
            CMINVOKECOMMANDINFOEX ci{}; ci.cbSize = sizeof(ci); ci.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
            ci.hwnd = launcher; ci.lpVerb = MAKEINTRESOURCEA(cmd - 1); ci.lpVerbW = MAKEINTRESOURCEW(cmd - 1);
            ci.nShow = SW_SHOWNORMAL; ci.ptInvoke = pt;
            cm->InvokeCommand((CMINVOKECOMMANDINFO*)&ci);
            primaDelLauncher = nullptr;   // ha aperto qualcosa: il primo piano va a quello
            chiudiLauncher();
            appDaAggiornare = true;       // "Aggiungi alla barra" e simili cambiano le icone fisse del dock
        }
    }
    DestroyMenu(m);
    cm->Release();
}

// Collegamento nella cartella delle app fisse della barra (quella che legge il dock)
inline auto Motore::aggiungiAlDock(const std::wstring& nome, const std::wstring& exe, const std::wstring& id, const std::wstring& avvio, const std::wstring& icona) -> void {
    wchar_t cart[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, cart))) return;
    std::wstring n = nome;
    for (auto& c : n) if (wcschr(L"\\/:*?\"<>|", c)) c = L'_';
    const std::wstring file = std::wstring(cart) + L"\\Microsoft\\Internet Explorer\\Quick Launch\\User Pinned\\TaskBar\\" + n + L".lnk";
    if (GetFileAttributesW(file.c_str()) != INVALID_FILE_ATTRIBUTES) return;   // gia' nel dock
    IShellLinkW* sl = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sl)))) return;
    bool ok = false;
    if (!exe.empty() && GetFileAttributesW(exe.c_str()) != INVALID_FILE_ATTRIBUTES) ok = SUCCEEDED(sl->SetPath(exe.c_str()));
    else {   // app dello Store o collegamento URI: il collegamento punta al PIDL
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (SUCCEEDED(SHParseDisplayName(avvio.c_str(), nullptr, &pidl, 0, nullptr))) { ok = SUCCEEDED(sl->SetIDList(pidl)); CoTaskMemFree(pidl); }
    }
    if (ok && !icona.empty() && (finisceCon(icona, L".ico") || finisceCon(icona, L".exe")) && esisteFile(icona))
        sl->SetIconLocation(icona.c_str(), 0);
    if (ok && !id.empty()) {   // cosi' il dock riconosce le finestre dell'app
        IPropertyStore* ps = nullptr;
        if (SUCCEEDED(sl->QueryInterface(IID_PPV_ARGS(&ps)))) {
            PROPVARIANT pv; if (SUCCEEDED(InitPropVariantFromString(id.c_str(), &pv))) { ps->SetValue(PKEY_AppUserModel_ID, pv); ps->Commit(); PropVariantClear(&pv); }
            ps->Release();
        }
    }
    IPersistFile* pf = nullptr;
    if (ok && SUCCEEDED(sl->QueryInterface(IID_PPV_ARGS(&pf)))) { pf->Save(file.c_str(), TRUE); pf->Release(); }
    sl->Release();
    appDaAggiornare = true;
    if (registro) { char b[200]; snprintf(b, 200, "aggiunta al dock: %ls", n.c_str()); scrivi(b); }
}

inline auto Motore::aggiungiGiocoFile(const std::wstring& percorso) -> void {
    if (percorso.empty()) return;
    std::wstring nome;
    if (finisceCon(percorso, L".exe")) nome = nomeExe(percorso);
    if (nome.empty()) {
        const size_t bar = percorso.find_last_of(L"\\/");
        nome = bar == std::wstring::npos ? percorso : percorso.substr(bar + 1);
        const size_t dot = nome.rfind(L'.');
        if (dot != std::wstring::npos) nome = nome.substr(0, dot);
    }
    for (auto& c : nome) if (c == L'\t' || c == L'\r' || c == L'\n') c = L' ';
    while (!nome.empty() && nome.front() == L' ') nome.erase(nome.begin());
    while (!nome.empty() && nome.back() == L' ') nome.pop_back();
    if (nome.empty()) return;
    auto el = leggiGiochiAggiunti();
    const std::wstring pMin = minuscolo(percorso);
    for (const auto& kv : el) if (minuscolo(kv.second) == pMin) return;
    el.push_back({nome, percorso});
    salvaGiochiAggiunti(el);
}

inline auto Motore::aggiungiGiocoDialogo() -> void {
    if (lDialogoAperto) return;
    lDialogoAperto = true;
    wchar_t p[MAX_PATH * 4] = L"";
    OPENFILENAMEW of{};
    of.lStructSize = sizeof(of);
    of.hwndOwner = launcher;
    of.lpstrFile = p;
    of.nMaxFile = MAX_PATH * 4;
    of.lpstrFilter = L"Games (*.exe;*.lnk;*.url)\0*.exe;*.lnk;*.url\0All files (*.*)\0*.*\0";
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    const bool scelto = GetOpenFileNameW(&of) != FALSE;
    lDialogoAperto = false;
    lSopraEnergia = false;
    if (lAperto && !lChiusura) prendiFuoco(launcher);
    if (scelto && p[0]) {
        aggiungiGiocoFile(p);
        leggiTuttiGiochi(true);
    }
    lDirty = true;
}

inline auto Motore::cambiaIconaGioco(const AppL& a) -> void {
    if (lDialogoAperto) return;
    lDialogoAperto = true;
    const std::wstring avvio = a.avvio;
    wchar_t p[MAX_PATH * 4] = L"";
    OPENFILENAMEW of{};
    of.lStructSize = sizeof(of);
    of.hwndOwner = launcher;
    of.lpstrFile = p;
    of.nMaxFile = MAX_PATH * 4;
    of.lpstrFilter = L"Icons & images (*.ico;*.png;*.jpg;*.jpeg;*.bmp;*.webp;*.exe)\0*.ico;*.png;*.jpg;*.jpeg;*.bmp;*.webp;*.exe\0All files (*.*)\0*.*\0";
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    const bool scelto = GetOpenFileNameW(&of) != FALSE;
    lDialogoAperto = false;
    if (lAperto && !lChiusura) prendiFuoco(launcher);
    if (scelto && p[0]) {
        auto ic = leggiIconeGiochi();
        ic[minuscolo(avvio)] = p;
        salvaIconeGiochi(ic);
        iconeChieste.erase(chiaveIcona(p, 96));
        for (auto& g : tuttiGiochi) {
            if (g.avvio == avvio) {
                if (g.icona) { g.icona->Release(); g.icona = nullptr; }
                g.provata = false;
            }
        }
        leggiTuttiGiochi(true);
    }
    lDirty = true;
}

inline auto Motore::menuGioco(int i, POINT pt) -> void {
    if (i < 0 || i >= (int)filtrate.size()) return;
    const AppL a = tuttiGiochi[filtrate[i]];
    const bool isExe = finisceCon(a.avvio, L".exe");
    const bool isLnk = finisceCon(a.avvio, L".lnk");
    const bool isSteam = a.avvio.rfind(L"steam://", 0) == 0;
    const bool hasFile = esisteFile(a.avvio);
    const bool hasCartella = !a.cartella.empty() && esisteCartella(a.cartella);

    std::vector<menu::Voce> vv = {
        menu::titolo(a.nome),
        menu::voce(testi::t(L"Gioca", L"Play"), 1)
    };
    if (isExe || isLnk || isSteam)
        vv.push_back(menu::voce(testi::t(L"Aggiungi al dock", L"Add to dock"), 2));
    if (hasCartella || hasFile)
        vv.push_back(menu::voce(testi::t(L"Apri percorso", L"Open folder"), 3));
    vv.push_back(menu::voce(testi::t(L"Cambia icona…", L"Change icon…"), 6));
    if (a.iconaPers)
        vv.push_back(menu::voce(testi::t(L"Ripristina icona", L"Reset icon"), 7));
    vv.push_back(menu::separatore());
    if (a.manuale)
        vv.push_back(menu::voce(testi::t(L"Rimuovi dai giochi", L"Remove from Games"), 4));
    else
        vv.push_back(menu::voce(testi::t(L"Nascondi dai giochi", L"Hide from Games"), 5));

    const UINT cmd = menu::mostra(vv, pt, launcher);
    if (cmd == 1) {
        primaDelLauncher = nullptr;
        chiudiLauncher();
        ShellExecuteW(nullptr, L"open", a.avvio.c_str(), nullptr, a.cartella.empty() ? nullptr : a.cartella.c_str(), SW_SHOWNORMAL);
    } else if (cmd == 2) {
        if (isExe) {
            aggiungiAlDock(a.nome, a.avvio, L"", a.avvio, a.iconaFile);
        } else if (isLnk) {
            std::wstring exe, id;
            IShellLinkW* sl = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sl)))) {
                IPersistFile* pf = nullptr;
                if (SUCCEEDED(sl->QueryInterface(IID_PPV_ARGS(&pf))) && SUCCEEDED(pf->Load(a.avvio.c_str(), STGM_READ))) {
                    wchar_t dest[MAX_PATH] = L"";
                    if (SUCCEEDED(sl->GetPath(dest, MAX_PATH, nullptr, SLGP_RAWPATH)) && dest[0]) {
                        wchar_t esp[MAX_PATH] = L"";
                        ExpandEnvironmentStringsW(dest, esp, MAX_PATH);
                        exe = esp;
                    }
                    IPropertyStore* ps = nullptr;
                    if (SUCCEEDED(sl->QueryInterface(IID_PPV_ARGS(&ps)))) {
                        PROPVARIANT pv; PropVariantInit(&pv);
                        if (SUCCEEDED(ps->GetValue(PKEY_AppUserModel_ID, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal) id = pv.pwszVal;
                        PropVariantClear(&pv); ps->Release();
                    }
                }
                if (pf) pf->Release();
                sl->Release();
            }
            aggiungiAlDock(a.nome, exe, id, a.avvio, a.iconaFile);
        } else if (isSteam) {
            std::wstring icona = (finisceCon(a.iconaFile, L".ico") || finisceCon(a.iconaFile, L".exe")) ? a.iconaFile : L"";
            if (icona.empty() && !a.cartella.empty()) icona = trovaExeGioco(a.cartella);
            aggiungiAlDock(a.nome, L"", L"", a.avvio, icona);
        }
    } else if (cmd == 3) {
        if (hasFile) ShellExecuteW(nullptr, L"open", L"explorer.exe", (L"/select,\"" + a.avvio + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
        else if (hasCartella) ShellExecuteW(nullptr, L"open", a.cartella.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else if (cmd == 4) {
        auto el = leggiGiochiAggiunti();
        const std::wstring avvioMin = minuscolo(a.avvio);
        el.erase(std::remove_if(el.begin(), el.end(), [&](const auto& kv) {
            if (minuscolo(kv.second) == avvioMin) return true;
            if (finisceCon(kv.second, L".url") && minuscolo(kv.first) == a.minuscolo) {
                wchar_t u[2048] = L"";
                GetPrivateProfileStringW(L"InternetShortcut", L"URL", L"", u, 2048, kv.second.c_str());
                return minuscolo(u[0] ? std::wstring(u) : kv.second) == avvioMin;
            }
            return false;
        }), el.end());
        salvaGiochiAggiunti(el);
        leggiTuttiGiochi(true);
    } else if (cmd == 5) {
        auto n = leggiGiochiNascosti();
        n.insert(minuscolo(a.avvio));
        salvaGiochiNascosti(n);
        leggiTuttiGiochi(true);
    } else if (cmd == 6) {
        cambiaIconaGioco(a);
    } else if (cmd == 7) {
        auto ic = leggiIconeGiochi();
        ic.erase(minuscolo(a.avvio));
        ic.erase(a.minuscolo);
        salvaIconeGiochi(ic);
        for (auto& g : tuttiGiochi) {
            if (g.avvio == a.avvio) {
                if (g.icona) { g.icona->Release(); g.icona = nullptr; }
                g.provata = false;
            }
        }
        leggiTuttiGiochi(true);
    }
}

inline auto Motore::menuSpazioGiochi(POINT pt) -> void {
    const auto nascosti = leggiGiochiNascosti();
    std::vector<menu::Voce> vv = {
        menu::voce(testi::t(L"Aggiungi gioco…", L"Add game shortcut…"), 1),
        menu::voce(testi::t(L"Aggiorna giochi", L"Refresh games"), 2)
    };
    if (!nascosti.empty()) {
        vv.push_back(menu::separatore());
        vv.push_back(menu::voce(testi::t(L"Mostra giochi nascosti", L"Show hidden games"), 3));
    }
    const UINT cmd = menu::mostra(vv, pt, launcher);
    if (cmd == 1) aggiungiGiocoDialogo();
    else if (cmd == 2) leggiTuttiGiochi(true);
    else if (cmd == 3) { salvaGiochiNascosti({}); leggiTuttiGiochi(true); }
}

// Accensione: come il pulsante dello Start
inline auto Motore::menuEnergia(POINT pt) -> void {
    const int s = (int)menu::mostra({menu::voce(testi::t(L"Sospendi", L"Sleep"), 1), menu::voce(testi::t(L"Riavvia", L"Restart"), 2),
                                     menu::voce(testi::t(L"Arresta il sistema", L"Shut down"), 3), menu::separatore(),
                                     menu::voce(testi::t(L"Blocca", L"Lock"), 4), menu::voce(testi::t(L"Disconnetti", L"Sign out"), 5)}, pt, launcher);
    lSopraEnergia = false; lDirty = true;
    if (s <= 0) return;
    chiudiLauncher();
    energia(s);
}

inline auto Motore::energia(int s) -> void {
    if (s == 4) { LockWorkStation(); return; }
    // privilegio di arresto: ce l'ha ogni utente, va solo attivato per questo processo
    HANDLE tok = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) {
        TOKEN_PRIVILEGES tp{}; tp.PrivilegeCount = 1; tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid);
        AdjustTokenPrivileges(tok, FALSE, &tp, 0, nullptr, nullptr);
        CloseHandle(tok);
    }
    const DWORD motivo = SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_MINOR_OTHER | SHTDN_REASON_FLAG_PLANNED;
    // le app con lavoro non salvato possono fermare riavvio e arresto (nessun EWX_FORCE), come dallo Start
    if (s == 1) SetSuspendState(FALSE, FALSE, FALSE);
    else if (s == 2) ExitWindowsEx(EWX_REBOOT, motivo);
    else if (s == 3) ExitWindowsEx(EWX_SHUTDOWN | EWX_POWEROFF | 0x00400000 /* EWX_HYBRID_SHUTDOWN: avvio rapido */, motivo);
    else if (s == 5) ExitWindowsEx(EWX_LOGOFF, 0);
}

inline auto Motore::selezionaVisibile() -> void {
    const auto r = lCella(lSel);
    const float top = lGrigliaTop(), bot = lGrigliaBottom();
    if (r.top < top) lScrollObj -= top - r.top;
    else if (r.bottom > bot) lScrollObj += r.bottom - bot;
    lScrollObj = std::clamp(lScrollObj, 0.0, lScrollMax());
}

// icona "App" nel dock: il logo di Windows 11 (quattro riquadri azzurri sfumati) su vetro chiaro
inline auto Motore::disegnaIconaApp(const D2D1_RECT_F& r, float alfa, bool scuro) -> void {
    const float d = r.right - r.left;
    pennello->SetColor(scuro ? D2D1::ColorF(1, 1, 1, 0.16f * alfa) : D2D1::ColorF(1, 1, 1, 0.82f * alfa));
    dc->FillRoundedRectangle({r, d * 0.24f, d * 0.24f}, pennello);
    pennello->SetColor(D2D1::ColorF(0, 0, 0, (scuro ? 0.3f : 0.08f) * alfa));
    dc->DrawRoundedRectangle({r, d * 0.24f, d * 0.24f}, pennello, 1.0f);
    const float lato = d * 0.50f, fessura = d * 0.028f, q = (lato - fessura) / 2;
    const float x0 = (r.left + r.right - lato) / 2, y0 = (r.top + r.bottom - lato) / 2;
    // una sola sfumatura su tutto il logo, dall'azzurro chiaro in alto a sinistra al blu in basso a destra
    const D2D1_GRADIENT_STOP fermate[2] = {{0, D2D1::ColorF(0.33f, 0.80f, 1.00f, alfa)}, {1, D2D1::ColorF(0.00f, 0.42f, 0.86f, alfa)}};
    ID2D1GradientStopCollection* fc = nullptr; ID2D1LinearGradientBrush* sf = nullptr;
    if (SUCCEEDED(dc->CreateGradientStopCollection(fermate, 2, &fc)))
        dc->CreateLinearGradientBrush({{x0, y0}, {x0 + lato, y0 + lato}}, fc, &sf);
    if (!sf) pennello->SetColor(D2D1::ColorF(0.10f, 0.58f, 0.95f, alfa));
    for (int k = 0; k < 4; k++) {
        const float x = x0 + (k % 2) * (q + fessura), y = y0 + (k / 2) * (q + fessura);
        const D2D1_RECT_F p{x, y, x + q, y + q};
        const float ra = d * 0.012f;
        if (sf) dc->FillRoundedRectangle({p, ra, ra}, sf); else dc->FillRoundedRectangle({p, ra, ra}, pennello);
    }
    if (sf) sf->Release();
    if (fc) fc->Release();
}

inline auto Motore::disegnaIconaGiochi(const D2D1_RECT_F& r, float alfa, bool scuro) -> void {
    const float d = r.right - r.left;
    pennello->SetColor(scuro ? D2D1::ColorF(1, 1, 1, 0.16f * alfa) : D2D1::ColorF(1, 1, 1, 0.82f * alfa));
    dc->FillRoundedRectangle({r, d * 0.24f, d * 0.24f}, pennello);
    pennello->SetColor(D2D1::ColorF(0, 0, 0, (scuro ? 0.3f : 0.08f) * alfa));
    dc->DrawRoundedRectangle({r, d * 0.24f, d * 0.24f}, pennello, 1.0f);

    const float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    const float u = d * 0.01f;
    auto P = [&](float x, float y) { return D2D1::Point2F(cx + x * u, cy + y * u); };

    const D2D1_GRADIENT_STOP fermate[2] = {
        {0, D2D1::ColorF(0.72f, 0.33f, 0.97f, alfa)},
        {1, D2D1::ColorF(0.37f, 0.36f, 0.90f, alfa)}
    };
    ID2D1GradientStopCollection* fc = nullptr;
    ID2D1LinearGradientBrush* sf = nullptr;
    if (SUCCEEDED(dc->CreateGradientStopCollection(fermate, 2, &fc)))
        dc->CreateLinearGradientBrush({P(-30, -16), P(30, 16)}, fc, &sf);
    if (!sf) pennello->SetColor(D2D1::ColorF(0.55f, 0.35f, 0.94f, alfa));

    ID2D1PathGeometry* g = nullptr;
    if (SUCCEEDED(f2d->CreatePathGeometry(&g))) {
        ID2D1GeometrySink* s = nullptr;
        if (SUCCEEDED(g->Open(&s))) {
            s->BeginFigure(P(-15, -16), D2D1_FIGURE_BEGIN_FILLED);
            s->AddLine(P(15, -16));
            s->AddBezier({P(23, -16), P(27, -10), P(30, 2)});
            s->AddBezier({P(33, 13), P(26, 18), P(20, 15)});
            s->AddBezier({P(15, 12), P(12, 6), P(8, 6)});
            s->AddLine(P(-8, 6));
            s->AddBezier({P(-12, 6), P(-15, 12), P(-20, 15)});
            s->AddBezier({P(-26, 18), P(-33, 13), P(-30, 2)});
            s->AddBezier({P(-27, -10), P(-23, -16), P(-15, -16)});
            s->EndFigure(D2D1_FIGURE_END_CLOSED);
            s->Close();
            s->Release();
            dc->FillGeometry(g, sf ? (ID2D1Brush*)sf : (ID2D1Brush*)pennello);
        }
        g->Release();
    }
    if (sf) sf->Release();
    if (fc) fc->Release();

    pennello->SetColor(D2D1::ColorF(1, 1, 1, 0.95f * alfa));
    const float lx = cx - 13.5f * u, ly = cy - 4.5f * u;
    dc->FillRoundedRectangle({{lx - 1.6f * u, ly - 5.0f * u, lx + 1.6f * u, ly + 5.0f * u}, 0.8f * u, 0.8f * u}, pennello);
    dc->FillRoundedRectangle({{lx - 5.0f * u, ly - 1.6f * u, lx + 5.0f * u, ly + 1.6f * u}, 0.8f * u, 0.8f * u}, pennello);

    const float rx = cx + 13.5f * u, ry = cy - 4.5f * u, off = 4.2f * u, rad = 1.75f * u;
    dc->FillEllipse({{rx, ry - off}, rad, rad}, pennello);
    dc->FillEllipse({{rx, ry + off}, rad, rad}, pennello);
    dc->FillEllipse({{rx - off, ry}, rad, rad}, pennello);
    dc->FillEllipse({{rx + off, ry}, rad, rad}, pennello);
}

// restituisce false se il launcher si e' appena chiuso
inline auto Motore::renderLauncher() -> bool {
    // scala con una molla: apre con un piccolo rimbalzo (gelatina), rientra nell'icona rapida e senza
    // in chiusura lo stesso movimento all'indietro: rientra nell'icona con la molla e sfuma solo nell'ultimo tratto
    bool anim = molla(lScala, lVel, lChiusura ? 0.0 : 1.0, 240, lChiusura ? 0.8 : 0.55, 0.0005);
    if (lChiusura) {
        lAlfa = std::clamp((lScala - 0.10) / 0.30, 0.0, 1.0);
        if (lAlfa <= 0.02 || !anim) { nascondiLauncher(); return false; }
    } else {
        lAlfa = verso(lAlfa, 1.0, 0.045);
        if (lAlfa != 1.0) anim = true;
    }
    posizionaLauncher();
    if (!lChiusura && !anim && !lRegione) {
        // aperto: fuori dal launcher (sopra il dock) i clic devono passare
        SetWindowRgn(launcher, CreateRectRgn(0, 0, (int)LW, (int)LH), FALSE); lRegione = true;
    }
    const double sc = verso(lScroll, lScrollObj, 0.06);
    if (sc != lScroll) { lScroll = sc; anim = true; }
    raccogliIcone();   // (quelle che arrivano dal thread rimettono lDirty)

    const bool scuro = temaScuro();
    const D2D1_COLOR_F ink = scuro ? D2D1::ColorF(1, 1, 1, 0.92f) : D2D1::ColorF(0.08f, 0.08f, 0.1f, 0.88f);
    dc->SetTarget(bersaglioL);
    dc->BeginDraw();
    dc->Clear(D2D1::ColorF(0, 0, 0, 0));
    disegnaFondo({0.5f, 0.5f, LW - 0.5f, LH - 0.5f}, scuro);

    // pulsante di accensione (sospendi, riavvia, arresta, blocca, disconnetti) a destra della ricerca
    {
        const D2D1_RECT_F re = rettEnergia();
        const float cx = (re.left + re.right) / 2, cy = (re.top + re.bottom) / 2, rr = (re.bottom - re.top) / 2;
        pennello->SetColor(scuro ? D2D1::ColorF(1, 1, 1, lSopraEnergia ? 0.18f : 0.10f) : D2D1::ColorF(1, 1, 1, lSopraEnergia ? 0.85f : 0.6f));
        dc->FillEllipse({{cx, cy}, rr, rr}, pennello);
        pennello->SetColor(ink);
        if (lModoGiochi) {
            linea(-6, 0, 6, 0, cx, cy, 2.0f);
            linea(0, -6, 0, 6, cx, cy, 2.0f);
        } else {
            simboloEnergia(cx, cy);
        }
    }
    // campo di ricerca
    const D2D1_RECT_F rc = {24 * dpi, 22 * dpi, rettEnergia().left - 10 * dpi, 62 * dpi};
    pennello->SetColor(scuro ? D2D1::ColorF(1, 1, 1, 0.10f) : D2D1::ColorF(1, 1, 1, 0.6f));
    dc->FillRoundedRectangle({rc, 10 * dpi, 10 * dpi}, pennello);
    pennello->SetColor({ink.r, ink.g, ink.b, 0.55f});
    const float lx = rc.left + 22 * dpi, ly = (rc.top + rc.bottom) / 2;
    dc->DrawEllipse({{lx - 2 * dpi, ly - 2 * dpi}, 6 * dpi, 6 * dpi}, pennello, 1.8f * dpi);
    linea(2.5f, 2.5f, 7, 7, lx, ly);
    const D2D1_RECT_F rt = {lx + 18 * dpi, rc.top, rc.right - 14 * dpi, rc.bottom};
    if (query.empty()) scrivi(lModoGiochi ? testi::t(L"Cerca un gioco", L"Search for a game") : testi::t(L"Cerca un'app", L"Search for an app"), fmtTitolo, rt);
    else {
        pennello->SetColor(ink);
        scrivi(query, fmtTitolo, rt);
        // cursore dopo il testo
        const float lt = larghezzaTesto(query, fmtTitolo);
        pennello->SetColor(D2D1::ColorF(0.2f, 0.5f, 1.0f, 0.9f));
        dc->DrawLine({rt.left + lt + 2 * dpi, ly - 9 * dpi}, {rt.left + lt + 2 * dpi, ly + 9 * dpi}, pennello, 1.6f * dpi);
    }

    // griglia delle app
    dc->PushAxisAlignedClip({0, lGrigliaTop() - 4 * dpi, LW, lGrigliaBottom()}, D2D1_ANTIALIAS_MODE_ALIASED);
    const float lato = 56 * dpi;
    auto& el = elencoAttivo();
    for (int i = 0; i < (int)filtrate.size(); i++) {
        const auto r = lCella(i);
        if (r.bottom < lGrigliaTop() - lCellaH() || r.top > lGrigliaBottom() + lCellaH()) continue;
        AppL& a = el[filtrate[i]];
        const bool sopra = i == lSopra, scelta = i == lSel && !query.empty();
        if (sopra || scelta) {
            pennello->SetColor(scuro ? D2D1::ColorF(1, 1, 1, sopra ? 0.12f : 0.08f) : D2D1::ColorF(1, 1, 1, sopra ? 0.55f : 0.4f));
            const D2D1_RECT_F h = {r.left + 4 * dpi, r.top + 2 * dpi, r.right - 4 * dpi, r.bottom - 2 * dpi};
            dc->FillRoundedRectangle({h, 8 * dpi, 8 * dpi}, pennello);
            if (scelta) { pennello->SetColor(D2D1::ColorF(0.2f, 0.5f, 1.0f, 0.7f)); dc->DrawRoundedRectangle({h, 8 * dpi, 8 * dpi}, pennello, 1.5f * dpi); }
        }
        const float cx = (r.left + r.right) / 2;
        const D2D1_RECT_F ri = {cx - lato / 2, r.top + 12 * dpi, cx + lato / 2, r.top + 12 * dpi + lato};
        if (a.icona) dc->DrawBitmap(a.icona, ri, 1.0f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
        else { pennello->SetColor(D2D1::ColorF(0.5f, 0.5f, 0.55f, 0.5f)); dc->FillRoundedRectangle({ri, lato * 0.22f, lato * 0.22f}, pennello); }
        pennello->SetColor(ink);
        scrivi(a.nome, fmtNome, {r.left + 6 * dpi, ri.bottom + 6 * dpi, r.right - 6 * dpi, ri.bottom + 24 * dpi});
    }
    if (filtrate.empty()) {
        pennello->SetColor({ink.r, ink.g, ink.b, 0.5f});
        scrivi(lModoGiochi ? testi::t(L"Nessun gioco trovato · clicca + o trascina qui un gioco", L"No games found · click + or drop a game here")
                           : testi::t(L"Nessuna app trovata", L"No apps found"),
               fmtNome, {0, lGrigliaTop() + 40 * dpi, LW, lGrigliaTop() + 70 * dpi});
    }
    dc->PopAxisAlignedClip();
    // barra di scorrimento sottile, solo se serve; col mouse sopra (o trascinandola) si allarga, come su macOS
    if (lScrollMax() > 0) {
        const D2D1_RECT_F b = lCursoreBarra();
        const bool larga = lSopraBarra || lTrascinoBarra;
        const float w = (larga ? 7.0f : 3.0f) * dpi, x1 = LW - 6 * dpi;
        pennello->SetColor({ink.r, ink.g, ink.b, larga ? 0.42f : 0.25f});
        dc->FillRoundedRectangle({{x1 - w, b.top, x1, b.bottom}, w / 2, w / 2}, pennello);
    }
    controllaGpu(dc->EndDraw());
    dc->SetTarget(bersaglio);
    lAnim = anim; lDirty = false;
    return true;
}

inline LRESULT CALLBACK Motore::procLauncher(HWND h, UINT m, WPARAM w, LPARAM l) {
    Motore* d = gIstanza();
    if (!d) return DefWindowProcW(h, m, w, l);
    const float x = (float)GET_X_LPARAM(l), y = (float)GET_Y_LPARAM(l);
    switch (m) {
        case WM_LBUTTONDOWN: {
            // barra di scorrimento: preso il cursore lo si trascina; un clic sul binario salta di una pagina
            const D2D1_RECT_F b = d->lRettBarra();
            if (d->lScrollMax() > 0 && x >= b.left && x < b.right && y >= b.top && y < b.bottom) {
                const D2D1_RECT_F c = d->lCursoreBarra();
                if (y < c.top || y >= c.bottom) {
                    const double pagina = (b.bottom - b.top) * 0.9;
                    d->lScrollObj = std::clamp(d->lScrollObj + (y < c.top ? -pagina : pagina), 0.0, d->lScrollMax());
                } else {
                    d->lTrascinoBarra = true; d->lPresaY = y; d->lPresaScroll = d->lScroll;
                    SetCapture(h);
                }
                d->lDirty = true;
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (!d->lMouseDentro) { TRACKMOUSEEVENT tm{sizeof(tm), TME_LEAVE, h, 0}; TrackMouseEvent(&tm); d->lMouseDentro = true; }
            if (d->lTrascinoBarra) {
                // la barra segue il mouse 1:1 (niente inseguimento: lScroll = lScrollObj)
                const double alt = d->lGrigliaBottom() - d->lGrigliaTop(), c = d->lCursoreBarra().bottom - d->lCursoreBarra().top;
                const double corsa = std::max(1.0, alt - c);
                d->lScrollObj = d->lScroll = std::clamp(d->lPresaScroll + (y - d->lPresaY) * d->lScrollMax() / corsa, 0.0, d->lScrollMax());
                d->lDirty = true;
                return 0;
            }
            {
                const D2D1_RECT_F b = d->lRettBarra();
                const bool sb = d->lScrollMax() > 0 && x >= b.left && x < b.right && y >= b.top && y < b.bottom;
                if (sb != d->lSopraBarra) { d->lSopraBarra = sb; d->lDirty = true; }
            }
            const int s = d->lCellaIn(x, y);
            if (s != d->lSopra) { d->lSopra = s; d->lDirty = true; }
            const D2D1_RECT_F re = d->rettEnergia();
            const bool e = x >= re.left && x < re.right && y >= re.top && y < re.bottom;
            if (e != d->lSopraEnergia) { d->lSopraEnergia = e; d->lDirty = true; }
            return 0;
        }
        case WM_MOUSELEAVE: d->lMouseDentro = false; d->lSopra = -1; d->lSopraEnergia = false; d->lSopraBarra = false; d->lDirty = true; return 0;
        case WM_LBUTTONUP: {
            if (d->lTrascinoBarra) { d->lTrascinoBarra = false; ReleaseCapture(); d->lDirty = true; return 0; }
            { const D2D1_RECT_F b = d->lRettBarra(); if (d->lScrollMax() > 0 && x >= b.left && x < b.right && y >= b.top && y < b.bottom) return 0; }
            if (y > d->LH) { d->chiudiLauncher(); return 0; }   // clic sotto (durante l'animazione): chiude
            if (d->lSopraEnergia) {
                if (d->lModoGiochi) { d->aggiungiGiocoDialogo(); return 0; }
                POINT p{(LONG)x, (LONG)y}; ClientToScreen(h, &p); d->menuEnergia(p); return 0;
            }
            const int i = d->lCellaIn(x, y);
            if (i >= 0) d->avviaApp(i);
            return 0;
        }
        case WM_RBUTTONUP: {   // il menu di Windows dell'app (amministratore, percorso, disinstalla...)
            const int i = d->lCellaIn(x, y);
            POINT p{(LONG)x, (LONG)y}; ClientToScreen(h, &p);
            if (d->lModoGiochi) {
                if (i >= 0) d->menuGioco(i, p);
                else if (y < d->LH) d->menuSpazioGiochi(p);
            } else if (i >= 0) {
                d->menuApp(i, p);
            }
            return 0;
        }
        case WM_DROPFILES: {
            HDROP hd = (HDROP)w;
            const UINT n = DragQueryFileW(hd, 0xFFFFFFFF, nullptr, 0);
            bool aggiunto = false;
            for (UINT i = 0; i < n; i++) {
                wchar_t p[MAX_PATH * 4] = L"";
                if (!DragQueryFileW(hd, i, p, MAX_PATH * 4)) continue;
                const std::wstring s = p;
                if (finisceCon(s, L".exe") || finisceCon(s, L".lnk") || finisceCon(s, L".url")) {
                    d->aggiungiGiocoFile(s);
                    aggiunto = true;
                }
            }
            DragFinish(hd);
            if (aggiunto) {
                if (!d->lModoGiochi) {
                    d->lModoGiochi = true;
                    float ax = d->W / 2.0f;
                    const auto pos = d->posizioniRiposo();
                    for (size_t i = 0; i < d->voci.size() && i < pos.size(); i++) if (d->voci[i].avvio == AVVIO_GIOCHI) ax = pos[i] + d->S / 2;
                    d->ancora = {(LONG)std::clamp(d->schermo.left + ax - d->rettLauncher.left, 40 * d->dpi, d->LW - 40 * d->dpi), d->ancora.y};
                    d->query.clear(); d->lSopra = -1; d->lScroll = d->lScrollObj = 0;
                    d->dirty = true;
                }
                d->leggiTuttiGiochi(true);
                prendiFuoco(h);
            }
            return 0;
        }
        // sottomenu del menu di Windows ("Invia a", "Apri con"...): li disegna lui
        case WM_INITMENUPOPUP: case WM_DRAWITEM: case WM_MEASUREITEM: case WM_MENUCHAR:
            if (d->menuShell3) { LRESULT r = 0; if (SUCCEEDED(d->menuShell3->HandleMenuMsg2(m, w, l, &r))) return r; }
            else if (d->menuShell2) { if (SUCCEEDED(d->menuShell2->HandleMenuMsg(m, w, l))) return m == WM_INITMENUPOPUP ? 0 : TRUE; }
            break;
        case WM_MOUSEWHEEL:
            d->lScrollObj = std::clamp(d->lScrollObj - GET_WHEEL_DELTA_WPARAM(w) / (double)WHEEL_DELTA * d->lCellaH() * 0.9, 0.0, d->lScrollMax());
            d->lDirty = true; return 0;
        case WM_CHAR:
            if (w == 27) d->chiudiLauncher();
            // Invio: con una ricerca la prima trovata (o quella scelta con le frecce); senza, quella scelta
            else if (w == 13) d->avviaApp(!d->query.empty() || d->lSopra == d->lSel ? d->lSel : -1);
            else if (w == 8) { if (!d->query.empty()) { d->query.pop_back(); d->filtra(); } }
            else if (w == 127 || (w == 8 && (GetKeyState(VK_CONTROL) & 0x8000))) {   // Ctrl+Backspace: via l'ultima parola
                while (!d->query.empty() && d->query.back() == L' ') d->query.pop_back();
                while (!d->query.empty() && d->query.back() != L' ') d->query.pop_back();
                d->filtra();
            }
            else if (w == 22) {   // Ctrl+V: incolla (solo la prima riga)
                if (OpenClipboard(h)) {
                    if (HANDLE g = GetClipboardData(CF_UNICODETEXT)) {
                        if (const wchar_t* t = (const wchar_t*)GlobalLock(g)) {
                            for (const wchar_t* c = t; *c && *c != L'\r' && *c != L'\n' && d->query.size() < 100; c++) if (*c >= 32) d->query += *c;
                            GlobalUnlock(g);
                        }
                    }
                    CloseClipboard();
                    d->filtra();
                }
            }
            else if (w >= 32) { d->query += (wchar_t)w; d->filtra(); }
            return 0;
        case WM_KEYDOWN: {
            int s = d->lSel;
            // pagine: tante righe quante se ne vedono
            const int righePagina = std::max(1, (int)((d->lGrigliaBottom() - d->lGrigliaTop()) / d->lCellaH()));
            if (w == VK_LEFT) s--; else if (w == VK_RIGHT) s++; else if (w == VK_UP) s -= 6; else if (w == VK_DOWN) s += 6;
            else if (w == VK_PRIOR) s -= 6 * righePagina; else if (w == VK_NEXT) s += 6 * righePagina;
            else if (w == VK_HOME) s = 0; else if (w == VK_END) s = (int)d->filtrate.size() - 1;
            else break;
            // senza ricerca la prima freccia mostra solo la scelta attuale; la scelta si vede come il passaggio del mouse
            if (d->query.empty() && d->lSopra != d->lSel) s = d->lSel;
            d->lSel = std::clamp(s, 0, std::max(0, (int)d->filtrate.size() - 1));
            if (d->query.empty() && !d->filtrate.empty()) d->lSopra = d->lSel;
            d->selezionaVisibile(); d->lDirty = true;
            return 0;
        }
        case WM_ACTIVATE:
            if (LOWORD(w) == WA_INACTIVE) {
                if (d->lDialogoAperto) return 0;
                // Appena aperto, il primo piano preso agganciandosi all'app di prima a volte torna a lei subito dopo
                // lo sgancio (come per la ricerca): non e' un clic altrove, si riprende una volta invece di chiudersi
                if (d->lAperto && !d->lChiusura && !d->lRipreso && adesso() - d->lT0 < 0.3 &&
                    d->primaDelLauncher && GetForegroundWindow() == d->primaDelLauncher) {
                    d->lRipreso = true;
                    d->scrivi("launcher: primo piano tornato all'app di prima, lo riprende");
                    PostMessageW(h, WM_APP + 3, 0, 0);
                    return 0;
                }
                // il fuoco va al dock perche' si e' cliccata l'icona App: la chiusura la fa quel clic (apriLauncher
                // fa da interruttore). Chiudendo qui, il clic subito dopo lo riaprirebbe
                if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) && d->mouseSuIconaApp()) return 0;
                d->chiudiLauncher();
            }
            return 0;
        case WM_APP + 3: if (d->lAperto && !d->lChiusura) prendiFuoco(h); return 0;
        case WM_CLOSE: d->chiudiLauncher(); return 0;   // Alt+F4: come Esc (la finestra non si distrugge, si riusa)
    }
    return DefWindowProcW(h, m, w, l);
}

}  // namespace dock
