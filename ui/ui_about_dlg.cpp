#include "ui_about_dlg.h"
#include "ui_updater.h"
#include "ui_theme.h"
#include "ui_brand.h"
#include "ui_fonts.h"
#include "octans_version.h"
#include <stdio.h>

namespace octans_ui {

static constexpr int IDC_ABOUT_SECONDARY = 201;
static constexpr int IDC_ABOUT_PRIMARY   = 202;
static constexpr int IDC_ABOUT_CLOSE     = 203;

static constexpr UINT WM_ABOUT_CHECK_DONE = WM_APP + 30;
static constexpr UINT WM_ABOUT_DL_PROG    = WM_APP + 31;
static constexpr UINT WM_ABOUT_DL_DONE    = WM_APP + 32;
static constexpr UINT_PTR TIMER_ABOUT_PULSE = 1;

enum AboutPhase : uint8_t {
    ABOUT_IDLE = 0,
    ABOUT_CHECKING,
    ABOUT_UP_TO_DATE,
    ABOUT_AVAILABLE,
    ABOUT_DOWNLOADING,
    ABOUT_VERIFYING,
    ABOUT_ERROR
};

struct AboutState {
    bool is_dark = true;
    bool done = false;
    bool launched_installer = false;
    UINT dpi = 96;
    float logical_w = 372.0f;
    float logical_h = 286.0f;

    AboutPhase phase = ABOUT_IDLE;
    int dl_percent = 0;
    float pulse_t = 0.0f;
    int hover_btn = 0; /* 0=none, 1=secondary, 2=primary, 3=close */

    UpdateInfo upd = {};
    wchar_t downloaded_exe[MAX_PATH] = {};
    wchar_t status_line[160] = {};
    wchar_t detail_line[256] = {};

    HWND hwnd = nullptr;
    HWND btn_secondary = nullptr;
    HWND btn_primary = nullptr;
    HWND btn_close = nullptr;
    HFONT font_btn = nullptr;

    HANDLE worker = nullptr;
    volatile LONG cancel_flag = 0;

    D2D1_RECT_F card_rc = {};
    D2D1_RECT_F sec_btn_rc = {};
    D2D1_RECT_F pri_btn_rc = {};
    D2D1_RECT_F close_btn_rc = {};

    ComPtr<ID2D1Factory> d2d_factory;
    ComPtr<ID2D1DCRenderTarget> rt;
    ComPtr<IDWriteFactory> dwrite;
    ComPtr<IDWriteTextFormat> fmt_meta;
    ComPtr<IDWriteTextFormat> fmt_status;
    ComPtr<IDWriteTextFormat> fmt_detail;
};

static bool EnsureAboutResources(AboutState &st) {
    if (!st.d2d_factory) {
        D2D1_FACTORY_OPTIONS fo = {};
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), &fo, st.d2d_factory.PutVoid()))) {
            return false;
        }
    }
    if (!st.dwrite) {
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown **>(st.dwrite.Put())))) {
            return false;
        }
        auto mk = [&](ComPtr<IDWriteTextFormat> &out, DWRITE_FONT_WEIGHT w, float sz, DWRITE_TEXT_ALIGNMENT al, bool ell) {
            CreateOctansTextFormat(st.dwrite.Get(), w, sz, al, DWRITE_FONT_STRETCH_NORMAL, ell, out);
        };
        mk(st.fmt_meta,   DWRITE_FONT_WEIGHT_REGULAR,   11.2f, DWRITE_TEXT_ALIGNMENT_CENTER,  false);
        mk(st.fmt_status, DWRITE_FONT_WEIGHT_SEMI_BOLD, 12.2f, DWRITE_TEXT_ALIGNMENT_CENTER,  true);
        mk(st.fmt_detail, DWRITE_FONT_WEIGHT_REGULAR,   10.8f, DWRITE_TEXT_ALIGNMENT_CENTER,  false);
    }
    if (!st.rt) {
        D2D1_RENDER_TARGET_PROPERTIES rtp = {};
        rtp.type = D2D1_RENDER_TARGET_TYPE_SOFTWARE;
        rtp.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
        rtp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
        rtp.dpiX = static_cast<float>(st.dpi);
        rtp.dpiY = static_cast<float>(st.dpi);
        if (FAILED(st.d2d_factory->CreateDCRenderTarget(&rtp, st.rt.Put()))) {
            return false;
        }
    }
    return st.rt.Get() != nullptr;
}

static void RepaintAbout(AboutState &st) {
    if (st.hwnd) {
        InvalidateRect(st.hwnd, nullptr, FALSE);
        if (st.btn_secondary) InvalidateRect(st.btn_secondary, nullptr, FALSE);
        if (st.btn_primary) InvalidateRect(st.btn_primary, nullptr, FALSE);
        if (st.btn_close) InvalidateRect(st.btn_close, nullptr, FALSE);
    }
}

static void RenderAboutToDC(AboutState &st, HDC target_dc) {
    if (!target_dc || !st.hwnd || !EnsureAboutResources(st)) return;

    RECT crc = {};
    GetClientRect(st.hwnd, &crc);
    int pw = crc.right - crc.left;
    int ph = crc.bottom - crc.top;
    if (pw < 2 || ph < 2) return;

    HDC mem_dc = CreateCompatibleDC(target_dc);
    HBITMAP bmp = CreateCompatibleBitmap(target_dc, pw, ph);
    if (!mem_dc || !bmp) {
        if (bmp) DeleteObject(bmp);
        if (mem_dc) DeleteDC(mem_dc);
        return;
    }
    HGDIOBJ old_bmp = SelectObject(mem_dc, bmp);

    ThemePalette pal = GetThemePalette(st.is_dark);
    ID2D1DCRenderTarget *rt = st.rt.Get();

    if (SUCCEEDED(rt->BindDC(mem_dc, &crc))) {
        rt->BeginDraw();
        rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

        D2D1_COLOR_F bg = st.is_dark ? HexColor(0x202025, 1.0f) : HexColor(0xF8F8FA, 1.0f);
        rt->Clear(bg);

        ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(pal.text_primary, b.Put());
        if (b) {
            /* Hairline outer border */
            b->SetColor(pal.panel_outer_border);
            rt->DrawRoundedRectangle(
                MakeRoundedRect(MakeRect(0.5f, 0.5f, st.logical_w - 0.5f, st.logical_h - 0.5f), 7.5f, 7.5f),
                b.Get(), 1.0f
            );

            /* Centered Octans Logo + Wordmark */
            float cx = st.logical_w * 0.5f;
            DrawOctansLogo(st.d2d_factory.Get(), rt, cx, 42.0f, 20.0f, st.is_dark, false, 1.0f, 1.0f);
            D2D1_RECT_F wm_rc = MakeRect(cx - 44.0f, 68.0f, cx + 44.0f, 90.0f);
            DrawOctansWordmark(st.d2d_factory.Get(), rt, wm_rc, st.is_dark, 1.0f, 1.0f);

            /* Version / Author / License meta */
            b->SetColor(pal.text_secondary);
            const wchar_t *meta1 = L"v" OCTANS_VERSION_WSTR L" \u00b7 \u4f5c\u8005 s0ng1v \u00b7 GPL-3.0";
            rt->DrawText(meta1, static_cast<UINT32>(wcslen(meta1)), st.fmt_meta.Get(),
                         MakeRect(20.0f, 94.0f, st.logical_w - 20.0f, 114.0f), b.Get());

            /* Frosted update status card */
            D2D1_ROUNDED_RECT crr = MakeRoundedRect(st.card_rc, 12.0f, 12.0f);
            b->SetColor(st.is_dark ? MakeColor(1, 1, 1, 0.055f) : MakeColor(1, 1, 1, 0.90f));
            rt->FillRoundedRectangle(crr, b.Get());
            b->SetColor(pal.card_border);
            rt->DrawRoundedRectangle(crr, b.Get(), 1.0f);

            D2D1_COLOR_F st_col = pal.text_primary;
            if (st.phase == ABOUT_UP_TO_DATE || st.phase == ABOUT_AVAILABLE) {
                st_col = st.is_dark ? HexColor(0x34D399, 1.0f) : HexColor(0x059669, 1.0f);
            } else if (st.phase == ABOUT_ERROR) {
                st_col = pal.ring_low;
            }
            b->SetColor(st_col);
            rt->DrawText(st.status_line, static_cast<UINT32>(wcslen(st.status_line)), st.fmt_status.Get(),
                         MakeRect(st.card_rc.left + 12.0f, st.card_rc.top + 10.0f, st.card_rc.right - 12.0f, st.card_rc.top + 32.0f),
                         b.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

            if (st.phase == ABOUT_CHECKING || st.phase == ABOUT_DOWNLOADING || st.phase == ABOUT_VERIFYING) {
                /* Smooth progress bar inside status card */
                float pl = st.card_rc.left + 24.0f;
                float pr = st.card_rc.right - 24.0f;
                float py = st.card_rc.top + 46.0f;
                float ph_bar = 5.0f;
                b->SetColor(st.is_dark ? MakeColor(1, 1, 1, 0.12f) : MakeColor(0, 0, 0, 0.08f));
                rt->FillRoundedRectangle(MakeRoundedRect(MakeRect(pl, py, pr, py + ph_bar), 2.5f, 2.5f), b.Get());

                b->SetColor(st.is_dark ? HexColor(0x30D158, 1.0f) : HexColor(0x34C759, 1.0f));
                if (st.phase == ABOUT_CHECKING) {
                    float w = (pr - pl) * 0.34f;
                    float pos = 0.5f - 0.5f * cosf(st.pulse_t * 3.2f);
                    float x0 = LerpF(pl, pr - w, pos);
                    rt->FillRoundedRectangle(MakeRoundedRect(MakeRect(x0, py, x0 + w, py + ph_bar), 2.5f, 2.5f), b.Get());
                } else {
                    float f = ClampF(static_cast<float>(st.dl_percent) / 100.0f, 0.04f, 1.0f);
                    rt->FillRoundedRectangle(MakeRoundedRect(MakeRect(pl, py, pl + (pr - pl) * f, py + ph_bar), 2.5f, 2.5f), b.Get());
                }

                b->SetColor(pal.text_muted);
                rt->DrawText(st.detail_line, static_cast<UINT32>(wcslen(st.detail_line)), st.fmt_detail.Get(),
                             MakeRect(st.card_rc.left + 12.0f, st.card_rc.top + 60.0f, st.card_rc.right - 12.0f, st.card_rc.bottom - 8.0f),
                             b.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            } else {
                b->SetColor(pal.text_secondary);
                rt->DrawText(st.detail_line, static_cast<UINT32>(wcslen(st.detail_line)), st.fmt_detail.Get(),
                             MakeRect(st.card_rc.left + 14.0f, st.card_rc.top + 36.0f, st.card_rc.right - 14.0f, st.card_rc.bottom - 8.0f),
                             b.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
        }

        HRESULT hr = rt->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) {
            st.rt.Reset();
        }
    }

    BitBlt(target_dc, 0, 0, pw, ph, mem_dc, 0, 0, SRCCOPY);
    SelectObject(mem_dc, old_bmp);
    DeleteObject(bmp);
    DeleteDC(mem_dc);
}

static const wchar_t *GetPrimaryBtnLabel(const AboutState &st) {
    switch (st.phase) {
        case ABOUT_CHECKING:    return L"\u68c0\u6d4b\u4e2d...";
        case ABOUT_AVAILABLE:   return L"\u7acb\u5373\u4e0b\u8f7d\u5e76\u66f4\u65b0";
        case ABOUT_DOWNLOADING: return L"\u6b63\u5728\u4e0b\u8f7d...";
        case ABOUT_VERIFYING:   return L"\u6821\u9a8c\u4e2d...";
        case ABOUT_ERROR:       return L"\u524d\u5f80\u53d1\u5e03\u9875\u4e0b\u8f7d";
        default:                return L"\u68c0\u67e5\u66f4\u65b0";
    }
}

static const wchar_t *GetSecondaryBtnLabel(const AboutState &st) {
    if (st.phase == ABOUT_AVAILABLE || st.phase == ABOUT_ERROR) {
        return L"\u624b\u52a8\u4e0b\u8f7d";
    }
    return L"\u9879\u76ee\u4e3b\u9875";
}

static void DrawAboutButton(const AboutState &st, const DRAWITEMSTRUCT *dis) {
    if (!dis) return;
    RECT rc = dis->rcItem;
    HDC hdc = dis->hDC;
    COLORREF bg_parent = st.is_dark ? RGB(32, 32, 37) : RGB(248, 248, 250);
    HBRUSH pb = CreateSolidBrush(bg_parent);
    FillRect(hdc, &rc, pb);
    DeleteObject(pb);

    bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    if (dis->CtlID == IDC_ABOUT_CLOSE) {
        bool hov = (st.hover_btn == 3);
        if (hov || pressed) {
            COLORREF c = st.is_dark ? RGB(54, 54, 60) : RGB(228, 228, 234);
            HBRUSH cb = CreateSolidBrush(c);
            HPEN cp = CreatePen(PS_SOLID, 1, c);
            HGDIOBJ ob = SelectObject(hdc, cb), op = SelectObject(hdc, cp);
            Ellipse(hdc, rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1);
            SelectObject(hdc, ob); SelectObject(hdc, op);
            DeleteObject(cb); DeleteObject(cp);
        }
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, st.is_dark ? RGB(205, 205, 212) : RGB(90, 90, 96));
        HGDIOBJ of = st.font_btn ? SelectObject(hdc, st.font_btn) : nullptr;
        DrawTextW(hdc, L"\u00d7", 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (of) SelectObject(hdc, of);
        return;
    }

    bool is_pri = (dis->CtlID == IDC_ABOUT_PRIMARY);
    bool busy = is_pri && (st.phase == ABOUT_CHECKING || st.phase == ABOUT_DOWNLOADING || st.phase == ABOUT_VERIFYING);
    bool hov = (st.hover_btn == (is_pri ? 2 : 1)) && !busy;

    COLORREF fill_col, border_col, txt_col;
    if (is_pri) {
        if (busy) fill_col = st.is_dark ? RGB(42, 128, 66) : RGB(126, 214, 148);
        else if (pressed) fill_col = st.is_dark ? RGB(38, 174, 72) : RGB(41, 168, 74);
        else if (hov) fill_col = st.is_dark ? RGB(59, 224, 102) : RGB(59, 214, 98);
        else fill_col = st.is_dark ? RGB(48, 209, 88) : RGB(52, 199, 89);
        border_col = fill_col;
        txt_col = RGB(255, 255, 255);
    } else {
        if (pressed) fill_col = st.is_dark ? RGB(66, 66, 72) : RGB(218, 218, 224);
        else if (hov) fill_col = st.is_dark ? RGB(58, 58, 64) : RGB(228, 228, 234);
        else fill_col = st.is_dark ? RGB(46, 46, 52) : RGB(236, 236, 241);
        border_col = st.is_dark ? RGB(70, 70, 76) : RGB(214, 214, 220);
        txt_col = st.is_dark ? RGB(240, 240, 245) : RGB(28, 28, 30);
    }

    int rad = static_cast<int>(roundf(10.0f * static_cast<float>(st.dpi) / 96.0f));
    HBRUSH fb = CreateSolidBrush(fill_col);
    HPEN fp = CreatePen(PS_SOLID, 1, border_col);
    HGDIOBJ ob = SelectObject(hdc, fb), op = SelectObject(hdc, fp);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, rad * 2, rad * 2);
    SelectObject(hdc, ob); SelectObject(hdc, op);
    DeleteObject(fb); DeleteObject(fp);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, txt_col);
    HGDIOBJ of = st.font_btn ? SelectObject(hdc, st.font_btn) : nullptr;
    const wchar_t *lbl = is_pri ? GetPrimaryBtnLabel(st) : GetSecondaryBtnLabel(st);
    DrawTextW(hdc, lbl, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (of) SelectObject(hdc, of);
}

static DWORD WINAPI CheckWorkerProc(LPVOID param) {
    auto *st = static_cast<AboutState *>(param);
    UpdateInfo info = {};
    bool ok = CheckForUpdatesSync(&info);
    if (InterlockedCompareExchange(&st->cancel_flag, 0, 0) == 0 && st->hwnd) {
        st->upd = info;
        PostMessageW(st->hwnd, WM_ABOUT_CHECK_DONE, ok ? 1 : 0, 0);
    }
    return 0;
}

static void OnDlProgress(int pct, bool verifying, void *ctx) {
    auto *st = static_cast<AboutState *>(ctx);
    if (InterlockedCompareExchange(&st->cancel_flag, 0, 0) == 0 && st->hwnd) {
        PostMessageW(st->hwnd, WM_ABOUT_DL_PROG, static_cast<WPARAM>(pct), verifying ? 1 : 0);
    }
}

static DWORD WINAPI DownloadWorkerProc(LPVOID param) {
    auto *st = static_cast<AboutState *>(param);
    wchar_t exe[MAX_PATH] = {};
    wchar_t err[128] = {};
    bool ok = DownloadAndVerifyInstallerSync(st->upd, OnDlProgress, st, exe, MAX_PATH, err, 128);
    if (InterlockedCompareExchange(&st->cancel_flag, 0, 0) == 0 && st->hwnd) {
        if (ok) {
            wcsncpy(st->downloaded_exe, exe, MAX_PATH - 1);
            st->downloaded_exe[MAX_PATH - 1] = L'\0';
        } else {
            wcsncpy(st->upd.error_msg, err, 127);
            st->upd.error_msg[127] = L'\0';
        }
        PostMessageW(st->hwnd, WM_ABOUT_DL_DONE, ok ? 1 : 0, 0);
    }
    return 0;
}

static void OpenReleasePage(const AboutState &st) {
    const wchar_t *url = st.upd.page_cn[0] ? st.upd.page_cn : L"https://github.com/s0ng1v/Octans/releases/latest";
    if (!st.upd.used_cn_mirror && st.upd.page_global[0]) {
        url = st.upd.page_global;
    }
    ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
}

static LRESULT CALLBACK AboutBtnSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto orig = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    HWND parent = GetParent(hwnd);
    auto *st = parent ? reinterpret_cast<AboutState *>(GetWindowLongPtrW(parent, GWLP_USERDATA)) : nullptr;
    int id = GetDlgCtrlID(hwnd);
    int want = (id == IDC_ABOUT_SECONDARY) ? 1 : ((id == IDC_ABOUT_PRIMARY) ? 2 : 3);

    if (msg == WM_SETCURSOR) {
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
    }
    if (msg == WM_MOUSEMOVE && st) {
        if (st->hover_btn != want) {
            st->hover_btn = want;
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    } else if (msg == WM_MOUSELEAVE && st) {
        if (st->hover_btn == want) {
            st->hover_btn = 0;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    }
    return orig ? CallWindowProcW(orig, hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK AboutSheetProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto *st = reinterpret_cast<AboutState *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_NCCREATE: {
            auto *cs = reinterpret_cast<CREATESTRUCTW *>(lp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return DefWindowProcW(hwnd, msg, wp, lp);
        }

        case WM_TIMER:
            if (st && wp == TIMER_ABOUT_PULSE) {
                st->pulse_t += 0.03f;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            break;

        case WM_ABOUT_CHECK_DONE:
            if (st) {
                KillTimer(hwnd, TIMER_ABOUT_PULSE);
                if (st->worker) { CloseHandle(st->worker); st->worker = nullptr; }
                if (wp == 1) {
                    if (st->upd.has_update) {
                        st->phase = ABOUT_AVAILABLE;
                        wchar_t vw[32] = {};
                        MultiByteToWideChar(CP_UTF8, 0, st->upd.latest_version, -1, vw, 32);
                        _snwprintf(st->status_line, 159, L"\u53d1\u73b0\u65b0\u7248\u672c v%ls", vw);
                        if (st->upd.release_notes[0]) {
                            wcsncpy(st->detail_line, st->upd.release_notes, 255);
                        } else {
                            wcscpy(st->detail_line, L"\u70b9\u51fb\u4e0b\u65b9\u6309\u94ae\u81ea\u52a8\u4e0b\u8f7d\u3001\u6821\u9a8c SHA256 \u5e76\u9759\u9ed8\u5347\u7ea7");
                        }
                    } else {
                        st->phase = ABOUT_UP_TO_DATE;
                        wcscpy(st->status_line, L"\u5f53\u524d\u5df2\u662f\u6700\u65b0\u7248\u672c");
                        wcscpy(st->detail_line, L"Octans v" OCTANS_VERSION_WSTR L" \u00b7 \u5df2\u901a\u8fc7\u955c\u50cf\u6e90\u6821\u9a8c");
                    }
                } else {
                    st->phase = ABOUT_ERROR;
                    wcscpy(st->status_line, L"\u68c0\u67e5\u66f4\u65b0\u5931\u8d25");
                    wcsncpy(st->detail_line, st->upd.error_msg[0] ? st->upd.error_msg : L"\u8bf7\u70b9\u51fb\u4e0b\u65b9\u6309\u94ae\u524d\u5f80\u53d1\u5e03\u9875\u624b\u52a8\u4e0b\u8f7d", 255);
                }
                RepaintAbout(*st);
            }
            return 0;

        case WM_ABOUT_DL_PROG:
            if (st) {
                st->dl_percent = static_cast<int>(wp);
                if (lp != 0) {
                    st->phase = ABOUT_VERIFYING;
                    wcscpy(st->status_line, L"\u6b63\u5728\u6821\u9a8c SHA256 \u5b8c\u6574\u6027...");
                    wcscpy(st->detail_line, L"\u786e\u4fdd\u5b89\u88c5\u5305\u672a\u88ab\u7be1\u6539");
                } else {
                    st->phase = ABOUT_DOWNLOADING;
                    _snwprintf(st->status_line, 159, L"\u6b63\u5728\u4e0b\u8f7d\u65b0\u7248\u5b89\u88c5\u5305 %d%%", st->dl_percent);
                    wcscpy(st->detail_line, L"\u4f18\u5148\u4f7f\u7528\u56fd\u5185 AtomGit \u955c\u50cf\uff0c\u5931\u8d25\u81ea\u52a8\u56de\u9000 GitHub");
                }
                RepaintAbout(*st);
            }
            return 0;

        case WM_ABOUT_DL_DONE:
            if (st) {
                KillTimer(hwnd, TIMER_ABOUT_PULSE);
                if (st->worker) { CloseHandle(st->worker); st->worker = nullptr; }
                if (wp == 1 && st->downloaded_exe[0]) {
                    if (LaunchSilentInstaller(st->downloaded_exe)) {
                        st->launched_installer = true;
                        st->done = true;
                        return 0;
                    }
                    st->phase = ABOUT_ERROR;
                    wcscpy(st->status_line, L"\u542f\u52a8\u5b89\u88c5\u7a0b\u5e8f\u5931\u8d25");
                    wcscpy(st->detail_line, L"\u8bf7\u70b9\u51fb\u4e0b\u65b9\u6309\u94ae\u524d\u5f80\u53d1\u5e03\u9875\u624b\u52a8\u4e0b\u8f7d");
                } else {
                    st->phase = ABOUT_ERROR;
                    wcscpy(st->status_line, L"\u81ea\u52a8\u66f4\u65b0\u5931\u8d25");
                    wcsncpy(st->detail_line, st->upd.error_msg[0] ? st->upd.error_msg : L"\u8bf7\u524d\u5f80\u53d1\u5e03\u9875\u624b\u52a8\u4e0b\u8f7d\u5b89\u88c5\u5305", 255);
                }
                RepaintAbout(*st);
            }
            return 0;

        case WM_COMMAND:
            if (st) {
                int ctl = LOWORD(wp);
                if (ctl == IDC_ABOUT_CLOSE) {
                    st->done = true;
                    return 0;
                }
                if (ctl == IDC_ABOUT_SECONDARY) {
                    if (st->phase == ABOUT_AVAILABLE || st->phase == ABOUT_ERROR) {
                        OpenReleasePage(*st);
                    } else {
                        ShellExecuteW(nullptr, L"open", L"https://github.com/s0ng1v/Octans", nullptr, nullptr, SW_SHOWNORMAL);
                    }
                    return 0;
                }
                if (ctl == IDC_ABOUT_PRIMARY) {
                    if (st->phase == ABOUT_CHECKING || st->phase == ABOUT_DOWNLOADING || st->phase == ABOUT_VERIFYING) {
                        return 0;
                    }
                    if (st->phase == ABOUT_ERROR) {
                        OpenReleasePage(*st);
                        return 0;
                    }
                    if (st->phase == ABOUT_AVAILABLE) {
                        st->phase = ABOUT_DOWNLOADING;
                        st->dl_percent = 2;
                        wcscpy(st->status_line, L"\u6b63\u5728\u4e0b\u8f7d\u65b0\u7248\u5b89\u88c5\u5305...");
                        wcscpy(st->detail_line, L"\u4e0b\u8f7d\u5b8c\u6210\u540e\u5c06\u6821\u9a8c SHA256 \u5e76\u81ea\u52a8\u9759\u9ed8\u8986\u76d6\u5347\u7ea7");
                        st->worker = CreateThread(nullptr, 0, DownloadWorkerProc, st, 0, nullptr);
                        RepaintAbout(*st);
                        return 0;
                    }
                    /* Start check for updates */
                    st->phase = ABOUT_CHECKING;
                    st->pulse_t = 0.0f;
                    wcscpy(st->status_line, L"\u6b63\u5728\u68c0\u6d4b\u66f4\u65b0...");
                    wcscpy(st->detail_line, L"\u4f18\u5148\u8fde\u63a5\u56fd\u5185 AtomGit \u955c\u50cf\uff0c\u8d85\u65f6\u81ea\u52a8\u56de\u9000 GitHub");
                    SetTimer(hwnd, TIMER_ABOUT_PULSE, 25, nullptr);
                    st->worker = CreateThread(nullptr, 0, CheckWorkerProc, st, 0, nullptr);
                    RepaintAbout(*st);
                    return 0;
                }
            }
            break;

        case WM_DRAWITEM:
            if (st) {
                DrawAboutButton(*st, reinterpret_cast<const DRAWITEMSTRUCT *>(lp));
                return TRUE;
            }
            break;

        case WM_LBUTTONDOWN:
            SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps = {};
            HDC hdc = BeginPaint(hwnd, &ps);
            if (st) RenderAboutToDC(*st, hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_CLOSE:
            if (st) st->done = true;
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool ShowAboutDialog(HWND owner, bool is_dark) {
    static const wchar_t *kClassName = L"OctansAboutSheetWnd";
    static bool registered = false;
    HINSTANCE hinst = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = AboutSheetProc;
        wc.hInstance = hinst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        RegisterClassExW(&wc);
        registered = true;
    }

    AboutState st = {};
    st.is_dark = is_dark;
    st.dpi = GetWindowDpiSafe(owner);
    float scale = static_cast<float>(st.dpi) / 96.0f;

    wcscpy(st.status_line, L"\u8f7b\u91cf\u539f\u751f Windows \u65e0\u7ebf\u5916\u8bbe\u7535\u91cf\u6258\u76d8");
    wcscpy(st.detail_line, L"\u9759\u9ed8\u9a7b\u7559 0% \u540e\u53f0\u8054\u7f51 \u00b7 \u4ec5\u624b\u52a8\u70b9\u51fb\u68c0\u67e5\u66f4\u65b0\u65f6\u8bbf\u95ee\u7f51\u7edc");

    st.card_rc      = MakeRect(20.0f, 124.0f, st.logical_w - 20.0f, 218.0f);
    st.sec_btn_rc   = MakeRect(20.0f, 234.0f, 134.0f, 266.0f);
    st.pri_btn_rc   = MakeRect(146.0f, 234.0f, st.logical_w - 20.0f, 266.0f);
    st.close_btn_rc = MakeRect(st.logical_w - 34.0f, 10.0f, st.logical_w - 10.0f, 34.0f);

    int px_w = static_cast<int>(ceilf(st.logical_w * scale));
    int px_h = static_cast<int>(ceilf(st.logical_h * scale));

    RECT anchor_rc = {};
    int x = 0, y = 0;
    if (owner && IsWindowVisible(owner) && GetWindowRect(owner, &anchor_rc)) {
        x = (anchor_rc.left + anchor_rc.right - px_w) / 2;
        y = (anchor_rc.top + anchor_rc.bottom - px_h) / 2;
    } else {
        POINT cur = {};
        GetCursorPos(&cur);
        HMONITOR mon = MonitorFromPoint(cur, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(mon, &mi);
        x = (mi.rcWork.left + mi.rcWork.right - px_w) / 2;
        y = (mi.rcWork.top + mi.rcWork.bottom - px_h) / 2;
    }

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kClassName,
        L"\u5173\u4e8e Octans",
        WS_POPUP | WS_CLIPCHILDREN,
        x, y, px_w, px_h,
        owner, nullptr, hinst, &st
    );
    if (!hwnd) return false;
    st.hwnd = hwnd;
    DisableWindowIme(hwnd);

    BOOL dark_val = is_dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, 20, &dark_val, sizeof(dark_val));
    DWORD corner = 2;
    DwmSetWindowAttribute(hwnd, 33, &corner, sizeof(corner));

    auto sc = [scale](float dp) { return static_cast<int>(roundf(dp * scale)); };
    st.font_btn = CreateFontW(
        -sc(12.8f), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_ROMAN,
        GetEditControlFontFamily()
    );

    auto mk_btn = [&](const D2D1_RECT_F &r, int id, const wchar_t *txt) -> HWND {
        HWND b = CreateWindowExW(
            0, L"BUTTON", txt,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            sc(r.left), sc(r.top), sc(r.right - r.left), sc(r.bottom - r.top),
            hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hinst, nullptr
        );
        if (b) {
            WNDPROC ob = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(b, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(AboutBtnSubclassProc)));
            SetWindowLongPtrW(b, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ob));
        }
        return b;
    };

    st.btn_secondary = mk_btn(st.sec_btn_rc, IDC_ABOUT_SECONDARY, L"\u9879\u76ee\u4e3b\u9875");
    st.btn_primary   = mk_btn(st.pri_btn_rc, IDC_ABOUT_PRIMARY, L"\u68c0\u67e5\u66f4\u65b0");
    st.btn_close     = mk_btn(st.close_btn_rc, IDC_ABOUT_CLOSE, L"\u00d7");

    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    RepaintAbout(st);

    MSG msg = {};
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            st.done = true;
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    InterlockedExchange(&st.cancel_flag, 1);
    KillTimer(hwnd, TIMER_ABOUT_PULSE);
    if (st.worker) {
        WaitForSingleObject(st.worker, 1500);
        CloseHandle(st.worker);
        st.worker = nullptr;
    }

    bool launched = st.launched_installer;
    st.rt.Reset();
    st.fmt_detail.Reset();
    st.fmt_status.Reset();
    st.fmt_meta.Reset();
    st.dwrite.Reset();
    st.d2d_factory.Reset();
    DestroyWindow(hwnd);
    if (st.font_btn) DeleteObject(st.font_btn);
    return launched;
}

} /* namespace octans_ui */
