#include "texture_loader.h"

#include <fstream>
#include <vector>

#include "third_party/imgui/imgui.h" // IM_COL32 - voir le commentaire sur l'ordre des octets

// Declarations seules : l'implementation de stb_image est compilee une unique
// fois dans stb_image_impl.cpp (voir ce fichier pour le pourquoi).
#define STBI_NO_STDIO
#include "../stb/stb_image.h"

bool LoadTextureFromMemory(IDirect3DDevice9* device,
                           const void* data, int len,
                           IDirect3DTexture9** outTex,
                           int* outW, int* outH) {
    if (outTex) *outTex = nullptr;
    if (!device || !data || len <= 0 || !outTex)
        return false;

    int w = 0, h = 0, channels = 0;
    // 4 = on force RGBA en sortie, quel que soit le format d'entree (PNG
    // indexe, gris, RGB sans transparence...). Evite d'avoir a gerer tous les
    // cas ici.
    stbi_uc* pixels = stbi_load_from_memory((const stbi_uc*)data, len, &w, &h, &channels, 4);
    if (!pixels)
        return false;

    IDirect3DTexture9* tex = nullptr;
    // Rigoureusement les memes parametres que l'atlas de police
    // (ImGui_ImplDX9_CreateFontsTexture) : c'est la seule combinaison dont on
    // sait qu'elle s'affiche correctement sur console. Voir texture_loader.h
    // pour l'explication du format LINEAIRE.
    if (device->CreateTexture(w, h, 1, D3DUSAGE_CPU_CACHED_MEMORY,
                              D3DFMT_LIN_A8R8G8B8, D3DPOOL_DEFAULT, &tex, NULL) < 0) {
        stbi_image_free(pixels);
        return false;
    }

    D3DLOCKED_RECT rect;
    if (tex->LockRect(0, &rect, NULL, 0) != D3D_OK) {
        tex->Release();
        stbi_image_free(pixels);
        return false;
    }

    // ORDRE DES OCTETS - le point delicat, et la raison de passer par IM_COL32.
    //
    // stb_image rend les octets dans l'ordre R, G, B, A. La texture, elle,
    // attend un entier 32 bits par pixel, dont la decomposition en octets
    // depend du boutisme - et la Xbox 360 est GROS-BOUTISTE, contrairement au
    // PC. Raisonner dans l'abstrait sur cet ordre est le meilleur moyen de se
    // tromper (et de n'obtenir qu'un rendu aux couleurs interverties, sans
    // aucune erreur pour le signaler).
    //
    // CORRIGE le 2026-08-13 apres verification sur console : les canaux ROUGE
    // et BLEU doivent etre echanges. Constat a l'ecran avant correction - le
    // bouton Y (jaune) apparaissait cyan, le X (bleu) orange, le B (rouge)
    // bleu, et le A (vert) restait vert : signature exacte d'une permutation
    // rouge/bleu, le vert etant au milieu donc epargne.
    //
    // Explication : IM_COL32(r,g,b,a) construit (a<<24)|(b<<16)|(g<<8)|r. Sur
    // cette console GROS-BOUTISTE, cet entier s'ecrit en memoire dans l'ordre
    // A, B, G, R - alors que D3DFMT_LIN_A8R8G8B8 attend A, R, G, B. Le vert
    // tombe juste, le rouge et le bleu sont intervertis.
    //
    // Pourquoi l'atlas de police ne revelait pas le probleme : il est
    // quasi-monochrome (blanc avec de l'alpha), donc R = G = B et l'echange
    // reste invisible. C'est la premiere image en couleurs qui l'a fait
    // apparaitre - d'ou l'interet de l'avoir teste tot sur du materiel reel.
    //
    // On passe donc src[2] (bleu) en position rouge et src[0] (rouge) en
    // position bleue, plutot que de reconstruire l'entier a la main : garder
    // IM_COL32 preserve la coherence avec le reste du rendu ImGui.
    for (int y = 0; y < h; y++) {
        ImU32* dst = (ImU32*)((unsigned char*)rect.pBits + rect.Pitch * y);
        const stbi_uc* src = pixels + (size_t)w * 4 * y;
        for (int x = 0; x < w; x++, src += 4)
            dst[x] = IM_COL32(src[2], src[1], src[0], src[3]);
    }

    tex->UnlockRect(0);
    stbi_image_free(pixels);

    *outTex = tex;
    if (outW) *outW = w;
    if (outH) *outH = h;
    return true;
}

bool LoadTextureFromFile(IDirect3DDevice9* device,
                         const char* path,
                         IDirect3DTexture9** outTex,
                         int* outW, int* outH) {
    if (outTex) *outTex = nullptr;
    if (!device || !path || !outTex)
        return false;

    // Lecture complete en memoire via std::ifstream - meme approche que
    // LoadMappingsFromFile et ReadInputState ailleurs dans le projet, deja
    // eprouvee sur HDD:. On ne passe pas par stbi_load(path) : STBI_NO_STDIO
    // est defini, et le stdio de cette plateforme se comporte differemment.
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open())
        return false;
    std::streamoff size = f.tellg();
    if (size <= 0)
        return false;
    f.seekg(0, std::ios::beg);

    std::vector<char> buf((size_t)size);
    f.read(&buf[0], size);
    f.close();

    return LoadTextureFromMemory(device, &buf[0], (int)size, outTex, outW, outH);
}

bool LoadTextureFromRGBA(IDirect3DDevice9* device,
                         const unsigned char* rgba, int w, int h,
                         IDirect3DTexture9** outTex) {
    if (outTex) *outTex = nullptr;
    if (!device || !rgba || w <= 0 || h <= 0 || !outTex)
        return false;
    IDirect3DTexture9* tex = nullptr;
    // Memes parametres et meme boucle de copie que LoadTextureFromMemory
    // ci-dessus (format LINEAIRE, rouge/bleu echanges : voir les explications
    // plus haut, confirmees sur console).
    if (device->CreateTexture(w, h, 1, D3DUSAGE_CPU_CACHED_MEMORY,
                              D3DFMT_LIN_A8R8G8B8, D3DPOOL_DEFAULT, &tex, NULL) < 0)
        return false;
    D3DLOCKED_RECT rect;
    if (tex->LockRect(0, &rect, NULL, 0) != D3D_OK) {
        tex->Release();
        return false;
    }
    for (int y = 0; y < h; y++) {
        ImU32* dst = (ImU32*)((unsigned char*)rect.pBits + rect.Pitch * y);
        const unsigned char* src = rgba + (size_t)w * 4 * y;
        for (int x = 0; x < w; x++, src += 4)
            dst[x] = IM_COL32(src[2], src[1], src[0], src[3]);
    }
    tex->UnlockRect(0);
    *outTex = tex;
    return true;
}
