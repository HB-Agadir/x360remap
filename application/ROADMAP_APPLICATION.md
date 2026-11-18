# Cahier des charges - application.xex

Gestionnaire de configuration graphique (wizard clavier, réglages souris, profils par jeu) pour le fork hiddriver360. Ce document fixe les jalons jusqu'à une version utilisable sur console, avec pour chacun un objectif, un livrable, et un critère de validation vérifiable - pas de jalon "terminé" sans preuve concrète.

## Méthode de travail

Elle découle directement de ce qu'on a appris cette session, pas de principes abstraits :

1. **Toute logique indépendante de l'API Xbox est écrite ET testée dans mon bac à sable avant d'être intégrée** (compilée et exécutée réellement, comme `hiddriver/tests/` et `application/tests/` aujourd'hui) - je ne livre pas de logique "probablement correcte".
2. **Tout code touchant à l'API Xbox (D3D9, XInput, XUI, XexUtils) est un premier jet que je ne peux ni compiler ni exécuter.** Le cycle est : j'écris -> tu builds -> tu me colles l'erreur ou le comportement exact -> je corrige -> on recompile. Un jalon qui touche à cette catégorie n'est validé que par un retour concret de ta part, jamais par supposition de ma part.
3. **Sauvegarde avant toute modification non triviale** (`_backups/<date>/`), comme déjà en place.
4. **Je décide de l'implémentation technique et j'exécute** ; je ne te demande pas de choisir entre des options d'architecture. Je te sollicite uniquement pour : lancer un build, me donner un résultat/erreur, ou trancher une préférence produit (pas technique).
5. Chaque jalon est marqué fait uniquement quand son critère de validation est rempli - pas avant.

## État actuel (déjà fait, base de départ)

- `plugin.xex` : détection clavier/souris, fusion en un seul joueur, hot-reload de `hiddriver.json`, écriture continue de `input_state.json` (ce dernier point pas encore rebuild/testé sur console).
- Schéma `hiddriver.json` étendu : profils par jeu, deadzone, courbe de sensibilité - testé.
- `application/wizard_session.*`, `config_writer.*`, `input_state_reader.*` : toute la logique du wizard et des réglages, testée (7 scénarios, tous verts).
- Cœur Dear ImGui v1.90.9 vendorisé et compilé avec succès en C++14 portable (`application/third_party/imgui/`).
- XexUtils vendorisé (`XexUtils/`), API réelle (`Input::GetInput`, `Xam::XNotify`) confirmée en lisant les headers et câblée dans `application/main.cpp`.
- `application/main.cpp` + `imgui_impl_dx9_xenon.*` : premier jet du rendu ImGui/D3D9 + navigation manette XexUtils, jamais compilé sur le vrai toolchain Xbox.

## Jalon 1 - Environnement de build complet [FAIT le 2026-08-01]

**Objectif** : tous les fichiers nécessaires sont en place pour tenter une compilation.
**Livrable** : cœur Dear ImGui vendorisé (`application/third_party/imgui/`, voir `VENDORING.md`) ; XexUtils vendorisé dans `XexUtils/` et intégré à `application.vcxproj` en compilant directement les fichiers nécessaires (`Input.cpp`, `Xam.cpp`, `Formatter.cpp`, `General.cpp`, `Log.cpp`) plutôt qu'en référençant `XexUtils.vcxproj` comme projet complet (celui-ci requiert BearSSL pour `Http`/`Socket`, submodule qui n'a pas pu être cloné depuis mon bac à sable - voir `ARCHITECTURE.md`, section XexUtils). `main.cpp` corrigé pour utiliser la vraie API (`XexUtils::Input::GetInput()`, `XexUtils::Xam::XNotify()`) au lieu des noms devinés dans le tout premier jet.
**Validation** : `hiddriver.sln` s'ouvre dans Visual Studio avec les 2 projets (`hiddriver`, `application`) sans erreur de chargement - **observé/à confirmer par toi**, je n'ai pas d'accès à Visual Studio. Reste un jalon "livrable prêt" plutôt que "validé sur ta machine" tant que tu n'as pas ouvert le `.sln` toi-même.
**Risque** : faible - étapes mécaniques. Point d'attention réel : si un futur besoin nécessite `Http.h`/`Socket.h` de XexUtils, il faudra lancer `git submodule update --init --recursive` dans `XexUtils/` sur ta machine avant d'ajouter `XexUtils.vcxproj` comme vrai projet.

## Jalon 2 - Premier build qui compile [FAIT le 2026-08-01]

**Objectif** : `application.xex` compile et s'édite des liens sans erreur.
**Livrable** : itérations sur `main.cpp`/`imgui_impl_dx9_xenon.*`/`application.vcxproj` jusqu'à build propre.
**Validation** : build "Release Retail | Xbox 360" réussi, `application.xex` généré dans `application\Release Retail\` - **confirmé par build réel côté utilisateur** ("Génération : 1 a réussi, 0 a échoué, 0 a été ignoré").
**Risque réalisé** : 6 allers-retours de build réels ont été nécessaires (voir `PROJECT_NOTES.md` pour le détail complet) : includes/NSDMI/enum class/rapidjson endian, ImGui v1.90.9 incompatible (rétrogradé à v1.72b), absence totale de pipeline fixe sur le Xenos (backend réécrit en shaders), conflit de link xkelib/d3d9, et enfin abandon de la compilation de shaders au runtime (D3DXCompileShader introuvable côté link) au profit d'une compilation hors-ligne automatisée via `fxc.exe` (compilateur HLSL du XDK, câblé en `CustomBuild` dans `application.vcxproj`).

## Jalon 3 - Premier rendu à l'écran [FAIT le 2026-08-01]

**Objectif** : l'appli se lance depuis Aurora et affiche quelque chose (même incomplet/moche).
**Livrable** : correction du chemin de rendu D3D9/ImGui - c'est ici que le point d'incertitude n°1 (pipeline fixe vs shaders, voir `imgui_impl_dx9_xenon.cpp`) se résout concrètement.
**Validation** : lancement depuis Aurora, au moins une fenêtre ImGui visible à l'écran (texte ou widgets, peu importe l'apparence) - **confirmé par photo d'écran réelle** : fenêtre "Configuration clavier / souris", onglets "Wizard clavier"/"Reglages souris", texte du wizard entièrement lisible.
**Risque réalisé** : élevé comme prévu - 2 bugs réels rencontrés et corrigés après le premier écran noir : (1) matrice de projection mal empaquetée (colonnes vs lignes, fix `/Zpr` sur `fxc.exe`) qui effondrait toute la géométrie en une ligne ; (2) atlas de police invisible car les textures Xbox 360 sont tuilées (tiled) par défaut en mémoire et le remplissage CPU était fait en supposant un agencement linéaire comme sur PC (fix `D3DFMT_LIN_A8R8G8B8`). Voir `PROJECT_NOTES.md` pour le détail complet de chaque diagnostic.

## Jalon 4 - Navigation manette fonctionnelle [REPORTÉ le 2026-08-01, décision utilisateur]

**Objectif** : se déplacer entre les onglets et les widgets à la manette (D-pad/stick, A/B).
**Livrable** : ajustement de `UpdateGamepadNavigation()` selon le comportement réel observé.
**Validation** : navigation fluide entre "Wizard clavier" et "Réglages souris", sélection d'un widget visible (surbrillance).
**Risque** : moyen - l'API `io.NavInputs` d'ImGui 1.90.9 est documentée mais jamais éprouvée sur ce portage.
**Statut réel** : la navigation manette d'origine restait imparfaite (un seul axe fonctionnait), mais le curseur souris ajouté en parallèle (voir section "Curseur souris" de `PROJECT_NOTES.md`) couvre déjà le besoin - clavier détecté, wizard fonctionnel, curseur cliquable confirmés sur hardware réel. Décision utilisateur : sauter ce jalon pour économiser du temps/budget, `UpdateGamepadNavigation()` reste en l'état (code de secours, non prioritaire). À reprendre uniquement si redemandé.

## Jalon 5 - Wizard clavier bout en bout sur hardware [FAIT le 2026-08-02]

**Objectif** : configurer un vrai clavier de bout en bout depuis l'appli.
**Livrable** : aucun changement de code attendu a priori (la logique est déjà testée) - ce jalon sert à confirmer que `input_state.json` (écrit par le plugin, jamais testé sur console) est bien lu par l'appli en temps réel.
**Validation initiale (2026-08-02, incomplete)** : clavier détecté par l'appli, wizard fonctionnel pour la NAVIGATION (`input_state.json` bien lu en temps réel post-fix `MountHdd()`, overlay de debug `valid=1`, tick réel qui avance). Mais la SAUVEGARDE elle-meme s'est revelee cassee - voir ci-dessous.
**Bug reel trouve en analysant un vrai `hiddriver.json` recupere via `rgh fs cat`** : l'entree clavier existait (bon vid/pid) mais sans aucune touche (`keys` absent) - le wizard se terminait ("Configuration enregistree" affiche) sans avoir reellement assigne de touches. Cause : `MappingAssistant::holdTicksToSkip_` (defaut 60) est calibre pour l'assistant manette original, appele une fois toutes les ~50ms. `DrawWizardTab()` dans `application.xex` appelle `Tick()` une fois par FRAME ImGui (~16ms a 60fps) - donc "60 ticks" ne representait qu'~1s de maintien reel au lieu des "3 secondes" annoncees a l'ecran, ce qui pouvait faire sauter (skip) une etape des qu'une touche etait relachee un peu tard, videant silencieusement `Results()`.
**Fix applique** : `WizardSession` accepte desormais un `holdTicksToSkip` explicite (nouveau parametre, defaut 60 inchange pour ne pas casser les tests existants) ; `DrawWizardTab()` passe explicitement 180 (60fps*3s). Indicateur de progression ajoute a l'ecran ("Etape X/25, assignees : Y") pour detecter immediatement un skip inattendu plutot qu'apres coup. Logique testee via g++ (`TestWizardHoldTicksMatchesCallerCadence`, reproduit le bug avec l'ancien defaut puis confirme le fix).
**Confirmation finale** : refait avec le fix - `app_log.txt` a montre "Wizard clavier: 21 touches enregistrees" (21, pas 25 - stick droit retire entre-temps, voir Jalon 6/PROJECT_NOTES) et l'utilisateur a confirme "pour le clavier c'est ok" après test en jeu.

## Jalon 6 - Réglages souris bout en bout sur hardware [FAIT le 2026-08-02]

**Objectif** : ajuster sensibilité/deadzone/inversion Y via les glissières et voir l'effet en jeu.
**Livrable** : idem jalon 5, logique déjà testée, validation hardware uniquement - a fini par inclure deux corrections de bugs réels et une inertie ajoutée en cours de route (voir Risque réalisé).
**Validation** : **confirmé par l'utilisateur sur hardware réel, deux jeux différents** (Tomb Raider Underworld, Gears of War 3) - "très fluide". Diagnostiqué avec de vrais chiffres (log DIAG côté plugin, retiré une fois la calibration confirmée) plutôt que des suppositions successives.
**Risque réalisé** : plus élevé que prévu - la sensibilité/deadzone existaient dans le schéma/UI depuis longtemps mais n'étaient jamais correctement branchées côté plugin (deadzone : jamais lue du tout ; sensibilité : formule qui saturait dès les premiers pixels de mouvement). Découvert aussi en cours de route : une limitation structurelle de l'émulation souris->stick (impossible de maintenir "à fond" en continu comme un vrai stick) - traitée avec une inertie (décroissance de vitesse, en dur dans le code, pas encore exposée dans l'UI).

## Jalon 7 - Profils par jeu + deadzone/courbe (UI) [PREMIER JET le 2026-08-02, pas encore confirmé sur hardware]

**Objectif** : exposer dans l'interface ce qui existe déjà dans le schéma JSON testé (`HidDeviceProfile`).
**Livrable** : un `ImGui::Combo` pour choisir/créer un profil par `titleId`, un `ImGui::SliderFloat` pour l'exposant de la courbe de sensibilité.
**Validation** : créer un profil pour un jeu précis, vérifier qu'il s'applique uniquement à ce jeu (via `XamGetCurrentTitleId()` côté plugin - à câbler côté plugin si pas déjà fait, voir Notes).
**Risque anticipé, confirmé réel** : la question posée dans la note ci-dessous ("le plugin lit-il déjà `profiles[]` au runtime ?") avait sa réponse - non. Le schéma était bien parsé/sérialisé (`hiddriver/mapping.cpp`, déjà couvert par `TestProfilesDeadzoneCurveRoundTrip`) mais jamais consulté par `XInputdReadStateHook` : aucune référence à `profiles`/`curveType`/`XamGetCurrentTitleId` dans tout `hiddriver/main.cpp` avant ce jalon - même nature de lacune que le bug deadzone du Jalon 6 (schéma+UI existants, zéro effet en jeu).

**Ce qui a été fait** :
- `hiddriver/mapping.h`/`.cpp` : `FindActiveProfile()`, `ResolveEffectiveMouseSettings()` (résout sensibilité/invertY/deadzone/courbe en combinant device + profil actif, sentinelles "inherit" respectées) et `ApplySensitivityCurve()` (courbe exponentielle optionnelle, appliquée après mise à l'échelle/clamp, avant la deadzone). Logique pure, **testée en g++** (`hiddriver/tests/test_mapping_assistant.cpp` : `TestFindActiveProfile`, `TestResolveEffectiveMouseSettings`, `TestApplySensitivityCurve` - 10 fonctions de test au total, toutes vertes).
- `hiddriver/main.cpp` (`XInputdReadStateHook`) : câblé sur `ResolveEffectiveMouseSettings(mouseCtrl->map, g_lastTitleId)` - `g_lastTitleId` était déjà tenu à jour (voir le bloc "Automatic title-switch rebind", jamais exploité jusqu'ici pour les profils). **PREMIER JET NON COMPILÉ/TESTÉ SUR CONSOLE** (code touchant l'API Xbox, comme toujours).
- `application/config_writer.h`/`.cpp` : `ListMouseProfiles`, `ApplyMouseProfile` (crée ou remplace intégralement un profil, pas de fusion partielle), `RemoveMouseProfile`. Testés via fichier temporaire (`application/tests/test_wizard_session.cpp`, 5 nouveaux tests) - **un bug réel attrapé par ces tests avant tout build** : `RemoveMouseProfile` renvoyait `true` même quand le `titleId` demandé n'existait pas (la boucle de filtrage recopiait simplement tous les profils inchangés sans le signaler) ; corrigé en comparant le nombre de profils avant/après.
- `application/main.cpp` : nouvel onglet "Profils" (`DrawProfilesTab`) - combo profil existant/nouveau, saisie du Title ID en hexadécimal (l'appli tourne hors d'un jeu, lancée depuis Aurora, donc `XamGetCurrentTitleId()` n'y a pas de sens - contrairement au plugin), sliders sensibilité/deadzone/exposant, checkbox invertY, combo type de courbe, boutons Enregistrer/Supprimer. **PREMIER JET NON COMPILÉ** (D3D9/ImGui/XexUtils, comme tout `application/main.cpp`). Un bug de logique (mélange de deux espaces d'index différents pour le combo profil, aurait cassé la persistance de la sélection entre deux frames) a été repéré et corrigé **avant même la compilation**, en relisant le code par rapport au contrat attendu d'`ImGui::Combo`.
- Portée volontairement exclue de ce jalon (voir `config_writer.h`) : un profil ne peut pas encore surcharger le mapping boutons/clavier (`buttonMap`/`keyMap`) via cet écran, uniquement sensibilité/deadzone/invertY/courbe - le schéma le permettrait, mais hors scope pour rester dans le budget.

**Confirmation hardware partielle (2026-08-02)** : premier écran testé - rendu correct, mais `ImGui::InputText` du Title ID ne recevait aucun caractère (bug réel, voir "Ce qui a été fait" ci-dessous) et l'utilisateur a demandé deux extensions, toutes deux traitées le même jour :

- **Bug corrigé** : l'appli n'avait jamais câblé le clavier physique vers la machinerie de saisie texte d'ImGui - fix en réutilisant le snapshot mono-touche déjà existant (`input_state.json`, même mécanisme que le wizard), touche par touche avec détection de front montant. Champ passé en lecture seule côté ImGui (`ImGuiInputTextFlags_ReadOnly`), piloté entièrement à la main.
- **Historique des jeux vus (`hiddriver/known_titles.h`/`.cpp`)** : le plugin persiste désormais chaque nouveau `titleId` détecté (`HDD:\X360RemapStudio\known_titles.txt`) - l'onglet Profils propose un combo "jeux détectés" qui préremplit le Title ID, la saisie manuelle restant disponible en repli pour un jeu jamais lancé avec le plugin actif. Logique de parsing/sérialisation/dédoublonnage testée en g++ (4 nouveaux tests).
- **Raccourci F9 "sauvegarder depuis le jeu"** : maintenir F9 ~1.5s en jeu fige les réglages souris effectifs courants comme profil du jeu en cours, sans repasser par l'appli. La logique de création/remplacement de profil (`SetMouseProfile`) a été extraite dans `hiddriver/mapping.cpp` - **partagée** entre `application/config_writer.cpp` (écran Profils) et `hiddriver/main.cpp` (raccourci), pour éviter deux implémentations qui pourraient diverger. Détection/comptage du maintien fait dans `MappingManagerThreadProc` (thread déjà utilisé pour tout I/O disque, pas dans le hot path `XInputdReadStateHook`).
- **Overlay in-game (mini-menu affiché par-dessus le jeu)** évalué à la demande de l'utilisateur mais **pas construit ce round-ci** - voir `PROJECT_NOTES.md`, section "Faisabilité d'un overlay in-game" pour l'analyse complète. Resterait un chantier à part (nouvelle catégorie de risque : hook D3D9 dans le moteur de rendu d'UN JEU EN COURS, pas notre propre device contrôlé) - candidat à un futur jalon dédié, pas intégré au planning actuel.

**Confirmation hardware (2026-08-02, suite)** : `hiddriver.xex` reconstruit après le fix C2440 (`const HidDeviceMapping*` - voir Notes ci-dessous), F9 confirmé fonctionnel par l'utilisateur ("j'ai vue la notification"). Deux manques réels remontés dans la foulée, tous deux corrigés :

- **Nom du jeu absent** : le combo "jeux détectés"/profils n'affichait que le `titleId` en hexadécimal, y compris pour un jeu déjà joué (ex: Tomb Raider) - aucun moyen de savoir "de quel jeu il s'agit" sans outil externe. Ajout d'un champ `gameName[32]` sur `HidDeviceProfile` (`hiddriver/mapping.h`/`.cpp`), lu/écrit dans le JSON (`"gameName"`, optionnel), affiché dans le combo Profils et le combo "jeux détectés" sous la forme `Nom (titleId)` quand renseigné. Convention "vide/nul = ne pas toucher au nom existant" reprise par `SetMouseProfile`/`ApplyMouseProfile`, pour que le raccourci F9 (qui n'a aucun moyen de saisir un nom depuis le jeu) ne l'efface jamais silencieusement.
- **Saisie clavier limitée à l'hexadécimal** : le champ Title ID acceptait `0-9`/`A-F` uniquement (voir le fix InputText plus haut) - insuffisant pour taper un nom. Ajout de `HidKeyCodeToAsciiChar` (`application/main.cpp`, alphabet `a-z` minuscules + chiffres + espace - pas de majuscules, le snapshot `input_state.json` ne porte pas l'état de Shift) et d'un champ "Nom du jeu (optionnel)" dans l'onglet Profils. Comme le snapshot ne porte qu'UNE touche par tick, un jeu de deux boutons radio ("Taper le Title ID" / "Taper le nom du jeu") détermine quel champ reçoit la frappe pour un nouveau profil ; un profil existant n'a que le nom à éditer (Title ID figé), donc pas d'ambiguïté à résoudre là.
- Testé en g++ des deux côtés : `hiddriver/tests/test_mapping_assistant.cpp` (nom fourni enregistré, nom absent préservé, round-trip JSON avec `"gameName"`) et `application/tests/test_wizard_session.cpp` (`TestApplyMouseProfileGameName`) - tous verts. **PREMIER JET NON COMPILÉ SUR CONSOLE** pour la partie UI (`application/main.cpp`), comme toujours pour ce fichier.

**Reste à faire pour clore ce jalon** : build + déploiement des deux `.xex` avec ce dernier ajout, puis test réel - taper un nom pour un profil (nouveau et existant), vérifier qu'il survit à un redémarrage et qu'il s'affiche bien dans les deux combos, et confirmer qu'un enregistrement F9 ultérieur ne l'efface pas.

**Extension boutons/touches par profil (2026-08-02, suite - demande utilisateur)** : deux besoins remontés ensemble, tous deux liés au fait qu'un profil ne pouvait jusqu'ici surcharger QUE sensibilité/deadzone/invertY/courbe, pas les boutons/touches (exclu du scope initial de ce jalon, voir plus haut) :

1. **Profil sans re-remapper tout le clavier/souris à chaque jeu** ("associer RB au clic droit uniquement pour CE jeu, sans changer le mapping global à chaque partie"). Le schéma JSON avait déjà `HidDeviceProfile::buttonMap`/`keyMap` (jamais consultés au runtime - vérifié par grep, tout `hiddriver/main.cpp` lisait directement `device->buttonMap`/`device->keyMap`). Ajout de `ResolveEffectiveButtonMap`/`ResolveEffectiveKeyMap` (`hiddriver/mapping.h`/`.cpp`, même famille que `ResolveEffectiveMouseSettings`) - convention "tout ou rien" (pas de fusion champ par champ) : si le profil actif définit son propre buttonMap/keyMap, il remplace ENTIÈREMENT celui du device pour ce jeu, sinon le device s'applique tel quel. Câblé dans les 4 endroits de `hiddriver/main.cpp` qui lisaient `map->buttonMap`/`map->keyMap` en dur (`HidFillButtonsReport`, `HidFillMouseState`, `HidFillBootMouseState`, `HidFillBootKeyboardState`). `SetProfileButtonOverride`/`SetProfileKeyOverride` (mapping.h/.cpp) créent/mettent à jour UNE SEULE entrée du buttonMap/keyMap d'un profil sans toucher au reste (contrairement à `SetMouseProfile` qui remplace tout le profil) - nécessaire pour le point 2.

2. **Lier un bouton depuis le jeu, sans menu, sans sortir du jeu** - proposition explicite de l'utilisateur ("pas besoin de menu... juste des notifications, comme F9"). Contrainte technique confirmée avant de commencer : `application.xex` et un jeu ne peuvent pas tourner en même temps sur Xbox 360 (un seul .xex actif à la fois hors dashboard), donc ce flux DOIT vivre entièrement dans `hiddriver.xex` - même famille de solution que le Jalon 11 (overlay) écartait le risque (aucun rendu dans le jeu, uniquement `XNotifyUI`, exactement comme F9).

   Flux retenu ("liaison rapide", `hiddriver/main.cpp`) : maintenir **Back+Start** ensemble ~1.5s sur la VRAIE manette physique (branchée à côté de la souris/clavier - PAS celle émulée par ce plugin) arme le mode pour le profil du jeu en cours ; le prochain bouton pressé sur cette vraie manette devient la CIBLE (`RealButtonTarget`, table associant chaque bouton XInput à la fois à un `ButtonsReport::*` et un `KeyAction` - les deux représentations coexistantes du projet) ; la touche clavier ou le clic souris suivant devient la SOURCE et est écrit dans le profil via `SetProfileKeyOverride`/`SetProfileButtonOverride`. Chaque étape confirmée par notification (`XNotifyUI`), timeout de sécurité de 10s par étape (annule silencieusement si abandonné). Back/Start volontairement exclus des cibles possibles (ce sont les boutons d'activation eux-mêmes).

   **Point technique le plus incertain de tout le jalon** : lire la VRAIE manette (distincte de celle émulée par ce plugin) depuis `MappingManagerThreadProc`. Confirmé auprès de l'utilisateur que ses manettes sont sans fil - `ReadRealControllerState` interroge `XInputGetState()` directement (pas `XInputdReadStateDetour`, qui n'intercepte que les `dwDeviceContext` émulés par ce plugin - une valeur totalement différente d'un simple user index 0..3) pour chaque slot 0..3 non déjà occupé par un de nos propres périphériques HID (`Controller::userIndex`, déjà suivi). Ce même appel `XInputGetState()` est déjà utilisé ailleurs dans `hiddriver/main.cpp` (repli de `GetCapabilities`) pour lire un vrai état manette, ce qui donne une base de confiance raisonnable, mais **rien de tout ce mécanisme n'est testable en g++** (dépend entièrement de l'API XInput Xbox) - **PREMIER JET NON COMPILÉ SUR CONSOLE**, à valider avec la plus grande prudence (le pire cas serait un blocage/plantage du thread `MappingManagerThreadProc`, qui gère aussi le hot-reload JSON et le raccourci F9 - une régression ici casserait ces deux fonctionnalités déjà confirmées).

   Testé en g++ (partie logique pure, sans API Xbox) : `ResolveEffectiveButtonMap`/`ResolveEffectiveKeyMap` (device vs profil, jeu différent, profil sans config propre) et `SetProfileButtonOverride`/`SetProfileKeyOverride` (création avec sentinelles correctes, remplacement en place par idx/code, préservation du reste du profil) - `hiddriver/tests/test_mapping_assistant.cpp`, 4 nouveaux tests, tous verts.

**Reste à faire (extension boutons/touches)** : build + déploiement de `hiddriver.xex` (reboot complet requis), test réel avec la vraie manette sans fil de l'utilisateur - vérifier que Back+Start armé n'interfère pas avec le jeu, que la cible/source se capturent comme attendu, et surtout que rien ne régresse sur F9/le hot-reload JSON (même thread).

**Confirmation hardware (2026-08-03)** : testé en conditions réelles par l'utilisateur, avec une vraie manette sans fil. F9 et le hot-reload JSON toujours fonctionnels (pas de régression). Séquence complète observée dans `hiddriver_log.txt` :
```
Liaison rapide armee - attente du bouton cible
Liaison rapide annulee (delai depasse)          <- 1er essai, timeout 10s normal (pas assez rapide)
Liaison rapide armee - attente du bouton cible
Liaison rapide: cible capturee (index 5)         <- RB
Liaison rapide: cible index 5 liee
hiddriver.json changed - mappings reloaded, no reboot needed
```
Utilisateur confirme qu'en jeu, l'action associée à RB se déclenche bien via la touche/le clic lié - donc `ReadRealControllerState` (lecture de la vraie manette, l'hypothèse la plus incertaine du jalon) fonctionne comme prévu sur ce hardware, et `ResolveEffectiveButtonMap`/`ResolveEffectiveKeyMap` appliquent bien la surcharge du profil en jeu. **Les deux notions (profils avec boutons/touches propres + liaison rapide sans menu) sont validées de bout en bout sur console réelle.** Ce jalon (profils buttons/keys) est clos.

## Jalon 8 - Sélection dynamique des périphériques

**Objectif** : remplacer les VID/PID actuellement en dur dans `main.cpp` par une vraie liste (lue depuis `hiddriver.json` ou détectée en direct).
**Livrable** : écran de sélection avant le wizard/réglages.
**Validation** : deux souris différentes proposées séparément si les deux ont déjà une entrée dans `hiddriver.json`.
**Risque** : faible - logique de lecture déjà présente dans `mapping.cpp`.

**Implémenté (2026-08-03)** : `hiddriver.json` n'a pas de champ "type" explicite par device (le plugin distingue souris/clavier par la classe USB au moment de la détection, jamais persisté tel quel dans le JSON) - `ListKnownDevices()` (`application/config_writer.h`/`.cpp`) classe donc chaque device par heuristique sur ce qui a déjà été écrit pour lui, au niveau device OU profil : un `keyMap` non vide (le wizard clavier est la seule chose qui en écrit un) → `looksLikeKeyboard` ; un `buttonMap`/`mouseSensitivity`/`deadzone` non nul (seuls les écrans souris de cette appli en écrivent) → `looksLikeMouse`. Couvre explicitement le cas du raccourci "liaison rapide" (Jalon buttons/keys, Back+Start) qui ne touche jamais les réglages globaux du device, seulement un profil.

Côté UI (`application/main.cpp`) : `DrawDeviceSelector()`, affiché juste sous le diagnostic visuel, avant la barre d'onglets - deux combos ("Clavier"/"Souris"), chacun avec "Par défaut (VID:PID)" en premier choix (repli identique au comportement d'avant ce jalon, pour ne rien casser au tout premier lancement où `hiddriver.json` est vide) suivi des devices trouvés dans le fichier, plus un bouton "Actualiser" pour relire sans relancer l'appli. Les 7 endroits qui utilisaient `kKeyboardVid`/`kKeyboardPid`/`kMouseVid`/`kMousePid` en dur utilisent maintenant `g_selectedKeyboardVid`/`g_selectedMouseVid` (etc.) - changer de souris/clavier dans le sélecteur invalide `g_mouseSettingsLoaded`/`g_profilesLoaded` pour forcer un rechargement des onglets concernés, plutôt que de laisser afficher les réglages de l'ancien device.

Testé en g++ (`application/tests/test_wizard_session.cpp`, `TestListKnownDevices`) : fichier absent → liste vide ; clavier et souris classés correctement une fois chacun configuré une fois ; un device qui n'a QU'un override de profil (pas de réglage device-level) est quand même classé "souris" - couvre le cas liaison rapide. Tous les tests (hiddriver + application) toujours verts après ce changement.

**Statut console** : partie `config_writer.cpp` testée en g++ (portable, pas d'API Xbox). Partie UI (`main.cpp`, `DrawDeviceSelector` et les combos ImGui) **PREMIER JET NON COMPILÉ SUR CONSOLE**, comme tout `main.cpp` - repose sur les mêmes primitives ImGui (`ImGui::Combo`, getter par callback) déjà confirmées fonctionnelles sur hardware pour les combos "Profil"/"Jeux détectés" du Jalon 7, donc risque faible, mais pas encore buildé/déployé.
**Reste à faire** : build + déploiement de `application.xex`, test réel - vérifier qu'avec un seul clavier/une seule souris connue, "Par défaut" et l'entrée détectée pointent vers le même device (comportement inchangé) ; brancher un second clavier ou une seconde souris (si disponible) et vérifier qu'ils apparaissent bien comme deux entrées séparées et sélectionnables indépendamment.

**Correction (2026-08-03, retour utilisateur réel dès le premier test)** : cette première version ne listait un device QUE s'il avait déjà reçu un réglage explicite via le wizard/réglages souris (heuristique sur `hiddriver.json`) - avec une deuxième souris jamais configurée, les deux combos n'affichaient donc que "Par défaut", strictement inutilisable. Proposition initiale (saisie manuelle du VID/PID) explicitement refusée par l'utilisateur : *"c'est une mauvaise idée le mieux c'est que cela ce fait a la detection du branchement sur la console"*.

Solution retenue : nouveau format `known_devices.txt` (`hiddriver/known_devices.h`/`.cpp`, même esprit que `known_titles.txt` du Jalon 7 - une ligne `VVVV:PPPP:TYPE` par device) alimenté directement par `hiddriver.xex` au moment où il classe un device en souris/clavier via `DetectIsMouse`/`DetectIsKeyboard` (juste après les notifications "Mouse detected"/"Keyboard ready" déjà existantes, `hiddriver/main.cpp`) - donc disponible dès le branchement, sans configuration préalable ni saisie manuelle. `application/config_writer.h`::`ListKnownDevices()` lit maintenant ce fichier en lecture seule (`isMouse` devient un booléen exact - la classification réelle faite par le plugin - et non plus une heuristique côté application). `application/main.cpp` (`DrawDeviceSelector`) adapté en conséquence (un seul champ `isMouse` au lieu de `looksLikeKeyboard`/`looksLikeMouse`, plus d'ambiguïté "device qui matche les deux").

Fichiers projet Visual Studio (`hiddriver/hiddriver.vcxproj`, `application/application.vcxproj`) mis à jour pour inclure `known_devices.h`/`.cpp` (même convention `ObjectFileName` dédié côté `application.vcxproj` que `known_titles.cpp`, pour éviter une collision de `.obj` avec la copie compilée côté `hiddriver.vcxproj`).

Testé en g++ : `hiddriver/tests/test_mapping_assistant.cpp` (parsing/sérialisation/dédoublonnage de `known_devices.txt`, 4 nouveaux tests) et `application/tests/test_wizard_session.cpp` (`ListKnownDevices` relit correctement le nouveau format, fichier absent -> liste vide) - tous verts.

**Portée explicitement reportée** (question posée par l'utilisateur mais pas traitée ici) : gérer 2 joueurs avec chacun clavier+souris (4 périphériques simultanés) - `connectedControllers[4]` supporte déjà jusqu'à 4 devices côté plugin, mais l'attribution à des slots XInput distincts pour 2 joueurs indépendants n'a jamais été conçue ni testée. À traiter comme un jalon à part si le besoin se confirme.

**Bug distinct signalé pendant ce test** : curseur cassé (vertical seulement) sur une 2e souris jamais vue - résolu via le fix `SET_PROTOCOL` général, voir `PROJECT_NOTES.md` ("Curseur casse sur une 2e souris jamais vue").

**Suite (2026-08-03)** : le curseur de la 2e souris fonctionnant, un nouveau test avec les deux souris a révélé que la molette (idx 3/4) ne fonctionnait pour AUCUNE des deux, malgré des sauvegardes confirmées réussies côté appli. Cause réelle trouvée : `LoadMouseSettingsOnce()` ne réinitialisait pas les sélections de boutons/molette en changeant de périphérique dans ce même sélecteur - une souris sans réglage propre héritait silencieusement de l'affichage (et donc, si sauvegardé, des valeurs) de la précédente. Corrigé, plus un diagnostic (`GetMouseButtonMapCountOnDisk`) et la troncature des logs à chaque démarrage pour fiabiliser les prochains tests. Détail complet dans `PROJECT_NOTES.md` ("Bug molette confirmé + fuite de sélection UI + logs qui s'accumulaient").

**Liaison rapide étendue au D-Pad (2026-08-03)**, demande explicite HB : 4 cibles ajoutées à `kRealButtonTargets[]` (`hiddriver/main.cpp`). Back/Start restent hors de cette liste (ils arment le mode liaison rapide lui-même) - point signalé à HB plutôt que tranché unilatéralement.

## Jalon 9 - Finalisation

**Objectif** : version présentable pour la vidéo de démo mentionnée précédemment.
**Livrable** : menu principal, sortie propre de l'application, notifications via `Xam::Notify` (XexUtils) à la place du code fait main, relecture de toute la documentation (`ARCHITECTURE.md`, `BUILD_APPLICATION.md`, `README.md`) — **et sélecteur de langue FR/EN/PT-BR pour l'interface du Studio** (décision utilisateur du 2026-08-02, étendue au portugais (Brésil) le même jour après recherche marché - voir `PROJECT_NOTES.md`) : tous les textes de `application/main.cpp` (onglets, wizard, boutons, notifications) passent par une table de chaînes indexée par langue, avec un réglage persistant dans `hiddriver.json` ou un fichier dédié. Portée volontairement limitée à l'interface du Studio pour ce jalon - ni les logs/notifications du plugin (`hiddriver.xex`), ni la documentation GitHub ne sont concernés ici. Point d'attention propre au portugais : l'atlas de police ImGui (`io.Fonts->AddFontFromFileTTF`) devra couvrir les caractères accentués du PT-BR (ã, õ, ç, etc.), à vérifier au moment de l'implémentation - pas un problème pour FR/EN qui restent en Latin de base.
**Validation** : parcours complet filmable sans erreur ni plantage, du lancement depuis Aurora jusqu'au retour au dashboard ; bascule FR/EN/PT-BR dans l'appli vérifiée à l'écran dans les trois langues (accents portugais bien affichés, pas de glyphe manquant).
**Rappel lié** : tâche encore ouverte depuis le début du chantier clavier - remplacer le `hiddriver.json` personnel par un mapping par défaut universel avant toute publication GitHub (voir `PROJECT_NOTES.md`, section "RAPPEL AVANT PUBLICATION GITHUB").
**Rappel lié (documentation)** : traduction de la documentation GitHub (`README.md` et docs publiques) en anglais - volontairement repoussée à la toute fin du projet, après le Jalon 10, pas à ce jalon-ci (voir `PROJECT_NOTES.md` pour le contexte de cette décision).

## Jalon 10 - Habillage visuel

**Objectif** : passer du look ImGui par défaut (gris, widgets standards) à une interface soignée - direction validée avec l'utilisateur le 2026-08-01 : fond sombre, accents neon (vert clavier / bleu souris), schéma de manette central avec lignes de connexion vers chaque mapping, légende de boutons en bas d'écran.
**Livrable** :
- Thème ImGui personnalisé (`ImGui::PushStyleColor`/`PushStyleVar` sur les couleurs de fond, cadres, sliders, arrondis) - widgets standards, pas de risque technique particulier.
- Police custom chargée dans l'atlas de polices ImGui (`io.Fonts->AddFontFromFileTTF`), déployée avec l'appli.
- Schéma de manette + lignes de connexion dessinés à la main via `ImDrawList` (`AddLine`, `AddCircleFilled`, `AddText`) - aucune primitive nouvelle par rapport à ce que les widgets standards utilisent déjà en interne, donc pas de risque de plateforme supplémentaire une fois le Jalon 3 validé, juste du travail de mise en page (coordonnées de chaque point de connexion sur le dessin de manette).
**Validation** : capture d'écran ou vidéo de l'appli avec ce visuel, comparée au mockup de référence.
**Risque** : faible - tout ce jalon repose sur des primitives ImGui déjà utilisées ailleurs dans le projet, aucune nouvelle API Xbox à découvrir.
**Dépendance** : Jalons 3 (rendu à l'écran) et 4 (navigation manette) validés en premier - inutile de peaufiner un visuel sur un rendu pas encore confirmé fonctionnel.

**Confirmation et clôture (2026-08-17)** : direction visuelle largement construite et vérifiée sur plusieurs sessions, via le pipeline de prévisualisation logicielle (`preview/`, rendu réel du code de production, pas une maquette séparée) plutôt que par supposition :

- Thème sombre complet (`theme.cpp`) : palette de couleurs, hiérarchie typographique (Inter, 4 tailles), accents vert clavier / bleu souris, tokens d'arrondi cohérents (`kRSm`/`kRMd`/`kRLg`).
- Système de skin par textures 9-slice (`Draw9Slice`), avec repli procédural (`ImDrawList`) si aucune texture n'est fournie - les deux chemins vérifiés au rendu.
- Halo lumineux (`GlowBorder`) sur le bouton d'action et le badge contextuel, y compris à travers plusieurs couches de fenêtres enfants ImGui (bug de z-order trouvé et corrigé - un enfant `BeginChild` a son propre calque, toujours au-dessus du parent quel que soit l'ordre du code).
- Reflet du visuel manette/souris façon fiche produit (bande dédiée réservée dans la mise en page, image miroitée + dégradé vers la couleur du panneau).
- Ligne de connexion entre le badge de sélection et la case du bouton concerné, ancrée pour ne jamais traverser l'image du périphérique.
- Stepper (flèches + molette) remplaçant l'ancienne liste déroulante pour le mapping bouton par bouton - retour utilisateur direct ("pourquoi ce drop menu d'un kilomètre").
- Passe de cohérence espacements : anti Cumulative-Layout-Shift (hauteurs de panneau réservées à l'avance, pas dépendantes de l'état), audit systématique cadres/texte collés au bord, retrait texte/cadre +4px sur les boutons et le badge.

Chaque item ci-dessus vérifié par un rendu réel (pas juste une relecture de code) avant d'être considéré acquis - voir la méthode dans `preview/LISEZ-MOI.md`. **Non re-confirmé sur hardware Xbox** depuis la bascule vers ce visuel (le pipeline de prévisualisation logicielle donne une lecture fidèle de la mise en page mais pas du rendu D3D9/Xenos réel) - à faire avant toute reprise si le projet redémarre.

## Jalon 11 - Overlay in-game (exploratoire, PAS ENGAGÉ) [candidat, évalué le 2026-08-02]

**Objectif** : mini-menu affiché par-dessus un jeu en cours d'exécution pour ajuster sensibilité/deadzone/profil sans quitter le jeu - idée de l'utilisateur, en complément du raccourci F9 (Jalon 7).
**Statut** : non planifié - évalué à la demande explicite de l'utilisateur ("l'évaluer plus sérieusement"), pas construit. Voir `PROJECT_NOTES.md`, section "Faisabilité d'un overlay in-game" pour l'analyse complète.
**Pourquoi c'est plus réaliste qu'il n'y paraît** : `hiddriver/Detours.cpp` (déjà utilisé, confirmé sur hardware pour hooker XInput/HID) est le même type d'outil qu'il faudrait pour hooker `IDirect3DDevice9::Present`/`EndScene` - pas une techno à découvrir de zéro. Précédent réel trouvé dans la scène RGH/JTAG (mod menus avec code source public pour des jeux comme Battlefield 4) - la technique fonctionne sur cette console.
**Pourquoi ça reste un chantier à part** : (1) trouver l'adresse réelle de `Present`/`EndScene` pour le kernel/dashboard de la console cible - recherche/reverse engineering non trivial, pas quelque chose à deviner ; (2) dessiner à l'intérieur du device D3D9 d'un jeu qu'on ne contrôle pas nécessite un renderer minimal indépendant, pas un simple portage d'ImGui (déjà fait pour `application.xex`, qui a son propre device) ; (3) risque de plantage du jeu hôte en cas d'adresse/hook incorrect - sur la seule console de l'utilisateur.
**Prochaine étape si engagé un jour** : session de reverse engineering dédiée (XBDM/dump mémoire) pour confirmer l'adresse `Present`/`EndScene` du kernel de la console, avant d'écrire la moindre ligne de hook.

## Jalon 12 - Pause du projet [2026-08-17, décision utilisateur]

**Décision** : HB met le projet en pause après cette session. Raison donnée, dans ses mots : pas un échec technique, mais un manque d'écho côté communauté RGH - moins de retours, de sollicitations et de critiques qu'espéré, et le constat que la Xbox 360 est devenue une plateforme de niche vieillissante ("une relique") dont tout le monde ne veut pas forcément cette solution.

**Ce que ça ne change pas** : le projet reste public sous GPLv3 (comme le projet d'origine d'EinTim23) - rien de ce travail ne disparaît, il reste disponible pour quiconque voudrait le reprendre plus tard, avec ou sans HB.

**État réel à la pause** (voir tableau récapitulatif dans `README.md`) :
- Fonctionnel et confirmé sur hardware : détection/fusion clavier+souris, wizard clavier bout en bout, réglages souris bout en bout, profils par jeu (y compris surcharge boutons/touches et liaison rapide Back+Start), sélection dynamique des périphériques.
- Visuel du Studio (Jalon 10) largement construit et vérifié par rendu logiciel, **mais pas re-confirmé sur hardware Xbox réel** depuis la bascule vers ce visuel - premier test hardware à faire avant toute reprise.
- Jalon 9 (finalisation - menu principal, sélecteur de langue FR/EN/PT-BR, nettoyage notifications) non commencé.
- Jalon 11 (overlay in-game) resté à l'état d'évaluation, jamais engagé.
- Rappel toujours ouvert : remplacer le `hiddriver.json` personnel par un mapping par défaut universel avant toute publication GitHub (voir `PROJECT_NOTES.md`).

**Pistes explorées en fin de session, pas engagées** (discussion avec HB, 2026-08-17) - à traiter comme des idées de départ pour une reprise, pas des jalons planifiés :

- **Application compagnon Android**, séparée du plugin (licence propriétaire possible car communication réseau locale uniquement, pas de code partagé - voir le raisonnement légal détaillé et l'étude complète dans `etude_projet_compagnon_android.html`, à la racine du dépôt). Couvre mapping/réglages/profils à distance ; l'assistant de calibration en direct (pression de touche en temps réel) reste nécessairement sur la TV, voir le document pour le détail de cette distinction.
- **Manette virtuelle sans fil via téléphone** (boutons/stick tactiles envoyés en réseau vers la console) - jugée réaliste, réutilise le même mécanisme que la fusion clavier+souris existante. Le vrai point d'attention est la latence : privilégier un flux UDP "état courant" à haute fréquence plutôt que du HTTP requête/réponse, et envisager le Wi-Fi Direct pour éviter un saut par le routeur.
- **Diffusion de l'écran de la console vers mobile sans HDMI** - séduisant mais matériellement incertain sur ce hardware (pas de puce d'encodage vidéo compressé accessible en logiciel, CPU déjà occupé par le jeu, aucun précédent trouvé dans la scène RGH/JTAG). À traiter comme une expérimentation isolée à part, pas un engagement.
- **Clavier/souris utilisables sur les menus natifs de la console** (Aurora ou tout autre dashboard, pas seulement en jeu) - objectif original de HB, jamais concrètement chantier à part jusqu'ici. Le curseur souris fonctionne déjà dans Aurora (confirmé Jalon 8) ; la vraie saisie de texte (taper dans un champ natif du dashboard, pas seulement dans `application.xex`) resterait à explorer.
- **Rendre les jeux FPS plus agréables/amusants** à la souris - piste encore vague, à préciser (viser plus juste ? réduire le décalage stick-vers-souris ? courbe de sensibilité par arme/jeu ?) avant de devenir un vrai jalon.
- **Détection d'autres types de périphériques** au-delà clavier/souris (HB n'a pas précisé lesquels - à clarifier avec lui si la reprise se fait).
- **Émulateurs Xbox 360 sur Android** (xenia-android, aX360e, XenDroid - tous en bêta/développement précoce au 2026-08-17) : piste distincte du plugin RGH, potentiellement plus prometteuse pour toucher un public large sans les limites de la scène RGH (légalement plus simple aussi, aucun conflit avec les politiques des stores contrairement à un outil de hack de console) - le vrai savoir-faire réutilisable serait le moteur de mapping (courbes, deadzone, profils), pas le code console lui-même. Non explorée techniquement, juste identifiée comme piste.

## Notes

- Le changement backend `input_state.json` (dans `hiddriver/main.cpp`) n'a jamais été recompilé - à valider dès que possible, idéalement avant le jalon 5, pour ne pas découvrir un problème plugin en même temps qu'un problème appli.
- Chaque jalon 2 à 6 peut révéler des ajustements aux jalons suivants - l'ordre ci-dessus n'est pas rigide, mais respecte la dépendance logique (pas de navigation manette utile sans rendu à l'écran, pas de wizard testable sans navigation).
- Ce fichier est mis à jour au fil de l'eau, comme `PROJECT_NOTES.md` - j'y coche les jalons validés avec la date et ce qui a été observé.
