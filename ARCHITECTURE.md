# Architecture - X360 Remap Core (backend) + X360 Remap Studio (frontend)

## Nommage (2026-08-01)

Produit global : **X360 Remap**. Les fichiers `.xex` gardent leurs noms techniques actuels (`hiddriver.xex`, `application.xex`) jusqu'au Jalon 9 (finalisation, voir `application/ROADMAP_APPLICATION.md`) pour ne pas perturber les chemins de build/deploiement en plein chantier fonctionnel - mais les deux composants sont designes ci-dessous par leur nom produit :

- **X360 Remap Core** = `plugin.xex` / `hiddriver.xex` (backend)
- **X360 Remap Studio** = `application.xex` (frontend)

## Vue d'ensemble

- **X360 Remap Core** (backend, deja fonctionnel) : plugin DashLaunch permanent, capture les entrees USB HID clavier/souris et les emule en entrees manette Xbox 360. Lit `config.json` (= `hiddriver.json`) a la racine du disque dur, recharge a chaud des qu'il change (deja teste et confirme sur hardware).
- **X360 Remap Studio** (frontend, a construire) : homebrew graphique lance depuis Aurora, gestionnaire de configuration - wizard d'assignation touche/bouton, profils par jeu, deadzone, courbe de sensibilite souris. Ecrit `config.json`.

## Rendu : Dear ImGui (remplace le plan XUI initial, 2026-08-01)

**Decision revue a la demande explicite de l'utilisateur.** Le plan XUI ci-dessous (section suivante, gardee pour l'historique) avait un vrai point faible : la compilation `.xui` -> `.xur` passe par `XUI Studio`, un outil GUI Windows que je ne peux pas executer ni verifier - toute la chaine de production de l'ecran restait une inconnue jusqu'au premier essai de l'utilisateur.

Recherche faite avant de trancher : aucun portage existant de Dear ImGui vers le XDK Xbox 360 n'a ete trouve (recherche sur plusieurs formulations, GitHub inclus). Ce n'est donc **pas** un chemin deja balise comme XUI l'etait via `snes360-enhanced` - c'est un premier portage, avec un risque reel. Mais ce risque est plus faible qu'il n'y parait pour une raison precise : `application.xex` possede son **propre device Direct3D9** (comme n'importe quel homebrew Xbox 360 - `snes360-enhanced` par exemple - `Direct3DCreate9`/`CreateDevice` standard), donc pas besoin de hooker le device d'Aurora comme le font les overlays ImGui PC habituels. Le backend de rendu ImGui pour D3D9 (`imgui_impl_dx9.cpp`, verifie en le recuperant directement depuis GitHub avant d'ecrire quoi que ce soit) est volontairement petit (~400 lignes) et n'utilise que des appels D3D9 basiques (buffers de vertex/index, textures, etats de rendu) - le genre de code qu'un portage plateforme peut raisonnablement adapter.

Avantage concret par rapport a XUI : **zero outil externe**. Tout est du C++ que j'ecris et peux raisonner dessus ligne par ligne, meme si je ne peux pas le compiler moi-meme - contrairement au fichier `.xui`, dont je ne pouvais meme pas confirmer le format d'edition.

### ADDENDUM 2026-09-17 : le blocage invoque ci-dessus n'existe plus

**A lire avant de reutiliser cette section comme justification.** La decision du
2026-08-01 repose sur une contrainte qui etait la mienne, pas celle du produit :
"la compilation `.xui` -> `.xur` passe par XUI Studio, un outil GUI Windows que je
ne peux pas executer ni verifier". Deux elements l'ont invalidee depuis.

**1. Je peux piloter des applications Windows.** Demontre dans ce projet en
septembre 2026 en conduisant X360DebuggerWV directement (connexion, Stop/Go,
lecture memoire). XUI Studio n'est plus hors de portee par nature.

**2. Il existe une chaine `.xui` <-> `.xur` en ligne de commande, libre.**
[XUIHelper](https://github.com/SGCSam/XUIHelper) (SGCSam, **GPL-3.0** - meme
licence que ce projet, donc compatible), signale par HB le 2026-09-17 :

- suite C# : `XUIHelper.Core` (bibliotheque) + `XUIHelper.CLI` + `XUIHelper.GUI` ;
- lecture/ecriture **XUR v5** (kernels 1888-9199), **quasi complete pour XUR v8**
  (12611-17559), lecture/ecriture **XUI v12** ;
- **interoperabilite complete entre les trois** - c'est le point decisif : la
  conversion `.xui` -> `.xur` ne depend plus d'un outil GUI proprietaire ;
- systeme d'extensions XML pour enregistrer des classes personnalisees, compatible
  avec XuiTool ;
- exemple d'appel : `conv -s <XUR> -f "xuiv12" -o <XUI> -g "V8"`.

Reserves honnetes, tirees de la section "Known Issues" du depot lui-meme :

- la section KEYD de XUR v8 n'est pas entierement retro-concue (6 bits d'un octet
  de flags non identifies) ;
- la verification des en-tetes de comptage v5/v8 est **desactivee** a cause d'un
  bug de logique - donc un aller-retour n'est pas garanti bit a bit ;
- projet peu diffuse (16 etoiles) : aucun filet de securite communautaire si on
  bute sur un cas non couvert.

**Ce que ca change, et ce que ca ne change pas.** Ca supprime le seul argument
technique reellement opposant de la decision de 2026-08-01. Ca ne dit rien du
cout de reecriture de l'interface, ni du fait qu'ImGui fonctionne aujourd'hui sur
le materiel. Le vrai argument en faveur de XUI reste celui identifie en septembre
2026 : **l'aspect statique est atteignable en ImGui - les maquettes le prouvent -
mais les transitions et animations ne le sont pas sans coder chacune a la main**,
et c'est ce qui fait qu'une interface parait agreable.

Statut : **piste ouverte, non engagee.** Sequencement decide par HB le
2026-09-16 : tests d'UI et d'ergonomie d'abord, XUI ensuite si ca vaut encore le
coup. Objectif general du projet a cette date : finir le perimetre convenu
(correctifs discutes + refonte de l'UI), puis passer la main a la communaute.

Fichiers :
- `application/third_party/imgui/imgui_impl_dx9_xenon.cpp/.h` : backend adapte du `imgui_impl_dx9.cpp` officiel **v1.72b** (voir plus bas pourquoi cette version, pas v1.90.9). **Reecrit le 2026-08-01** en shaders (HLSL SM2.0, compiles au runtime via `D3DXCompileShader`) apres confirmation reelle (erreurs de build) que le Xenos GPU n'a aucun pipeline fixe - `SetTransform`/`SetTextureStageState` n'existent pas du tout sur `D3DDevice`, contrairement a l'hypothese initiale plus prudente ("peut ne pas etre emule"). Voir `PROJECT_NOTES.md` (section "le Xenos GPU n'a AUCUN pipeline fixe") pour le detail complet et les sources (code reel de `snes360-enhanced`). Point d'incertitude restant, documente dans le fichier : `SetVertexShaderConstantF` (pour la matrice de projection) est une methode D3D9 standard plausible sur Xenos mais pas trouvee confirmee dans le code source consulte - si le rendu est deforme/mal positionne mais pas noir, c'est le premier suspect. `IDirect3DStateBlock9`/`CreateStateBlock` reste aussi a surveiller (support parfois different sur consoles - si ca echoue, `RenderDrawData` s'arrete silencieusement, "ca tourne mais rien ne s'affiche").
- `application/third_party/imgui/VENDORING.md` : instructions pour recuperer le coeur ImGui (portable, aucune adaptation Xbox necessaire) depuis la release officielle - pas vendorise fichier par fichier ici pour eviter tout risque d'oubli/troncature.
- `application/main.cpp` : reecrit pour creer son propre device D3D9, boucle ImGui, navigation manette via XexUtils::Input (les widgets glissiere/liste deroulante demandes explicitement par l'utilisateur pour la sensibilite et les profils sont exactement le point fort d'ImGui par rapport a XUI, ou chaque ecran demandait de la mise en page manuelle).

### Retrogradation v1.90.9 -> v1.72b (2026-08-01)

Le coeur ImGui vendorise une premiere fois etait la derniere release (v1.90.9, 2024) - un choix par defaut raisonnable en apparence, mais qui s'est revele incompatible avec le toolset XDK Xbox 360 des le premier vrai build : ce toolset (confirme par plusieurs erreurs reelles - NSDMI, `enum class`) est un compilateur C++ tres ancien, essentiellement C++03 avec quelques ajouts de bibliotheque backportes (`nullptr`, `std::unique_ptr`, `std::move`), **sans aucun support des enums avant-declares a type explicite** (`enum ImGuiDir : int;`) ni des `enum class` - deux constructions que v1.90.9 utilise dans son API **publique** (`imgui.h`), pas juste en interne. Resultat : la simple inclusion de `imgui.h` faisait deja echouer toute compilation qui le touchait (`main.cpp`, le coeur ImGui lui-meme, le backend) - pas quelque chose de patchable ligne a ligne.

Plutot que de continuer a deviner, verification reelle : clone de plusieurs tags GitHub d'ImGui et compilation de leur coeur avec `g++ -std=c++03` (le mode le plus proche disponible dans mon bac a sable d'un compilateur pre-C++11 strict) jusqu'a trouver une version compilant sans la moindre erreur. **v1.72b (juillet 2019)** est cette version : zero `enum class`, zero enum avant-declare a type explicite, zero NSDMI dans son coeur - et elle a deja tout ce dont l'application a besoin (`BeginTabBar`/`BeginTabItem`, glissieres, cases a cocher, et surtout l'API de navigation manette par `io.NavInputs[ImGuiNavInput_XXX]` que `main.cpp` utilise deja - cette API a ete remplacee par un systeme d'evenements clavier dans les versions posterieures a ~1.87, donc v1.72b correspond aussi mieux au code deja ecrit, pas seulement au compilateur).

Vendorisation refaite a l'identique (clone reel depuis GitHub, tag v1.72b, memes fichiers coeur), backend D3D9 reecrit a partir du vrai `examples/imgui_impl_dx9.cpp` de cette version (plus simple que celui de v1.90.9 - variables globales statiques, pas d'indirection `ImGui_ImplDX9_Data`/`GetBackendData()`). `imgui_tables.cpp` (fonctionnalite tables, introduite bien apres 1.72b) retire de la compilation - n'existe pas dans cette version et n'etait pas utilise.

Un deuxieme probleme independant a ete trouve et corrige au meme moment : `imgui.cpp` tente `#include <Windows.h>` pour le presse-papier/IME quand `_WIN32` est defini (ce qui semble etre le cas sur ce toolset, meme pour une cible console) - `<Windows.h>` n'existe pas dans les chemins d'include du XDK Xbox 360 (`xtl.h` n'est pas un remplacement direct). Corrige en definissant `IMGUI_DISABLE_WIN32_FUNCTIONS` dans `imconfig.h` (point d'extension officiellement prevu par ImGui pour ce cas) - pas besoin de presse-papier bureau ni d'IME sur une interface manette/clavier Xbox de toute facon.

Verification reelle avant integration : les trois fichiers du coeur (`imgui.cpp`, `imgui_draw.cpp`, `imgui_widgets.cpp`) compilent sans la moindre erreur ni avertissement lie a une feature C++11 avec `g++ -std=c++03 -c`, avec `imconfig.h` tel qu'utilise dans le projet.

## XexUtils - bibliotheque d'utilitaires Xbox 360 (adoptee et vendorisee, 2026-08-01)

A la demande explicite de l'utilisateur : [ClementDreptin/XexUtils](https://github.com/ClementDreptin/XexUtils), licence MIT (compatible), verifiee reellement sur GitHub - bibliotheque mature, maintenue activement, compilable avec le meme toolchain (VS2010 pour la cible Xbox 360) deja utilise pour le plugin. Remplace du code qu'on aurait sinon ecrit a la main : `Xam.h`/`XNotify` pour les notifications natives (equivalent de notre wrapper `XNotifyUI` cote plugin), `Input.h`/`GetInput()` pour la lecture manette avec debounce et thumbsticks deja normalises.

**API reelle confirmee en lisant les headers** (`XexUtils/src/Input.h`, `Xam.h`) plutot que devinee, et corrigee dans `application/main.cpp` en consequence :
- `XexUtils::Input::Gamepad *XexUtils::Input::GetInput(uint32_t userIndex = 0)` - ne retourne jamais `nullptr` (pointe toujours vers un slot statique interne), expose `wButtons`/`PressedButtons` (edge, deja debounce) et `ThumbLeftX`/`ThumbLeftY` (deja normalises en [-1,+1], deadzone deja appliquee).
- `void XexUtils::Xam::XNotify(const std::string &text, XNOTIFYQUEUEUI_TYPE type = XNOTIFYUI_TYPE_PREFERRED_REVIEW)` - prend une `std::string` etroite (pas `wstring` comme suppose dans le tout premier jet de `main.cpp`), convertit en interne.

**Vendorise reellement** dans `hiddriver360-master/XexUtils/` (clone complet moins `.git` et moins le contenu du sous-module `deps/BearSSL`, indisponible - voir plus bas). **Choix delibere de ne pas referencer `XexUtils.vcxproj` comme projet complet dans `hiddriver.sln`** : ce projet lie aussi `Http.cpp`/`Socket.cpp`/`TlsSession.cpp`, qui incluent des headers BearSSL - meme sans jamais appeler ces fonctions, la compilation de ces fichiers echouerait sans BearSSL present. `application.vcxproj` compile donc directement les 5 fichiers reellement necessaires (`Input.cpp`, `Xam.cpp`, et leurs dependances directes `Formatter.cpp`/`General.cpp`/`Log.cpp`), verifie fichier par fichier en lisant chaque chaine d'`#include` pour confirmer qu'aucun ne touche a Http/Socket/BearSSL. C'est toujours le vrai code XexUtils (pas une reecriture) - juste un sous-ensemble compile directement plutot qu'une dependance de projet qui echouerait pour une raison sans rapport avec ce dont on a besoin.

**Gap BearSSL** : le sous-module `deps/BearSSL` n'a pas pu etre clone depuis mon bac a sable (403 du proxy reseau sur bearssl.org, une restriction du sandbox, pas un probleme du projet). `deps/BearSSL/` existe mais est vide. Sans impact pour l'usage actuel (Input/Xam uniquement). Si un futur besoin necessite `Http.h`/`Socket.h` (ex: verification de mise a jour en ligne), il faudra que tu lances `git submodule update --init --recursive` dans `XexUtils/` sur ta propre machine (reseau non restreint) avant d'ajouter `XexUtils.vcxproj` comme vrai projet reference.

## [Historique] Choix technique initial : XUI (abandonne, voir section ci-dessus)

Le XDK Xbox 360 (deja installe par l'utilisateur, SDK preset "FULL") inclut **XUI** (Xbox User Interface), le framework graphique officiel de Microsoft pour les interfaces Xbox 360 - celui qui propulse le dashboard officiel, et des dashboards homebrew comme FreeStyle Dash (dont Aurora descend directement) et vraisemblablement Aurora elle-meme.

Preuve concrete que ce choix est solide, pas juste theorique : [`frankischilling/snes360-enhanced`](https://github.com/frankischilling/snes360-enhanced) (GitHub, licence **GPL-3.0** - compatible avec la notre), un portage Xbox 360 actif et recent (derniere release nov. 2025), "confirmed working on Xbox 360 SDK 21256.3", **buildable avec Visual Studio 2010+ et le meme XDK**. Il utilise des scenes XUI + Direct3D9 + le clavier virtuel Xbox (`XShowKeyboardUI`-style) avec un pattern d'I/O asynchrone non bloquant pour ne pas geler l'interface pendant la saisie - exactement le genre de piege (freeze si mal fait) qu'on a deja rencontre et corrige cote plugin cette session (cf. PROJECT_NOTES.md, boot freeze / re-entrance USB).

Pourquoi XUI plutot que dessiner nous-memes en D3D9 :

- Deja inclus dans le SDK installe - zero dependance a ajouter.
- Pipeline connu et documente : ecrans decrits en XML (`.xui`), compiles en binaire (`.xur`) par l'outil `XUI Studio` / `xuicomp` (fourni avec le XDK), empaquetes en `.xzp` (Xbox UI Package) charge au runtime - snes360-enhanced documente ce pipeline pas a pas (y compris un script `Build_XZP.bat` qu'on peut adapter).
- Widgets standards (listes, boutons, texte, focus/navigation) au lieu de tout redessiner a la main - moins de surface d'erreur sur du code que je ne peux ni compiler ni executer moi-meme.

**Limite honnete, a dire clairement plutot que la decouvrir plus tard** : je peux ecrire les fichiers `.xui` (c'est du XML, donc du texte que je produis directement comme n'importe quel fichier source) et tout le code C++ de l'application. Mais la compilation `.xui` -> `.xur` necessite `XUI Studio` / `xuicomp`, un outil Windows du XDK que je ne peux pas executer depuis mon bac a sable Linux. Cette etape reste cote utilisateur, avec des instructions precises (meme esprit que `build_and_deploy.ps1`, deja utilise pour le plugin).

## IPC entre les deux .xex

Probleme concret : le wizard doit savoir "quelle touche/bouton vient d'etre presse" pour la lier a l'action affichee a l'ecran. Deux `.xex` separes ne partagent pas de memoire directement, et faire lire l'USB par les deux en parallele risquerait un conflit d'acces au device (le plugin applique deja une politique stricte de "qui a le droit de claim quel device" - `CLAIM_ONLY_SUPPORTED_DEVICES`, voir main.cpp).

**Solution retenue** : le plugin (deja actif en permanence) ecrit en continu un petit fichier d'etat, `input_state.json`, depuis le meme thread securise qui gere deja le hot-reload de `config.json` et le flush des logs (`MappingManagerThreadProc`) - aucun nouveau risque de reentrance introduit, c'est le thread le plus eprouve du projet. L'application lit ce fichier a chaque frame, sur le meme principe deja teste et confirme fonctionnel sur hardware pour le hot-reload (`CreateFileA` + `GetFileTime`, pas `stat()` - voir PROJECT_NOTES.md pour pourquoi) plutot que d'inventer un nouveau mecanisme.

## Profils par jeu, deadzone, courbe de sensibilite

Etendu dans le schema JSON existant (`mapping.h`/`mapping.cpp`, deja couvert par les tests dans `hiddriver/tests/`) : un profil est identifie par le titleId Xbox (`XamGetCurrentTitleId()`, deja utilise ailleurs dans le plugin pour d'autres besoins), et contient son propre mapping clavier/souris, sa deadzone et sa courbe de sensibilite. Absence de profil pour un jeu = profil par defaut (comportement actuel, inchange).

## `input_state.json` - format (implemente cote plugin le 2026-08-01)

Ecrit par le plugin a chaque iteration de `MappingManagerThreadProc` (~100ms, meme cadence que le flush des logs, deja eprouvee sur hardware), uniquement si un clavier et/ou une souris boot-protocol est connecte(e) - rien n'est ecrit si aucun des deux n'est branche.

```json
{ "keyCode": 26, "mouseButton": -1, "tick": 123456 }
```

- `keyCode` : code HID (page 0x07) de la premiere touche actuellement maintenue sur le premier clavier connecte, ou `-1` si rien n'est presse. Les touches modificatrices (Ctrl/Maj/Alt) sont rapportees avec leurs codes 0xE0-0xE7 si aucune touche normale n'est maintenue.
- `mouseButton` : index du premier bouton souris maintenu sur la premiere souris connectee (0=gauche, 1=droit, 2=milieu, convention HID deja utilisee ailleurs dans le projet), ou `-1` si rien n'est presse.
- `tick` : `GetTickCount()` au moment de l'ecriture, pour que le frontend puisse detecter un plugin fige (fichier qui ne change plus) plutot que de rester bloque a attendre indefiniment.

Cote plugin : `Controller::lastKeyCode` / `Controller::lastMouseButtonIdx` sont ecrits depuis le contexte USB brut (memes regles de securite que `mouseAccumX` - ecriture d'entier simple uniquement, aucun I/O), initialises a `0xFF` (= rien de presse) a la fois au demarrage du thread et a chaque liberation de slot (`HidRemoveDeviceHook`), pour eviter qu'un slot reutilise ne rapporte une pression fantome avant son premier vrai rapport HID.

Cote frontend (a construire, voir plus bas) : lire ce fichier a chaque frame avec le meme mecanisme deja teste et fonctionnel pour le hot-reload de `config.json` (`CreateFileA` + `GetFileTime`, pas `stat()`), et nourrir `MappingAssistant::Tick(keyCode ou mouseButton selon l'etape en cours)`.

## Ce qui reste incertain, a verifier au premier build/test hardware

- Ce nouveau bloc d'ecriture dans `main.cpp` n'a pas encore ete compile ni teste sur console (je ne peux pas compiler de code Xbox depuis mon bac a sable) - premiere chose a verifier au prochain build du plugin.
- Le nom exact des fonctions XUI/clavier disponibles dans le XDK installe chez l'utilisateur (les versions XDK varient legerement) - a confirmer au premier essai de build de `application.xex`, comme pour tout code Xbox que je ne peux pas compiler moi-meme.
