#include "ui_fonts.h"
#include <dwrite_3.h>
#include <stdio.h>

namespace octans_ui {

struct FontEngine {
    bool initialized = false;
    bool loaded_anthropic_gdi = false;
    bool loaded_noto_gdi = false;
    wchar_t anthropic_path[MAX_PATH] = {};
    wchar_t noto_path[MAX_PATH] = {};

    ComPtr<IDWriteFactory5> factory5;
    ComPtr<IDWriteFontCollection1> custom_collection;
    ComPtr<IDWriteFontFallback> font_fallback;
};

static FontEngine g_fonts;

static bool FileExistsW(const wchar_t *path) {
    if (!path || !path[0]) return false;
    DWORD attr = GetFileAttributesW(path);
    return (attr != INVALID_FILE_ATTRIBUTES) && ((attr & FILE_ATTRIBUTE_DIRECTORY) == 0);
}

static bool LocateFontFile(const wchar_t *filename, const wchar_t *system_fallback, wchar_t *out_path, size_t cap) {
    out_path[0] = L'\0';

    wchar_t exe_dir[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, exe_dir, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        wchar_t *slash = wcsrchr(exe_dir, L'\\');
        if (slash) *slash = L'\0';
    }

    const wchar_t *rel_candidates[] = {
        L"%ls\\fonts\\%ls",
        L"%ls\\..\\ui\\fonts\\%ls",
        L"%ls\\ui\\fonts\\%ls"
    };

    for (size_t i = 0; i < 3; ++i) {
        if (!exe_dir[0]) continue;
        _snwprintf(out_path, cap - 1, rel_candidates[i], exe_dir, filename);
        out_path[cap - 1] = L'\0';
        if (FileExistsW(out_path)) return true;
    }

    _snwprintf(out_path, cap - 1, L"ui\\fonts\\%ls", filename);
    out_path[cap - 1] = L'\0';
    if (FileExistsW(out_path)) return true;

    _snwprintf(out_path, cap - 1, L"fonts\\%ls", filename);
    out_path[cap - 1] = L'\0';
    if (FileExistsW(out_path)) return true;

    if (system_fallback && FileExistsW(system_fallback)) {
        wcsncpy(out_path, system_fallback, cap - 1);
        out_path[cap - 1] = L'\0';
        return true;
    }

    out_path[0] = L'\0';
    return false;
}

static bool EnsureDirectWriteResources() {
    if (g_fonts.custom_collection && g_fonts.font_fallback) return true;

    if (!g_fonts.factory5) {
        if (FAILED(DWriteCreateFactory(
                DWRITE_FACTORY_TYPE_SHARED,
                __uuidof(IDWriteFactory5),
                reinterpret_cast<IUnknown **>(g_fonts.factory5.Put())))) {
            return false;
        }
    }

    /* 1. Build private DirectWrite FontCollection containing Anthropic Serif Web + Noto Serif SC */
    if (!g_fonts.custom_collection) {
        ComPtr<IDWriteFontSetBuilder1> builder;
        if (SUCCEEDED(g_fonts.factory5->CreateFontSetBuilder(builder.Put()))) {
            bool added_any = false;
            if (g_fonts.anthropic_path[0]) {
                ComPtr<IDWriteFontFile> ff;
                if (SUCCEEDED(g_fonts.factory5->CreateFontFileReference(g_fonts.anthropic_path, nullptr, ff.Put()))) {
                    if (SUCCEEDED(builder->AddFontFile(ff.Get()))) {
                        added_any = true;
                    }
                }
            }
            if (g_fonts.noto_path[0]) {
                ComPtr<IDWriteFontFile> ff;
                if (SUCCEEDED(g_fonts.factory5->CreateFontFileReference(g_fonts.noto_path, nullptr, ff.Put()))) {
                    if (SUCCEEDED(builder->AddFontFile(ff.Get()))) {
                        added_any = true;
                    }
                }
            }
            if (added_any) {
                ComPtr<IDWriteFontSet> font_set;
                if (SUCCEEDED(builder->CreateFontSet(font_set.Put()))) {
                    g_fonts.factory5->CreateFontCollectionFromFontSet(font_set.Get(), g_fonts.custom_collection.Put());
                }
            }
        }
    }

    /* 2. Build strict script-routed IDWriteFontFallback:
     *    - Latin / ASCII / Arabic numerals / common symbols -> Anthropic Serif Web
     *    - CJK Unified Ideographs & CJK punctuation         -> Noto Serif SC (思源宋体)
     *    - System fallback for anything outside both ranges */
    if (!g_fonts.font_fallback && g_fonts.custom_collection) {
        ComPtr<IDWriteFontFallbackBuilder> fb_builder;
        if (SUCCEEDED(g_fonts.factory5->CreateFontFallbackBuilder(fb_builder.Put()))) {
            static const DWRITE_UNICODE_RANGE kLatinRanges[] = {
                { 0x0000u, 0x024Fu }, /* Basic Latin, Latin-1 Supplement (incl. ·), Latin Extended */
                { 0x2000u, 0x206Fu }  /* General Punctuation (—, …, ·, etc.) */
            };
            static const DWRITE_UNICODE_RANGE kCjkRanges[] = {
                { 0x2E80u, 0x2FFFu }, /* CJK Radicals */
                { 0x3000u, 0x303Fu }, /* CJK Symbols & Punctuation (「」) */
                { 0x3400u, 0x4DBFu }, /* CJK Extension A */
                { 0x4E00u, 0x9FFFu }, /* CJK Unified Ideographs */
                { 0xF900u, 0xFAFFu }, /* CJK Compatibility Ideographs */
                { 0xFF00u, 0xFFEFu }  /* Halfwidth & Fullwidth Forms */
            };

            const wchar_t *anthropic_fam[1] = { L"Anthropic Serif Web Text" };
            const wchar_t *noto_fam[1]      = { L"Noto Serif SC" };

            if (g_fonts.anthropic_path[0]) {
                fb_builder->AddMapping(
                    kLatinRanges, 2,
                    anthropic_fam, 1,
                    g_fonts.custom_collection.Get(),
                    nullptr, nullptr, 1.0f
                );
            }
            if (g_fonts.noto_path[0]) {
                fb_builder->AddMapping(
                    kCjkRanges, 6,
                    noto_fam, 1,
                    g_fonts.custom_collection.Get(),
                    nullptr, nullptr, 1.0f
                );
            }

            ComPtr<IDWriteFontFallback> sys_fb;
            if (SUCCEEDED(g_fonts.factory5->GetSystemFontFallback(sys_fb.Put()))) {
                fb_builder->AddMappings(sys_fb.Get());
            }

            fb_builder->CreateFontFallback(g_fonts.font_fallback.Put());
        }
    }

    return g_fonts.custom_collection.Get() != nullptr;
}

bool FontsInit() {
    if (g_fonts.initialized) return true;

    LocateFontFile(L"AnthropicSerifWebText.ttf", nullptr, g_fonts.anthropic_path, MAX_PATH);
    LocateFontFile(L"NotoSerifSC-VF.ttf", L"C:\\Windows\\Fonts\\NotoSerifSC-VF.ttf", g_fonts.noto_path, MAX_PATH);

    if (g_fonts.anthropic_path[0]) {
        g_fonts.loaded_anthropic_gdi = (AddFontResourceExW(g_fonts.anthropic_path, FR_PRIVATE, nullptr) > 0);
    }
    if (g_fonts.noto_path[0]) {
        g_fonts.loaded_noto_gdi = (AddFontResourceExW(g_fonts.noto_path, FR_PRIVATE, nullptr) > 0);
    }

    EnsureDirectWriteResources();
    g_fonts.initialized = true;
    return true;
}

void FontsShutdown() {
    g_fonts.font_fallback.Reset();
    g_fonts.custom_collection.Reset();
    g_fonts.factory5.Reset();

    if (g_fonts.loaded_anthropic_gdi && g_fonts.anthropic_path[0]) {
        RemoveFontResourceExW(g_fonts.anthropic_path, FR_PRIVATE, nullptr);
        g_fonts.loaded_anthropic_gdi = false;
    }
    if (g_fonts.loaded_noto_gdi && g_fonts.noto_path[0]) {
        RemoveFontResourceExW(g_fonts.noto_path, FR_PRIVATE, nullptr);
        g_fonts.loaded_noto_gdi = false;
    }
    g_fonts.initialized = false;
}

bool CreateOctansTextFormat(
    IDWriteFactory *dwrite,
    DWRITE_FONT_WEIGHT weight,
    float font_size,
    DWRITE_TEXT_ALIGNMENT text_align,
    DWRITE_FONT_STRETCH stretch,
    bool add_ellipsis,
    ComPtr<IDWriteTextFormat> &out
) {
    if (!dwrite) return false;
    if (!g_fonts.initialized) FontsInit();
    EnsureDirectWriteResources();

    const wchar_t *primary = g_fonts.anthropic_path[0] ? L"Anthropic Serif Web Text" : L"Noto Serif SC";
    IDWriteFontCollection *col = g_fonts.custom_collection.Get();

    HRESULT hr = dwrite->CreateTextFormat(
        primary,
        col,
        weight,
        DWRITE_FONT_STYLE_NORMAL,
        stretch,
        font_size,
        L"zh-CN",
        out.Put()
    );
    if (FAILED(hr) || !out) {
        hr = dwrite->CreateTextFormat(
            L"Noto Serif SC",
            nullptr,
            weight,
            DWRITE_FONT_STYLE_NORMAL,
            stretch,
            font_size,
            L"zh-CN",
            out.Put()
        );
        if (FAILED(hr) || !out) return false;
    }

    out->SetTextAlignment(text_align);
    out->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    out->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    if (g_fonts.font_fallback) {
        ComPtr<IDWriteTextFormat1> fmt1;
        if (SUCCEEDED(out->QueryInterface(__uuidof(IDWriteTextFormat1), fmt1.PutVoid()))) {
            fmt1->SetFontFallback(g_fonts.font_fallback.Get());
        }
    }

    if (add_ellipsis) {
        DWRITE_TRIMMING trimming = {};
        trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
        ComPtr<IDWriteInlineObject> ellipsis;
        if (SUCCEEDED(dwrite->CreateEllipsisTrimmingSign(out.Get(), ellipsis.Put()))) {
            out->SetTrimming(&trimming, ellipsis.Get());
        }
    }
    return true;
}

bool CreateOctansDigitFormat(
    IDWriteFactory *dwrite,
    DWRITE_FONT_WEIGHT weight,
    float font_size,
    ComPtr<IDWriteTextFormat> &out
) {
    if (!dwrite) return false;
    if (!g_fonts.initialized) FontsInit();
    EnsureDirectWriteResources();

    const wchar_t *primary = g_fonts.anthropic_path[0] ? L"Anthropic Serif Web Text" : L"Noto Serif SC";
    HRESULT hr = dwrite->CreateTextFormat(
        primary,
        g_fonts.custom_collection.Get(),
        weight,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        font_size,
        L"en-US",
        out.Put()
    );
    if (FAILED(hr) || !out) return false;

    out->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    out->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    out->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    if (g_fonts.font_fallback) {
        ComPtr<IDWriteTextFormat1> fmt1;
        if (SUCCEEDED(out->QueryInterface(__uuidof(IDWriteTextFormat1), fmt1.PutVoid()))) {
            fmt1->SetFontFallback(g_fonts.font_fallback.Get());
        }
    }
    return true;
}

const wchar_t *GetEditControlFontFamily() {
    if (!g_fonts.initialized) FontsInit();
    return g_fonts.noto_path[0] ? L"Noto Serif SC" : L"Anthropic Serif Web Text";
}

} /* namespace octans_ui */
