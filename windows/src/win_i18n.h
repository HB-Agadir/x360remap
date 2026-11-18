#pragma once
// Texts that exist only in the Windows build (status bar, dialogs, toasts), in
// the four languages of the Studio. The shared pages keep using Tr() from
// application/i18n.h; this helper follows the same current language.
#include "../../application/i18n.h"

inline const char* L4(const char* fr, const char* en, const char* es, const char* pt) {
    switch (GetLanguage()) {
        case LANG_FR:    return fr;
        case LANG_ES:    return es;
        case LANG_PT_BR: return pt;
        default:         return en;
    }
}
