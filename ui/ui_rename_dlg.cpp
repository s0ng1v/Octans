#include "ui_rename_dlg.h"
#include "ui_theme.h"
#include "ui_fonts.h"

namespace octans_ui {

static constexpr int IDC_RENAME_EDIT   = 101;
static constexpr int IDC_RENAME_CANCEL = IDCANCEL;
static constexpr int IDC_RENAME_SAVE   = IDOK;

struct RenameSheetState {
    bool is_dark = true;
    bool accepted = false;
    bool done = false;
    UINT dpi = 96;
    float logical_w = 336.0f;
    float logical_h = 166.0f;

    int hover_btn = 0; /* 0=none, 1=cancel, 2=save */

    D2D1_RECT_F input_box_rc = {};
    D2D1_RECT_F cancel_btn_rc = {};
    D2D1_RECT_F save_btn_rc = {};

    wchar_t result[64] = {};
    HWND hwnd = nullptr;
    HWND edit = nullptr;
    HWND btn_cancel = nullptr;
    HWND btn_save = nullptr;
    HFONT font_edit = nullptr;
    HFONT font_btn = nullptr;
    HBRUSH edit_brush = nullptr;

    ComPtr<ID2D1Factory> d2d_factory;
    ComPtr<ID2D1DCRenderTarget> rt;
    ComPtr<IDWriteFactory> dwrite;
    ComPtr<IDWriteTextFormat> fmt_title;
    ComPtr<IDWriteTextFormat> fmt_sub;
};

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

static void RenderRenameSheetToDC(RenameSheetState &st, HDC target_dc) {
    if (!target_dc || !st.hwnd || !EnsureSheetResources(st)) return;

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

        D2D1_COLOR_F sheet_bg = st.is_dark ? HexColor(0x222226, 1.0f) : HexColor(0xF8F8FA, 1.0f);
        rt->Clear(sheet_bg);

        ComPtr<ID2D1SolidColorBrush> brush;
        rt->CreateSolidColorBrush(pal.text_primary, brush.Put());
        if (brush) {
            brush->SetColor(pal.panel_outer_border);
            rt->DrawRoundedRectangle(
                MakeRoundedRect(MakeRect(0.5f, 0.5f, st.logical_w - 0.5f, st.logical_h - 0.5f), 7.5f, 7.5f),
                brush.Get(), 1.0f
            );

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

static void DrawOwnerDrawPillButton(const RenameSheetState &st, const DRAWITEMSTRUCT *dis) {
    if (!dis) return;
    bool is_save = (dis->CtlID == IDC_RENAME_SAVE);
    bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    bool hovered = (st.hover_btn == (is_save ? 2 : 1));
    RECT rc = dis->rcItem;
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    HDC hdc = dis->hDC;
    COLORREF bg_parent = st.is_dark ? RGB(34, 34, 38) : RGB(248, 248, 250);
    HBRUSH pb = CreateSolidBrush(bg_parent);
    FillRect(hdc, &rc, pb);
    DeleteObject(pb);

    COLORREF fill_col;
    COLORREF border_col;
    COLORREF txt_col;
    if (is_save) {
        if (pressed) fill_col = st.is_dark ? RGB(38, 174, 72) : RGB(41, 168, 74);
        else if (hovered) fill_col = st.is_dark ? RGB(59, 224, 102) : RGB(59, 214, 98);
        else fill_col = st.is_dark ? RGB(48, 209, 88) : RGB(52, 199, 89);
        border_col = fill_col;
        txt_col = RGB(255, 255, 255);
    } else {
        if (pressed) fill_col = st.is_dark ? RGB(66, 66, 72) : RGB(218, 218, 224);
        else if (hovered) fill_col = st.is_dark ? RGB(58, 58, 64) : RGB(228, 228, 234);
        else fill_col = st.is_dark ? RGB(48, 48, 54) : RGB(236, 236, 241);
        border_col = st.is_dark ? RGB(72, 72, 78) : RGB(214, 214, 220);
        txt_col = st.is_dark ? RGB(245, 245, 247) : RGB(28, 28, 30);
    }

    int rad = static_cast<int>(roundf(10.0f * static_cast<float>(st.dpi) / 96.0f));
    HBRUSH fb = CreateSolidBrush(fill_col);
    HPEN fp = CreatePen(PS_SOLID, 1, border_col);
    HGDIOBJ old_b = SelectObject(hdc, fb);
    HGDIOBJ old_p = SelectObject(hdc, fp);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, rad * 2, rad * 2);
    SelectObject(hdc, old_b);
    SelectObject(hdc, old_p);
    DeleteObject(fb);
    DeleteObject(fp);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, txt_col);
    HGDIOBJ old_f = st.font_btn ? SelectObject(hdc, st.font_btn) : nullptr;
    const wchar_t *label = is_save ? L"\u4fdd\u5b58" : L"\u53d6\u6d88";
    DrawTextW(hdc, label, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (old_f) SelectObject(hdc, old_f);
}

static void ConfirmSheet(RenameSheetState &st) {
    if (st.edit) {
        GetWindowTextW(st.edit, st.result, 64);
    }
    st.accepted = true;
    st.done = true;
}

static LRESULT CALLBACK RenameBtnSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto orig = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    HWND parent = GetParent(hwnd);
    auto *st = parent ? reinterpret_cast<RenameSheetState *>(GetWindowLongPtrW(parent, GWLP_USERDATA)) : nullptr;
    int id = GetDlgCtrlID(hwnd);

    if (msg == WM_SETCURSOR) {
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
    }
    if (msg == WM_MOUSEMOVE && st) {
        int want = (id == IDC_RENAME_SAVE) ? 2 : 1;
        if (st->hover_btn != want) {
            st->hover_btn = want;
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    } else if (msg == WM_MOUSELEAVE && st) {
        int want = (id == IDC_RENAME_SAVE) ? 2 : 1;
        if (st->hover_btn == want) {
            st->hover_btn = 0;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    }
    return orig ? CallWindowProcW(orig, hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK RenameEditSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto orig = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_KEYDOWN) {
        if (wp == VK_RETURN) {
            SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(IDC_RENAME_SAVE, BN_CLICKED), 0);
            return 0;
        }
        if (wp == VK_ESCAPE) {
            SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(IDC_RENAME_CANCEL, BN_CLICKED), 0);
            return 0;
        }
    }
    if (msg == WM_CHAR && (wp == L'\r' || wp == 27)) {
        return 0;
    }
    return orig ? CallWindowProcW(orig, hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK RenameSheetProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto *st = reinterpret_cast<RenameSheetState *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_NCCREATE: {
            auto *cs = reinterpret_cast<CREATESTRUCTW *>(lp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return DefWindowProcW(hwnd, msg, wp, lp);
        }

        case WM_COMMAND:
            if (st) {
                int ctl = LOWORD(wp);
                if (ctl == IDC_RENAME_SAVE) {
                    ConfirmSheet(*st);
                    return 0;
                }
                if (ctl == IDC_RENAME_CANCEL) {
                    st->accepted = false;
                    st->done = true;
                    return 0;
                }
            }
            break;

        case WM_DRAWITEM:
            if (st) {
                DrawOwnerDrawPillButton(*st, reinterpret_cast<const DRAWITEMSTRUCT *>(lp));
                return TRUE;
            }
            break;

        case WM_CTLCOLOREDIT:
            if (st) {
                HDC hdc = reinterpret_cast<HDC>(wp);
                SetBkColor(hdc, st->is_dark ? RGB(46, 46, 51) : RGB(255, 255, 255));
                SetTextColor(hdc, st->is_dark ? RGB(245, 245, 247) : RGB(28, 28, 30));
                return reinterpret_cast<LRESULT>(st->edit_brush);
            }
            break;

        case WM_LBUTTONDOWN:
            if (st) {
                float scale = static_cast<float>(st->dpi) / 96.0f;
                float dx = static_cast<float>(static_cast<short>(LOWORD(lp))) / scale;
                float dy = static_cast<float>(static_cast<short>(HIWORD(lp))) / scale;
                if (dx >= st->input_box_rc.left && dx <= st->input_box_rc.right &&
                    dy >= st->input_box_rc.top && dy <= st->input_box_rc.bottom) {
                    SetFocus(st->edit);
                } else {
                    SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
                }
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

    BOOL dark_val = is_dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, 20, &dark_val, sizeof(dark_val));
    DWORD corner = 2; /* DWMWCP_ROUND */
    DwmSetWindowAttribute(hwnd, 33, &corner, sizeof(corner));

    auto sc = [scale](float dp) { return static_cast<int>(roundf(dp * scale)); };

    st.font_edit = CreateFontW(
        -sc(14.5f), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_ROMAN,
        GetEditControlFontFamily()
    );
    st.font_btn = CreateFontW(
        -sc(13.0f), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_ROMAN,
        GetEditControlFontFamily()
    );

    st.edit = CreateWindowExW(
        0,
        L"EDIT",
        current_name ? current_name : L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
        sc(st.input_box_rc.left + 12.0f),
        sc(st.input_box_rc.top + 9.0f),
        sc((st.input_box_rc.right - st.input_box_rc.left) - 24.0f),
        sc(20.0f),
        hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RENAME_EDIT)),
        hinst,
        nullptr
    );
    SendMessageW(st.edit, WM_SETFONT, reinterpret_cast<WPARAM>(st.font_edit), TRUE);
    SendMessageW(st.edit, EM_SETLIMITTEXT, 63, 0);

    WNDPROC old_edit = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(st.edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(RenameEditSubclassProc)));
    SetWindowLongPtrW(st.edit, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(old_edit));

    /* Real native Win32 owner-draw BUTTON controls -> unbreakable clicks + zero IME/focus cancellation! */
    st.btn_cancel = CreateWindowExW(
        0, L"BUTTON", L"\u53d6\u6d88",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
        sc(st.cancel_btn_rc.left), sc(st.cancel_btn_rc.top),
        sc(st.cancel_btn_rc.right - st.cancel_btn_rc.left),
        sc(st.cancel_btn_rc.bottom - st.cancel_btn_rc.top),
        hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RENAME_CANCEL)),
        hinst, nullptr
    );
    st.btn_save = CreateWindowExW(
        0, L"BUTTON", L"\u4fdd\u5b58",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
        sc(st.save_btn_rc.left), sc(st.save_btn_rc.top),
        sc(st.save_btn_rc.right - st.save_btn_rc.left),
        sc(st.save_btn_rc.bottom - st.save_btn_rc.top),
        hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RENAME_SAVE)),
        hinst, nullptr
    );
    if (st.btn_cancel) {
        WNDPROC ob = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(st.btn_cancel, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(RenameBtnSubclassProc)));
        SetWindowLongPtrW(st.btn_cancel, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ob));
    }
    if (st.btn_save) {
        WNDPROC ob = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(st.btn_save, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(RenameBtnSubclassProc)));
        SetWindowLongPtrW(st.btn_save, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ob));
    }

    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
    UpdateWindow(hwnd);
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
    st.fmt_sub.Reset();
    st.fmt_title.Reset();
    st.dwrite.Reset();
    st.d2d_factory.Reset();
    DestroyWindow(hwnd);
    if (st.font_edit) DeleteObject(st.font_edit);
    if (st.font_btn) DeleteObject(st.font_btn);
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
