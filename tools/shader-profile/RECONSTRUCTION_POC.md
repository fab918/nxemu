# NXEmu — POC de reconstruction Shader Profile

Résultats du 22 octobre 2026, RTX 5070 Ti, pilote NVIDIA 616.92.0.0.

## Résultat livré

**61 pipelines sur un échantillon de 64 sont reconstruits depuis le manifeste et
la copie locale de Mario Kart : 95,31 %.** Le fichier
`MarioKart-reconstruction.nxshader` pèse **40 939 octets** et ne contient pas
d'instructions shader. Les 61 cas reproduisent les 122 stages IR et SPIR-V de
référence et aboutissent aux créations natives attendues.

Les trois pipelines restants sont bloqués par quatre shaders non localisés.
Ils ne sont pas remplacés par des approximations et ne sont pas déclarés
dynamiques sans preuve.

Le prototype reste dans les outils et le checkout de diagnostic séparé.
Cette étape n'ajoute aucune modification au chemin de rendu normal, ne change
pas le cache interne v20 et n'optimise pas le compilateur. L'adaptateur utilise
le format POC v1 existant, avec les recettes expérimentales en métadonnées
supplémentaires ; aucun format V2 public n'est défini.

## Périmètre et référence

Le cache macOS fourni a servi de corpus initial et de source de références de
ressources déjà vérifiées. Une ancienne clé macOS ne permet pas de récupérer
les champs invités qu'elle a éliminés comme états dynamiques. Pour comparer
avec les recettes effectivement capturées et avec le compilateur exécuté pendant
le jeu, cette étape utilise donc une nouvelle capture Windows à cache NXEmu vide.

Cette capture de 90 secondes applique bien l'update locale 3.0.3 ; les journaux
du loader identifient sa version interne comme `v0.19.0`. Elle contient
1 108 recettes, 545 entrées graphiques et 10 entrées compute dans `vulkan.bin`,
pour 530 blocs shader distincts. Les démos sont aléatoires : ce corpus ne
représente ni toutes les courses, ni toute la couverture du jeu.

L'échantillon de 64 est sélectionné **avant** le filtrage des ressources
retrouvables. Il couvre les trois topologies observées (points, triangles,
triangle strips), les formats de cible observés, les contextes HLE/ordinaires,
les requêtes de types de textures et de constant buffers. Il comprend deux
stages par pipeline : vertex et fragment. Il ne valide pas tessellation,
geometry, transform feedback, dual-source blending ou compute : ces cas ne
sont pas présents dans l'échantillon graphique capturé.

## P0 — Reconstruction et comparaison avec le chemin normal

1. Le checkout de diagnostic enregistre les clés passées au compilateur pendant
   le jeu, l'IR après traduction, les flags des instructions IR, le SPIR-V émis
   et le retour réussi de la création du pipeline Vulkan.
2. Un outil C++ relit les mots de la recette et appelle les fonctions existantes
   de projection avec les capacités de la machine destinataire du test.
3. L'adaptateur rapproche la clé effective d'un environnement sérialisé dans le
   cache de cette même capture. Il ne choisit pas arbitrairement un environnement
   par seul hash shader. Les tailles non utilisées des constant buffers et des
   tables de textures sont normalisées comme dans le renderer.
4. Les 64 recettes sont préchauffées dans une autre instance portable, avec les
   octets du cache comme référence P0. Les produits sont comparés à ceux de la
   compilation pendant le jeu, et pas seulement à une deuxième lecture du cache.

| Comparaison | Résultat P0 |
|---|---:|
| Recettes de l'échantillon reconstruites et validées | 64 / 64 |
| Clés demandées effectives égales à la référence cache | 64 / 64 |
| IR structurel + flags identiques | 128 / 128 stages |
| SPIR-V identique octet pour octet | 128 / 128 stages |
| Créations natives distinctes nécessaires et réussies | 59 / 59 |

L'IR imprimé contient des adresses d'objets : seuls ces préfixes diagnostiques
sont retirés avant comparaison. Les flags IR sont comparés aussi ; les fichiers
IR/SPIR-V eux-mêmes restent dans le répertoire privé de test.

**Réutilisation HLE expliquée.** Le renderer partage déjà les pipelines HLE et
ordinaires lorsque la traduction n'a demandé aucun remplacement de constante.
Dans le chemin normal, 10 recettes de l'échantillon réutilisent leur jumeau.
Dans le replay limité à l'échantillon, 5 le réutilisent et 5 sont construites
sous leur clé HLE car leur jumeau n'est pas dans ce sous-ensemble. Les produits
IR/SPIR-V sont identiques. Ainsi, 59 clés d'objets natifs sur 64 sont littéralement
identiques entre les deux chemins ; 5 diffèrent uniquement par `app_stage` et
respectent la règle de partage existante. Ce n'est pas une perte de combinaison
ou une divergence shader. Les comparaisons ne masquent aucun autre écart de clé.

Sur les 1 108 recettes, 1 105 trouvent directement un environnement de cache.
Les trois autres ont un jumeau HLE/ordinaire sérialisé, avec exactement les mêmes
champs hors `app_stage` et aucun remplacement de constante. Il ne manque pas de
code shader pour ces trois observations ; la relation de partage n'était pas
explicitement enregistrée par la sonde.

### Données que la recette seule ne fournit pas

La recette contient l'état graphique invité, les hashes des six emplacements
de stages, les tailles de constant buffers et des tables de textures. Le cache
fournit encore les informations suivantes, indispensables à cette reconstruction :

- l'association exacte de chaque stage à son bloc shader et à son en-tête ;
- le stage, la mémoire locale, le buffer de textures, le repère des lectures du
  programme et l'état de transformation du viewport ;
- les réponses aux requêtes de types et formats de textures, de valeurs des
  constant buffers et de remplacements de constantes ;
- selon le stage, le masque geometry passthrough, le workgroup et la mémoire
  partagée ; ces variantes n'ont pas été testées nativement dans cet échantillon.

Le manifeste emporte ces métadonnées de compilation, avec les références locales
et les empreintes des shaders. Les profils/capacités de compilation propres au
GPU sont recalculés localement par NXEmu. Une clé ou le seul hash du code ne
suffit donc pas à remplacer l'environnement.

## P1 — Localisation dans les ressources du jeu

Les fichiers sont lus sans modification. La recherche accepte uniquement des
correspondances complètes des octets du shader, vérifiées par SHA-256.

| Localisation | Shaders sur les 530 de la capture |
|---|---:|
| Après références existantes, recherche brute et Yaz0 indexé | 413 |
| Ajoutés par recherche de flux physiques Yaz0 dans l'update | 89 |
| Total vérifié | **502 / 530 — 94,72 %** |

Le premier indexeur ne remonte pas le RomFS virtuel BKTR de l'update. La recherche
physique a néanmoins trouvé 951 flux Yaz0 complets et valides dans son fichier,
permettant 89 correspondances supplémentaires. Les 55 autres signatures
plausibles n'ont pas donné de flux contigus valides ; cela ne prouve pas qu'elles
contiennent les shaders encore absents.

Chaque référence décrit soit un offset physique direct dans le fichier `.dnsp`,
soit l'offset et la longueur exacte d'un flux Yaz0 physique, puis l'offset du
shader dans le résultat décompressé. Elle inclut la taille du shader et son
SHA-256. Les nouveaux flux de l'update ont aussi une empreinte du conteneur
compressé : sept de ces conteneurs sont nécessaires à l'échantillon final et
leurs empreintes ont été vérifiées séparément.

Le chemin RomFS logique des flux repérés physiquement dans BKTR reste inconnu.
La référence physique est valable pour ces fichiers précis ; elle ne promet
pas de rester valable après repacking ou changement d'update. **Aucune adresse
GPU n'est utilisée comme offset de fichier.** Les repères invités présents dans
les environnements servent uniquement à l'interprétation du programme.

Sur la capture complète, les octets et métadonnées sont localisables pour
504/545 entrées graphiques (92,48 %) et 10/10 entrées compute, soit 514/555
entrées (92,61 %). Ce sont des taux de localisation/reconstruction hors ligne ;
les créations natives de tout ce corpus n'ont pas été testées.

Il reste 28 blocs shader non localisés dans le corpus complet. Ils ne sont pas
retrouvés exactement dans les zones brutes, les ressources Yaz0 indexées de base
ou les flux physiques complets de l'update. Leur corps n'est pas non plus celui
d'un shader déjà localisé avec un simple changement d'en-tête. L'origine réelle
de ces octets demeure indéterminée. L'absence de reconstruction du RomFS BKTR et
d'autres transformations de ressources est une limite de la méthode ; elle ne
permet pas de conclure que ces shaders sont générés dynamiquement.

## P2 — Manifeste seul + copie locale du jeu

Le manifeste final contient les 64 recettes sélectionnées, 95 identités shader,
115 environnements de compilation et les références vérifiées. Aucun programme
invité, en-tête shader brut, IR, SPIR-V ou binaire natif n'y est inclus.

L'import est effectué dans un processus indépendant. Un audit Python interdit
les lectures des ZIP et des fichiers `vulkan.bin` sources pendant cet import.
L'outil de projection local ne reçoit que les mots issus du manifeste et un
chemin de sortie. L'importeur reconstruit les clés depuis les recettes, retrouve
les octets dans les fichiers du jeu et contrôle les SHA-256 avant d'écrire un
cache local temporaire. L'audit est une vérification de ce prototype, pas une
sandbox système pour du code externe arbitraire.

| Mesure finale | Résultat |
|---|---:|
| Taille `.nxshader` gzip / JSON | **40 939 / 390 784 octets** |
| Shaders de l'échantillon retrouvés | 91 / 95 — 95,79 % |
| Pipelines reconstruits depuis le jeu | **61 / 64 — 95,31 %** |
| IR et SPIR-V identiques pour ces pipelines | **122 / 122 stages** |
| Créations natives distinctes attendues / réussies | **56 / 56** |
| Cache reconstruit identique au sous-ensemble P0 | Oui, octet pour octet |

Le replay repart d'un nouveau répertoire portable contenant uniquement le cache
généré par cet import. Aucun cache natif NXEmu n'est fourni. Les 56 objets natifs
couvrent les 61 recettes selon le partage HLE décrit plus haut. Les trois échecs
concernent les indices 3, 28 et 48 de l'échantillon et quatre hashes shader ; les
identités, tailles et stages exacts sont dans `verified-results.json`.

SHA-256 du manifeste :
`c1f2e54183af37f49b49abeee2bc65503805bf1d281a4e75385f29d75d898c09`.

## Limites et décision

- Validation sur cette configuration Windows/RTX ; le transfert inter-GPU,
  Mac et Linux n'est pas validé ici. L'adaptateur fixe explicitement les capacités
  destinataires de ce test. Ce n'est pas encore un import communautaire universel.
- Les mots de recette et les clés sont liés au schéma expérimental actuel et
  à l'ABI cache v20. Le format public n'est pas figé ou refondu par cette étape.
- Les échecs restants viennent de shaders non localisés, et non d'un échec de
  traduction ou de création Vulkan sur les shaders retrouvés.
- L'égalité des produits du compilateur et le succès des créations natives sont
  vérifiés ; aucune comparaison pixel à pixel ni inspection du binaire opaque
  compilé par le pilote n'a été réalisée.
- Aucun gain de stutters ou de temps de compilation n'est revendiqué. Le cache
  disque NVIDIA n'a pas été isolé pour ces comparaisons ; les nombres rapportés
  sont les appels de création Vulkan réussis, pas les recompilations internes
  effectives du pilote. Le cache natif géré par NXEmu est désactivé.

**POC partiel validé :** la reconstruction sans instructions dans le manifeste
fonctionne pour 61/64 cas représentatifs observés. La publication communautaire
générale reste à valider. La prochaine investigation utile porte sur les quatre
blocs manquants de l'échantillon, notamment via une lecture correcte du RomFS
BKTR. Aucune optimisation du compilateur n'est nécessaire pour poursuivre cette
vérification.

## Reproduction

Les assistants sont dans `tools/shader-profile/`. Ils emploient les chemins
locaux de cette machine pour la capture, la recherche et les lancements de jeu.
Ils refusent d'écraser leurs principaux résultats existants. Le checkout de
mesure est `build/shader-profile-poc/master`, basé sur upstream/master
`e1faa8c99c5571884d934a23e30d4fab9a539c0f` ; aucun merge ou reset du checkout
de travail modifié n'a été effectué.

```powershell
cmake --build build/shader-recipe-poc/tests --config Release --target recipe-projection yaz0-scan
python tools/shader-profile/recipe_manifest.py import MANIFESTE.nxshader DOSSIER_JEU NOUVEAU_DOSSIER/vulkan.bin
python tools/shader-profile/run_rebuild_case.py NOUVEAU_TEST --seconds 20 --cache NOUVEAU_DOSSIER/vulkan.bin
python tools/shader-profile/compare_rebuild_trace.py NOUVEAU_TEST --resolved-only --profile build/shader-rebuild-poc/validation/physical-resolved-profile.json
python -m unittest discover -s tools/shader-profile -p 'test*.py'
```

Les 14 tests Python passent, notamment le bornage du flux Yaz0 et les conditions
de partage HLE (un remplacement de constante ou une différence SPIR-V interdit
de déclarer le cas validé). Les assertions d'intégration contrôlent aussi les
empreintes et l'identité intégrale du cache reconstruit. Les résultats détaillés
restent sous `build/shader-rebuild-poc/validation/`. Ne pas distribuer ses caches
ou ses traces privées contenant IR/SPIR-V.
