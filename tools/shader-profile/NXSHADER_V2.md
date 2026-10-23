# feature-nxshad — première version native NXShader

Le workflow automatique et l’export C++ compact actuels sont décrits dans
[NATIVE_WORKFLOW.md](NATIVE_WORKFLOW.md). Les commandes Python ci-dessous restent
des outils de référence et de diagnostic.

Les mesures par étape et les réductions de temps de reconstruction sont décrites
dans [P0_TIMING.md](P0_TIMING.md). Elles s'activent explicitement avec
`NXEMU_NXSHADER_TIMING=1` à l'import et `--timings` à l'export.

## Correction des clés de profondeur inactive

Le chemin normal, la projection NXShader et la lecture des anciens caches
normalisent désormais `depth_format` à zéro lorsque `depth_enabled` est faux.
Le renderer crée déjà un render pass sans profondeur dans ce cas. Les formats
de profondeur actifs et les variantes HLE restent distincts. Les anciens
profils v2 et caches restent lisibles ; l'exporteur applique la même règle pour
apparier leurs clés aux nouvelles projections.

Vérification sur le profil Mac de première course : les cinq recompilations
initiales dues à ce champ ont disparu. Le préchauffage passe de 1 255 à 1 236
créations natives, toutes réussies. Ce correctif ne complète pas les programmes
ou combinaisons absents du profil. Une nouvelle course complète reste nécessaire
pour mesurer le nombre total de traductions après correction. Les logs d'entrée
dans `CreateGraphicsPipeline` comptent des traductions tentées, pas toujours une
création native : le chemin HLE peut réutiliser un pipeline après traduction.

## Ce qui fonctionne dans cette version

Le correctif de bornes est intégré. Vulkan écrit désormais `vulkan.nxc`, un cache
local qui déduplique séparément les instructions, les environnements et les
combinaisons de pipelines. L'ancien `vulkan.bin` reste lisible. Les binaires
opaques du pilote restent séparés dans `vulkan_pipelines.bin`.

NXEmu lit nativement le nouveau `.nxshader` v2 au démarrage, retrouve les
instructions dans les fichiers locaux indiqués, vérifie leurs SHA-256, projette
les recettes invitées selon les capacités du GPU destinataire et préchauffe les
pipelines par le chemin habituel du renderer. Aucun interpréteur Python n'est
nécessaire sur la machine qui importe.

**L'interface clic droit Exporter/Importer n'est pas encore raccordée.** Cette
version de test utilise des arguments de lancement et un exporteur hors ligne
Python 3.10+ sans dépendances pip. La capture des recettes est explicite ; un
ancien cache seul ne permet pas de récupérer les états perdus lors de sa
projection sur le GPU source. Le recorder expérimental a un coût : il ne tourne
pas lors d'une session normale sans `--nxshader-record`.

## Test Mac → Windows

Sur le Mac, utiliser cette branche et les dépendances du build habituel :

```sh
sh src/script/build_macos.sh Release build/nxshader-mac
cmake -S tools/shader-profile/tests -B build/nxshader-tools -DCMAKE_BUILD_TYPE=Release
cmake --build build/nxshader-tools --config Release
ctest --test-dir build/nxshader-tools --output-on-failure
```

Lancer le binaire de l'application, jouer, puis quitter normalement pour
enregistrer les recettes. Adapter le chemin du binaire au bundle construit :

```sh
./bin/arm64/Release/NxEmu.app/Contents/MacOS/nxemu \
  '/Jeux/Mario Kart base.dnsp' \
  --nxshader-record "$PWD/mk8-recipes.json"
```

L'update utilisée en jeu doit être activée dans NXEmu comme habituellement.
Ouvrir le dossier de cache du jeu pour repérer `vulkan.nxc`. Construire ensuite
le profil avec cette même base et cette même update :

```sh
python3 tools/shader-profile/portable_profile.py export \
  --cache '/chemin/du/cache/0100152000022000/vulkan.nxc' \
  --recipes "$PWD/mk8-recipes.json" \
  --game-root '/Jeux' --base 'Mario Kart base.dnsp' --update 'Mario Kart update.dnsp' \
  --title-id 0100152000022000 --content-label 'Mario Kart + update 3.0.3' \
  --projector build/nxshader-tools/recipe-projection \
  --yaz0-library build/nxshader-tools/libyaz0-scan.dylib \
  --output "$PWD/MarioKart-Mac.nxshader"
```

La recherche lit les ressources hors jeu ; elle peut être longue. Les conteneurs
actuellement pris en charge sont les DNCA/PFS déchiffrés du POC, avec RomFS/BKTR,
Yaz0 et segments NSO/LZ4. Un NSP/XCI chiffré n'est pas pris en charge par cet
exporteur. L'export échoue si un shader nécessaire reste sans référence, sauf
demande explicite `--allow-partial`. Son rapport indique les observations sans
environnement sérialisé et les recettes réellement exportables.

Copier uniquement le `.nxshader` vers Windows. Les fichiers de jeu doivent avoir
les mêmes noms de base que dans les locateurs, dans le dossier local choisi.
Leur emplacement absolu peut changer ; leurs contenus doivent correspondre.

```powershell
.\nxemu.exe 'D:\Jeux\Mario Kart base.dnsp' `
  --nxshader-import 'D:\Profils\MarioKart-Mac.nxshader' `
  --nxshader-game-root 'D:\Jeux'
```

Activer Vulkan et le cache shader disque. Pour un essai isolé, déplacer les
caches du **jeu testé** dans un dossier de sauvegarde avant le lancement. Il ne
faut pas fournir le `vulkan.bin` ou `vulkan.nxc` du Mac. Le jeu et son update
doivent aussi être configurés localement sur Windows. Les messages `NXShader
imported`, les avertissements et `Total Pipeline Count` se trouvent dans le log
NXEmu. Une erreur d'import est signalée dans le log ; le jeu peut ensuite
continuer avec des compilations normales. Retirer l'argument d'import pour les
lancements suivants : ils utilisent le nouveau cache local.

## Formats expérimentaux

`.nxshader` v2 contient des métadonnées UTF-8 JSON strictes, compressées par défaut
avec Zstandard au niveau 3 : `format=nxshader`, `version=2`,
identifiant du titre, version de l'état invité, tables de shaders et
d'environnements, recettes graphiques/compute et observations non résolues.
Les entiers supérieurs à 32 bits sont des chaînes décimales pour conserver les
hashes exactement entre lecteurs JSON. Les environnements sont identifiés par
SHA-256 de leur JSON canonique ; le code est identifié par son propre SHA-256.

Les recettes contiennent les champs invités, pas une copie mémoire de struct C++
ni les clés du GPU source. Les instructions, en-têtes shader bruts, IR, SPIR-V,
binaires pilote et données de correction sont absents. Les observations sans
environnement exploitable sont conservées séparément et ne sont pas présentées
comme précompilables. Les combinaisons distinctes ne sont pas fusionnées sous
prétexte qu'elles partagent un shader.

Les références décrivent des plages physiques vérifiées, ou l'assemblage ordonné
des segments d'une ressource BKTR, puis éventuellement Yaz0/LZ4 et la plage du
programme décompressé. Les positions GPU sont uniquement des métadonnées de
traduction ; elles ne servent jamais d'offset de fichier. Les empreintes de
ressource et de programme sont contrôlées. Ces références ne promettent pas de
survivre à une nouvelle update ou à un reconditionnement des conteneurs.

Le v1 gzip du POC n'est pas directement accepté par le lecteur natif. Conversion
d'un profil v1 déjà enrichi de recettes et de locateurs :

```sh
python3 tools/shader-profile/portable_profile.py convert ancien.nxshader nouveau.nxshader \
  --projector build/nxshader-tools/recipe-projection
```

Le lecteur accepte aussi le v2 JSON non compressé (`--plain` à l'export).
L'enveloppe compressée commence par `NXSHADR2`, puis `version:u32le=1`,
`codec:u32le=1`, taille décompressée et taille stockée `u64le`, puis une seule
frame Zstandard avec checksum. Les tailles sont contrôlées avant allocation,
les dictionnaires externes et frames concaténées sont refusés. Le contenu
décompressé reste borné à 32 Mio. Zstandard est déjà une dépendance du dépôt.
Ce format expérimental n'est pas une ABI publique figée.

Mesure sur les mêmes 1 108 observations, 529 programmes et dix entrées compute :

| Encodage | Octets | Compression en mémoire | Décompression en mémoire |
|---|---:|---:|---:|
| JSON brut | 2 245 260 | — | — |
| Zstandard 3, défaut | **133 266** | **1,65 ms** | **0,70 ms** |
| Zstandard 6 | 121 940 | 4,58 ms | 0,66 ms |
| Zstandard 19 | 104 392 | 160,18 ms | 0,60 ms |

Ces chiffres sont les médianes de dix essais après échauffement, sur cette
machine Windows. Ils excluent la recherche des ressources, le lancement de
Python et la compilation GPU. Le parsing JSON reste autour de 55–60 ms : la
compression réduit de 94,1 % les octets à lire et transférer, mais ne démontre
pas à elle seule une accélération du parsing ou du chargement complet du jeu.
Le niveau 19 n'est pas retenu par défaut pour préserver la vitesse de création.

L'exporteur utilise l'encodeur natif dans la bibliothèque Yaz0 quand disponible,
puis l'exécutable `nxshader-codec` en secours. Cette voie évite un processus et
un fichier JSON intermédiaire. La découverte filtre les candidats sur 128 octets,
arrête la recherche une fois tous les programmes localisés, et utilise le RomFS
logique de l'update pour éviter de décompresser les anciennes ressources remplacées.
Les empreintes finales restent contrôlées ; aucune instruction n'est supprimée
pour gagner de la place. L'optimisation de recherche n'est pas un gain mesuré de
compilation des shaders.

L'export autonome complet, depuis le cache capturé et les ressources du jeu
sans reprendre les anciens locateurs du POC, a également réussi : **130 998
octets**, 529 programmes, 1 105 recettes graphiques exploitables, dix compute et
les trois observations non résolues conservées. La recherche a pris environ
151 secondes sur cette machine ; l'encodage final et l'écriture ont pris environ
7,6 ms. Ce temps de recherche hors ligne est distinct des mesures de codec
ci-dessus et n'est pas un benchmark macOS.

Le cache local `NXSCACHE`, révision de stockage 1, conserve l'ABI shader 20 :
en-tête de 16 octets, puis frames `type:u8, taille:u32le, SHA256:32, payload`.
Les types code/environnement/pipeline sont séparés. La somme porte sur le type
et le payload. Une fin d'append incomplète est ignorée, puis tronquée au prochain
append ; une empreinte erronée est rejetée. Les combinaisons sont dédupliquées
sur leurs clés **et** environnements, jamais sur le shader seul. Les payloads
locaux restent propres à l'ABI NXEmu little endian ; ils ne sont pas partageables.
Des tables bornées et des limites de taille sont imposées aux deux lecteurs.

## Validation et limites

Sur Windows RTX 5070 Ti : import natif depuis le profil et les ressources locales,
redémarrage depuis `.nxc`, et lecture de l'ancien v20 ont chacun validé les
545 entrées graphiques et les dix compute du corpus, avec 1 090 comparaisons de
stages graphiques IR/SPIR-V conformes. Le partage existant donne 481 créations
graphiques et cinq compute. Ces validations ont utilisé un build de diagnostic
isolé. Le paquet Windows final retire ses traces et son arrêt automatique.

Le profil contient 1 105 recettes graphiques préchauffables et dix compute ;
trois autres observations graphiques sans enregistrement source correspondant
sont conservées dans `unresolved_recipes`. Toutes les 1 108 observations sont
donc conservées et les 555 entrées du cache de référence sont couvertes. Le
cache local issu de ce test contient 905 combinaisons clé/environnement et prend
2 974 806 octets, contre 5 443 100 octets pour les mêmes enregistrements développés
en v20, soit **45,3 % de réduction** ; il ne s'agit pas de 905 créations natives.
Le profil compressé reconstruit un cache local identique octet pour octet à
celui produit depuis le JSON brut. L'import compressé a aussi été lancé dans
NXEmu, avec les 486 créations natives attendues.

Les tests couvrent le stockage/dédoublonnage, les bornes, les états invités,
l'intégrité, les chemins, l'identifiant de titre et les entiers 64 bits. **Le
build macOS et le transfert d'un vrai fichier produit sur Mac ne sont pas encore
validés ici.** Ce sont les prochains essais. Aucun gain de stutters ou de temps
de compilation du pilote n'est établi ; son cache disque n'a pas été isolé.
La couverture reste limitée aux parcours capturés, sans comparaison pixel à pixel.
Les métadonnées d'environnement sont celles observées sur le GPU source. Le
test croisé doit aussi vérifier les lectures supplémentaires éventuellement
requises par le traducteur cible, les clés et les produits de compilation ; un
compteur de pipelines construits ne suffit pas à prouver leur équivalence.

### Implémentation native et banc de validation

La cible de production est un export entièrement C++ sans dépendance Python.
Le POC reste orchestré par `portable_profile.py` ; ses moteurs C++ de recherche
et de décompression sont dans `shader_profile_search.h` et
`shader_profile_resources.h`. Le helper `yaz0-scan` sert uniquement au banc.
Avec un helper récent fourni via `--yaz0-library`, le banc utilise la recherche
native ; les anciens helpers conservent la référence historique. Voir
[P0_TIMING.md](P0_TIMING.md) pour les mesures et les limites de cette migration.
