# P0 — mesures de reconstruction et premières réductions

Mesures Windows, RTX 5070 Ti, Mario Kart 8 Deluxe + update 3.0.3,
23 octobre 2026. Le profil Mac v2 contient 1 314 programmes et 2 838 recettes,
pour 476 303 octets. Son SHA-256 est
`dc6ecbef761786cbef43e5c381044c9076b57909d3569ca69b251a4cc4825593`.

## Activer les mesures

L'import natif et le lecteur hors ligne acceptent
`NXEMU_NXSHADER_TIMING=1`. NXEmu écrit une ligne `NXShader timings` dans son log.
Avec `NXEMU_NXSHADER_TIMING_FILE=/chemin/existant/rapport.json`, NXEmu écrit
aussi le rapport à la fin du préchauffage. Le dossier parent doit déjà exister.
Le lecteur `nxshader-reader` ajoute les mesures à son objet JSON de sortie.
L'exporteur accepte `--timings` et enrichit son `.report.json`.

Par défaut, les mesures sont désactivées. Le collecteur n'est installé que dans
les tâches du préchauffage, puis retiré. Aucune collecte par draw n'est ajoutée.

Les phases `startup_wall` et `reader_total` sont des temps écoulés inclusifs.
`resource_total` contient les lectures, décompressions et empreintes. Les phases
du compilateur sont des durées cumulées entre threads : elles peuvent dépasser
le temps écoulé et comprennent des attentes dans le pilote. Elles ne sont pas
des mesures de temps CPU pur et ne doivent pas être ajoutées au temps mural.
`warmup_wait` mesure seulement l'attente restante après la mise en file.

Les compteurs distinguent les lots de traduction, émissions SPIR-V, créations
de modules shader et appels natifs graphiques/compute. `completed` signifie que
l'appel mesuré est revenu sans exception ; les erreurs du renderer restent
dans le log. Les alias HLE et les tâches interrompues sont comptés séparément.

## Diagnostic de l'import

Le fichier du profil est petit, mais ses 240 groupes de ressources demandent
3 209 384 527 octets de lecture et, auparavant, 5 917 276 612 octets de données
décompressées ou brutes. Il comprend 109 ressources BKTR/Yaz0, 130 BKTR brutes
et un segment NSO/LZ4. Ce sont les volumes des ressources référencées, pas la
taille des instructions shader ni la consommation mémoire simultanée.

Sur trois répétitions séquentielles du lecteur hors ligne :

| Phase | Avant | Après |
|---|---:|---:|
| Lecture du profil | environ 0,45 ms | même ordre |
| Décompression du profil Zstd | environ 2,3 ms | même ordre |
| Parsing JSON et contrôle de profondeur | environ 144 ms | même ordre |
| Lecture des ressources | environ 1,61 s | même volume |
| Décompression des ressources | environ 8,98 s | environ 0,67 s |
| Vérifications des empreintes des ressources/programmes | environ 7,49 s | environ 1,26 s |
| Projection et écriture du cache | environ 0,51 s | même ordre |
| **Processus complet, médiane** | **19,71 s** | **5,04 s** |

Le même cache local est produit octet pour octet. La normalisation de profondeur
précédente est présente dans les deux conditions : 2 590 enregistrements locaux,
8 580 640 octets. Ce compteur est distinct du nombre de créations natives.

## Changements retenus après mesure

La vérification reste SHA-256. Le lecteur évite la copie par valeur du gros
buffer et utilise le fournisseur système Windows (CNG), CommonCrypto sur Mac,
ou OpenSSL s'il est disponible sur Linux. La voie logicielle existante reste
en secours. Windows est testé ici ; les voies Mac/Linux doivent être validées
sur ces systèmes. Aucun nouvel algorithme d'empreinte n'est imposé au format.

Yaz0 et LZ4 utilisent une sortie préallouée et copient les répétitions par blocs
non chevauchants. Les distances, tailles et bornes restent contrôlées.

Pour BKTR/Yaz0 uniquement, le bloc compressé complet est vérifié avant de
décompresser jusqu'au dernier octet demandé par les programmes. Le suffixe
inutilisé n'est plus décompressé. Les programmes extraits gardent leur propre
vérification SHA-256. Cela ramène le volume produit à 1 027 743 192 octets,
dont 752 957 888 octets de préfixes Yaz0. Les anciens locateurs Yaz0 sans
empreinte du bloc complet restent décompressés entièrement. NSO/LZ4 conserve
la décompression et la vérification du segment complet.

## Validation native et limites

Deux imports avec cache NXEmu initialement vide ont été mesurés par condition :

| Essai | Avant | Après |
|---|---:|---:|
| Premier préchauffage | 21,89 s | 7,21 s |
| Préchauffage suivant | 20,56 s | 5,85 s |

Chaque import réussit 1 231 appels natifs graphiques et cinq compute, émet
2 467 modules SPIR-V et réutilise 139 alias HLE. Aucun échec shader/pipeline.
Les durées natives varient fortement entre premiers essais et répétitions :
elles sont rapportées séparément dans les fichiers JSON, sans être assimilées
à une compilation pilote entièrement froide.

La politique NVIDIA par exécutable désactivant le cache disque a été vérifiée,
et le cache opaque Vulkan de NXEmu était désactivé. Le cache fichiers Windows
n'a pas été purgé, et l'état interne du pilote ne peut pas être certifié froid.
Le profil NVIDIA temporaire a été supprimé après les mesures. Les builds des
outils et du runtime utilisent deux versions de MSVC ; comparer chaque série
à elle-même. Les timings natifs excluent l'initialisation de la fenêtre et du GPU.
Les essais s'arrêtent après le préchauffage et ne remplacent pas une course.

Les tests couvrent les empreintes standard et les frontières de blocs SHA-256,
les répétitions Yaz0/LZ4 chevauchantes, les préfixes finissant dans une répétition,
et la corruption d'un suffixe non décompressé : celle-ci reste rejetée par
l'empreinte du bloc complet. Les tests du collecteur couvrent l'accumulation
multithread, les exceptions et sa désactivation.

## Création du manifeste : export complet

L'export autonome a été mesuré séparément sur le corpus antérieur de 555 entrées,
529 programmes et 1 108 observations graphiques. Ce n'est pas la capture Mac
de 1 314 programmes : les tailles et temps des deux corpus ne sont pas directement
comparables. Résultat : 1 105 recettes graphiques et dix compute exportées,
trois observations non appariées conservées, fichier de 130 267 octets.

| Phase d'export | Temps |
|---|---:|
| Recherche des programmes dans les données | 121,47 s |
| Décompression pendant la découverte | 18,87 s |
| Vérifications d'empreintes | 4,28 s |
| Lectures explicites des ressources | 3,37 s |
| Découverte totale, phases ci-dessus incluses | 150,64 s |
| Assemblage et projection des recettes | 113,90 ms |
| Sérialisation JSON | 15,05 ms |
| Compression et chargement de sa bibliothèque | 2,51 ms |
| Écriture du profil | 0,44 ms |
| **Export complet** | **150,83 s** |

La recherche parcourt cumulativement 28,73 Go, dont les conteneurs mmap et
les ressources décompressées ; les défauts de page mmap sont inclus dans le
temps de recherche, pas isolés comme de l'I/O physique. 5 864 fichiers RomFS
sont examinés, 10,74 Go sont lus explicitement et 16,60 Go sont produits par
la découverte. Ces totaux peuvent compter plusieurs représentations des mêmes
données et ne sont pas des volumes uniques de disque.

La sérialisation et la compression ne dominent donc pas la création. L'index
persistant doit permettre de réutiliser les références vérifiées et d'éviter
la recherche intégrale à chaque export. Cette optimisation n'est pas réalisée
par les seuls changements d'import décrits plus haut.

Les preuves détaillées locales se trouvent dans `build/shader-timing-p0` :
`offline-results.json`, `prefix-offline-results.json`, `native-results.json`,
`prefix-native-results.json` et `MarioKart-P0-export.nxshader.report.json`.
Les caches et ressources restent locaux. Les essais natifs utilisent le
checkout de diagnostic isolé, basé sur upstream `e1faa8c9` avec les sources
de la fonctionnalité copiées depuis la branche, et non un build intégral du
checkout principal contenant ses autres modifications locales.

## Priorités suivantes

Le JSON et la compression du manifeste ne constituent pas le goulet actuel.
Pour réduire encore l'import : viser le volume des ressources lues et les
références choisies, puis comparer les empreintes accélérées sur chaque système.
Un remplacement par BLAKE3 demanderait une identification explicite de
l'algorithme et le maintien de la lecture des anciens profils SHA-256 ; ce
changement n'est pas effectué ici. L'index persistant des ressources reste la
priorité pour éviter de refaire les recherches lors des exports suivants.

## Recherche C++ et décompresseur commun

La découverte utilise maintenant `shader_profile_search.h`, un index multi-préfixes
portable en C++20, avec table de sauts sur deux octets et traitement des zones
remplies de zéros. Il renvoie des lots bornés de positions, dans l'ordre,
avec la même sémantique non chevauchante que la référence. La comparaison de
l'intégralité du programme reste obligatoire avant de créer un locateur ; les
préfixes ne sont ni une preuve d'identité ni une clé de pipeline.

`shader_profile_resources.h` contient désormais les décompresseurs Yaz0/LZ4 du
lecteur natif. Le banc de découverte appelle exactement ce même décompresseur
Yaz0, avec copie des répétitions par doublement et traitement groupé des
littéraux. Le lecteur conserve la validation de l'empreinte complète avant
une décompression partielle. Aucun changement du format ou des combinaisons
shader n'est nécessaire.

Une mesure d'export sur le même corpus de 555 entrées donne :

| Phase | Avant | Avec moteurs C++ |
|---|---:|---:|
| Recherche | 121,47 s | 9,60 s |
| Décompression | 18,87 s | 15,30 s |
| Export complet | 150,83 s | 35,32 s |

Le fichier est **strictement identique**, 130 267 octets, SHA-256
`ec51dd37e75588913bfef81f17491fa29acde2ef49752ab23f65888e8b88e28a`.
Les compteurs de parcours sont identiques. Cette comparaison porte sur une
mesure complète de chaque version, sans purge du cache de fichiers de l'OS ;
ce n'est pas une garantie de temps sur disque froid. Rapport après :
`build/shader-timing-p0/MarioKart-native-search-full.nxshader.report.json`.
La capture historique ne sérialise pas ses feature bits ; la fixture de ce banc
rétablit explicitement la valeur 63 utilisée par la projection validée du POC.

Les six tests CTest et 51 tests du banc Python passent. Le scanner est comparé
à une recherche exhaustive en C++, puis à la référence sur les frontières de
blocs mmap, les préfixes chevauchants, les petits buffers et les lots denses.
L'adaptateur garde l'ancien scanner pour les anciens helpers et les préfixes
de moins de seize octets ; il ne faut pas en faire une dépendance de production.

**État de la migration :** l'import est natif ; les moteurs de recherche et de
décompression communs sont en C++. Le script reste le pilote du POC pour
l'énumération des conteneurs, l'assemblage du manifeste et les comparaisons.
L'export de production entièrement C++ et son intégration UI ne sont pas encore
livrés. Il doit réutiliser la lecture logique des ressources du chargeur
(base et update/BKTR), appeler directement ces moteurs, puis le codec natif ;
il ne doit pas lancer Python ou charger la bibliothèque du banc. L'index local
persistant devra être associé à l'identité des contenus et revérifié après
changement d'update. Aucun scanner ni décompresseur n'est ajouté aux draws.
