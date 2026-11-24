#pragma once
// Dica (tooltip) do Mac ao parar o mouse num botão: um balão claro (escuro no tema escuro) de cantos 5, texto de 11,5
// e sombra curta, logo abaixo do ponteiro, depois de ~0,7 s parado (ou na hora, passando de um botão a outro com a
// dica já aberta). Janela em camadas própria (WacOSDica), sem foco e sem receber o mouse. Quem usa: o Quick Look do
// Shell (src\shell\quicklook.cpp) e o do Finder (issue #8: "mostrar o texto do que o ícone faz").
//   no WM_MOUSEMOVE: dica::Quer(hwnd, Tr(L"Fechar"), escuro)   (texto vazio = nenhum botão sob o mouse)
//   no WM_MOUSELEAVE / clique / fechar: dica::Esconde()
#include <windows.h>
#include "fonte_cjk.h" // a reserva para coreano, japonês e chinês
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <algorithm>
#include <cmath>
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace dica {

struct Estado { HWND dono = nullptr, wnd = nullptr; std::wstring quer, mostra; bool escuro = false; };
inline Estado& S() { static Estado s; return s; }
const UINT_PTR TIMER = 0x51D1;

inline void Esconde() {
    Estado& s = S();
    if (s.dono) KillTimer(s.dono, TIMER);
    s.quer.clear(); s.mostra.clear();
    if (s.wnd) ShowWindow(s.wnd, SW_HIDE);
}

inline IDWriteTextFormat* Fonte() {
    static IDWriteTextFormat* tf = nullptr;
    static bool feito = false;
    if (feito) return tf;
    feito = true;
    IDWriteFactory* dw = nullptr;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&dw)) || !dw) return nullptr;
    const wchar_t* fam = L"Segoe UI";
    IDWriteFontCollection* c = nullptr;
    if (SUCCEEDED(dw->GetSystemFontCollection(&c)) && c) {
        for (const wchar_t* f : { L"SF Pro Text", L"Inter" }) { UINT32 i; BOOL ex = FALSE; c->FindFamilyName(f, &i, &ex); if (ex) { fam = f; break; } }
        c->Release();
    }
    cjk::Format(dw, fam, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.5f, L"", &tf);
    if (tf) { tf->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP); tf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER); }
    dw->Release();
    return tf;
}

inline LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (m == WM_NCHITTEST) return HTTRANSPARENT;
    return DefWindowProcW(h, m, w, l);
}

inline void Mostra() {
    Estado& s = S();
    if (s.quer.empty()) return;
    IDWriteTextFormat* tf = Fonte();
    static ID2D1Factory* d2d = nullptr;
    if (!d2d) D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d);
    if (!tf || !d2d) return;
    if (!s.wnd) {
        HINSTANCE hi = GetModuleHandleW(nullptr);
        WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = hi; wc.lpszClassName = L"WacOSDica";
        RegisterClassW(&wc);
        s.wnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_TOPMOST, L"WacOSDica", L"",
                                WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, hi, nullptr);
        if (!s.wnd) return;
    }
    POINT cur; GetCursorPos(&cur);
    HMONITOR mon = MonitorFromPoint(cur, MONITOR_DEFAULTTONEAREST);
    UINT dpi = s.dono ? GetDpiForWindow(s.dono) : 96;
    float sc = (dpi ? dpi : 96) / 96.f;
    // largura do texto
    float tw = 60;
    IDWriteFactory* dw = nullptr;
    if (SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&dw)) && dw) {
        IDWriteTextLayout* lay = nullptr;
        if (SUCCEEDED(dw->CreateTextLayout(s.quer.c_str(), (UINT32)s.quer.size(), tf, 2000, 40, &lay)) && lay) {
            DWRITE_TEXT_METRICS tm{}; lay->GetMetrics(&tm); tw = tm.widthIncludingTrailingWhitespace; lay->Release();
        }
        dw->Release();
    }
    const float SOM = 5;  // margem da sombra
    float W = std::ceil(tw) + 14 + 2 * SOM, H = 19 + 2 * SOM;
    int w = (int)std::lround(W * sc), h = (int)std::lround(H * sc);
    MONITORINFO mi{ sizeof(mi) }; GetMonitorInfoW(mon, &mi);
    int x = cur.x - (int)std::lround((SOM + 4) * sc), y = cur.y + (int)std::lround(20 * sc) - (int)std::lround(SOM * sc);
    x = std::clamp(x, (int)mi.rcWork.left, (int)mi.rcWork.right - w);
    if (y + h > mi.rcWork.bottom) y = cur.y - h - (int)std::lround(4 * sc);  // sem lugar embaixo: em cima do ponteiro
    HDC screen = GetDC(nullptr), mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{}; bi.bmiHeader = { sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB };
    void* bits = nullptr; HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mem, bmp);
    ID2D1DCRenderTarget* rt = nullptr;
    auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96 * sc, 96 * sc);
    if (SUCCEEDED(d2d->CreateDCRenderTarget(&props, &rt)) && rt) {
        RECT rb{ 0, 0, w, h }; rt->BindDC(mem, &rb);
        rt->BeginDraw(); rt->Clear(D2D1::ColorF(0, 0, 0, 0));
        ID2D1SolidColorBrush* b = nullptr; rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.05f), &b);
        if (b) {
            D2D1_RECT_F r = D2D1::RectF(SOM, SOM, W - SOM, H - SOM);
            for (int k = 4; k >= 1; k--) {  // sombra curta, mais forte embaixo
                b->SetColor(D2D1::ColorF(0, 0, 0, s.escuro ? 0.10f : 0.05f));
                rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left - k * 0.6f, r.top - k * 0.3f, r.right + k * 0.6f, r.bottom + k * 0.9f), 5 + k * 0.6f, 5 + k * 0.6f), b);
            }
            b->SetColor(s.escuro ? D2D1::ColorF(0.17f, 0.17f, 0.18f, 0.98f) : D2D1::ColorF(0.965f, 0.965f, 0.965f, 0.98f));
            rt->FillRoundedRectangle(D2D1::RoundedRect(r, 5, 5), b);
            b->SetColor(s.escuro ? D2D1::ColorF(1, 1, 1, 0.16f) : D2D1::ColorF(0, 0, 0, 0.14f));
            rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left + 0.25f, r.top + 0.25f, r.right - 0.25f, r.bottom - 0.25f), 4.75f, 4.75f), b, 0.5f);
            b->SetColor(s.escuro ? D2D1::ColorF(1, 1, 1, 0.92f) : D2D1::ColorF(0, 0, 0, 0.86f));
            rt->DrawTextW(s.quer.c_str(), (UINT32)s.quer.size(), tf, D2D1::RectF(r.left + 7, r.top, r.right, r.bottom), b);
            b->Release();
        }
        rt->EndDraw(); rt->Release();
    }
    POINT dst{ x, y }, src{ 0, 0 }; SIZE sz{ w, h };
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(s.wnd, screen, &dst, &sz, mem, &src, 0, &bf, ULW_ALPHA);
    SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem); ReleaseDC(nullptr, screen);
    SetWindowPos(s.wnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    s.mostra = s.quer;
}

inline void CALLBACK Tempo(HWND h, UINT, UINT_PTR id, DWORD) { KillTimer(h, id); Mostra(); }

// o botão sob o mouse mudou (ou continua o mesmo: não faz nada). Texto vazio esconde.
inline void Quer(HWND dono, const std::wstring& texto, bool escuro) {
    Estado& s = S();
    if (texto == s.quer && dono == s.dono) return;
    bool aberta = !s.mostra.empty();
    Esconde();
    s.dono = dono; s.quer = texto; s.escuro = escuro;
    if (texto.empty() || !dono) return;
    SetTimer(dono, TIMER, aberta ? 30 : 700, Tempo);  // já tinha dica aberta: a do botão vizinho aparece na hora
}

}  // namespace dica
