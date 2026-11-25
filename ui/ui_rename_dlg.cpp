#include "ui_rename_dlg.h"
#include "ui_theme.h"
#include "ui_fonts.h"

namespace octans_ui {

static constexpr int IDC_RENAME_EDIT = 101;

struct RenameSheetState {
    bool is_dark = true;
    bool accepted = false;
    bool done = false;
    bool backdrop_ok = false;
    UINT dpi = 96;
    float logical_w = 336.0f;
    float logical_h = 166.0f;

    int hover_btn = 0;   /* 0=none, 1=cancel, 2=save */
    int pressed_btn = 0; /* 0=none, 1=cancel, 2=save */
    bool mouse_tracked = false;

    D2D1_RECT_F input_box_rc = {};
    D2D1_RECT_F cancel_btn_rc = {};
    D2D1_RECT_F save_btn_rc = {};

    wchar_t result[64] = {};
    HWND hwnd = nullptr;
    HWND edit = nullptr;
    HFONT font = nullptr;
    HBRUSH edit_brush = nullptr;

    ComPtr<ID2D1Factory> d2d_factory;
    ComPtr<ID2D1DCRenderTarget> rt;
    ComPtr<IDWriteFactory> dwrite;
    ComPtr<IDWriteTextFormat> fmt_title;
    ComPtr<IDWriteTextFormat> fmt_sub;
    ComPtr<IDWriteTextFormat> fmt_btn;
};

static bool PtInRc(const D2D1_RECT_F &r, float x, float y) {
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

static bool EnsureSheetResources(RenameSheetState &st) {
    if (!st.d2d_factory) {
        D2D1_FACTORY_OPTIONS fo = {};
        if (FAILED(D2D1CreateFactory(
                D2D1_FACTORY_TYPE_SINGLE_THREADED,
                __uuidof(ID2D1Factory),
                &fo,
                st.d2d_factory.PutVoid()))) {
            return false;
        }
    }
    if (!st.dwrite) {
        if (FAILED(DWriteCreateFactory(
                DWRITE_FACTORY_TYPE_SHARED,
                __uuidof(IDWriteFactory),
                reinterpret_cast<IUnknown **>(st.dwrite.Put())))) {
            return false;
        }
        auto make_fmt = [&](ComPtr<IDWriteTextFormat> &out, DWRITE_FONT_WEIGHT w, float sz, DWRITE_TEXT_ALIGNMENT al) {
            CreateOctansTextFormat(st.dwrite.Get(), w, sz, al, DWRITE_FONT_STRETCH_NORMAL, false, out);
        };
        make_fmt(st.fmt_title, DWRITE_FONT_WEIGHT_SEMI_BOLD, 15.0f, DWRITE_TEXT_ALIGNMENT_LEADING);
        make_fmt(st.fmt_sub,   DWRITE_FONT_WEIGHT_REGULAR,   11.5f, DWRITE_TEXT_ALIGNMENT_LEADING);
        make_fmt(st.fmt_btn,   DWRITE_FONT_WEIGHT_SEMI_BOLD, 12.5f, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    if (!st.rt) {
        D2D1_RENDER_TARGET_PROPERTIES rtp = {};
        rtp.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
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

static void RenderRenameSheetToDC(RenameSheetState &st, HDC target_dc) {
    if (!target_dc || !st.hwnd || !EnsureSheetResources(st)) return;

    RECT crc = {};
    GetClientRect(st.hwnd, &crc);
    int pw = crc.right - crc.left;
    int ph = crc.bottom - crc.top;
    if (pw < 2 || ph < 2) return;

    /* Double-buffered offscreen DC -> zero flicker with child EDIT control */
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

        D2D1_COLOR_F sheet_bg = st.is_dark ? HexColor(0x222226, 1.0f) : HexColor(0xF8F8FA, 1.0f);
        rt->Clear(sheet_bg);

        ComPtr<ID2D1SolidColorBrush> brush;
        rt->CreateSolidColorBrush(pal.text_primary, brush.Put());
        if (brush) {
            /* Subtle hairline outer border */
            brush->SetColor(pal.panel_outer_border);
            rt->DrawRoundedRectangle(
                MakeRoundedRect(MakeRect(0.5f, 0.5f, st.logical_w - 0.5f, st.logical_h - 0.5f), 7.5f, 7.5f),
                brush.Get(), 1.0f
            );

            /* Title & Subtitle */
            brush->SetColor(pal.text_primary);
            rt->DrawText(
                L"\u91cd\u547d\u540d\u8bbe\u5907", 5,
                st.fmt_title.Get(),
                MakeRect(20.0f, 16.0f, st.logical_w - 20.0f, 38.0f),
                brush.Get()
            );

            brush->SetColor(pal.text_secondary);
            rt->DrawText(
                L"\u81ea\u5b9a\u4e49\u5728\u9762\u677f\u4e0e\u6258\u76d8\u4e2d\u663e\u793a\u7684\u540d\u79f0", 15,
                st.fmt_sub.Get(),
                MakeRect(20.0f, 38.0f, st.logical_w - 20.0f, 56.0f),
                brush.Get()
            );

            /* Modern Apple-style rounded input box with green focus accent ring */
            D2D1_ROUNDED_RECT inp_rr = MakeRoundedRect(st.input_box_rc, 10.0f, 10.0f);
            brush->SetColor(st.is_dark ? HexColor(0x2E2E33, 1.0f) : HexColor(0xFFFFFF, 1.0f));
            rt->FillRoundedRectangle(inp_rr, brush.Get());

            brush->SetColor(st.is_dark ? HexColor(0x30D158, 0.80f) : HexColor(0x34C759, 0.88f));
            rt->DrawRoundedRectangle(
                MakeRoundedRect(
                    MakeRect(st.input_box_rc.left + 0.75f, st.input_box_rc.top + 0.75f,
                             st.input_box_rc.right - 0.75f, st.input_box_rc.bottom - 0.75f),
                    9.5f, 9.5f
                ),
                brush.Get(), 1.5f
            );

            /* Cancel button (soft frosted pill) */
            {
                D2D1_COLOR_F bg = st.is_dark
                    ? (st.pressed_btn == 1 ? MakeColor(1, 1, 1, 0.16f) : (st.hover_btn == 1 ? MakeColor(1, 1, 1, 0.12f) : MakeColor(1, 1, 1, 0.08f)))
                    : (st.pressed_btn == 1 ? MakeColor(0, 0, 0, 0.12f) : (st.hover_btn == 1 ? MakeColor(0, 0, 0, 0.08f) : MakeColor(0, 0, 0, 0.05f)));
                brush->SetColor(bg);
                rt->FillRoundedRectangle(MakeRoundedRect(st.cancel_btn_rc, 10.0f, 10.0f), brush.Get());

                brush->SetColor(pal.card_border);
                rt->DrawRoundedRectangle(
                    MakeRoundedRect(
                        MakeRect(st.cancel_btn_rc.left + 0.5f, st.cancel_btn_rc.top + 0.5f,
                                 st.cancel_btn_rc.right - 0.5f, st.cancel_btn_rc.bottom - 0.5f),
                        9.5f, 9.5f
                    ),
                    brush.Get(), 1.0f
                );

                brush->SetColor(pal.text_primary);
                rt->DrawText(L"\u53d6\u6d88", 2, st.fmt_btn.Get(), st.cancel_btn_rc, brush.Get());
            }

            /* Save primary button (Apple green pill) */
            {
                D2D1_COLOR_F base = st.is_dark ? HexColor(0x30D158, 1.0f) : HexColor(0x34C759, 1.0f);
                if (st.pressed_btn == 2) {
                    base = st.is_dark ? HexColor(0x26AE48, 1.0f) : HexColor(0x29A84A, 1.0f);
                } else if (st.hover_btn == 2) {
                    base = st.is_dark ? HexColor(0x3BE066, 1.0f) : HexColor(0x3BD662, 1.0f);
                }
                brush->SetColor(base);
                rt->FillRoundedRectangle(MakeRoundedRect(st.save_btn_rc, 10.0f, 10.0f), brush.Get());

                brush->SetColor(MakeColor(1.0f, 1.0f, 1.0f, 0.99f));
                rt->DrawText(L"\u4fdd\u5b58", 2, st.fmt_btn.Get(), st.save_btn_rc, brush.Get());
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

static void RenderRenameSheet(RenameSheetState &st) {
    if (st.hwnd) {
        InvalidateRect(st.hwnd, nullptr, FALSE);
        UpdateWindow(st.hwnd);
    }
}

static void ConfirmSheet(RenameSheetState &st) {
    GetWindowTextW(st.edit, st.result, 64);
    st.accepted = true;
    st.done = true;
}

static LRESULT CALLBACK RenameEditSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto orig = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_KEYDOWN) {
        if (wp == VK_RETURN) {
            SendMessageW(GetParent(hwnd), WM_APP + 1, 1, 0);
            return 0;
        }
        if (wp == VK_ESCAPE) {
            SendMessageW(GetParent(hwnd), WM_APP + 1, 0, 0);
            return 0;
        }
    }
    return orig ? CallWindowProcW(orig, hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK RenameSheetProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto *st = reinterpret_cast<RenameSheetState *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_NCCREATE: {
            auto *cs = reinterpret_cast<CREATESTRUCTW *>(lp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return TRUE;
        }
        case WM_APP + 1:
            if (st) {
                if (wp == 1) ConfirmSheet(*st);
                else { st->accepted = false; st->done = true; }
            }
            return 0;

        case WM_CTLCOLOREDIT:
            if (st) {
                HDC hdc = reinterpret_cast<HDC>(wp);
                SetBkColor(hdc, st->is_dark ? RGB(46, 46, 51) : RGB(255, 255, 255));
                SetTextColor(hdc, st->is_dark ? RGB(245, 245, 247) : RGB(28, 28, 30));
                return reinterpret_cast<LRESULT>(st->edit_brush);
            }
            break;

        case WM_MOUSEMOVE:
            if (st) {
                if (!st->mouse_tracked) {
                    TRACKMOUSEEVENT tme = {};
                    tme.cbSize = sizeof(tme);
                    tme.dwFlags = TME_LEAVE;
                    tme.hwndTrack = hwnd;
                    TrackMouseEvent(&tme);
                    st->mouse_tracked = true;
                }
                float scale = static_cast<float>(st->dpi) / 96.0f;
                float dx = static_cast<float>(static_cast<short>(LOWORD(lp))) / scale;
                float dy = static_cast<float>(static_cast<short>(HIWORD(lp))) / scale;
                int nh = 0;
                if (PtInRc(st->cancel_btn_rc, dx, dy)) nh = 1;
                else if (PtInRc(st->save_btn_rc, dx, dy)) nh = 2;
                if (nh != st->hover_btn) {
                    st->hover_btn = nh;
                    SetCursor(LoadCursorW(nullptr, nh ? IDC_HAND : IDC_ARROW));
                    RenderRenameSheet(*st);
                }
            }
            return 0;

        case WM_MOUSELEAVE:
            if (st) {
                st->mouse_tracked = false;
                if (st->hover_btn != 0 || st->pressed_btn != 0) {
                    st->hover_btn = 0;
                    st->pressed_btn = 0;
                    RenderRenameSheet(*st);
                }
            }
            return 0;

        case WM_LBUTTONDOWN:
            if (st) {
                float scale = static_cast<float>(st->dpi) / 96.0f;
                float dx = static_cast<float>(static_cast<short>(LOWORD(lp))) / scale;
                float dy = static_cast<float>(static_cast<short>(HIWORD(lp))) / scale;
                if (PtInRc(st->cancel_btn_rc, dx, dy)) {
                    st->pressed_btn = 1;
                    SetCapture(hwnd);
                    RenderRenameSheet(*st);
                } else if (PtInRc(st->save_btn_rc, dx, dy)) {
                    st->pressed_btn = 2;
                    SetCapture(hwnd);
                    RenderRenameSheet(*st);
                } else if (PtInRc(st->input_box_rc, dx, dy)) {
                    SetFocus(st->edit);
                } else {
                    /* Drag smoothly by header/background */
                    ReleaseCapture();
                    SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
                }
            }
            return 0;

        case WM_LBUTTONUP:
            if (st && st->pressed_btn != 0) {
                int p = st->pressed_btn;
                st->pressed_btn = 0;
                ReleaseCapture();
                float scale = static_cast<float>(st->dpi) / 96.0f;
                float dx = static_cast<float>(static_cast<short>(LOWORD(lp))) / scale;
                float dy = static_cast<float>(static_cast<short>(HIWORD(lp))) / scale;
                RenderRenameSheet(*st);
                if (p == 1 && PtInRc(st->cancel_btn_rc, dx, dy)) {
                    st->accepted = false;
                    st->done = true;
                } else if (p == 2 && PtInRc(st->save_btn_rc, dx, dy)) {
                    ConfirmSheet(*st);
                }
            }
            return 0;

        case WM_ACTIVATE:
            if (st && LOWORD(wp) == WA_INACTIVE) {
                st->accepted = false;
                st->done = true;
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps = {};
            HDC hdc = BeginPaint(hwnd, &ps);
            if (st) RenderRenameSheetToDC(*st, hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_CLOSE:
            if (st) {
                st->accepted = false;
                st->done = true;
            }
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool ShowRenameAliasDialog(
    HWND owner,
    bool is_dark,
    const wchar_t *current_name,
    wchar_t *out_alias,
    size_t out_cap
) {
    if (!out_alias || out_cap == 0) return false;

    static const wchar_t *kClassName = L"OctansRenameSheetWnd";
    static bool registered = false;
    HINSTANCE hinst = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = RenameSheetProc;
        wc.hInstance = hinst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        RegisterClassExW(&wc);
        registered = true;
    }

    RenameSheetState st = {};
    st.is_dark = is_dark;
    st.dpi = GetWindowDpiSafe(owner);
    float scale = static_cast<float>(st.dpi) / 96.0f;

    st.input_box_rc  = MakeRect(20.0f, 66.0f, st.logical_w - 20.0f, 102.0f);
    st.save_btn_rc   = MakeRect(st.logical_w - 20.0f - 80.0f, 118.0f, st.logical_w - 20.0f, 150.0f);
    st.cancel_btn_rc = MakeRect(st.save_btn_rc.left - 10.0f - 80.0f, 118.0f, st.save_btn_rc.left - 10.0f, 150.0f);

    int px_w = static_cast<int>(ceilf(st.logical_w * scale));
    int px_h = static_cast<int>(ceilf(st.logical_h * scale));

    /* Center neatly over the flyout panel (or active monitor work area) */
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

    st.edit_brush = CreateSolidBrush(is_dark ? RGB(46, 46, 51) : RGB(255, 255, 255));

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kClassName,
        L"\u91cd\u547d\u540d\u8bbe\u5907",
        WS_POPUP | WS_CLIPCHILDREN,
        x, y, px_w, px_h,
        owner,
        nullptr,
        hinst,
        &st
    );
    if (!hwnd) {
        DeleteObject(st.edit_brush);
        return false;
    }
    st.hwnd = hwnd;

    /* Native Win11 rounded corners + shadow + immersive dark mode */
    BOOL dark_val = is_dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, 20, &dark_val, sizeof(dark_val));
    DWORD corner = 2; /* DWMWCP_ROUND */
    DwmSetWindowAttribute(hwnd, 33, &corner, sizeof(corner));

    auto sc = [scale](float dp) { return static_cast<int>(roundf(dp * scale)); };

    st.font = CreateFontW(
        -sc(14.5f), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_ROMAN,
        GetEditControlFontFamily()
    );

    /* Borderless native EDIT nested inside our Direct2D rounded input pill */
    st.edit = CreateWindowExW(
        0,
        L"EDIT",
        current_name ? current_name : L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        sc(st.input_box_rc.left + 12.0f),
        sc(st.input_box_rc.top + 9.0f),
        sc((st.input_box_rc.right - st.input_box_rc.left) - 24.0f),
        sc(20.0f),
        hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RENAME_EDIT)),
        hinst,
        nullptr
    );

    SendMessageW(st.edit, WM_SETFONT, reinterpret_cast<WPARAM>(st.font), TRUE);
    SendMessageW(st.edit, EM_SETLIMITTEXT, 63, 0);

    WNDPROC old_edit = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(st.edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(RenameEditSubclassProc)));
    SetWindowLongPtrW(st.edit, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(old_edit));

    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    RenderRenameSheet(st);
    SetFocus(st.edit);
    SendMessageW(st.edit, EM_SETSEL, 0, -1);

    MSG msg = {};
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            st.accepted = false;
            st.done = true;
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    st.rt.Reset();
    st.fmt_btn.Reset();
    st.fmt_sub.Reset();
    st.fmt_title.Reset();
    st.dwrite.Reset();
    st.d2d_factory.Reset();
    DestroyWindow(hwnd);
    if (st.font) DeleteObject(st.font);
    if (st.edit_brush) DeleteObject(st.edit_brush);

    if (st.accepted) {
        const wchar_t *s = st.result;
        while (*s == L' ' || *s == L'\t') ++s;
        size_t len = wcslen(s);
        while (len > 0 && (s[len - 1] == L' ' || s[len - 1] == L'\t')) --len;
        if (len > 0) {
            if (len >= out_cap) len = out_cap - 1;
            wmemcpy(out_alias, s, len);
            out_alias[len] = L'\0';
            return true;
        }
    }
    return false;
}

} /* namespace octans_ui */
