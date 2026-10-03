# Compilation des shaders ImGui (Xbox 360 / Xenon)

## Pourquoi ce dossier existe

Le rendu ImGui sur Xbox 360 passe obligatoirement par des shaders (le Xenos
n'a aucun pipeline fixe - confirme par compilation reelle, voir
`PROJECT_NOTES.md` a la racine du projet). Compiler ces shaders **au
runtime**, sur la console, via `D3DXCompileShader()`, a ete essaye et
**echoue au link** (26 symboles internes a `d3dx9.lib` non resolus -
`XShaderPDBBuilder_*`, `XConvertShaderToMicrocode`, etc.). Une recherche
ciblee a confirme qu'aucun projet Xbox 360 XDK reel et buildable
(RetroArch-360 notamment) n'utilise ce chemin : ces symboles appartiennent
aux outils internes du compilateur/debugger de shaders du XDK, pas a une
bibliotheque destinee a etre linkee dans un jeu/homebrew.

Le vrai pattern, partout observe : **compiler le HLSL une seule fois, hors
ligne (sur PC, au moment du build), et charger directement le microcode
Xenon precompile** via `CreateVertexShader()`/`CreatePixelShader()`. C'est
ce que ce dossier met en place.

## Fichiers

- `imgui_vs.hlsl` / `imgui_ps.hlsl` : la source HLSL (a ne PAS modifier sans
  aussi regenerer les `.h` correspondants).
- `imgui_vs.h` / `imgui_ps.h` : **PLACEHOLDERS pour l'instant** (contiennent
  volontairement un `#error` pour bloquer la compilation tant qu'ils n'ont
  pas ete remplaces par le vrai microcode compile - voir plus bas). C'est
  ce que `imgui_impl_dx9_xenon.cpp` inclut et utilise directement.

## Resolu le 2026-08-01 : compilation automatique via fxc.exe

Verifie cote utilisateur : cette installation de Visual Studio n'a pas de
type d'element "HLSL Compiler"/"Effect Compiler Tool" natif pour les
projets Xbox 360 (liste complete du menu "Type d'element" inspectee - rien
de tel). L'outil en ligne de commande a ete localise et confirme :

```
C:\Program Files (x86)\Microsoft Xbox 360 SDK\bin\win32\fxc.exe
```

(`fxc.exe /?` confirme : "Microsoft (R) D3DX9 Shader Compiler 2.0.21256.0
Xbox 360"). `imgui_vs.hlsl` et `imgui_ps.hlsl` sont maintenant declares
dans `application.vcxproj` comme des elements `CustomBuild` qui invoquent
directement ce `fxc.exe` :

```
fxc.exe /nologo /Tvs_2_0 /Emain /Fh"imgui_vs.h" /Vng_ImGuiVertexShader "imgui_vs.hlsl"
fxc.exe /nologo /Tps_2_0 /Emain /Fh"imgui_ps.h" /Vng_ImGuiPixelShader "imgui_ps.hlsl"
```

**Aucune action manuelle requise** : MSBuild execute cette commande
automatiquement a chaque build (des que le `.hlsl` est plus recent que le
`.h` genere), avant de compiler `imgui_impl_dx9_xenon.cpp` qui inclut ces
`.h`. Les placeholders `imgui_vs.h`/`imgui_ps.h` (avec leur `#error`) sont
remplaces par le vrai microcode compile des le premier build apres ce
wiring.

Si `fxc.exe` echoue (chemin different sur une autre machine, erreur de
syntaxe HLSL, etc.), le message d'erreur de `fxc.exe` remonte directement
dans la fenetre de sortie de Visual Studio comme une erreur de build
normale - coller cette erreur telle quelle suffit pour la corriger.
