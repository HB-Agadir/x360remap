#include "theme.h"
#include "font_data_inter.h"
#include "font_data_barlow.h"
#include <cstdio>   // _snprintf, pour le rapport d'atlas de police

// Conversion hex -> ImVec4, alpha 1.0 par defaut - juste pour ecrire la
// palette de ETAT_DU_PROJET.md telle quelle (#0A0A0C etc) au lieu de la
// retranscrire a la main en flottants 0..1, source d'erreurs faciles a ne
// pas remarquer visuellement (une difference de 1/255 sur un fond quasi
// noir ne saute pas aux yeux, mieux vaut ecrire exactement le code hex
// donne par HB).
static ImVec4 HexColor(unsigned int hex, float alpha = 1.0f) {
    float r = ((hex >> 16) & 0xFF) / 255.0f;
    float g = ((hex >> 8) & 0xFF) / 255.0f;
    float b = (hex & 0xFF) / 255.0f;
    return ImVec4(r, g, b, alpha);
}

const ImVec4 kColorKeyboardAccent = ImVec4(0x8Bu / 255.0f, 0xD4u / 255.0f, 0x1Au / 255.0f, 1.0f); // #8BD41A
const ImVec4 kColorMouseAccent    = ImVec4(0x2Eu / 255.0f, 0x9Bu / 255.0f, 0xF0u / 255.0f, 1.0f); // #2E9BF0

void ApplyX360RemapTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // Palette ETAT_DU_PROJET.md section 3 :
    //   Fond              #0A0A0C
    //   Cartes/panneaux   #131417
    //   Texte principal   blanc casse
    //   Texte secondaire  #8A94A6
    const ImVec4 bg            = HexColor(0x0A0A0C);
    const ImVec4 panel         = HexColor(0x131417);
    const ImVec4 panelHovered  = HexColor(0x1B1D22);
    const ImVec4 panelActive   = HexColor(0x22252B);
    const ImVec4 border        = HexColor(0x2A2D33);
    const ImVec4 textMain      = HexColor(0xF2F1ED);
    const ImVec4 textSecondary = HexColor(0x8A94A6);

    colors[ImGuiCol_Text]           = textMain;
    colors[ImGuiCol_TextDisabled]   = textSecondary;
    colors[ImGuiCol_WindowBg]       = bg;
    colors[ImGuiCol_ChildBg]        = panel;
    colors[ImGuiCol_PopupBg]        = panel;
    colors[ImGuiCol_Border]         = border;
    // Ombres portees : pas de support simple en ImGui 1.72b (voir
    // ETAT_DU_PROJET.md, contraintes techniques) - BorderShadow existe mais
    // ne dessine qu'une bordure supplementaire decalee, pas un vrai flou.
    // Desactive plutot que d'approximer mal ; a simuler plus tard si besoin
    // via un ImDrawList dedie (rectangle semi-transparent legerement decale
    // derriere chaque carte), pas via ce canal.
    colors[ImGuiCol_BorderShadow]   = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    colors[ImGuiCol_FrameBg]        = panel;
    colors[ImGuiCol_FrameBgHovered] = panelHovered;
    colors[ImGuiCol_FrameBgActive]  = panelActive;

    colors[ImGuiCol_TitleBg]          = bg;
    colors[ImGuiCol_TitleBgActive]    = bg;
    colors[ImGuiCol_TitleBgCollapsed] = bg;
    colors[ImGuiCol_MenuBarBg]        = panel;

    colors[ImGuiCol_ScrollbarBg]          = bg;
    colors[ImGuiCol_ScrollbarGrab]        = panelHovered;
    colors[ImGuiCol_ScrollbarGrabHovered] = panelActive;
    colors[ImGuiCol_ScrollbarGrabActive]  = kColorMouseAccent;

    colors[ImGuiCol_CheckMark]       = kColorMouseAccent;
    colors[ImGuiCol_SliderGrab]      = kColorMouseAccent;
    colors[ImGuiCol_SliderGrabActive] = kColorKeyboardAccent;

    colors[ImGuiCol_Button]        = panel;
    colors[ImGuiCol_ButtonHovered] = panelHovered;
    colors[ImGuiCol_ButtonActive]  = panelActive;

    colors[ImGuiCol_Header]        = panelHovered;
    colors[ImGuiCol_HeaderHovered] = panelActive;
    colors[ImGuiCol_HeaderActive]  = kColorMouseAccent;

    colors[ImGuiCol_Separator]        = border;
    colors[ImGuiCol_SeparatorHovered] = kColorMouseAccent;
    colors[ImGuiCol_SeparatorActive]  = kColorMouseAccent;

    colors[ImGuiCol_ResizeGrip]        = border;
    colors[ImGuiCol_ResizeGripHovered] = kColorMouseAccent;
    colors[ImGuiCol_ResizeGripActive]  = kColorMouseAccent;

    colors[ImGuiCol_Tab]                = panel;
    colors[ImGuiCol_TabHovered]         = panelHovered;
    colors[ImGuiCol_TabActive]          = panelActive;
    colors[ImGuiCol_TabUnfocused]       = panel;
    colors[ImGuiCol_TabUnfocusedActive] = panelHovered;

    colors[ImGuiCol_PlotLines]            = kColorMouseAccent;
    colors[ImGuiCol_PlotLinesHovered]     = kColorKeyboardAccent;
    colors[ImGuiCol_PlotHistogram]        = kColorMouseAccent;
    colors[ImGuiCol_PlotHistogramHovered] = kColorKeyboardAccent;

    colors[ImGuiCol_TextSelectedBg] = ImVec4(kColorMouseAccent.x, kColorMouseAccent.y, kColorMouseAccent.z, 0.35f);
    colors[ImGuiCol_DragDropTarget] = kColorKeyboardAccent;

    // Focus manette franc et visible (ETAT_DU_PROJET.md, contraintes
    // techniques : "pas de survol souris garanti") - c'est CE liseret qui
    // indique a l'utilisateur quel element la manette va activer, doit donc
    // etre net et contraste, pas une simple teinte discrete.
    colors[ImGuiCol_NavHighlight]          = kColorMouseAccent;
    colors[ImGuiCol_NavWindowingHighlight] = kColorMouseAccent;
    colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(bg.x, bg.y, bg.z, 0.7f);
    colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(bg.x, bg.y, bg.z, 0.7f);

    // Cartes aux coins arrondis avec bordure subtile (maquette 2, voir
    // ETAT_DU_PROJET.md) - arrondi modere, pas de style "bulle" exagere.
    style.WindowRounding   = 8.0f;
    style.ChildRounding    = 8.0f;
    style.FrameRounding    = 6.0f;
    style.PopupRounding    = 8.0f;
    style.TabRounding      = 6.0f;
    style.GrabRounding     = 6.0f;
    style.ScrollbarRounding = 8.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize  = 1.0f;
    style.FrameBorderSize  = 1.0f;
    style.PopupBorderSize  = 1.0f;

    // Respiration genereuse - lecture a 2-3m (ETAT_DU_PROJET.md), un
    // agencement dense comme sur un ecran de PC serait illisible depuis un
    // canape. Valeurs de depart, a affiner une fois la mise en page de la
    // maquette 2 en place.
    style.WindowPadding = ImVec2(20.0f, 20.0f);
    style.FramePadding  = ImVec2(12.0f, 8.0f);
    style.ItemSpacing    = ImVec2(12.0f, 12.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 8.0f);
    style.IndentSpacing  = 24.0f;
}

void LoadX360RemapFont(float pixelSize) {
    ImGuiIO& io = ImGui::GetIO();

    // Plages de glyphes : Latin de base (0x0020-0x00FF, couvre deja les
    // accents francais les plus courants via Latin-1 Supplement : é è ê à
    // ù ç ...) + Latin Extended-A (0x0100-0x017F, couvre les caracteres
    // portugais restants : ã õ, et quelques francais rares comme œ selon
    // la police). Tableau statique : ImGui garde un pointeur vers ces
    // donnees, elles doivent survivre au-dela de cet appel (voir
    // ImFontAtlas::AddFontFromFileTTF, parametre glyph_ranges).
    static const ImWchar s_glyphRanges[] = {
        0x0020, 0x00FF, // Latin de base + Latin-1 Supplement
        0x0100, 0x017F, // Latin Extended-A
        0,
    };

    // Chargement DEPUIS LA MEMOIRE (2026-08-08, revu suite a une remarque de
    // HB) - plus un chargement depuis un fichier sur HDD:\... comme la
    // premiere version de cette fonction. Un fichier externe aurait oblige
    // CHAQUE utilisateur de X360RemapStudio.xex a se procurer la police et
    // la deposer manuellement au bon endroit ; le tableau d'octets
    // kInterFont24ptRegularData (font_data_inter.h/.cpp, genere depuis le
    // .ttf fourni par HB) est compile directement dans le binaire, donc
    // present pour tout le monde sans aucune manipulation - meme principe
    // que le microcode des shaders ImGui deja embarque dans ce projet.
    //
    // FontDataOwnedByAtlas = false est OBLIGATOIRE ici : par defaut, ImGui
    // prend possession du buffer passe a AddFontFromMemoryTTF et appelle
    // free() dessus a la destruction de l'atlas. kInterFont24ptRegularData
    // est un tableau `const` statique, pas une allocation sur le tas -
    // laisser ImGui tenter de le liberer corromprait la memoire. Voir le
    // commentaire d'AddFontFromMemoryTTF dans imgui.h.
    ImFontConfig fontConfig;
    fontConfig.FontDataOwnedByAtlas = false;

    // --- Correctif du rendu casse des accents (2026-09-13) ---
    //
    // SYMPTOME : un meme mot accentue ("configuracoes" en portugais,
    // "configuracion" en espagnol) s'affiche correctement dans un panneau et
    // en "?" dans un autre. Pas un probleme de caractere ni de police : le
    // .ttf embarque contient bien tous les glyphes, et le texte source decode
    // proprement en UTF-8 (verifie caractere par caractere). C'est un
    // probleme propre a CERTAINES TAILLES de police (suivi interne)
    // section 5.2.
    //
    // CAUSE : ImGui range tous les glyphes de toutes les tailles dans UN SEUL
    // atlas, et son propre code porte le commentaire "FIXME: We are not
    // handling packing failure here" - un glyphe qui ne rentre pas n'est pas
    // signale, il reste simplement corrompu. Avec quatre tailles chargees et
    // ~350 glyphes chacune, l'atlas deborde silencieusement.
    //
    // DEUX LEVIERS, appliques ensemble :
    //
    // 1. Sur-echantillonnage ramene a 1. Par defaut ImGui rasterise chaque
    //    glyphe TROIS FOIS plus large (OversampleH = 3) pour un
    //    positionnement sous-pixel plus fin. Sur un ecran de television, a
    //    ces tailles, le gain est invisible ; le cout, lui, est un atlas trois
    //    fois plus large. C'est le levier le plus efficace.
    //
    // 2. Largeur d'atlas forcee a 2048 au lieu de l'heuristique automatique
    //    d'ImGui (qui choisit 512/1024/2048 selon une estimation de surface -
    //    estimation qui, visiblement, se trompait ici). Plus l'atlas est
    //    large, moins il est haut : la texture reste petite en memoire.
    fontConfig.OversampleH = 1;
    fontConfig.OversampleV = 1;
    io.Fonts->TexDesiredWidth = 2048;

    // QUATRE TAILLES (2026-08-14). La maquette repose sur une hierarchie
    // typographique : logo, titres de section en petites capitales espacees,
    // texte courant, libelles compacts. Avec une seule taille, la structure
    // serait correcte mais le rendu resterait plat - c'est la hierarchie qui
    // fait qu'un ecran parait concu plutot qu'aligne.
    //
    // Le meme .ttf est charge plusieurs fois : ImGui rasterise un jeu de
    // glyphes distinct par taille dans le meme atlas. Le cout est en surface
    // d'atlas, pas en memoire de police (les octets du .ttf ne sont pas
    // dupliques, seul le pointeur est reutilise).
    //
    // L'ORDRE COMPTE : la premiere police ajoutee devient la police par
    // defaut. On charge donc `pixelSize` (le texte courant) en premier, pour
    // que tout le code existant qui n'appelle pas PushFont continue de
    // s'afficher exactement comme avant.
    g_fontBody = io.Fonts->AddFontFromMemoryTTF(
        (void*)kInterFont24ptRegularData, kInterFont24ptRegularSize,
        pixelSize, &fontConfig, s_glyphRanges);

    g_fontSmall = io.Fonts->AddFontFromMemoryTTF(
        (void*)kInterFont24ptRegularData, kInterFont24ptRegularSize,
        pixelSize * 0.72f, &fontConfig, s_glyphRanges);

    g_fontTitle = io.Fonts->AddFontFromMemoryTTF(
        (void*)kInterFont24ptRegularData, kInterFont24ptRegularSize,
        pixelSize * 1.30f, &fontConfig, s_glyphRanges);

    g_fontLogo = io.Fonts->AddFontFromMemoryTTF(
        (void*)kInterFont24ptRegularData, kInterFont24ptRegularSize,
        pixelSize * 1.75f, &fontConfig, s_glyphRanges);

    // Repli de securite si jamais le chargement echoue quand meme (donnees
    // corrompues, atlas hors memoire...) - meme raisonnement que pour
    // l'ancienne version basee fichier : ne JAMAIS laisser l'atlas de
    // police vide, ca plante le rendu ImGui des la premiere frame.
    if (!g_fontBody) {
        io.Fonts->AddFontDefault();
        // Les autres tailles restent nulles : PushX360Font() le teste et ne
        // fait rien dans ce cas, plutot que d'empiler un pointeur invalide.
        g_fontSmall = g_fontTitle = g_fontLogo = nullptr;
    }

    // --- Page "glossy" : Barlow Condensed (2026-09-24) ---
    // Ajoutees APRES Inter : la police par defaut (la premiere ajoutee) ne
    // change pas, les autres pages s'affichent exactement comme avant.
    // Sous-ensemble latin : Latin-1 (accents FR/PT/ES) + tirets + "...".
    // Les fleches n'y sont pas : la page glossy les dessine en image.
    {
        static const ImWchar s_glossyRanges[] = {
            0x0020, 0x00FF,
            0x2013, 0x2014,   // tirets demi-cadratin / cadratin
            0x2026, 0x2026,   // points de suspension
            0x2039, 0x203A,   // chevrons simples (cases souris, pagination)
            0,
        };
        ImFontConfig g;
        g.FontDataOwnedByAtlas = false;   // tableau statique : voir plus haut
        g.OversampleH = 1;
        g.OversampleV = 1;
        g_fontGBody  = io.Fonts->AddFontFromMemoryTTF((void*)kBarlowCond400Data, kBarlowCond400Size, 24.0f, &g, s_glossyRanges);
        g_fontGBodyM = io.Fonts->AddFontFromMemoryTTF((void*)kBarlowCond500Data, kBarlowCond500Size, 24.0f, &g, s_glossyRanges);
        g_fontGLarge = io.Fonts->AddFontFromMemoryTTF((void*)kBarlowCond400Data, kBarlowCond400Size, 26.0f, &g, s_glossyRanges);
        g_fontGTitle = io.Fonts->AddFontFromMemoryTTF((void*)kBarlowCond400Data, kBarlowCond400Size, 37.0f, &g, s_glossyRanges);
        g_fontGSmall = io.Fonts->AddFontFromMemoryTTF((void*)kBarlowCond400Data, kBarlowCond400Size, 20.0f, &g, s_glossyRanges);
        g_fontGCaps  = io.Fonts->AddFontFromMemoryTTF((void*)kBarlowCond500Data, kBarlowCond500Size, 18.0f, &g, s_glossyRanges);
    }

    BuildFontAtlasReport();
}

// --- Verification de l'atlas apres construction (2026-09-13) ---------------
//
// ImGui ne signale JAMAIS un glyphe qu'il n'a pas reussi a ranger ("FIXME: We
// are not handling packing failure here" dans son propre code). Le defaut ne
// se voit donc qu'a l'oeil, sur une chaine precise, dans un panneau precis -
// exactement ce qui a rendu ce bug si penible a cerner (signale le 2026-08-24
// sur le portugais, retrouve le 2026-09-13 sur l'espagnol).
//
// On verifie donc nous-memes, une fois l'atlas construit, qu'un echantillon
// de caracteres accentues existe bien A CHACUNE des quatre tailles. Le
// resultat part dans le journal de l'application : si le probleme revient un
// jour, il sera lisible dans un fichier plutot que devine sur une capture
// d'ecran.
static char s_fontReport[192] = "(rapport de police non genere)";

static int CountMissingGlyphs(ImFont* f) {
    if (!f) return -1;
    // Echantillon couvrant les besoins reels des quatre langues :
    // espagnol (a e i o u accentues, n tilde, point d'interrogation inverse),
    // portugais (a/o tilde, c cedille), francais (e accent grave/aigu).
    static const ImWchar kSample[] = {
        0x00E1, 0x00E9, 0x00ED, 0x00F3, 0x00FA,  // a e i o u accent aigu
        0x00F1, 0x00BF, 0x00A1,                  // n tilde, ? et ! inverses
        0x00E3, 0x00F5, 0x00E7,                  // a/o tilde, c cedille
        0x00E8, 0x00EA, 0x00E0,                  // e grave, e circonflexe, a grave
        0x00D3, 0x00C9,                          // O et E majuscules accentues
        0
    };
    int missing = 0;
    for (int i = 0; kSample[i]; i++)
        if (!f->FindGlyphNoFallback(kSample[i])) missing++;
    return missing;
}

void BuildFontAtlasReport() {
    ImGuiIO& io = ImGui::GetIO();

    // L'atlas n'est construit qu'a la premiere demande de pixels (par le
    // backend de rendu). On force la construction ici pour pouvoir verifier
    // tout de suite ; le backend reutilisera le resultat, sans surcout.
    io.Fonts->Build();

    _snprintf(s_fontReport, sizeof(s_fontReport) - 1,
              "Atlas police %dx%d - glyphes accentues manquants : body=%d small=%d title=%d logo=%d",
              io.Fonts->TexWidth, io.Fonts->TexHeight,
              CountMissingGlyphs(g_fontBody),  CountMissingGlyphs(g_fontSmall),
              CountMissingGlyphs(g_fontTitle), CountMissingGlyphs(g_fontLogo));
    s_fontReport[sizeof(s_fontReport) - 1] = '\0';
}

const char* GetFontAtlasReport() {
    return s_fontReport;
}

// Definis ici plutot que dans l'en-tete : ce sont des variables, pas des
// constantes, et elles ne doivent exister qu'en un seul exemplaire.
ImFont* g_fontBody  = nullptr;
ImFont* g_fontSmall = nullptr;
ImFont* g_fontTitle = nullptr;
ImFont* g_fontLogo  = nullptr;
ImFont* g_fontGBody  = nullptr;
ImFont* g_fontGBodyM = nullptr;
ImFont* g_fontGLarge = nullptr;
ImFont* g_fontGTitle = nullptr;
ImFont* g_fontGSmall = nullptr;
ImFont* g_fontGCaps  = nullptr;

void PushX360Font(ImFont* font) {
    // Tolerer nullptr simplifie tous les appelants : ils n'ont pas a verifier
    // que la police existe avant chaque PushFont/PopFont. En contrepartie,
    // PopX360Font doit recevoir le MEME pointeur pour savoir s'il y a
    // reellement quelque chose a depiler.
    if (font)
        ImGui::PushFont(font);
}

void PopX360Font(ImFont* font) {
    if (font)
        ImGui::PopFont();
}
