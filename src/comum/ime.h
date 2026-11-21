// Composição do IME (coreano, japonês, chinês) direto num campo nosso que só cresce no fim (Spotlight, Launchpad).
// Sem isto o Windows abria a janelinha de composição dele em cima do campo e o texto só chegava (WM_CHAR) quando a
// sílaba era fechada: em coreano, só depois de digitar a próxima letra (pedido de 22/11/2026). Agora a sílaba em
// composição já aparece no fim do texto (e já busca), e é trocada pela definitiva quando o IME a fecha.
#pragma once
#include <windows.h>
#include <imm.h>
#include <string>
#pragma comment(lib, "imm32.lib")

namespace ime {

struct State { std::wstring comp; };  // a sílaba em composição que está no fim do texto agora

// tira a composição do fim do texto (só se ela ainda estiver lá: o texto pode ter sido limpo nesse meio tempo)
inline void Drop(std::wstring& q, State& st) {
    if (!st.comp.empty() && q.size() >= st.comp.size() && !q.compare(q.size() - st.comp.size(), st.comp.size(), st.comp))
        q.resize(q.size() - st.comp.size());
    st.comp.clear();
}

// Trata as mensagens do IME. true = era do IME (devolva *res). *changed = o texto mudou (refazer a busca/desenho).
inline bool Handle(HWND h, UINT m, WPARAM w, LPARAM l, std::wstring& q, State& st, bool* changed, LRESULT* res, size_t maxLen = 200) {
    *changed = false;
    switch (m) {
    case WM_IME_SETCONTEXT:  // sem a janelinha de composição do Windows (a sílaba aparece no nosso campo)
        *res = DefWindowProcW(h, m, w, l & ~ISC_SHOWUICOMPOSITIONWINDOW);
        return true;
    case WM_IME_STARTCOMPOSITION:
        *res = 0;
        return true;
    case WM_IME_COMPOSITION: {
        HIMC c = ImmGetContext(h);
        if (!c) return false;
        auto get = [c](DWORD which) {
            std::wstring s;
            LONG b = ImmGetCompositionStringW(c, which, nullptr, 0);
            if (b > 0) { s.resize(b / sizeof(wchar_t)); ImmGetCompositionStringW(c, which, &s[0], b); }
            return s;
        };
        // o coreano manda as duas juntas: a sílaba que fechou (resultado) e a nova que começou (composição)
        if (l & GCS_RESULTSTR) { Drop(q, st); q += get(GCS_RESULTSTR); *changed = true; }
        if (l & GCS_COMPSTR) { Drop(q, st); st.comp = get(GCS_COMPSTR); q += st.comp; *changed = true; }
        if (q.size() > maxLen) { q.resize(maxLen); st.comp.clear(); }
        ImmReleaseContext(h, c);
        *res = 0;  // sem DefWindowProc: senão o resultado chegaria de novo como WM_CHAR
        return true;
    }
    case WM_IME_ENDCOMPOSITION:  // cancelada (Esc no IME): a sílaba sai
        if (!st.comp.empty()) { Drop(q, st); *changed = true; }
        *res = 0;
        return true;
    }
    return false;
}

// a janela vai fechar: o IME esquece a composição (senão ela voltava na próxima abertura)
inline void Cancel(HWND h, State& st) {
    if (HIMC c = ImmGetContext(h)) { ImmNotifyIME(c, NI_COMPOSITIONSTR, CPS_CANCEL, 0); ImmReleaseContext(h, c); }
    st.comp.clear();
}

}  // namespace ime
