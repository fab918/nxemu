# NXEmu — RomFS logique BKTR et validation des 555 entrées

État du POC au 23 octobre 2026. Pas de refonte V2 ni d'optimisation du compilateur.

## Résultat

La lecture logique du RomFS patché est implémentée et vérifiée. La validation
porte maintenant sur **les 555 entrées du cache capturé**, dont 545 graphiques
et 10 compute. Avec les environnements et octets du cache source comme référence,
les 555 entrées reproduisent les produits de compilation et les constructions
natives attendues.

Avec **uniquement le manifeste et les fichiers locaux du jeu**, le prototype
reconstruit et valide **530/555 entrées — 95,50 %**. Il reste 15 shaders non
localisés, bloquant 25 entrées graphiques. Ce résultat ne constitue donc pas
encore une reconstruction intégrale depuis le jeu seul.

| Mesure | Cache de référence | Manifeste + jeu seul |
|---|---:|---:|
| Entrées graphiques validées | 545 / 545 | 520 / 545 |
| Entrées compute validées | 10 / 10 | 10 / 10 |
| Total validé | **555 / 555** | **530 / 555** |
| Correspondances IR et SPIR-V graphiques par recette | 1 090 / 1 090 stages | 1 040 / 1 040 stages |
| Correspondances IR et SPIR-V compute par entrée | 10 / 10 | 10 / 10 |
| Objets graphiques natifs distincts créés avec succès | 481 | 460 |
| Objets compute natifs distincts créés avec succès | 5 | 5 |

Ces deux colonnes ont des sources différentes : la première vérifie toutes les
recettes contre le cache source ; la seconde prouve la reconstruction disponible
sans reprendre ses instructions. Les shaders non retrouvés ne sont pas insérés
dans le manifeste sous une autre forme.

## P0 — RomFS logique avec BKTR

Le lecteur indépendant est dans `bktr_resources.py`. Il reprend les règles des
implémentations NXEmu `fssystem_nca_file_system_driver`, `fssystem_indirect_storage`
et `fssystem_bucket_tree` :

- identifier la section correspondante dans les NCA de la base et de l'update ;
- lire et contrôler la table indirecte BKTR et ses blocs de feuilles de 16 Kio ;
- construire un espace logique dont chaque intervalle lit soit la section de
  base, soit la zone de données du patch ;
- appliquer ensuite la plage RomFS décrite par IVFC et parcourir ses répertoires
  et fichiers dans cet espace logique ;
- exposer séparément les positions logiques et les segments physiques réellement
  lus dans les deux fichiers.

La section Mario Kart utilise **3 089 entrées BKTR**, pour un RomFS logique de
**12 099 160 936 octets** et **6 596 fichiers**. La recherche a traité
18 081 308 809 octets de ressources après décompression Yaz0, sans fichier ignoré
pour dépassement de la limite prévue.

Cette lecture complète n'a retrouvé aucun des 28 shaders alors manquants. Elle
est néanmoins vérifiée par cinq tests de bornes, de feuilles, de cycles et de
lectures traversant les deux fichiers, ainsi que par **20 shaders réels** retrouvés
dans une ressource assemblée depuis **la base et l'update**. Les vingt références
BKTR sont incluses dans le manifeste final et réellement utilisées lors de son
import de contrôle.

Les locateurs logiques stockent le fichier de base, celui de l'update, les NCA,
la section, le chemin de ressource, sa position logique et son empreinte, puis
la position du shader dans cette ressource éventuellement décompressée. Les
segments physiques sont enregistrés séparément pour l'audit. Une adresse GPU
n'est jamais interprétée comme un offset de fichier.

Le lecteur est limité aux conteneurs **DNCA déjà en clair** de ce POC. Il ne
réalise pas de déchiffrement. Les couches sparse, compression NCA et patch
meta-hash non prises en charge sont rejetées. Il contrôle les structures et les
bornes, puis les empreintes des ressources/shaders utilisés ; il ne constitue
pas un vérificateur cryptographique complet de toute la chaîne IVFC.

## Localisation des shaders restants

Après le résultat négatif dans le RomFS patché, les segments NSO de l'ExeFS ont
également été décompressés. Le lecteur LZ4 vérifie les bornes et les tailles ; les
empreintes de segments présentes dans les en-têtes NSO sont vérifiées lors de la
recherche. **13 shaders supplémentaires** ont été retrouvés, octet pour octet,
dans le segment de données de `main` de la base.

Le corpus passe ainsi de **502/530 à 515/530 blocs shader localisés — 97,17 %**.
Le nombre d'entrées reconstructibles depuis le jeu passe de **514/555 à 530/555**.
Ce gain vient de la lecture NSO/LZ4, pas de BKTR : les deux résultats ne sont pas
confondus.

Pour les 15 blocs encore manquants :

- aucun programme complet n'a été retrouvé par les recherches brutes, Yaz0,
  RomFS logique patché ou NSO/LZ4 ;
- leurs en-têtes de 80 octets ont des correspondances dans le RomFS ;
- leurs corps d'instructions complets n'ont pas de correspondance dans les
  ressources logiques et segments NSO examinés. Un simple changement d'en-tête
  ne suffit donc pas à expliquer les échecs.

Ils restent classés **non localisés**, et non « dynamiques » sans preuve. Une
autre transformation de ressource ou une génération/modification par le jeu
reste possible. La prochaine investigation utile consiste à tracer la provenance
des uploads de ces quinze programmes. Leurs hashes, tailles, stages et les
25 entrées affectées sont listés dans `verified-results.json`.

## P1 — Validation du corpus complet

Les clés graphiques sont reconstruites à partir des recettes invitées par
l'outil C++ de projection existant. Les clés compute sont reconstruites à partir
de leurs paramètres numériques : identité shader, mémoire partagée, workgroup,
tailles de constant buffers et taille de table de textures.

Deux replays indépendants comparent le cache original aux 555 entrées régénérées.
Ils contrôlent les clés effectives, l'IR avec ses flags, le SPIR-V octet pour
octet et le succès des appels de création Vulkan. Les 545 entrées graphiques
sont aussi comparées à la capture de compilation pendant le jeu de l'étape
précédente. Elles concordent toutes.

**Partage existant conservé.** Le préchauffage réutilise 64 jumeaux HLE/ordinaires
sans remplacement de constante : 545 recettes graphiques demandent donc
481 objets natifs. Lors de la capture en jeu, huit de ces recettes avaient été
construites sous l'autre clé de contexte ; leur IR et leur SPIR-V sont identiques.
Le comportement de partage du renderer n'a pas été modifié.

Les dix entrées compute correspondent à **cinq clés distinctes** après la
normalisation déjà effectuée par le renderer. Cinq enregistrements se distinguent
par des tailles de buffers que ces shaders n'utilisent pas. Les dix entrées sont
validées contre les cinq objets natifs correspondants ; ce ne sont pas cinq
compilations manquantes. Les tailles utilisées, les workgroups et la mémoire
partagée ne sont pas fusionnés.

## Import avec le manifeste et le jeu seulement

`MarioKart-full-555-logical.nxshader` contient les 555 entrées, les 530 identités
shader et **les 1 108 combinaisons graphiques capturées**, y compris celles qui
partagent une clé sur la RTX. Sa taille est **280 478 octets** compressés,
pour 4 539 436 octets JSON.

Le fichier conserve le format POC v1 et le cache cible v20. Les locateurs BKTR
et NSO sont des extensions de l'adaptateur de recherche/import isolé ; aucune
ABI publique V2 n'est proposée. Le manifeste n'inclut pas d'instructions invitées,
d'en-tête shader brut, d'IR, de SPIR-V ou de binaire natif. Les paramètres de
compilation et empreintes sont conservés.

L'import indépendant interdit par audit Python les lectures des caches sources
et des ZIP. Il reconstruit les clés, lit les ressources du jeu, applique les
transformations déclarées et vérifie les empreintes. Le cache obtenu contient
530 entrées, dont les dix compute. Il est **identique octet pour octet au
sous-ensemble correspondant de la référence reconstruite**, et identique au
cache effectivement utilisé pour le replay Vulkan validé. L'audit est un contrôle
de ce prototype, pas une sandbox système pour du code arbitraire.

SHA-256 du manifeste :
`5d899f3cefa8b6a0e9c46347ce6c70d566c3fcf2d84dae6d2d729a5beee5f25d`.

SHA-256 du cache importé depuis le jeu :
`a456b6581be3a4f1f29bfe25c3271f85529bd073d908f6d0233df3e5b96e60ff`.

## Isolation, tests et limites

Les nouvelles traces compute et modifications nécessaires à leur enregistrement
se trouvent uniquement dans le checkout de diagnostic
`build/shader-profile-poc/master`. Aucun changement supplémentaire n'a été
apporté au chemin de rendu normal, au cache interne ou au compilateur.

Les **27 tests Python passent**. Ils couvrent notamment BKTR, LZ4, les contrôles
de tailles et le partage HLE ; les tests compute vérifient qu'une différence de
taille réellement utilisée, de workgroup ou de mémoire partagée reste distincte.
Les assertions d'intégration contrôlent l'identité du cache reconstruit et les
comparaisons de produits de compilation.

La validation native reste limitée à Windows, RTX 5070 Ti, pilote 616.92.0.0,
avec Mario Kart et l'update locale 3.0.3. Elle ne démontre pas encore le transfert
Mac/Windows/Linux. Les capacités destinataires du test sont explicites dans
l'adaptateur ; les chemins des assistants restent ceux de cette machine.

Le cache natif géré par NXEmu est désactivé pendant ces replays. Le cache disque
du pilote n'a pas été isolé : les nombres rapportés sont des créations Vulkan
réussies, pas des recompilations internes mesurées du pilote. Aucun gain de
stutters ou de temps de compilation n'est revendiqué. Aucun test pixel à pixel
ou comparaison du binaire opaque généré par le pilote n'est effectué.

Le corpus dépend des parcours/démos capturés et ne couvre pas toutes les courses.
La reconstruction intégrale depuis le jeu seul reste inachevée tant que les
15 corps shader manquants ne sont pas retrouvés ou reconstruits localement.

## Reproduction dans le dépôt existant

```powershell
python -m unittest discover -s tools/shader-profile -p 'test*.py'
python tools/shader-profile/full_recipe_manifest.py import MANIFESTE.nxshader DOSSIER_JEU NOUVEAU_DOSSIER/vulkan.bin
python tools/shader-profile/run_rebuild_case.py NOUVEAU_TEST --seconds 25 --cache NOUVEAU_DOSSIER/vulkan.bin
python tools/shader-profile/compare_full_replay.py NOUVEAU_TEST --resolved-only
```

Les assistants s'appuient sur les outils de projection/Yaz0 déjà compilés dans
le dépôt de travail. Les données et preuves privées restent dans
`build/shader-bktr-poc/` et `build/shader-rebuild-poc/`. Ne pas distribuer leurs
caches, snapshots ou traces IR/SPIR-V. Le paquet livré contient uniquement le
manifeste, les résultats numériques et les sources des outils.
