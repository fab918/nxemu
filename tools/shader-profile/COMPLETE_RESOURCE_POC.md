# NXEmu — reconstruction complète du corpus Shader Profile

Résultat du 23 octobre 2026. Ce rapport remplace le constat incomplet de
`FULL_BKTR_POC.md` pour les quinze derniers shaders. Le prototype reste isolé,
sans refonte V2 ni optimisation du compilateur ou du chemin de rendu normal.

## Résultat vérifié

L'import reconstruit **555/555 entrées (100 % du corpus capturé)** à partir du
manifeste et de la copie locale de Mario Kart avec son update 3.0.3. Pendant cet
import, un audit Python interdit les lectures du cache source et des ZIP.
Les caches et produits shader privés ne figurent pas dans le paquet livré.

| Mesure | Résultat |
|---|---:|
| Entrées graphiques reconstruites et validées | 545/545 |
| Entrées compute reconstruites et validées | 10/10 |
| Programmes distincts provenant des ressources locales | 529/529 |
| Combinaisons graphiques observées conservées | 1 108 |
| Correspondances IR et SPIR-V graphiques par recette | 1 090/1 090 stages |
| Créations Vulkan graphiques distinctes réussies | 481 |
| Créations Vulkan compute distinctes réussies | 5 |
| Manifeste gzip | 280 788 octets (274,2 Kio) |
| JSON décompressé | 4 569 159 octets |
| Tests Python | 31 réussis |

Les 555 recettes conservent leurs clés et leurs combinaisons. Le partage HLE
existant explique 64 recettes graphiques sans création native distincte. La
normalisation existante des tailles de buffers inutilisés ramène les dix
enregistrements compute à cinq clés natives ; les tailles utilisées, workgroups
et mémoires partagées restent distincts. Aucun nouveau regroupement de pipelines
n'est introduit. Les 1 090 comparaisons de stages incluent les produits partagés ;
ce ne sont pas 1 090 compilations physiques supplémentaires.

## Pourquoi les quinze derniers blobs ne correspondaient pas

L'audit des bornes du cache a identifié un mot de **8 octets après la branche
terminale** dans chacun des quinze blobs restants. Dans le chemin examiné,
`MakeShaderInfo` reçoit une taille incluant cette branche, puis `SetCachedSize`
l'interprète comme une borne haute inclusive ; `CachedSizeBytes` ajoute encore
un mot. La comparaison du blob entier incluait donc des octets au-delà du
programme détecté et échouait face aux ressources du jeu.

En excluant uniquement ce dernier mot, les quinze programmes correspondent à
des plages contiguës dans le RomFS logique, éventuellement décompressées avec
Yaz0. La branche terminale et toutes les instructions du programme sont
conservées. L'adaptateur réduit de huit octets la borne supérieure de lecture des
environnements concernés. Il ne synthétise ni instruction ni en-tête.

Les changements concernent 25 entrées, 27 occurrences d'environnement et quinze
identités de blobs source. Deux blobs ne différaient que par ce mot extérieur :
ils désignent le même programme de ressource. Le corpus passe ainsi de 530
identités de blobs à 529 programmes, sans perte de combinaison de pipeline.
Les 515 autres blobs restent identiques octet pour octet. Cette normalisation
est bornée aux cas retrouvés et vérifiés ; ce n'est pas une règle générale pour
tronquer des shaders arbitraires. Aucun correctif du renderer n'a été appliqué.

Les anciennes hypothèses de shaders dynamiques ou introuvables ne décrivent donc
pas ces quinze cas. Aucun programme ne reste introuvable dans ce corpus.

## Ressources locales et preuves

Le lecteur BKTR assemble la base et l'update dans un espace RomFS logique :
3 089 segments, 12 099 160 936 octets logiques, 6 596 fichiers. La recherche
précédente a également retrouvé treize shaders dans un segment NSO décompressé
avec LZ4. Vingt références ont été contrôlées dans une ressource réellement
assemblée depuis les deux fichiers.

Chaque référence indique le fichier, la ressource, les positions et tailles,
la transformation et les empreintes de vérification. Les positions physiques,
logiques et décompressées restent distinctes. Une adresse GPU n'est jamais
utilisée comme offset de fichier. Les références sont liées aux ressources de
cette version du jeu ; leur stabilité entre updates n'est pas démontrée.

`MarioKart-resource-spans.nxshader` contient des métadonnées, paramètres,
empreintes et locateurs, sans instructions invitées, en-têtes shader bruts, IR,
SPIR-V, binaires natifs ou données de correction embarquées. Il conserve le
format POC v1 et son adaptateur vers le cache interne v20. Ce n'est pas une ABI
communautaire finale.

SHA-256 du manifeste :
`6f33a985d5b14d81a9869f8a0f91648ff47732f80cf77e9bf2206a61d51a5c48`.

SHA-256 du cache local reconstruit, conservé uniquement pour la vérification :
`7aa9c7935da3497591dac9082ae29891321f0b15c0161e44f3b37049e009c488`.

`verified-results.json` vérifie les bornes modifiées, les octets préservés, les
clés et les 1 108 observations. `final-comparison.json` donne les comparaisons
avec le préchauffage de référence et le chemin normal en jeu. Contre le
préchauffage, les 545 clés natives effectives correspondent. Contre la capture
en jeu, 537 correspondent exactement ; huit contextes choisissent un autre
jumeau HLE/natif, avec les mêmes IR et SPIR-V. Cela ne constitue pas une preuve
d'identité de toutes les décisions d'exécution HLE ni du résultat pixel à pixel.

## Limites et décision

Validation native : Windows, RTX 5070 Ti, pilote 616.92.0.0. Les capacités cibles
du test restent explicites dans l'adaptateur. Mac → Windows, Windows → Mac et
Linux restent à tester. Le manifeste conserve les recettes invitées, mais ce
test ne prouve pas encore leur portabilité effective entre GPU.

Le cache natif NXEmu était désactivé pour les replays. Le cache disque du pilote
n'a pas été isolé : les 486 créations sont des appels Vulkan réussis, pas une
mesure de compilations internes du pilote. Aucun gain de stutters, de temps de
chargement ou de compilation n'est établi. Le corpus des démos/parcours ne
couvre pas toutes les courses. Aucun coût significatif en jeu n'est ajouté par
ces outils hors ligne ; cela ne mesure pas le coût d'une future intégration.

**GO pour poursuivre le POC portable ; NO-GO pour publier une fonctionnalité
communautaire présentée comme universelle.** La prochaine étape utile est un
test croisé sur un second GPU/OS, puis une comparaison reproductible à caches
pilote et NXEmu froids avec frametimes et compilations restantes. Conserver le
format expérimental jusque-là. Les bornes exactes des programmes et la séparation
recettes/programmes/références sont des exigences démontrées pour une future V2 ;
aucune refonte ni optimisation du compilateur n'est nécessaire à ce résultat.

## Reproduction dans le dépôt de travail

Les assistants utilisent les outils de projection et Yaz0 déjà compilés dans
le dépôt. Le paquet de sources n'est pas un exécutable autonome ; certains
chemins restent propres à cette machine.

```powershell
python -m unittest discover -s tools/shader-profile -p 'test*.py'
python tools/shader-profile/full_recipe_manifest.py import MANIFESTE.nxshader DOSSIER_JEU NOUVEAU_DOSSIER/vulkan.bin
python tools/shader-profile/run_rebuild_case.py NOUVEAU_TEST --seconds 25 --cache NOUVEAU_DOSSIER/vulkan.bin
python tools/shader-profile/compare_full_replay.py NOUVEAU_TEST
```

Preuves privées : `build/shader-origin-poc/` et
`build/shader-rebuild-poc/complete-resource-spans/`. Ne pas distribuer leurs
caches, snapshots ou traces de compilation.
