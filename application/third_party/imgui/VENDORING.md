# Vendoring Dear ImGui pour application.xex

## Pourquoi pas telecharge automatiquement

Le coeur de Dear ImGui (`imgui.h`, `imgui.cpp`, `imgui_draw.cpp`, `imgui_widgets.cpp`, `imgui_tables.cpp`, `imgui_internal.h`, `imstb_rectpack.h`, `imstb_textedit.h`, `imstb_truetype.h`, `imconfig.h`) est du **C++ portable, zero appel Xbox/Windows**, verifie en lisant le code source reel (MIT, github.com/ocornut/imgui) - il n'y a rien a adapter dedans. Le telecharger fichier par fichier depuis mon bac a sable serait moins fiable que de recuperer l'archive officielle complete (garantit qu'aucun fichier n'est oublie ou tronque).

## A faire (une fois, cote utilisateur)

1. Telecharger l'archive de la version **v1.90.9** (celle sur laquelle le backend adapte dans ce dossier, `imgui_impl_dx9_xenon.cpp`, est base - ne pas prendre une version plus recente sans re-verifier le backend PC correspondant, l'API texture a change depuis) : https://github.com/ocornut/imgui/releases/tag/v1.90.9
2. Copier ici (`application/third_party/imgui/`) uniquement :
   - `imgui.h`, `imgui.cpp`
   - `imgui_internal.h`
   - `imgui_draw.cpp`
   - `imgui_widgets.cpp`
   - `imgui_tables.cpp`
   - `imstb_rectpack.h`, `imstb_textedit.h`, `imstb_truetype.h`
   - `imconfig.h`
3. Ne PAS copier le `backends/imgui_impl_dx9.cpp` officiel - celui-ci suppose un D3D9 PC standard (voir `imgui_impl_dx9_xenon.cpp` dans ce dossier, deja adapte et avec les points incertains commentes).
4. Ajouter tous ces fichiers `.cpp` a `application.vcxproj` (deja fait pour `imgui_impl_dx9_xenon.cpp`, a completer pour les fichiers du coeur une fois copies - voir les `<ClCompile>` commentes dedans).

## Pourquoi ImGui plutot que XUI (decision du 2026-08-01)

Voir `ARCHITECTURE.md`, section "Rendu : Dear ImGui (remplace le plan XUI initial)". En resume : pas de portage Xbox 360 existant trouve malgre recherche, mais le risque est plus faible qu'il n'y parait - `application.xex` possede son propre device Direct3D9 (comme n'importe quel homebrew Xbox 360, `snes360-enhanced` par exemple), donc pas besoin de hooker le device d'Aurora. Zero outil GUI externe necessaire (contrairement a XUI Studio, jamais verifie) - tout est du code C++ que je peux ecrire et raisonner dessus directement, meme si je ne peux pas le compiler moi-meme.
