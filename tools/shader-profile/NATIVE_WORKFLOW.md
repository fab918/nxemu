# Workflow natif NXShader

## Utilisation

Jouer normalement, arrêter le jeu, puis clic droit sur le jeu → Shader profile →
Export shader profile. Aucune session d'enregistrement particulière, option ou
variable d'environnement n'est nécessaire. Choisir la mise à jour lors du premier
export ; ce choix est conservé localement. L'export et l'import ont une progression
et peuvent être annulés.

Sur l'autre machine, Import shader profile vérifie le profil avec les fichiers
locaux. Le prochain lancement reconstruit le cache local et préchauffe avec le
renderer habituel. Les lancements suivants réutilisent ce cache. Une suppression
complète du cache entraîne une nouvelle reconstruction du profil sélectionné si
sa sélection est conservée. Aucun Python n'est requis pour ces opérations.

L'application et ses modules doivent être reconstruits ensemble : l'interface
vidéo passe de 0x0126 à 0x0127, afin de rejeter proprement un ancien module.

## Enregistrement automatique

`vulkan.nxc` conserve les programmes, environnements et clés de pipelines locaux.
`guest-recipes-<contexte>.nxr` est son journal complémentaire de recettes invitées.
Le contexte identifie la base et la mise à jour chargées dès la capture. Un index
`.nxc.context-<contexte>` associe les records vérifiés au même contenu. Les
programmes identiques restent partagés dans le cache local.

Les écritures de registres pertinents incrémentent une révision. Les adresses de
buffers et de render targets ne sont pas suivies. Lors d'un draw, la révision,
la topologie, le mode HLE et le préfixe déjà calculé de la clé permettent de
réutiliser l'état capturé. Les états dynamiques du GPU source restent présents
dans la recette complète : des combinaisons utiles sur un autre GPU ne sont
donc pas fusionnées du seul fait qu'elles partagent une clé locale.

Seules les recettes inédites sont envoyées au worker existant de sérialisation.
Les SHA-256, écritures et flush de journal s'exécutent sur ce worker. Aucune
recherche de ressources, décompression, génération de JSON ou compression du
manifeste ne s'effectue pendant un draw. Le suivi des écritures de registres a
néanmoins un coût, à inclure dans une future mesure de FPS contrôlée.

Le journal commence par `NXREC001`. Chaque frame contient 383 u32 little endian
(capacités du GPU source, 278 mots invités, 104 mots de paramètres de clé), suivis
du SHA-256 hexadécimal de ces données. Les frames sont indépendantes ; un dernier
append interrompu est tronqué au démarrage suivant. Une corruption complète est
rejetée. La limite est de 16 384 recettes ; une erreur ou saturation produit un
marqueur `.failed` et empêche de présenter l'export comme complet. Le journal
est local, dédupliqué, sans instructions shader, et persiste entre les sessions.

L'ancien probe JSON et `--nxshader-record` restent des outils de diagnostic. Ils
ne font pas partie du workflow utilisateur.

## Export et index incrémental

L'export C++ lit directement le cache local et le journal. Il associe les recettes
aux environnements sérialisés en réutilisant la projection du renderer. Les
environnements distincts d'une même clé normalisée sont conservés, plutôt que
d'en choisir arbitrairement un. Les tailles de buffers et de pools inutilisés
par les programmes sont éliminées avant la déduplication des recettes exportées.

La recherche C++ couvre les conteneurs PFS0/DNCA non chiffrés, NSO/LZ4, RomFS
logique avec BKTR et ressources Yaz0. Chaque référence exportée contient une
localisation bornée, son encodage et des empreintes vérifiées ; aucune adresse
GPU n'est utilisée comme offset de fichier.

`discovery-index.json` mémorise les références et ressources déjà parcourues,
l'identité des fichiers base/update et l'ensemble des programmes recherchés.
Il est écrit atomiquement, protégé contre deux écrivains et contrôlé par SHA-256.
Une capture qui grandit réutilise les références encore vérifiées. Un changement
de fichier ou d'update invalide la progression de recherche. Les références sont
revérifiées avec les programmes locaux avant réutilisation. L'annulation conserve
un point de reprise sans publier de manifeste incomplet.

## Format compact

Le format partagé utilise une enveloppe `NXSHADR2` contenant une frame Zstd
avec checksum et limite de décompression, et un payload unique `NXSHBIN4` :
entiers explicites little endian/varints,
tables partagées de ressources, programmes, environnements et états invités,
état modal et différences creuses, recettes référencées par index.

Le lecteur reconstruit directement les objets C++ : il ne convertit pas le
binaire en JSON. Les SHA-256 de programmes, headers et ressources sont conservés.
Les données locales permettent de reconstituer les headers et instructions ; ils
ne sont jamais écrits dans le manifeste. Ce format est différent du prototype
Python `.nxcproto` du patch Mac, qu'il ne prétend pas lire.

Les recettes portent leur contexte de contenu. Des empreintes de conteneurs
permettent de retrouver une copie identique renommée, puis chaque programme est
revérifié. Les ressources absentes excluent uniquement les recettes dépendantes.
Les imports successifs réutilisent les recettes et programmes locaux vérifiés.

## Mesures Windows du 23 octobre 2026 (prototype précédent)

Build de validation isolé dans `build/shader-profile-poc/master`, avec les fichiers
de cette fonctionnalité, sans les modifications locales indépendantes de rendu
et de contrôleurs. Les tests ne modifient pas les caches de l'installation habituelle.

- Profil Mac existant : 476 303 → **193 235 octets** après conversion native v3.
  Les 2 838 recettes reconstruisent les mêmes 2 590 records locaux, avec un cache
  strictement identique octet par octet à la reconstruction v2 de référence.
  L'import automatique au lancement a également été exécuté : 1 236 créations
  natives réussies (1 231 graphiques, 5 compute), 5,13 s de préparation, dont
  4,07 s de lecture/reconstruction des ressources. Au second lancement : aucun
  nouveau décodage du profil ni relecture des ressources et 0,128 s de préparation.
  Le petit fichier compressé reste lu pour vérifier son empreinte de sélection. Ce second
  résultat bénéficie d'un pilote déjà chaud ; ce n'est pas un test de cache pilote
  froid. Le cache local continue de suivre son préchauffage normal.
- Essai de 45 secondes de Mario Kart sous Windows : **240 803 draws**, 615 recettes
  observées. Le journal automatique et le probe complet donnent exactement le
  même ensemble, sans manque, recette supplémentaire ou erreur de projection.
- Le chronométrage optionnel `NXEMU_RECIPE_RECORD_TIMING=1` mesure 96,283 ms dans
  l'observateur au total, soit **0,400 µs/draw**. Cette valeur inclut les appels à
  l'horloge et exclut le worker disque et le suivi des registres. Ce n'est pas une
  mesure contrôlée de perte de FPS.
- Cet essai exporte 294 programmes, 421 environnements, 431 recettes graphiques
  et 4 compute, sans programme manquant ni observation non résolue. Le manifeste
  fait **38 546 octets**. Les 276 entrées locales reconstruites sont fonctionnellement
  identiques aux 276 entrées capturées après normalisation des paramètres inutilisés.
  Les différentes combinaisons invitées sont conservées.
- Avant élimination des tailles inutilisées, le même export produisait 461 entrées
  locales : les 185 entrées supplémentaires étaient évitables.
- La première découverte sur ces fichiers prend environ **35,7 s**. L'index évite
  ce balayage aux exports suivants : **0,79 s**, 294 références réutilisées et
  aucune ressource à rechercher sur cet essai. Les détails reproductibles et sorties restent
  dans `build/nxshader-native-workflow/` ; ces fichiers locaux ne sont pas distribués.

## Limites et test Mac

La première recherche complète peut encore être longue. Le prototype ne déchiffre
pas de conteneur, ne recherche pas dans tous les formats de jeux, et ne remplace
pas encore son adaptateur de ressources par le VFS du loader. Base et update doivent
se trouver dans le même dossier. Les copies identiques renommées sont résolues
par empreinte ; les conteneurs réorganisés ne sont pas encore résolus par le VFS.
Les contenus trop grands ou encodages non pris en charge
doivent être signalés ; un export partiel n'augmente pas magiquement sa couverture.

La capture reste séparée par contexte base/update. Les recettes du profil sélectionné
provenant d’une autre version sont préparées comme candidats inactifs. À leur
première utilisation, le code et les dépendances de compilation doivent être
identiques au jeu chargé. Un candidat validé rejoint le cache normal et son
record est associé au contexte courant sans dupliquer le code. Un candidat
incompatible est écarté ; les shaders nouveaux ou modifiés suivent la compilation
et la capture normales. Voir [la validation des updates](CROSS_VERSION_2026_10_24.md).

Sur Mac : récupérer `feature-nxshad`, reconstruire l'application et les modules,
jouer puis arrêter normalement, exporter par le menu sans lancer de script.
Vérifier la présence du journal, l'absence de `.failed`, les compteurs de couverture
et la taille du profil. Retourner seulement le `.nxshader`, les versions base/update
et les logs de mesure. Tester ensuite ce nouveau fichier sur Windows à cache local
vide, même scénario, puis le sens inverse. La compilation et l'interface Mac/Linux,
le nouvel aller-retour Mac → Windows et les FPS/stutters comparatifs restent à valider.

## Validation synthétique du 24 octobre 2026

Voir [les tests, mesures et limites](SYNTHETIC_2026_10_24.md). Zstd reste au niveau 3.


## Dépôt automatique et écran de chargement

Le dossier durable `nxshader/`, dans le répertoire de données de NXEmu (à côté
de `shader/`), conserve les profils partagés. Il ne fait pas partie du cache
jetable. Le menu du jeu **Shader profile → Open NXShader folder** l’ouvre et le
crée si nécessaire. L’import par sélection de fichier y range également le
profil après validation, avec remplacement atomique du profil précédent.

Décompresser le ZIP éventuel et déposer le fichier nommé par titre, par exemple
`nxshader/0100000000010000.nxshader` pour Super Mario Odyssey. Le ZIP lui-même
n’est pas chargé. Au démarrage, la priorité est : option CLI explicite, fichier
dans `nxshader/`, fichier nommé par titre dans le cache du jeu ou le dossier
`shader/`, sélection GUI existante, puis fichier à côté du jeu.

Sur l’écran habituel avec la jaquette, une intégration nécessaire affiche :
**Lecture et vérification NXShader**, **Importation NXShader**, puis
**Compilation NXShader**. Chaque phase possède son compteur réel et sa barre ;
les premières lectures de métadonnées sans total connu restent indéterminées.
La compilation inclut le préchauffage du cache local de ce lancement. Le
démarrage du jeu suit normalement. Sans nouvelle intégration, l’écran revient à
la préparation habituelle des shaders.

Les compteurs sont actualisés au maximum toutes les 50 ms, avec émission
immédiate des changements de phase et des fins de phase. Un reçu vérifié évite
de reconstruire un profil déjà intégré ; un fichier modifié ou un cache incomplet
invalide ce reçu. Un échec d’import ne bloque pas le démarrage normal et reste
consigné dans le journal.

Validation du 24 octobre : application et modules macOS compilés ; 13 tests
natifs, 78 tests Python et les tests ciblés ASan/UBSan réussis. Les tests couvrent
la priorité des dossiers, les titres absents, les répertoires pris à tort pour des
fichiers, la conservation du profil après suppression du cache, et la progression
terminale du lecteur. Les quatre nouveaux libellés sont traduits dans les dix
catalogues. Le contrôle global des traductions conserve des erreurs préexistantes
sans rapport avec ces libellés ; aucune erreur supplémentaire n’a été ajoutée.

Le profil Windows fourni a passé `Inspect` sans lecture du jeu :
libellé **SUPER MARIO ODYSSEY 1.3.0**, titre `0100000000010000`,
2 705 recettes graphiques, 26 compute, 284 observations non résolues,
216 809 octets. Le libellé est une métadonnée descriptive, pas une preuve de
version CNMT. L’import réel et le rendu de cet écran pendant le lancement de
Mario restent à valider avec son conteneur local ; aucun essai en jeu n’est revendiqué.


## Variantes HLE récupérées à l’export

L’exporteur recherche d’abord la clé exacte. À défaut, une observation normale
peut retrouver la même clé avec le seul marqueur HLE activé, à condition que
**tous** les environnements correspondants soient sans remplacement HLE.
Aucun autre octet de la clé normalisée n’est modifié. L’inférence inverse
(normal vers HLE) reste interdite : une capture normale ne prouve pas l’absence
de remplacements dans un futur draw HLE.

Ce rapprochement réutilise les environnements existants et conserve l’état
invité de l’observation normale. Il n’ajoute aucune recherche, allocation ou
vérification sur le chemin des draws. Le format NXSHBIN4 et Zstd 3 restent
inchangés. Le rapport de l’outil natif expose `resolved_hle_aliases`, qui compte
les observations récupérées par cette règle.

Sur le profil Windows de Mario Odyssey fourni le 24 octobre, le défaut a été
reproduit hors ligne avec les 284 observations non résolues d’origine, sans
ajouter les nouvelles captures de la session Mac. Après correction : 234
observations récupérées, 50 restantes, 2 939 recettes graphiques et 26 compute.
Les 2 731 identités de recettes originales sont conservées ; 234 s’y ajoutent.

Une copie corrigée du profil a été réencodée avec le contexte source original,
puis relue contre les ressources du jeu local : aucun programme absent ou
incompatible. Elle fait 216 588 octets, contre 216 809 pour le fichier fourni.
Le profil installé a été remplacé après sauvegarde de l’original ; le jeu en
cours n’a pas été redémarré pour ce correctif.

Validation : 13 tests natifs, 87 tests Python dont neuf nouveaux scénarios
d’export HLE, compilation complète de l’application et des modules. Les scénarios
couvrent les remplacements sur un stage autre que le premier, le sens interdit,
les changements de programme/état/buffer/table, la priorité de la source exacte,
les variantes mélangées sûres et incompatibles, et le déterminisme du fichier.
Les 36 tests du workflow natif passent également avec l’exporteur et le lecteur
sous ASan/UBSan (détection de fuites indisponible sur le runtime Apple utilisé).

Résultats détaillés :
[reports/hle-alias-export-2026-10-24.json](reports/hle-alias-export-2026-10-24.json).
