// Il vetro del dock campiona le finestre retrostanti nel compositor di Windows.
// Nessuna immagine del desktop viene copiata nel processo o aggiornata con un timer.
#pragma once
#include <windows.h>
#include <dwmapi.h>
#include <roapi.h>
#include <windows.ui.composition.h>
#include <windows.foundation.h>
#include <d2d1effects.h>
#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <new>

namespace vetrodock {

namespace abi {
namespace c = ABI::Windows::UI::Composition;
namespace e = ABI::Windows::Graphics::Effects;
namespace f = ABI::Windows::Foundation;
using f::Numerics::Vector2;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Wrappers::HStringReference;

// Queste interfacce pubbliche del Windows SDK mancano ancora negli header MinGW.
// ICompositor7 mantiene l'ordine dei cinque metodi in Windows.UI.winmd.
struct DesktopInterop : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE CreateDesktopWindowTarget(HWND, BOOL, IInspectable**) = 0;
};
constexpr IID iidDesktop{0x29e691fa,0x4567,0x4dca,{0xb3,0x19,0xd0,0xf2,0x07,0xeb,0x68,0x07}};
struct Compositor2 : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateAmbientLight(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateAnimationGroup(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateBackdropBrush(IInspectable**) = 0;
};
constexpr IID iidCompositor2{0x735081dc,0x5e24,0x45da,{0xa3,0x8f,0xe3,0x2c,0xc3,0x49,0xa9,0xa0}};
struct RectangleClip : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Bottom(float*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Bottom(float) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_BottomLeftRadius(Vector2*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_BottomLeftRadius(Vector2) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_BottomRightRadius(Vector2*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_BottomRightRadius(Vector2) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Left(float*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Left(float) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Right(float*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Right(float) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Top(float*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Top(float) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TopLeftRadius(Vector2*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_TopLeftRadius(Vector2) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TopRightRadius(Vector2*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_TopRightRadius(Vector2) = 0;
};
struct Compositor7 : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_DispatcherQueue(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateAnimationPropertyInfo(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateRectangleClip(RectangleClip**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateRectangleClipWithSides(float,float,float,float,RectangleClip**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateRectangleClipWithSidesAndRadius(float,float,float,float,Vector2,Vector2,Vector2,Vector2,RectangleClip**) = 0;
};
constexpr IID iidCompositor7{0xd3483fad,0x9a12,0x53ba,{0xbf,0xc8,0x88,0xb7,0xff,0x79,0x77,0xc6}};
struct SourceFactory : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Create(HSTRING, IInspectable**) = 0;
};
constexpr IID iidSourceFactory{0xb3d9f276,0xaba3,0x4724,{0xac,0xf3,0xd0,0x39,0x74,0x64,0xdb,0x1c}};
struct EffectInterop : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetEffectId(GUID*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetNamedPropertyMapping(LPCWSTR, UINT*, INT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyCount(UINT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProperty(UINT, f::IPropertyValue**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSource(UINT, e::IGraphicsEffectSource**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSourceCount(UINT*) = 0;
};
constexpr IID iidEffect{0x2fc57384,0xa068,0x44d7,{0xa3,0x31,0x30,0x98,0x2f,0xcf,0x71,0x77}};

// Descrizione immutabile di un filtro Direct2D per CreateEffectFactory.
// La matrice CSS preserva la luminosita' anche oltre il 100% di saturazione.
class Filtro final : public e::IGraphicsEffect, public e::IGraphicsEffectSource, public EffectInterop {
public:
    enum Tipo { Sfocatura, Saturazione };
    Filtro(Tipo t, float v, f::IPropertyValueStatics* box, e::IGraphicsEffectSource* input)
        : tipo(t), valore(v), valori(box), sorgente(input) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IInspectable) || iid == __uuidof(e::IGraphicsEffect) || iid == __uuidof(IAgileObject))
            *out = static_cast<e::IGraphicsEffect*>(this);
        else if (iid == __uuidof(e::IGraphicsEffectSource)) *out = static_cast<e::IGraphicsEffectSource*>(this);
        else if (iid == iidEffect) *out = static_cast<EffectInterop*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++riferimenti; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n = --riferimenti; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG* n, IID** ids) override {
        if (!n || !ids) return E_POINTER;
        *n = 0; *ids = static_cast<IID*>(CoTaskMemAlloc(2 * sizeof(IID)));
        if (!*ids) return E_OUTOFMEMORY;
        (*ids)[0] = __uuidof(e::IGraphicsEffect); (*ids)[1] = __uuidof(e::IGraphicsEffectSource); *n = 2; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* out) override { return get_Name(out); }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* out) override { if (!out) return E_POINTER; *out = BaseTrust; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Name(HSTRING* out) override {
        if (!out) return E_POINTER;
        return tipo == Sfocatura ? HStringReference(L"Blur").CopyTo(out) : HStringReference(L"Color").CopyTo(out);
    }
    HRESULT STDMETHODCALLTYPE put_Name(HSTRING) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetEffectId(GUID* out) override {
        if (!out) return E_POINTER;
        *out = tipo == Sfocatura ? CLSID_D2D1GaussianBlur : CLSID_D2D1ColorMatrix; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetNamedPropertyMapping(LPCWSTR,UINT*,INT*) override { return E_INVALIDARG; }
    HRESULT STDMETHODCALLTYPE GetPropertyCount(UINT* n) override { if (!n) return E_POINTER; *n = 3; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetProperty(UINT i, f::IPropertyValue** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (i > 2) return E_BOUNDS;
        ComPtr<IInspectable> p;
        HRESULT hr;
        if (tipo == Sfocatura) {
            if (i == D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION) hr = valori->CreateSingle(valore, &p);
            else if (i == D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION) hr = valori->CreateUInt32(1 /* D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED */, &p);
            else hr = valori->CreateUInt32(D2D1_BORDER_MODE_HARD, &p);
        } else if (i == D2D1_COLORMATRIX_PROP_COLOR_MATRIX) {
            const float k = 1 - valore, r = 0.213f*k, g = 0.715f*k, b = 0.072f*k;
            float matrice[]{r+valore,r,r,0, g,g+valore,g,0, b,b,b+valore,0, 0,0,0,1, 0,0,0,0};
            hr = valori->CreateSingleArray(20, matrice, &p);
        } else if (i == D2D1_COLORMATRIX_PROP_ALPHA_MODE) hr = valori->CreateUInt32(D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED, &p);
        else hr = valori->CreateBoolean(true, &p);
        return FAILED(hr) ? hr : p.CopyTo(out);
    }
    HRESULT STDMETHODCALLTYPE GetSource(UINT i, e::IGraphicsEffectSource** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        return i == 0 ? sorgente.CopyTo(out) : E_BOUNDS;
    }
    HRESULT STDMETHODCALLTYPE GetSourceCount(UINT* n) override { if (!n) return E_POINTER; *n = 1; return S_OK; }
private:
    std::atomic<ULONG> riferimenti{1};
    Tipo tipo;
    float valore;
    ComPtr<f::IPropertyValueStatics> valori;
    ComPtr<e::IGraphicsEffectSource> sorgente;
};
} // namespace abi

class Sfondo {
public:
    bool avvia(HWND h) {
        chiudi();
        ultimo = inizializza(h);
        if (FAILED(ultimo)) chiudi();
        return SUCCEEDED(ultimo);
    }
    HRESULT ultimoErrore() const { return ultimo; }

    // Dimensioni e offset fisici del dock intero: il client HWND puo' essere
    // tagliato al bordo del monitor durante la scomparsa.
    bool imposta(float blur, float sat, float dpi, float raggio, int w, int h, int x = 0, int y = 0) {
        if (!visual || w <= 0 || h <= 0 || dpi <= 0) return false;
        const float deviazione = std::clamp(blur, 0.0f, 40.0f) * dpi;
        sat = std::clamp(sat, 0.5f, 2.0f);
        if (deviazione != ultimoBlur || sat != ultimaSat) {
            ultimo = filtri(deviazione, sat);
            if (FAILED(ultimo)) return false;
            ultimoBlur = deviazione; ultimaSat = sat;
        }
        // ok(): ricorda l'esito in 'ultimo' e dice se e' riuscito (le catene si fermano al primo errore, come prima con
        // "FAILED(ultimo = ...) ||", che clang-tidy segnala come assegnazione dentro l'if)
        auto ok = [this](HRESULT r) { ultimo = r; return SUCCEEDED(r); };
        if (w != ultimoW || h != ultimoH) {
            if (!ok(visual->put_Size({float(w),float(h)})) ||
                !ok(clip->put_Right(float(w))) || !ok(clip->put_Bottom(float(h)))) return false;
            ultimoW = w; ultimoH = h;
        }
        if (x != ultimoX || y != ultimoY) {
            if (!ok(visual->put_Offset({float(x),float(y),0}))) return false;
            ultimoX = x; ultimoY = y;
        }
        raggio = std::clamp(raggio, 0.f, std::min(w,h)*0.5f);
        if (raggio != ultimoRaggio) {
            const abi::Vector2 rr{raggio,raggio};
            if (!ok(clip->put_TopLeftRadius(rr)) || !ok(clip->put_TopRightRadius(rr)) ||
                !ok(clip->put_BottomLeftRadius(rr)) || !ok(clip->put_BottomRightRadius(rr))) return false;
            ultimoRaggio = raggio;
        }
        return true;
    }

    void tintaCentro(bool attiva, D2D1_COLOR_F tinta, float opacita, int w, int h) {
        using namespace abi;
        if (!attiva && !velo) return;
        if (!velo) {
            ComPtr<c::IContainerVisual> contenitore;
            ComPtr<c::IVisualCollection> figli;
            ComPtr<c::ISpriteVisual> strato;
            ComPtr<c::ICompositionBrush> brush;
            ComPtr<c::IVisual> v;
            if (FAILED(sprite.As(&contenitore)) || FAILED(contenitore->get_Children(&figli)) ||
                FAILED(compositor->CreateSpriteVisual(&strato)) || FAILED(strato.As(&v)) ||
                FAILED(compositor->CreateColorBrush(&coloreVelo)) || FAILED(coloreVelo.As(&brush)) ||
                FAILED(strato->put_Brush(brush.Get())) || FAILED(figli->InsertAtTop(v.Get()))) return;
            velo = std::move(v);
        }
        if (!attiva) { if (veloVisibile) velo->put_Opacity(0); veloVisibile = false; return; }
        auto byte = [](float f) { return BYTE(std::lround(255*std::clamp(f,0.f,1.f))); };
        coloreVelo->put_Color({byte(.12f+.88f*opacita), byte(tinta.r), byte(tinta.g), byte(tinta.b)});
        velo->put_Size({float(w),float(h)});
        if (!veloVisibile) velo->put_Opacity(1);
        veloVisibile = true;
    }
    void chiudi() {
        if (target) target->put_Root(nullptr);
        if (sprite) sprite->put_Brush(nullptr);
        velo.Reset(); coloreVelo.Reset(); veloVisibile = false;
        target.Reset(); visual.Reset(); sprite.Reset(); clip.Reset();
        sfondo.Reset(); sorgente.Reset(); box.Reset(); compositor.Reset();
        ultimoBlur = ultimaSat = ultimoRaggio = -1;
        ultimoW = ultimoH = ultimoX = ultimoY = 0;
    }
private:
    bool veloVisibile = false;
    abi::ComPtr<abi::c::IVisual> velo;
    abi::ComPtr<abi::c::ICompositionColorBrush> coloreVelo;
    abi::ComPtr<abi::c::ICompositor> compositor;
    abi::ComPtr<abi::c::ICompositionTarget> target;
    abi::ComPtr<abi::c::ISpriteVisual> sprite;
    abi::ComPtr<abi::c::IVisual> visual;
    abi::ComPtr<abi::RectangleClip> clip;
    abi::ComPtr<abi::c::ICompositionBrush> sfondo;
    abi::ComPtr<abi::e::IGraphicsEffectSource> sorgente;
    abi::ComPtr<abi::f::IPropertyValueStatics> box;
    HRESULT ultimo = S_OK;
    float ultimoBlur = -1, ultimaSat = -1, ultimoRaggio = -1;
    int ultimoW = 0, ultimoH = 0, ultimoX = 0, ultimoY = 0;

    HRESULT inizializza(HWND h) {
        using namespace abi;
        ComPtr<IInspectable> obj;
        const HStringReference classe(L"Windows.UI.Composition.Compositor");
        HRESULT hr = RoActivateInstance(classe.Get(), &obj);
        if (FAILED(hr)) {
            // Il thread UI e' gia' STA (OleInitialize). Serve una sola coda per
            // thread, condivisa fra le riaperture del dock.
            struct OpzioniCoda { DWORD size; int thread, apartment; };
            using CreaCoda = HRESULT (WINAPI*)(OpzioniCoda, IUnknown**);
            static HMODULE lib = LoadLibraryExW(L"CoreMessaging.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            static auto crea = lib ? reinterpret_cast<CreaCoda>(GetProcAddress(lib, "CreateDispatcherQueueController")) : nullptr;
            static thread_local ComPtr<IUnknown> coda;
            if (!crea || coda) return hr;
            hr = crea({sizeof(OpzioniCoda),2,0}, &coda);
            if (FAILED(hr)) return hr;
            hr = RoActivateInstance(classe.Get(), &obj);
            if (FAILED(hr)) return hr;
        }
        // ok(): ricorda l'esito in hr e dice se e' riuscito (vedi imposta)
        auto ok = [&hr](HRESULT r) { hr = r; return SUCCEEDED(r); };
        ComPtr<DesktopInterop> desktop;
        ComPtr<Compositor2> pennelli;
        ComPtr<Compositor7> geometria;
        ComPtr<IInspectable> destinazione, pennello;
        ComPtr<c::ICompositionClip> forma;
        ComPtr<SourceFactory> parametri;
        ComPtr<IInspectable> parametro;
        if (!ok(obj.As(&compositor)) || !ok(obj->QueryInterface(iidDesktop, reinterpret_cast<void**>(desktop.GetAddressOf()))) ||
            !ok(obj->QueryInterface(iidCompositor2, reinterpret_cast<void**>(pennelli.GetAddressOf()))) ||
            !ok(obj->QueryInterface(iidCompositor7, reinterpret_cast<void**>(geometria.GetAddressOf()))) ||
            !ok(desktop->CreateDesktopWindowTarget(h, TRUE, &destinazione)) ||
            !ok(destinazione.As(&target)) || !ok(pennelli->CreateBackdropBrush(&pennello)) ||
            !ok(pennello.As(&sfondo)) || !ok(compositor->CreateSpriteVisual(&sprite)) ||
            !ok(sprite.As(&visual)) || !ok(geometria->CreateRectangleClip(&clip)) ||
            !ok(clip.As(&forma)) || !ok(visual->put_Clip(forma.Get())) ||
            !ok(target->put_Root(visual.Get()))) return hr;
        if (!ok(RoGetActivationFactory(HStringReference(L"Windows.Foundation.PropertyValue").Get(),
                             __uuidof(f::IPropertyValueStatics), reinterpret_cast<void**>(box.GetAddressOf()))) ||
            !ok(RoGetActivationFactory(HStringReference(L"Windows.UI.Composition.CompositionEffectSourceParameter").Get(),
                             iidSourceFactory, reinterpret_cast<void**>(parametri.GetAddressOf()))) ||
            !ok(parametri->Create(HStringReference(L"Desktop").Get(), &parametro))) return hr;
        return parametro.As(&sorgente);
    }

    HRESULT filtri(float blur, float sat) {
        using namespace abi;
        ComPtr<e::IGraphicsEffectSource> input = sorgente;
        ComPtr<e::IGraphicsEffect> effetto;
        auto aggiungi = [&](Filtro::Tipo tipo, float v) -> HRESULT {
            auto* nodo = new(std::nothrow) Filtro(tipo, v, box.Get(), input.Get());
            if (!nodo) return E_OUTOFMEMORY;
            effetto.Attach(static_cast<e::IGraphicsEffect*>(nodo));
            input = static_cast<e::IGraphicsEffectSource*>(nodo);
            return S_OK;
        };
        HRESULT hr = S_OK;
        auto ok = [&hr](HRESULT r) { hr = r; return SUCCEEDED(r); };   // (vedi imposta)
        if (blur > 0 && !ok(aggiungi(Filtro::Sfocatura, blur))) return hr;
        if (sat != 1 && !ok(aggiungi(Filtro::Saturazione, sat))) return hr;
        if (!effetto) return sprite->put_Brush(nullptr);  // trasparenza ottica, nessun filtro
        ComPtr<c::ICompositionEffectFactory> fabbrica;
        ComPtr<c::ICompositionEffectBrush> pennello;
        ComPtr<c::ICompositionBrush> pronto;
        if (!ok(compositor->CreateEffectFactory(effetto.Get(), &fabbrica)) ||
            !ok(fabbrica->CreateBrush(&pennello)) || !ok(pennello->SetSourceParameter(HStringReference(L"Desktop").Get(), sfondo.Get())) ||
            !ok(pennello.As(&pronto))) return hr;
        return sprite->put_Brush(pronto.Get());
    }
};
} // namespace vetrodock
