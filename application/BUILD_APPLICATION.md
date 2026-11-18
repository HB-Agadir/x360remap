# Build de application.xex

## Etat honnete de ce dossier (lire avant de compiler)

Trois categories de fichiers, avec des niveaux de confiance tres differents :

- **Testes reellement, compiles et executes** : `config_writer.*`, `input_state_reader.*`, `wizard_session.*`. Aucune dependance Xbox - voir `application/tests/test_wizard_session.cpp`, qui tourne dans mon bac a sable et verifie tout le chemin bout en bout (lecture d'`input_state.json`, deroulement du wizard, reglages souris, ecriture dans `hiddriver.json`, relecture par le vrai code du plugin). Commande pour la relancer toi-meme si tu as g++ sous la main (WSL, ou n'importe quel Linux) :

  ```
  cd X360Remap-v1.2
  g++ -std=c++14 -I X360Remap -I X360Remap/rapidjson/include -I application -I preview/stubs -o /tmp/test_wizard_session \
    X360Remap/mapping.cpp X360Remap/mapping_assistant.cpp X360Remap/known_titles.cpp X360Remap/known_devices.cpp X360Remap/mouse_calibration.cpp \
    application/config_writer.cpp application/input_state_reader.cpp application/wizard_session.cpp application/i18n.cpp \
    application/tests/test_wizard_session.cpp
  /tmp/test_wizard_session
  ```

- **Testes reellement, compiles avec zero erreur (mais portable, pas du code Xbox)** : le coeur Dear ImGui v1.90.9 (`third_party/imgui/imgui.cpp`, `imgui_draw.cpp`, `imgui_widgets.cpp`, `imgui_tables.cpp`) - vendorise depuis la vraie release GitHub et compile avec g++ en C++14 dans mon bac a sable avant d'etre ajoute au projet.

- **Code Xbox reel, jamais compile sur le toolchain Xbox, mais dont l'API est verifiee (pas devinee)** : `XexUtils/src/Input.cpp`, `Xam.cpp`, `Formatter.cpp`, `General.cpp`, `Log.cpp` - vendorises depuis le vrai depot GitHub de XexUtils, utilises dans `main.cpp` avec la signature exacte lue directement dans leurs headers (`XexUtils::Input::GetInput()`, `XexUtils::Xam::XNotify()`).

- **Premier jet, jamais compile** : `main.cpp` et `third_party/imgui/imgui_impl_dx9_xenon.*`. Ils utilisent l'API D3D9 du XDK (creation de device, buffers, textures) que je ne peux ni compiler ni executer depuis mon bac a sable Linux. **Si la premiere compilation echoue, c'est normal et attendu** - colle-moi l'erreur exacte (nom de fichier .h manquant, symbole introuvable, etc.) et je corrige precisement. Voir les commentaires en tete de `imgui_impl_dx9_xenon.cpp` pour les trois points d'incertitude identifies (declaration de vertex, pipeline fixe vs shaders, D3DPOOL) et ce qu'il faut regarder si ca compile mais que rien ne s'affiche.

## Etapes de build

1. **Vendorer Dear ImGui** : suivre `third_party/imgui/VENDORING.md` (telecharger la release v1.90.9, copier les fichiers du coeur dans `third_party/imgui/`).

2. **XexUtils** : deja fait, rien a faire de ton cote pour ce jalon. Vendorise dans `XexUtils/` (clone complet moins `.git` et moins le contenu de `deps/BearSSL`, qui n'a pas pu etre clone depuis mon bac a sable - voir ci-dessous). `application.vcxproj` compile directement `XexUtils/src/Input.cpp`, `Xam.cpp`, `Formatter.cpp`, `General.cpp`, `Log.cpp` (deja ajoutes au projet, `AdditionalIncludeDirectories` pointe deja vers `XexUtils/src` et `XexUtils/include`) - **pas** de `<ProjectReference>` vers `XexUtils.vcxproj` : ce projet complet lie aussi `Http.cpp`/`Socket.cpp`, qui incluent des headers BearSSL absents ici, et echouerait a compiler pour une raison sans rapport avec ce dont on a besoin (notifications + manette). Si tu as besoin plus tard de `Http.h`/`Socket.h` (verif de mise a jour en ligne, par ex.), lance `git submodule update --init --recursive` dans `XexUtils/` depuis ta machine (reseau non restreint) puis ajoute `XexUtils.vcxproj` comme vrai projet reference a ce moment-la.

3. **Completer `application.vcxproj`** : decommenter/ajouter les `<ClCompile>` du coeur ImGui une fois copie (repere deja present dans le fichier).

4. **Compiler** : ouvrir `hiddriver.sln` (le projet `application` y est deja ajoute) dans Visual Studio 2019, configuration "Release Retail | Xbox 360", Build. Sortie attendue : `application\Release Retail\application.xex`.

   Si l'edition de liens echoue sur `d3d9.lib`/`d3dx9.lib`, verifier le nom exact dans `C:\Program Files (x86)\Microsoft Xbox 360 SDK\lib\xbox` et l'ajuster dans `<AdditionalDependencies>`.

5. **Deployer** : copier `application.xex` vers `HDD:\Apps\HidConfig\` sur la console (meme mecanisme FTP/`rgh fs put` deja utilise pour `hiddriver.xex`, voir `build_and_deploy.ps1` a la racine du projet). Lancer depuis Aurora (ajout d'un raccourci vers `application.xex`, comme n'importe quel homebrew) - pas de fichier `.xzp`/skin a deployer separement, contrairement au plan XUI initial : tout le rendu est genere par code.

## Si l'ecran reste noir alors que ca compile et tourne sans erreur

C'est le scenario a surveiller en priorite (voir `imgui_impl_dx9_xenon.cpp`, adaptation #2) : le pipeline fixe D3D9 (`SetVertexShader(nullptr)`) peut ne pas etre emule par le XDK installe. Un bloc de secours avec un vertex/pixel shader HLSL minimal est present dans le fichier, desactive par `#if 0` - a activer si ce cas se presente. Dis-le moi avec ce que tu observes exactement (ecran noir total, UI visible mais sans texte, erreur au lancement...) et je peux affiner directement.

## Pourquoi ce dossier n'a pas encore de menu de selection de peripherique ni de profils par jeu a l'ecran

Ce premier jet va directement a deux onglets fixes (wizard clavier + reglages souris, VID/PID en dur dans `main.cpp`) pour valider la chaine complete `input_state.json -> wizard/reglages -> hiddriver.json` et le rendu ImGui avant d'ajouter la navigation entre plusieurs ecrans. La logique des profils par jeu (deadzone, courbe de sensibilite) est deja dans le schema JSON teste (`hiddriver/mapping.h`/`.cpp`, voir `HidDeviceProfile`) - il ne reste que le cablage ImGui (un `ImGui::Combo` pour choisir un profil, un `ImGui::SliderFloat` pour l'exposant de la courbe), une fois ce premier ecran confirme buildable et navigable a la manette chez toi.
