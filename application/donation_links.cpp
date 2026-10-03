#include "donation_links.h"

// Chaines reelles - voir donation_links.h pour le pourquoi du hash juste en
// dessous de chacune. Si un de ces liens change un jour (nouveau compte,
// nouveau pseudo...), il FAUT regenerer le hash correspondant, sinon
// IsLinkIntact() le fera echouer pour de mauvaises raisons et le lien
// disparaitra silencieusement de l'onglet Support. Script de regeneration :
//
//   python3 -c "
//   def fnv1a(s):
//       h = 2166136261
//       for b in s.encode('utf-8'):
//           h ^= b
//           h = (h * 16777619) & 0xFFFFFFFF
//       return h
//   print(hex(fnv1a('LA_NOUVELLE_CHAINE_ICI')))
//   "
//
const char* const kPayPalEmail = "x360remap@gmail.com";
const uint32_t kPayPalEmailHash = 0x55F907E2u;

const char* const kKofiUrl = "ko-fi.com/x360remap";
const uint32_t kKofiUrlHash = 0xEF4D30D3u;

// Pas encore de depot public au moment de l'ecriture (2026-08-11) - pointe
// vers le profil en attendant. A mettre a jour (chaine + hash) une fois le
// depot cree, voir la tache "bouton update / nouvelles releases" evoquee
// par l'utilisateur pour une future iteration.
const char* const kGithubUrl = "github.com/HB-Agadir";
const uint32_t kGithubUrlHash = 0x22422515u;

// Page des versions (2026-09-25) - ouverte par le QR code de la page Soutenir
// (la console ne peut pas interroger GitHub elle-meme, voir ui_glossy.cpp).
// NOM DU DEPOT A CONFIRMER a sa creation : changer ICI + le hash + RELEASES_URL
// dans _design_refs/xui_kit/build_kit.py (qui dessine le QR code).
const char* const kReleasesUrl = "github.com/HB-Agadir/X360Remap/releases/latest";
const uint32_t kReleasesUrlHash = 0x985D5759u;

const char* const kYoutubeUrl = "youtube.com/@X360Remap";
const uint32_t kYoutubeUrlHash = 0xC5735EA2u;

uint32_t Fnv1aHash(const char* s) {
    uint32_t h = 2166136261u;
    for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
        h ^= *p;
        h *= 16777619u;
    }
    return h;
}

bool IsLinkIntact(const char* s, uint32_t expectedHash) {
    return Fnv1aHash(s) == expectedHash;
}
