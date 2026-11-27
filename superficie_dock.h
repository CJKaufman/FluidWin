#pragma once
#include <d2d1.h>
#include <algorithm>

namespace vetrodock {

// Il desktop filtrato è già sotto questa superficie. Come nel riferimento, il
// velo di tinta al 12% si compone sopra la tinta con l'opacità scelta.
inline void disegnaSuperficie(ID2D1RenderTarget* dc, ID2D1SolidColorBrush* pennello,
                              const D2D1_RECT_F& f, float raggio, float dpi,
                              D2D1_COLOR_F tinta, float opacita, float bordo) {
    tinta.a = opacita + 0.12f * (1 - opacita);
    pennello->SetColor(tinta);
    dc->FillRoundedRectangle({f, raggio, raggio}, pennello);
    if (bordo <= 0) return;

    const float mezzo = dpi / 2, r = std::max(0.0f, raggio - mezzo);
    const D2D1_ROUNDED_RECT contorno = {{f.left + mezzo, f.top + mezzo, f.right - mezzo, f.bottom - mezzo}, r, r};
    auto luce = D2D1::ColorF(0.9f + 0.1f * tinta.r, 0.9f + 0.1f * tinta.g, 0.9f + 0.1f * tinta.b, 0.34f * bordo);
    pennello->SetColor(luce);
    dc->DrawRoundedRectangle(contorno, pennello, dpi);

    // Riflesso incidente dall'alto, sempre rispetto allo schermo anche con il dock verticale.
    dc->PushAxisAlignedClip({f.left, f.top, f.right, f.top + dpi}, D2D1_ANTIALIAS_MODE_ALIASED);
    luce.a = (bordo - luce.a) / (1 - luce.a);
    pennello->SetColor(luce);
    dc->DrawRoundedRectangle(contorno, pennello, dpi);
    dc->PopAxisAlignedClip();
}

} // namespace vetrodock
