// Monitoraggio delle periferiche Attack Shark tramite HID (mouse wireless Beken e tastiera ROYUAN).
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <setupapi.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace attackshark {

struct Dispositivo {
    std::wstring id, nome, tipo, connessione;
    int batteria = -1;
    bool inCarica = false;
    bool online = false;
    bool attivo = false;
};

struct Riepilogo {
    bool connesso = false;
    int batteriaMin = -1;
    bool alcunoInCarica = false;
    std::vector<Dispositivo> dispositivi;
};

inline void apriSoftware() {
    static constexpr const wchar_t* CANDIDATI[] = {
        L"C:\\Program Files (x86)\\Attack Shark Software\\AttackShark.exe",
        L"C:\\Program Files\\Attack Shark Software\\AttackShark.exe"
    };
    for (const wchar_t* p : CANDIDATI) {
        const DWORD attr = GetFileAttributesW(p);
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            ShellExecuteW(nullptr, L"open", p, nullptr, nullptr, SW_SHOWNORMAL);
            return;
        }
    }
    ShellExecuteW(nullptr, L"open", L"https://attackshark.com", nullptr, nullptr, SW_SHOWNORMAL);
}

namespace dettagli {

static constexpr uint16_t VID_BEKEN = 0x1D57;
static constexpr uint16_t PID_ASHARK_MOUSE = 0xFA60;
static constexpr uint16_t VID_ROYUAN = 0x3151;
static constexpr uint16_t PID_RK86_WIRED = 0x4015;
static constexpr uint16_t PID_RK86_24G_A = 0x4011;
static constexpr uint16_t PID_RK86_24G_B = 0x4010;
static constexpr LONG HIDP_OK = 0x00110000L;
static constexpr ULONGLONG MOUSE_IDLE_MS = 30000;
static constexpr ULONGLONG ENUM_INTERVAL_MS = 8000;
static constexpr DWORD KB_POLL_TIMEOUT_MS = 350;

#pragma pack(push, 4)
struct HidAttributes {
    ULONG Size;
    USHORT VendorID;
    USHORT ProductID;
    USHORT VersionNumber;
};

struct HidCaps {
    USHORT Usage;
    USHORT UsagePage;
    USHORT InputReportByteLength;
    USHORT OutputReportByteLength;
    USHORT FeatureReportByteLength;
    USHORT Reserved[17];
    USHORT NumberLinkCollectionNodes;
    USHORT NumberInputButtonCaps;
    USHORT NumberInputValueCaps;
    USHORT NumberInputDataIndices;
    USHORT NumberOutputButtonCaps;
    USHORT NumberOutputValueCaps;
    USHORT NumberOutputDataIndices;
    USHORT NumberFeatureButtonCaps;
    USHORT NumberFeatureValueCaps;
    USHORT NumberFeatureDataIndices;
};
#pragma pack(pop)

template <typename Fn>
inline bool caricaProc(HMODULE dll, const char* nome, Fn& out) {
    FARPROC p = GetProcAddress(dll, nome);
    if (!p) return false;
    std::memcpy(&out, &p, sizeof(out));
    return true;
}

struct HidApi {
    using FnGetHidGuid = void (WINAPI*)(GUID*);
    using FnGetAttributes = BOOLEAN (WINAPI*)(HANDLE, HidAttributes*);
    using FnGetPreparsedData = BOOLEAN (WINAPI*)(HANDLE, void**);
    using FnFreePreparsedData = BOOLEAN (WINAPI*)(void*);
    using FnSetFeature = BOOLEAN (WINAPI*)(HANDLE, PVOID, ULONG);
    using FnGetFeature = BOOLEAN (WINAPI*)(HANDLE, PVOID, ULONG);
    using FnGetCaps = LONG (WINAPI*)(void*, HidCaps*);

    HMODULE dll = nullptr;
    FnGetHidGuid getHidGuid = nullptr;
    FnGetAttributes getAttributes = nullptr;
    FnGetPreparsedData getPreparsedData = nullptr;
    FnFreePreparsedData freePreparsedData = nullptr;
    FnSetFeature setFeature = nullptr;
    FnGetFeature getFeature = nullptr;
    FnGetCaps getCaps = nullptr;

    ~HidApi() { scarica(); }

    bool carica() {
        if (dll) return true;
        dll = LoadLibraryW(L"hid.dll");
        if (!dll) return false;
        const bool ok =
            caricaProc(dll, "HidD_GetHidGuid", getHidGuid) &&
            caricaProc(dll, "HidD_GetAttributes", getAttributes) &&
            caricaProc(dll, "HidD_GetPreparsedData", getPreparsedData) &&
            caricaProc(dll, "HidD_FreePreparsedData", freePreparsedData) &&
            caricaProc(dll, "HidD_SetFeature", setFeature) &&
            caricaProc(dll, "HidD_GetFeature", getFeature) &&
            caricaProc(dll, "HidP_GetCaps", getCaps);
        if (!ok) scarica();
        return ok;
    }

    void scarica() {
        if (dll) {
            FreeLibrary(dll);
            dll = nullptr;
        }
        getHidGuid = nullptr;
        getAttributes = nullptr;
        getPreparsedData = nullptr;
        freePreparsedData = nullptr;
        setFeature = nullptr;
        getFeature = nullptr;
        getCaps = nullptr;
    }
};

inline std::wstring minuscolo(std::wstring s) {
    for (wchar_t& c : s) c = (wchar_t)towlower((wint_t)c);
    return s;
}

inline bool ePidTastiera(uint16_t pid) {
    return pid == PID_RK86_WIRED || pid == PID_RK86_24G_A || pid == PID_RK86_24G_B;
}

inline int prioritaStato(const Dispositivo& d) {
    if (!d.online) return 2;
    return d.attivo ? 0 : 1;
}

inline bool ugualeDispositivo(const Dispositivo& a, const Dispositivo& b) {
    return a.id == b.id && a.nome == b.nome && a.tipo == b.tipo &&
           a.connessione == b.connessione && a.batteria == b.batteria &&
           a.inCarica == b.inCarica && a.online == b.online && a.attivo == b.attivo;
}

inline bool ugualeRiepilogo(const Riepilogo& a, const Riepilogo& b) {
    if (a.connesso != b.connesso || a.batteriaMin != b.batteriaMin ||
        a.alcunoInCarica != b.alcunoInCarica || a.dispositivi.size() != b.dispositivi.size()) {
        return false;
    }
    for (size_t i = 0; i < a.dispositivi.size(); i++) {
        if (!ugualeDispositivo(a.dispositivi[i], b.dispositivi[i])) return false;
    }
    return true;
}

inline HANDLE apriHid(const std::wstring& path, DWORD access, bool overlapped) {
    const DWORD flags = overlapped ? FILE_FLAG_OVERLAPPED : 0;
    return CreateFileW(path.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE,
                       nullptr, OPEN_EXISTING, flags, nullptr);
}

inline HANDLE apriHidConRipiego(const std::wstring& path, bool serveLettura, bool overlapped) {
    HANDLE h = apriHid(path, GENERIC_READ | GENERIC_WRITE, overlapped);
    if (h != INVALID_HANDLE_VALUE) return h;
    if (serveLettura) {
        h = apriHid(path, GENERIC_READ, overlapped);
        if (h != INVALID_HANDLE_VALUE) return h;
    } else {
        h = apriHid(path, 0, overlapped);
        if (h != INVALID_HANDLE_VALUE) return h;
    }
    return INVALID_HANDLE_VALUE;
}

inline bool leggiInfoHid(const HidApi& hid, const std::wstring& path, uint16_t& vid, uint16_t& pid, HidCaps& caps) {
    HANDLE h = apriHid(path, 0, false);
    if (h == INVALID_HANDLE_VALUE) return false;
    HidAttributes attr{};
    attr.Size = sizeof(attr);
    bool ok = hid.getAttributes(h, &attr) != FALSE;
    void* ppd = nullptr;
    if (ok && hid.getPreparsedData(h, &ppd) && ppd) {
        ok = (hid.getCaps(ppd, &caps) == HIDP_OK);
        hid.freePreparsedData(ppd);
    } else {
        ok = false;
    }
    CloseHandle(h);
    if (ok) {
        vid = attr.VendorID;
        pid = attr.ProductID;
    }
    return ok;
}

struct TrovatiHid {
    bool haMouse = false;
    std::wstring mousePath;
    USHORT mouseInLen = 0;

    struct TastieraCol {
        uint16_t pid = 0;
        std::wstring featPath;
        USHORT featLen = 0;
        std::wstring inPath;
        USHORT inLen = 0;
    };
    std::vector<TastieraCol> tastiere;
};

inline TrovatiHid enumeraDispositivi(const HidApi& hid) {
    TrovatiHid res;
    GUID hidGuid{};
    hid.getHidGuid(&hidGuid);
    HDEVINFO devs = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devs == INVALID_HANDLE_VALUE) return res;

    SP_DEVICE_INTERFACE_DATA ifData{};
    ifData.cbSize = sizeof(ifData);
    std::map<uint16_t, TrovatiHid::TastieraCol> kbPerPid;

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devs, nullptr, &hidGuid, i, &ifData); i++) {
        DWORD reqSize = 0;
        SetupDiGetDeviceInterfaceDetailW(devs, &ifData, nullptr, 0, &reqSize, nullptr);
        if (reqSize == 0) continue;

        std::vector<BYTE> buf(reqSize, 0);
        auto* detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)buf.data();
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(devs, &ifData, detail, reqSize, nullptr, nullptr)) continue;

        const std::wstring path = detail->DevicePath;
        const std::wstring low = minuscolo(path);
        const bool candMouse = low.find(L"vid_1d57") != std::wstring::npos;
        const bool candKb = low.find(L"vid_3151") != std::wstring::npos;
        if (!candMouse && !candKb) continue;

        uint16_t vid = 0, pid = 0;
        HidCaps caps{};
        if (!leggiInfoHid(hid, path, vid, pid, caps)) continue;

        if (vid == VID_BEKEN && pid == PID_ASHARK_MOUSE &&
            caps.UsagePage == 0x000A && caps.InputReportByteLength >= 5 && !res.haMouse) {
            res.haMouse = true;
            res.mousePath = path;
            res.mouseInLen = caps.InputReportByteLength;
        } else if (vid == VID_ROYUAN && ePidTastiera(pid)) {
            auto& entry = kbPerPid[pid];
            entry.pid = pid;
            if (caps.UsagePage == 0xFFFF && caps.Usage == 0x0002 &&
                caps.FeatureReportByteLength >= 64 && entry.featPath.empty()) {
                entry.featPath = path;
                entry.featLen = caps.FeatureReportByteLength;
            } else if (caps.UsagePage == 0xFFFF && caps.Usage == 0x0001 &&
                       caps.InputReportByteLength >= 4 && entry.inPath.empty()) {
                entry.inPath = path;
                entry.inLen = caps.InputReportByteLength;
            }
        }
    }

    SetupDiDestroyDeviceInfoList(devs);
    for (auto& [pid, entry] : kbPerPid) {
        if (!entry.featPath.empty() && !entry.inPath.empty()) {
            res.tastiere.push_back(std::move(entry));
        }
    }
    return res;
}

struct StatoTastieraCache {
    bool nomeLetto = false;
    std::wstring nome = L"Attack Shark RK86";
    int batteria = -1;
};

// For wired USB (3151:4015): 100% passive, zero HID writes.
// For 2.4G wireless dongles (3151:4011 / 4010): safe read-only 0xF7 status
// poll via SetFeature/GetFeature (never send 0x05, which resets RGB on wired).
inline bool interrogaTastiera(const HidApi& hid, const TrovatiHid::TastieraCol& col,
                              StatoTastieraCache& cache, HANDLE, Dispositivo& out) {
    wchar_t idBuf[24];
    swprintf(idBuf, 24, L"3151:%04X", (unsigned)col.pid);
    out = Dispositivo{};
    out.id = idBuf;
    out.nome = cache.nome;
    out.tipo = L"Keyboard";
    if (col.pid == PID_RK86_WIRED) {
        out.connessione = L"USB";
        out.batteria = -1;
        out.inCarica = false;
        out.online = true;
        out.attivo = true;
        return true;
    }

    out.connessione = L"2.4G Wireless";
    out.batteria = cache.batteria;
    HANDLE hFeat = apriHidConRipiego(col.featPath, false, false);
    if (hFeat == INVALID_HANDLE_VALUE) {
        out.online = false;
        out.attivo = false;
        return true;
    }

    const ULONG len = std::max<ULONG>(64, col.featLen);
    std::vector<BYTE> feat(len, 0);
    feat[0] = 0x00;
    feat[1] = 0xF7;
    if (hid.setFeature(hFeat, feat.data(), len)) {
        Sleep(50);
        std::vector<BYTE> resp(len, 0);
        resp[0] = 0x00;
        if (hid.getFeature(hFeat, resp.data(), len) && resp.size() >= 8) {
            const int bat = (int)resp[2];
            const bool linkOk = (resp[5] != 0 || resp[6] != 0 || resp[7] != 0);
            if (bat >= 1 && bat <= 100) {
                out.batteria = bat;
                cache.batteria = bat;
                out.inCarica = (resp[3] == 1 || resp[4] == 1);
                out.online = true;
                out.attivo = linkOk || true;
            } else {
                out.online = linkOk;
                out.attivo = linkOk;
            }
        }
    }
    CloseHandle(hFeat);
    return true;
}

}  // namespace dettagli

class Monitor {
public:
    std::atomic<uint64_t> versione{0};

    ~Monitor() { ferma(); }

    void avvia(HWND hwndNotifica, UINT msgNotifica) {
        if (threadH) return;
        notificaHwnd = hwndNotifica;
        notificaMsg = msgNotifica;
        stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!stopEvent || !wakeEvent) {
            if (stopEvent) { CloseHandle(stopEvent); stopEvent = nullptr; }
            if (wakeEvent) { CloseHandle(wakeEvent); wakeEvent = nullptr; }
            return;
        }
        threadH = CreateThread(nullptr, 0, threadProc, this, 0, nullptr);
        if (!threadH) {
            CloseHandle(stopEvent); stopEvent = nullptr;
            CloseHandle(wakeEvent); wakeEvent = nullptr;
        }
    }

    bool ferma(DWORD ms = 2000) {
        if (!threadH) {
            if (stopEvent) { CloseHandle(stopEvent); stopEvent = nullptr; }
            if (wakeEvent) { CloseHandle(wakeEvent); wakeEvent = nullptr; }
            return true;
        }
        SetEvent(stopEvent);
        const DWORD w = WaitForSingleObject(threadH, ms);
        const bool ok = (w == WAIT_OBJECT_0);
        if (ok) {
            CloseHandle(threadH);
            threadH = nullptr;
            CloseHandle(stopEvent); stopEvent = nullptr;
            CloseHandle(wakeEvent); wakeEvent = nullptr;
        }
        return ok;
    }

    Riepilogo copia() {
        AcquireSRWLockShared(&lock);
        Riepilogo r = stato;
        ReleaseSRWLockShared(&lock);
        return r;
    }

    void aggiornaOra() {
        if (wakeEvent) SetEvent(wakeEvent);
    }

private:
    SRWLOCK lock = SRWLOCK_INIT;
    Riepilogo stato;
    HANDLE stopEvent = nullptr;
    HANDLE wakeEvent = nullptr;
    HANDLE threadH = nullptr;
    HWND notificaHwnd = nullptr;
    UINT notificaMsg = 0;

    void pubblica(Riepilogo nuovo) {
        if (stopEvent && WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0) return;

        bool cambiato = false;
        AcquireSRWLockExclusive(&lock);
        if (!dettagli::ugualeRiepilogo(stato, nuovo)) {
            stato = std::move(nuovo);
            cambiato = true;
        }
        ReleaseSRWLockExclusive(&lock);

        if (cambiato) {
            versione.fetch_add(1, std::memory_order_relaxed);
            if (notificaHwnd && notificaMsg) PostMessageW(notificaHwnd, notificaMsg, 0, 0);
        }
    }

    struct StatoMouse {
        bool connesso = false;
        std::wstring path;
        USHORT inLen = 0;
        HANDLE handle = INVALID_HANDLE_VALUE;
        OVERLAPPED ov{};
        bool readPending = false;
        std::vector<BYTE> buf;
        std::wstring nome = L"Attack Shark Mouse";
        int batteria = -1;
        bool inCarica = false;
        bool online = false;
        bool attivo = false;
        ULONGLONG ultimoPacchetto = 0;

        void annullaLettura() {
            if (readPending && handle != INVALID_HANDLE_VALUE) {
                CancelIoEx(handle, &ov);
                DWORD n = 0;
                GetOverlappedResult(handle, &ov, &n, TRUE);
                readPending = false;
            }
        }

        void chiudi() {
            annullaLettura();
            if (handle != INVALID_HANDLE_VALUE) {
                CloseHandle(handle);
                handle = INVALID_HANDLE_VALUE;
            }
            connesso = false;
            path.clear();
            inLen = 0;
            buf.clear();
            nome = L"Attack Shark Mouse";
            batteria = -1;
            inCarica = false;
            online = false;
            attivo = false;
            ultimoPacchetto = 0;
        }

        void decodifica(const BYTE* p, DWORD len) {
            if (len < 5 || p[0] != 0x03) return;
            nome = (p[1] == 0x24) ? L"Attack Shark V6" : L"Attack Shark Mouse";
            const BYTE stateByte = p[2];
            const BYTE powerByte = p[3];
            online = (powerByte != 0);
            const bool charging = ((powerByte & 0x02) != 0) ||
                                  stateByte == 0x42 || stateByte == 0x43 ||
                                  stateByte == 0x02 || stateByte == 0x03;
            if (p[4] >= 1 && p[4] <= 100) {
                batteria = (int)p[4];
            } else if (p[4] == 0 && stateByte >= 1 && stateByte <= 10) {
                batteria = (int)stateByte * 10;
            }
            inCarica = online && charging;
            attivo = online;
            ultimoPacchetto = GetTickCount64();
        }
    };

    static Riepilogo costruisciRiepilogo(const StatoMouse& mouse,
                                         const std::vector<Dispositivo>& tastiere) {
        Riepilogo out;
        if (mouse.connesso) {
            Dispositivo dm;
            dm.id = L"1D57:FA60";
            dm.nome = mouse.nome;
            dm.tipo = L"Mouse";
            dm.connessione = L"2.4G Wireless";
            dm.batteria = mouse.batteria;
            dm.inCarica = mouse.attivo ? mouse.inCarica : false;
            dm.online = mouse.online;
            dm.attivo = mouse.attivo;
            out.dispositivi.push_back(std::move(dm));
        }

        for (const auto& kb : tastiere) {
            bool skip = false;
            bool haUsbOnline = false;
            for (const auto& alt : tastiere) {
                if (&alt == &kb || alt.nome != kb.nome) continue;
                if (alt.online && alt.connessione == L"USB") haUsbOnline = true;
                if (!kb.online && alt.online) { skip = true; break; }
                if (kb.batteria < 0 && alt.online && alt.batteria >= 0) { skip = true; break; }
            }
            if (!skip) {
                Dispositivo d = kb;
                if (haUsbOnline && d.online) {
                    d.connessione = L"USB";
                    d.inCarica = true;
                }
                out.dispositivi.push_back(std::move(d));
            }
        }

        std::sort(out.dispositivi.begin(), out.dispositivi.end(),
                  [](const Dispositivo& a, const Dispositivo& b) {
            const int pa = dettagli::prioritaStato(a);
            const int pb = dettagli::prioritaStato(b);
            if (pa != pb) return pa < pb;
            const bool ba = a.batteria >= 0, bb = b.batteria >= 0;
            if (ba != bb) return ba;
            const std::wstring na = dettagli::minuscolo(a.nome), nb = dettagli::minuscolo(b.nome);
            if (na != nb) return na < nb;
            return a.id < b.id;
        });

        out.connesso = !out.dispositivi.empty();
        for (const auto& d : out.dispositivi) {
            if (!d.online) continue;
            if (d.inCarica && (d.batteria < 0 || d.batteria <= 99)) out.alcunoInCarica = true;
            if (d.batteria >= 0 && (out.batteriaMin < 0 || d.batteria < out.batteriaMin)) {
                out.batteriaMin = d.batteria;
            }
        }
        return out;
    }

    static DWORD WINAPI threadProc(LPVOID p) {
        Monitor* self = (Monitor*)p;
        dettagli::HidApi hid;
        if (!hid.carica()) {
            self->pubblica(Riepilogo{});
            for (;;) {
                HANDLE waits[2] = {self->stopEvent, self->wakeEvent};
                const DWORD w = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                if (w == WAIT_OBJECT_0 || w == WAIT_FAILED) break;
            }
            return 0;
        }

        StatoMouse mouse;
        mouse.ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!mouse.ov.hEvent) return 0;

        std::map<uint16_t, dettagli::StatoTastieraCache> kbCache;
        std::vector<Dispositivo> tastiere;
        ULONGLONG prossimaEnum = 0;
        bool forzaScan = true;

        auto avviaLetturaMouse = [&]() {
            int syncCount = 0;
            while (mouse.handle != INVALID_HANDLE_VALUE && !mouse.readPending) {
                if (WaitForSingleObject(self->stopEvent, 0) == WAIT_OBJECT_0) break;
                ResetEvent(mouse.ov.hEvent);
                DWORD letti = 0;
                const BOOL ok = ReadFile(mouse.handle, mouse.buf.data(), (DWORD)mouse.buf.size(), &letti, &mouse.ov);
                if (ok) {
                    mouse.decodifica(mouse.buf.data(), letti);
                    if (++syncCount >= 8) break;
                    continue;
                }
                if (GetLastError() == ERROR_IO_PENDING) {
                    mouse.readPending = true;
                    break;
                }
                mouse.chiudi();
                break;
            }
        };

        for (;;) {
            if (WaitForSingleObject(self->stopEvent, 0) == WAIT_OBJECT_0) break;

            const ULONGLONG ora = GetTickCount64();
            if (forzaScan || ora >= prossimaEnum) {
                forzaScan = false;
                const dettagli::TrovatiHid trovati = dettagli::enumeraDispositivi(hid);

                if (trovati.haMouse) {
                    if (mouse.handle != INVALID_HANDLE_VALUE && mouse.path != trovati.mousePath) {
                        mouse.chiudi();
                    }
                    mouse.connesso = true;
                    if (mouse.handle == INVALID_HANDLE_VALUE) {
                        mouse.path = trovati.mousePath;
                        mouse.inLen = trovati.mouseInLen;
                        mouse.buf.assign(std::max<size_t>(5, mouse.inLen), 0);
                        mouse.handle = dettagli::apriHidConRipiego(mouse.path, true, true);
                    }
                } else if (mouse.connesso || mouse.handle != INVALID_HANDLE_VALUE) {
                    mouse.chiudi();
                }

                // Rimuovi la cache dei PID di tastiera scollegati
                for (auto it = kbCache.begin(); it != kbCache.end(); ) {
                    const bool presente = std::any_of(trovati.tastiere.begin(), trovati.tastiere.end(),
                                                      [&](const auto& t) { return t.pid == it->first; });
                    if (!presente) it = kbCache.erase(it);
                    else ++it;
                }

                tastiere.clear();
                for (const auto& col : trovati.tastiere) {
                    Dispositivo kd;
                    if (!dettagli::interrogaTastiera(hid, col, kbCache[col.pid], self->stopEvent, kd)) {
                        mouse.chiudi();
                        CloseHandle(mouse.ov.hEvent);
                        return 0;
                    }
                    tastiere.push_back(std::move(kd));
                }

                prossimaEnum = GetTickCount64() + dettagli::ENUM_INTERVAL_MS;
            }

            avviaLetturaMouse();

            if (mouse.attivo && mouse.ultimoPacchetto > 0 &&
                (GetTickCount64() - mouse.ultimoPacchetto) > dettagli::MOUSE_IDLE_MS) {
                mouse.attivo = false;
                mouse.inCarica = false;
            }

            self->pubblica(costruisciRiepilogo(mouse, tastiere));

            const ULONGLONG adesso = GetTickCount64();
            DWORD waitMs = (prossimaEnum > adesso) ? (DWORD)(prossimaEnum - adesso) : 100;
            if (mouse.attivo && mouse.ultimoPacchetto > 0) {
                const ULONGLONG scadIdle = mouse.ultimoPacchetto + dettagli::MOUSE_IDLE_MS + 50;
                const DWORD idleMs = (scadIdle > adesso) ? (DWORD)(scadIdle - adesso) : 50;
                waitMs = std::min(waitMs, idleMs);
            }
            waitMs = std::clamp<DWORD>(waitMs, 50, (DWORD)dettagli::ENUM_INTERVAL_MS);

            HANDLE waits[3] = {self->stopEvent, self->wakeEvent, mouse.ov.hEvent};
            const DWORD nWaits = mouse.readPending ? 3 : 2;
            const DWORD w = WaitForMultipleObjects(nWaits, waits, FALSE, waitMs);
            if (w == WAIT_OBJECT_0 || w == WAIT_FAILED) break;

            if (w == WAIT_OBJECT_0 + 1) {
                forzaScan = true;
            } else if (mouse.readPending && w == WAIT_OBJECT_0 + 2) {
                mouse.readPending = false;
                DWORD letti = 0;
                if (GetOverlappedResult(mouse.handle, &mouse.ov, &letti, FALSE)) {
                    mouse.decodifica(mouse.buf.data(), letti);
                    avviaLetturaMouse();
                    self->pubblica(costruisciRiepilogo(mouse, tastiere));
                } else {
                    mouse.chiudi();
                    forzaScan = true;
                }
            }
        }

        mouse.chiudi();
        CloseHandle(mouse.ov.hEvent);
        return 0;
    }
};

}  // namespace attackshark
