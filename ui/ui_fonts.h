#ifndef OCTANS_UI_FONTS_H
#define OCTANS_UI_FONTS_H

#include "ui_internal.h"

namespace octans_ui {

/* Load private Anthropic Serif Web + Noto Serif SC fonts into DirectWrite & GDI. */
bool FontsInit();
void FontsShutdown();

/* Create an IDWriteTextFormat using Anthropic Serif Web for Latin/Digits and
 * Noto Serif SC (思源宋体) for CJK via DirectWrite IDWriteFontFallback. */
bool CreateOctansTextFormat(
    IDWriteFactory *dwrite,
    DWRITE_FONT_WEIGHT weight,
    float font_size,
    DWRITE_TEXT_ALIGNMENT text_align,
    DWRITE_FONT_STRETCH stretch,
    bool add_ellipsis,
    ComPtr<IDWriteTextFormat> &out
);

/* Create a crisp DirectWrite format specifically tuned for bold Anthropic Serif digits. */
bool CreateOctansDigitFormat(
    IDWriteFactory *dwrite,
    DWRITE_FONT_WEIGHT weight,
    float font_size,
    ComPtr<IDWriteTextFormat> &out
);

/* Primary GDI font family name for native child EDIT controls. */
const wchar_t *GetEditControlFontFamily();

} /* namespace octans_ui */

#endif /* OCTANS_UI_FONTS_H */
