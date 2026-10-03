#include "ui_assets.h"
#include "texture_loader.h"

// Racine commune a tous les skins - un dossier par skin en dessous.
static const char* kUiSkinRoot = "HDD:\\X360RemapStudio\\ui_skin\\";

// --- Le manifeste ------------------------------------------------------------
// UN SEUL tableau, dans le MEME ordre que l'enum UiAssetId. C'est exactement
// le risque que CheckAssetTableIntegrity() verifie au demarrage : si une
// entree est inseree au mauvais endroit ici (comme ca a ete le cas dans la
// table i18n), l'id ne correspondra plus a sa position et ce sera detecte
// avant que quelqu'un le remarque a l'ecran.
//
// Taille et marges 9-slice PAR ROLE, pas un gabarit unique - voir le detail
// (et pourquoi) dans ui_assets.h et _design_refs/DESIGN_SYSTEM.md section 10.
// Chemins avec sous-dossier (panels/, buttons/, icons/) : BuildPath() ne fait
// qu'une concatenation, donc le "/" du nom de fichier suffit a ranger chaque
// famille dans son propre dossier sans code supplementaire.
const UiAssetSpec g_uiAssets[UI_ASSET_COUNT] = {
    { UI_ASSET_PANEL_DEFAULT, "panels/panel_default.png", 128, 128, 32, 32, 32, 32 },
    { UI_ASSET_PANEL_CTX,     "panels/panel_ctx.png",     128, 128, 32, 32, 32, 32 },
    { UI_ASSET_CARD_DEVICE,   "panels/card_device.png",   256, 64,  32, 20, 32, 20 },
    { UI_ASSET_CELL_GRID,     "panels/cell_grid.png",     128, 128, 24, 24, 24, 24 },
    { UI_ASSET_FIELD_COMBO,   "panels/field_combo.png",   128, 48,  16, 16, 16, 16 },
    { UI_ASSET_BUTTON_ACCENT, "buttons/button_accent.png", 128, 48, 20, 20, 20, 20 },
    { UI_ASSET_BUTTON_GHOST,  "buttons/button_ghost.png",  128, 48, 16, 16, 16, 16 },

    { UI_ASSET_ICON_KEYBOARD, "icons/icon_keyboard.png", 64, 64, 0, 0, 0, 0 },
    { UI_ASSET_ICON_MOUSE,    "icons/icon_mouse.png",    64, 64, 0, 0, 0, 0 },
    { UI_ASSET_ICON_GEAR,     "icons/icon_gear.png",     64, 64, 0, 0, 0, 0 },
    { UI_ASSET_ICON_PLAY,     "icons/icon_play.png",     64, 64, 0, 0, 0, 0 },
    { UI_ASSET_ICON_LIST,     "icons/icon_list.png",     64, 64, 0, 0, 0, 0 },
    { UI_ASSET_ICON_HELP,     "icons/icon_help.png",     64, 64, 0, 0, 0, 0 },
    { UI_ASSET_ICON_HEART,    "icons/icon_heart.png",    64, 64, 0, 0, 0, 0 },

    // Pas de 9-slice (marges 0) : l'image entiere est redimensionnee en
    // gardant ses proportions, comme les icones.
    { UI_ASSET_LOGO_WORDMARK, "brand/logo_wordmark.png", 1024, 92, 0, 0, 0, 0 },
};

int CheckAssetTableIntegrity() {
    for (int i = 0; i < UI_ASSET_COUNT; i++)
        if ((int)g_uiAssets[i].id != i)
            return i;
    return -1;
}

// Concatenation bornee a la main : ce fichier est console-only (xtl.h/d3d9.h,
// voir ui_assets.h) donc _snprintf serait recevable ici, mais on garde la
// meme discipline que le reste du projet pour rester coherent d'un fichier a
// l'autre.
static void BuildPath(const char* skinName, const char* fileName, char* out, int cap) {
    int n = 0;
    for (int i = 0; kUiSkinRoot[i] && n < cap - 1; i++) out[n++] = kUiSkinRoot[i];
    for (int i = 0; skinName[i] && n < cap - 1; i++) out[n++] = skinName[i];
    if (n < cap - 1) out[n++] = '\\';
    for (int i = 0; fileName[i] && n < cap - 1; i++) out[n++] = fileName[i];
    out[n] = '\0';
}

bool LoadUiSkin(IDirect3DDevice9* device, const char* skinName, UiSkin& out) {
    for (int i = 0; i < UI_ASSET_COUNT; i++) {
        out.tex[i] = 0; out.w[i] = out.h[i] = 0; out.present[i] = false;
    }
    if (!device || !skinName || !skinName[0]) return false;

    int found = 0;
    for (int i = 0; i < UI_ASSET_COUNT; i++) {
        char path[256];
        BuildPath(skinName, g_uiAssets[i].fileName, path, sizeof(path));

        IDirect3DTexture9* tex = 0;
        int w = 0, h = 0;
        // Absent = repli normal, pas une erreur : voir la regle de degrade
        // dans ui_assets.h. On ne journalise donc pas d'echec ici, seulement
        // le compte final (dans quel etat le skin a ete trouve).
        if (LoadTextureFromFile(device, path, &tex, &w, &h)) {
            out.tex[i] = tex; out.w[i] = w; out.h[i] = h; out.present[i] = true;
            found++;
        }
    }
    // Un skin partiel n'est pas un echec : chaque asset absent retombe sur le
    // rendu trace existant. On ne renvoie false que si RIEN n'a ete trouve,
    // ce qui signale plutot un nom de dossier errone qu'un skin incomplet.
    return found > 0;
}

void ReleaseUiSkin(UiSkin& skin) {
    for (int i = 0; i < UI_ASSET_COUNT; i++) {
        if (skin.tex[i]) skin.tex[i]->Release();
        skin.tex[i] = 0; skin.present[i] = false;
    }
}
