# Mission : première course Mario Kart, Mac vers Windows

Tu travailles sur le Mac. Construis et teste la branche `feature-nxshad` du dépôt
`https://github.com/fab918/nxemu.git`. L'utilisateur pilotera la première course :
prépare le lancement, puis laisse-lui les commandes. Ne simule pas la conduite.
Le but est de produire un profil sur Mac, que l'utilisateur retournera pour
tester sa reconstruction sur Windows. Ne refonds pas le format ni le renderer.

## 1. Préparer la version et le jeu

- Préserve les modifications locales et les données utilisateur. Utilise un
  checkout séparé si nécessaire. Récupère `origin/feature-nxshad`, initialise ses
  sous-modules et note le SHA exact, macOS, architecture, GPU et version Vulkan /
  MoltenVK. Lis `tools/shader-profile/NXSHADER_V2.md`.
- Construis l'application et les outils selon ce document. Exécute les CTests.
  Le script de build fourni cible arm64 ; adapte explicitement pour un Mac Intel.
  Un build et un partage réels depuis Mac n'ont pas encore été validés.
- Identifie la base Mario Kart, l'update active et leurs versions. Le test Windows
  utilise l'update 3.0.3 ; signale toute différence avant de comparer les résultats.
  L'exporteur actuel exige les conteneurs déchiffrés DNCA/PFS compatibles avec le
  POC. Il ne lit pas directement un NSP/XCI chiffré. Vérifie ce prérequis tôt ;
  n'annonce pas un export réussi si les ressources ne sont pas prises en charge.
- Utilise un dossier de résultats distinct. Note les paramètres graphiques et
  d'émulation et conserve-les entre les essais. Il n'y a pas encore de menu
  clic droit Exporter : utilise la capture CLI et l'exporteur hors ligne.

## 2. Préparer le cache froid

- Ferme NXEmu. Repère les chemins réellement utilisés pour ce titre, puis
  déplace ses caches vers une sauvegarde horodatée : ancien `vulkan.bin`, nouveau
  `vulkan.nxc`, cache de pipelines pilote `vulkan_pipelines.bin`, et autres caches
  du titre identifiés dans cette configuration. Ne touche pas aux sauvegardes,
  au firmware ni aux autres jeux. Ne copie aucun ancien cache dans le test.
- Isole aussi les caches persistants effectivement utilisés par MoltenVK / le
  pilote si un mécanisme documenté le permet. Documente les chemins et la méthode.
  Un nouveau processus et la suppression des caches NXEmu seuls ne prouvent pas
  que le cache natif du pilote est froid. Si tu ne peux pas l'isoler, annonce
  cette limite et nomme l'essai « cache NXEmu froid ».
- Vérifie et relève l'absence des caches du titre juste avant le lancement.

## 3. Première course, conduite par l'utilisateur

Lance le binaire réel du bundle construit, avec la base comme argument et :

```sh
"$NXEMU_BIN" "$GAME_BASE" --nxshader-record "$RESULTS/mk8-mac-recipes.json"
```

Les variables ci-dessus désignent les chemins vérifiés sur ce Mac. N'ajoute
aucun argument d'import. L'update doit être activée dans NXEmu.

- Arrive à l'écran de sélection puis laisse l'utilisateur choisir et piloter
  la première course. Note circuit, mode, cylindrée, personnage, véhicule et
  éventuels passages en démo. Ne change pas de course pour élargir la couverture.
- Relève le temps de démarrage / chargement, les compteurs de pipelines et les
  compilations si les journaux les exposent. Si une mesure de frametimes est
  disponible sans instrumentation intrusive, relève les saccades et la méthode.
  Sinon indique « non mesuré » ; ne déduis pas des stutters d'un compteur seul.
- Après la course, fais quitter NXEmu normalement pour finaliser la capture et
  le cache. Conserve les logs complets localement. Le recorder est expérimental
  et ajoute un coût : ce lancement n'est pas un benchmark absolu de performances.

## 4. Exporter le profil Mac

Repère le `vulkan.nxc` produit et exécute la commande `portable_profile.py export`
du guide avec cette capture, cette base et cette update, en précisant
`--projector`, `--yaz0-library` et `--output "$RESULTS/MarioKart-Mac-first-race.nxshader"`.
N'utilise pas `--allow-partial` pour masquer un échec. Si la recherche échoue,
conserve le diagnostic et liste les références manquantes.

Vérifie le rapport : programmes localisés, recettes graphiques et compute,
observations non résolues, taille du manifeste et durée de création. Vérifie
l'absence d'instructions shader, d'IR, de SPIR-V et de binaires pilote dans le
contenu décodé. Calcule le SHA-256 du `.nxshader`. La compression est Zstandard,
pas zlib. La taille et la couverture dépendront de cette seule course.

## 5. Remettre les résultats et préparer Windows

Remets à l'utilisateur :

- `MarioKart-Mac-first-race.nxshader` ;
- son rapport d'export JSON et un court compte rendu : commit, matériel,
  versions du jeu, circuit, méthode d'isolation des caches, mesures et limites ;
- les logs pertinents après vérification des chemins / données personnelles.

Ne distribue ni le cache local Mac, ni des instructions shader, ni des ressources
du jeu. Les captures de recettes et caches restent privés sur le Mac pour
diagnostiquer les écarts si nécessaire.

Sur Windows, l'autre agent sauvegardera puis isolera les caches du titre et du
pilote, importera uniquement ce manifeste avec sa copie locale du même jeu et
de la même update, puis l'utilisateur rejouera le même circuit avec les mêmes
options. Les noms des fichiers de base / update référencés doivent correspondre
aux noms locaux ; leur dossier absolu peut changer.

Comparer séparément les pipelines reconstruits au démarrage, les compilations
restantes en course, les temps de chargement et les frametimes réellement
mesurés. Une validation Mac → Windows exige un import réel sur le GPU Windows ;
un export réussi sur Mac ne suffit pas. Pour chiffrer le gain Windows, prévoir
également un contrôle Windows à cache froid sans import dans les mêmes conditions.
