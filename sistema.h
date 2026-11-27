// Funzioni di sistema per il centro di controllo, tutte con API normali (nessuna iniezione, niente amministratore):
//  - radio: Wi-Fi e Bluetooth accesi/spenti con Windows.Devices.Radios (la stessa API delle Impostazioni rapide);
//  - tema: modalita' chiara/scura (le due chiavi che cambia Impostazioni > Personalizzazione > Colori);
//  - uscite audio: elenco dei dispositivi e scelta di quello predefinito (IPolicyConfig, l'interfaccia che usa
//    il pannello Audio di Windows; non documentata ma stabile da Windows 7, usata da EarTrumpet e SoundSwitch);
//  - luminosita': DDC/CI per i monitor compatibili, WMI per i display esposti da Windows.
// Le operazioni lente (radio, DDC/CI) girano in un thread a parte: l'interfaccia legge solo valori gia' pronti.
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <inspectable.h>
#include <mmdeviceapi.h>
#include <propsys.h>
#include <physicalmonitorenumerationapi.h>
#include <highlevelmonitorconfigurationapi.h>
#include <wbemidl.h>
#include <wrl/client.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>

namespace sistema {

template <class F> static F metodoS(void* o, int i) { return (F)(*(void***)o)[i]; }
static void rilasciaS(void* o) { if (o) ((IUnknown*)o)->Release(); }

// attende un'operazione asincrona di WinRT (al massimo ms); true se e' completata
static bool attendi(void* op, int ms) {
    static const IID IID_Async = {0x00000036, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};   // IAsyncInfo
    void* info = nullptr; ((IUnknown*)op)->QueryInterface(IID_Async, &info);
    int stato = 0;
    for (int i = 0; i < ms && info; i++) {
        metodoS<HRESULT (STDMETHODCALLTYPE*)(void*, int*)>(info, 7)(info, &stato);
        if (stato != 0) break;
        Sleep(1);
    }
    rilasciaS(info);
    return stato == 1;
}

// ---------------------------------------------------------------------------------------------
class Sistema {
public:
    // -1 = non c'e', 0 = spento, 1 = acceso
    std::atomic<int> wifi{-1}, bluetooth{-1};
    std::atomic<int> luminosita{-1};   // 0..100, -1 = non regolabile
    std::atomic<bool> pronto{false};   // c'e' qualcosa di nuovo da ridisegnare

    // Lo schermo del dock (il centro di controllo regola la luminosita' di quello su cui sta): un punto al suo centro,
    // x nei 32 bit alti e y nei bassi; scritto dal thread principale, letto da quello della luminosita'
    std::atomic<long long> centroSchermo{0};
    void schermo(const RECT& r) {
        const auto c = (long long)((uint64_t)(unsigned)((r.left + r.right) / 2) << 32 |
                                   (unsigned)((r.top + r.bottom) / 2));
        if (centroSchermo.exchange(c) != c) { luminosita = -1; if (evento) rileggi(); }
    }

    void avvia() {
        if (thread) return;
        evento = CreateEventW(nullptr, FALSE, TRUE, nullptr);   // parte subito con una lettura
        thread = CreateThread(nullptr, 0, lavoro, this, 0, nullptr);
    }

    void rileggi() { SetEvent(evento); }
    // (una richiesta per radio: prima Wi-Fi e Bluetooth avevano un posto solo, e il secondo clic cancellava il primo,
    // R20261121-001738-S6)
    void impostaRadio(int tipo /* 1 Wi-Fi, 3 Bluetooth */, bool acceso) {
        (tipo == 1 ? wifi : bluetooth) = acceso ? 1 : 0;   // subito nell'interfaccia; il thread la rende vera
        (tipo == 1 ? richiestaWifi : richiestaBt) = acceso ? 1 : 0;
        SetEvent(evento);
    }
    // (lo schermo e' quello del momento del gesto: prima lo sceglieva il thread piu' tardi, e se intanto il dock era
    // passato su un altro schermo la luminosita' finiva li', S12)
    // valore e schermo si prendono insieme: prima due atomiche separate, e un gesto su un altro schermo durante il
    // lavoro faceva scrivere il valore del primo sul secondo (R20261121-120053 su S12)
    void impostaLuminosita(int v) { luminosita = v; { std::lock_guard<std::mutex> g(mLum); lum = {v, centroSchermo.load()}; } SetEvent(evento); }

    static bool risparmioEnergia() {
        SYSTEM_POWER_STATUS stato{};
        return GetSystemPowerStatus(&stato) && stato.SystemStatusFlag == 1;
    }

    static bool cambiaRisparmioEnergia(bool attiva) {
        // Comando di override usato da Windows per il Risparmio energia. Lo stato arriva
        // separatamente via GUID_ENERGY_SAVER_STATUS e la soglia automatica non viene scritta.
        using Pubblica = LONG (NTAPI*)(ULONGLONG, const void*, const void*, ULONG, const void*);
        static const auto pubblica = reinterpret_cast<Pubblica>(
            GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlPublishWnfStateData"));
        constexpr ULONGLONG overrideRisparmio = 0x41C6013DA3BC3075ULL;
        const DWORD comando = attiva ? 1 : 2;
        return pubblica && pubblica(overrideRisparmio, nullptr, &comando, sizeof(comando), nullptr) >= 0;
    }

    // ---- tema ----
    static bool temaScuro() {
        DWORD v = 1, n = sizeof(v);
        RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &n);
        return v == 0;
    }
    static void impostaTema(bool scuro) {
        const DWORD chiaro = scuro ? 0 : 1;
        const wchar_t* k = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
        RegSetKeyValueW(HKEY_CURRENT_USER, k, L"AppsUseLightTheme", REG_DWORD, &chiaro, sizeof(chiaro));
        RegSetKeyValueW(HKEY_CURRENT_USER, k, L"SystemUsesLightTheme", REG_DWORD, &chiaro, sizeof(chiaro));
        // l'avviso a tutte le finestre (come fa Impostazioni) in un thread a parte: aspetta le app una per una e
        // bloccherebbe FluidWin (e l'animazione del sole che diventa luna) anche per un secondo
        CloseHandle(CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
            DWORD_PTR r;
            SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"ImmersiveColorSet", SMTO_ABORTIFHUNG, 200, &r);
            return 0;
        }, nullptr, 0, nullptr));
    }

    // ---- uscite audio ----
    struct Uscita { std::wstring id, nome; bool predefinita = false; };
    static std::vector<Uscita> uscite() {
        std::vector<Uscita> out;
        static const PROPERTYKEY NOME = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};
        IMMDeviceEnumerator* en = nullptr;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&en))) return out;
        std::wstring pred;
        IMMDevice* d = nullptr;
        if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &d))) { LPWSTR id = nullptr; d->GetId(&id); if (id) pred = id; CoTaskMemFree(id); d->Release(); }
        IMMDeviceCollection* col = nullptr;
        if (SUCCEEDED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col))) {
            UINT n = 0; col->GetCount(&n);
            for (UINT i = 0; i < n; i++) {
                IMMDevice* x = nullptr; if (FAILED(col->Item(i, &x))) continue;
                Uscita u; LPWSTR id = nullptr; x->GetId(&id); if (id) u.id = id; CoTaskMemFree(id);
                IPropertyStore* ps = nullptr;
                if (SUCCEEDED(x->OpenPropertyStore(STGM_READ, &ps))) {
                    PROPVARIANT v; PropVariantInit(&v);
                    if (SUCCEEDED(ps->GetValue(NOME, &v)) && v.vt == VT_LPWSTR && v.pwszVal) u.nome = v.pwszVal;
                    PropVariantClear(&v); ps->Release();
                }
                u.predefinita = u.id == pred;
                if (!u.nome.empty()) out.push_back(u);
                x->Release();
            }
            col->Release();
        }
        en->Release();
        return out;
    }
    static bool impostaUscita(const std::wstring& id) {
        static const CLSID CLSID_Policy = {0x870af99c, 0x171d, 0x4f9e, {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9}};
        static const IID IID_Policy = {0xf8679f50, 0x850a, 0x41cf, {0x9c, 0x72, 0x43, 0x0f, 0x29, 0x02, 0x90, 0xc8}};
        void* pc = nullptr;
        if (FAILED(CoCreateInstance(CLSID_Policy, nullptr, CLSCTX_ALL, IID_Policy, &pc)) || !pc) return false;
        bool ok = true;
        for (int ruolo = 0; ruolo < 3; ruolo++)   // console, multimedia, comunicazioni
            ok &= SUCCEEDED(metodoS<HRESULT (STDMETHODCALLTYPE*)(void*, LPCWSTR, int)>(pc, 13)(pc, id.c_str(), ruolo));   // SetDefaultEndpoint
        rilasciaS(pc);
        return ok;
    }

private:
    HANDLE thread = nullptr, evento = nullptr;
    std::atomic<int> richiestaWifi{-1}, richiestaBt{-1};
    struct Lum { int v = -1; long long schermo = 0; };
    std::mutex mLum; Lum lum;   // richiesta di luminosita' (v -1 = nessuna)

    static DWORD WINAPI lavoro(LPVOID p) {
        Sistema* s = (Sistema*)p;
        RoInitialize(RO_INIT_MULTITHREADED);
        for (;;) {
            if (WaitForSingleObject(s->evento, INFINITE) != WAIT_OBJECT_0) break;
            const int rw = s->richiestaWifi.exchange(-1), rb = s->richiestaBt.exchange(-1);
            Lum rl; { std::lock_guard<std::mutex> g(s->mLum); rl = s->lum; s->lum = Lum{}; }
            s->radio(rw, rb);
            s->monitor(rl.v, rl.v >= 0 ? rl.schermo : s->centroSchermo.load());
            s->pronto = true;
        }
        return 0;
    }

    // legge (e imposta, se le richieste sono 0 o 1: spenta o accesa) le radio Wi-Fi e Bluetooth
    void radio(int vuoleWifi, int vuoleBt) {
        HSTRING cl = nullptr; const wchar_t* n = L"Windows.Devices.Radios.Radio";
        WindowsCreateString(n, (UINT32)wcslen(n), &cl);
        IInspectable* fab = nullptr;
        RoGetActivationFactory(cl, __uuidof(IInspectable), (void**)&fab);
        WindowsDeleteString(cl);
        if (!fab) return;
        static const IID IID_Fabbrica = {0x00000035, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
        ULONG c = 0; IID* ids = nullptr; fab->GetIids(&c, &ids);
        void* st = nullptr;
        for (ULONG i = 0; i < c && !st; i++) if (!IsEqualIID(ids[i], IID_Fabbrica)) fab->QueryInterface(ids[i], &st);
        CoTaskMemFree(ids); fab->Release();
        if (!st) return;
        void* op = nullptr;
        metodoS<HRESULT (STDMETHODCALLTYPE*)(void*, void**)>(st, 6)(st, &op);   // GetRadiosAsync
        rilasciaS(st);
        if (!op) return;
        void* vista = nullptr;
        if (attendi(op, 3000)) metodoS<HRESULT (STDMETHODCALLTYPE*)(void*, void**)>(op, 8)(op, &vista);
        rilasciaS(op);
        if (!vista) return;
        int w = -1, b = -1;
        UINT32 k = 0; metodoS<HRESULT (STDMETHODCALLTYPE*)(void*, UINT32*)>(vista, 7)(vista, &k);
        for (UINT32 i = 0; i < k; i++) {
            void* r = nullptr;
            metodoS<HRESULT (STDMETHODCALLTYPE*)(void*, UINT32, void**)>(vista, 6)(vista, i, &r);
            if (!r) continue;
            int stato = 0, genere = 0;   // RadioState: 1 acceso, 2 spento, 3 disattivato; RadioKind: 1 Wi-Fi, 3 Bluetooth
            metodoS<HRESULT (STDMETHODCALLTYPE*)(void*, int*)>(r, 11)(r, &genere);
            const int vuole = genere == 1 ? vuoleWifi : genere == 3 ? vuoleBt : -1;
            if (vuole >= 0) {
                void* o = nullptr;
                metodoS<HRESULT (STDMETHODCALLTYPE*)(void*, int, void**)>(r, 6)(r, vuole ? 1 : 2, &o);   // SetStateAsync
                if (o) { attendi(o, 3000); rilasciaS(o); }
            }
            metodoS<HRESULT (STDMETHODCALLTYPE*)(void*, int*)>(r, 9)(r, &stato);
            const int v = stato == 1 ? 1 : 0;
            if (genere == 1) w = std::max(w, v);
            if (genere == 3) b = std::max(b, v);
            rilasciaS(r);
        }
        rilasciaS(vista);
        wifi = w; bluetooth = b;
    }

    int monitorDDC(HMONITOR mon, int v) {
        DWORD n = 0;
        if (!GetNumberOfPhysicalMonitorsFromHMONITOR(mon, &n) || !n) return -1;
        std::vector<PHYSICAL_MONITOR> pm(n);
        if (!GetPhysicalMonitorsFromHMONITOR(mon, n, pm.data())) return -1;
        int esito = -1;
        for (const auto& p : pm) {
            DWORD mi = 0, cur = 0, ma = 0;
            if (!GetMonitorBrightness(p.hPhysicalMonitor, &mi, &cur, &ma) || ma <= mi) continue;
            const DWORD obiettivo = mi + (ma - mi) * (DWORD)std::max(v, 0) / 100;
            if (v >= 0 && SetMonitorBrightness(p.hPhysicalMonitor, obiettivo)) cur = obiettivo;
            esito = (int)((std::clamp(cur, mi, ma) - mi) * 100 / (ma - mi));
            break;
        }
        DestroyPhysicalMonitors(n, pm.data());
        return esito;
    }

    using Bstr = std::unique_ptr<OLECHAR, decltype(&SysFreeString)>;
    static Bstr testoWmi(const wchar_t* s) { return Bstr(SysAllocString(s), SysFreeString); }
    Microsoft::WRL::ComPtr<IWbemServices> wmi;

    bool collegaWmi() {
        if (wmi) return true;
        Microsoft::WRL::ComPtr<IWbemLocator> locator;
        if (FAILED(CoCreateInstance(__uuidof(WbemLocator), nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(locator.GetAddressOf())))) return false;
        const auto spazio = testoWmi(L"ROOT\\WMI");
        if (FAILED(locator->ConnectServer(spazio.get(), nullptr, nullptr, nullptr,
                                          WBEM_FLAG_CONNECT_USE_MAX_WAIT, nullptr, nullptr, &wmi))) return false;
        if (FAILED(CoSetProxyBlanket(wmi.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                                     RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                                     nullptr, EOAC_NONE))) wmi.Reset();
        return bool(wmi);
    }

    // Il percorso Win32 usa # al posto delle barre. Il provider WMI aggiunge _0 (o un altro indice).
    static std::vector<std::wstring> istanzeMonitor(HMONITOR mon) {
        MONITORINFOEXW info{}; info.cbSize = sizeof(info);
        std::vector<std::wstring> nomi;
        if (!GetMonitorInfoW(mon, &info)) return nomi;
        for (DWORD i = 0; ; ++i) {
            DISPLAY_DEVICEW d{}; d.cb = sizeof(d);
            if (!EnumDisplayDevicesW(info.szDevice, i, &d, EDD_GET_DEVICE_INTERFACE_NAME)) break;
            if (!(d.StateFlags & DISPLAY_DEVICE_ACTIVE)) continue;
            std::wstring nome(d.DeviceID);
            const auto guida = nome.rfind(L"#{");
            if (nome.rfind(L"\\\\?\\", 0) != 0 || guida == std::wstring::npos || guida <= 4) continue;
            nome = nome.substr(4, guida - 4);
            std::replace(nome.begin(), nome.end(), L'#', L'\\');
            nomi.push_back(nome + L"_");
        }
        return nomi;
    }

    bool scriviWmi(const std::wstring& nome, int v) {
        using Microsoft::WRL::ComPtr;
        std::wstring percorso = L"WmiMonitorBrightnessMethods.InstanceName=\"";
        for (wchar_t c : nome) { if (c == L'\\' || c == L'"') percorso += L'\\'; percorso += c; }
        percorso += L'"';
        const auto classe = testoWmi(L"WmiMonitorBrightnessMethods");
        const auto istanza = testoWmi(percorso.c_str());
        const auto metodo = testoWmi(L"WmiSetBrightness");
        ComPtr<IWbemClassObject> definizione, parametri, ingresso;
        if (FAILED(wmi->GetObject(classe.get(), 0, nullptr, &definizione, nullptr)) ||
            FAILED(definizione->GetMethod(metodo.get(), 0, &parametri, nullptr)) ||
            FAILED(parametri->SpawnInstance(0, &ingresso))) return false;
        VARIANT valore{};
        valore.vt = VT_I4; valore.lVal = 0;
        if (FAILED(ingresso->Put(L"Timeout", 0, &valore, 0))) return false;
        valore.vt = VT_UI1; valore.bVal = (BYTE)v;
        if (FAILED(ingresso->Put(L"Brightness", 0, &valore, 0))) return false;
        return SUCCEEDED(wmi->ExecMethod(istanza.get(), metodo.get(), 0, nullptr, ingresso.Get(), nullptr, nullptr));
    }

    int monitorWmi(HMONITOR mon, int v) {
        using Microsoft::WRL::ComPtr;
        const auto nomi = istanzeMonitor(mon);
        if (nomi.empty() || !collegaWmi()) return -1;
        const auto classe = testoWmi(L"WmiMonitorBrightness");
        ComPtr<IEnumWbemClassObject> elenco;
        if (FAILED(wmi->CreateInstanceEnum(classe.get(), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                           nullptr, &elenco))) { wmi.Reset(); return -1; }
        for (;;) {
            ComPtr<IWbemClassObject> riga;
            ULONG lette = 0;
            if (FAILED(elenco->Next(1000, 1, &riga, &lette)) || !lette) break;
            VARIANT attivo{}, nome{}, valore{};
            riga->Get(L"Active", 0, &attivo, nullptr, nullptr);
            riga->Get(L"InstanceName", 0, &nome, nullptr, nullptr);
            riga->Get(L"CurrentBrightness", 0, &valore, nullptr, nullptr);
            int corrente = -1;
            std::wstring istanza;
            if (attivo.vt == VT_BOOL && attivo.boolVal && nome.vt == VT_BSTR && nome.bstrVal &&
                std::any_of(nomi.begin(), nomi.end(), [&](const auto& n) { return !_wcsnicmp(nome.bstrVal, n.c_str(), n.size()); })) {
                corrente = valore.vt == VT_UI1 ? valore.bVal : valore.vt == VT_I4 ? valore.lVal : -1;
                istanza = nome.bstrVal;
            }
            VariantClear(&attivo); VariantClear(&nome); VariantClear(&valore);
            if (corrente >= 0 && corrente <= 100) return v >= 0 && scriviWmi(istanza, v) ? v : corrente;
        }
        return -1;
    }

    void monitor(int v, long long c) {
        HMONITOR mon = MonitorFromPoint({(LONG)(c >> 32), (LONG)c}, MONITOR_DEFAULTTONULL);
        const int ddc = mon ? monitorDDC(mon, v) : -1;
        const int risultato = ddc >= 0 || !mon ? ddc : monitorWmi(mon, v);
        std::lock_guard<std::mutex> g(mLum);
        if (c == centroSchermo.load() && (lum.v < 0 || lum.schermo != c)) luminosita = risultato;
    }
};

}  // namespace sistema
