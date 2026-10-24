# NXShader : réutilisation entre base et update

Cette modification autorise une réutilisation prouvée entre contextes connus du
même titre, sans modifier le format NXSHBIN4 ni la compression Zstd 3. Il ne
s’agit pas d’une comparaison approximative : le programme doit avoir exactement
la même empreinte SHA-256 et les dépendances du pipeline doivent correspondre.

## Comportement

1. Le lecteur reconstruit les recettes du profil sélectionné depuis les
   ressources locales vérifiées. Une ressource absente ou différente exclut ses
   recettes dépendantes. Une provenance inconnue n’est pas devinée.
2. Les recettes de la version courante suivent le préchauffage normal. Celles
   d’une autre version sont préparées séparément avec le compilateur et le GPU
   actuels. Elles restent inactives et associées à leur contexte source.
3. Lors d’un premier défaut dans le cache actif, la recherche par clé de pipeline
   retrouve ses candidats. La taille et le SHA-256 du programme du jeu chargé
   doivent correspondre. Les constantes, tailles de buffers interrogées, types
   et formats de textures, tables de textures, paramètres de stage et états
   nécessaires à la compilation sont comparés.
4. Les requêtes HLE positives **et négatives** sont observées pendant la préparation
   locale. Un remplacement nouvellement présent ou disparu invalide le candidat.
   Les valeurs par défaut interrogées sont également couvertes. Une banque de
   constantes désactivée est rejetée avant tout accès qui pourrait déclencher
   une assertion.
5. Si la preuve passe, le pipeline préparé entre dans le cache actif et le worker
   associe son record au contexte courant. Sinon, le chemin normal compile et
   capture le shader du jeu chargé. Les shaders nouveaux suivent aussi ce chemin.

Un déplacement d’adresse est accepté pour un programme identique avec le même
alignement des mots de scheduling et une plage d’adresses valide. Les sauts
absolus JMX/JMP/JCAL et lectures du PC LEPC interdisent cette réutilisation après
déplacement. Les dispositions de programme inhabituelles restent exclues.

## Coûts évités

- Aucun champ supplémentaire dans le fichier partagé : aucun octet ajouté pour
  cette preuve. Les quatre corpus synthétiques conservent exactement les mêmes
  payloads, profils, caches reconstruits et identités de recettes.
- SHA-256 du programme cible mémorisé par propriétaire immuable, partagé entre
  pipelines, sans conserver son corps dans le mémo. Un propriétaire expiré ne
  peut pas faire passer un nouveau programme pour l’ancien.
- Le code source temporaire est libéré après préparation des candidats. Seuls
  leurs pipelines, métadonnées de preuve et identifiants de records restent.
- Une promotion ajoute 64 octets dans l’index local de contexte, plus un header
  unique de 8 octets. Elle ne réécrit ni ne rehache les programmes. Les promotions
  répétées n’ajoutent rien. Le writer partagé vérifie que le record existe encore.
- Les candidats dupliqués par identifiant et ceux déjà couverts par un record
  natif de la version courante ne sont pas préparés une seconde fois.
- Les vérifications ne sont exécutées que sur un défaut du cache actif. Après
  promotion, les accès normaux au pipeline ne refont pas la preuve.
- Les lectures de validation n’enregistrent pas de nouvelles tables de requêtes
  temporaires. L’empreinte initiale et la première insertion du mémo ont néanmoins
  un coût et peuvent allouer.
- Les entrées de mémo et les métadonnées HLE sont bornées. Le budget des entrées
  de compilation et preuves au démarrage reste limité à 256 Mio ; ce budget
  n’inclut pas les allocations internes du pilote.

## Validation effectuée

- 13 tests natifs et 77 tests Python passent, dont quatre scénarios supplémentaires
  d’import interversion : mise en attente sans association cible, profil mixte,
  ressource changée avec conservation des autres recettes, provenance inconnue.
- 6 tests passent sous ASan/UBSan ; la détection de fuites n’est pas disponible
  avec le runtime Apple utilisé.
- 33 cas de mutations de dépendances et 18 000 cas de propriétaires expirés.
  Simulation de 1 000 candidats : les 250 compatibles sont réutilisés, les
  750 incompatibles suivent la recompilation simulée.
- Le store est testé avec troncature, record absent, répétitions et 400 associations
  concurrentes à 400 appends concurrents. Aucune duplication des données.
- 2 196 passages dans le vrai CFG/traducteur/optimiseur/émetteur SPIR-V sur des
  programmes NOP/EXIT et BRA relatif écrits pour les tests. Les 36 modules sont
  validés par `spirv-val --target-env vulkan1.1`. Les sorties sont identiques
  dans 12 groupes à trois adresses : 0, 32 et 65 536.
- Application et modules macOS reconstruits avec succès.

Sur cette machine, le microbenchmark obtient environ **20 ns** par preuve avec
empreinte déjà mémorisée et fausse mémoire en RAM, contre **20 µs** pour la
première empreinte d’un programme de 64 Kio. Ces chiffres ne mesurent ni les
synchronisations de mémoire GPU ni le pilote Vulkan et ne prédisent pas les FPS.
Les imports complets des quatre corpus prennent respectivement 57,61 / 60,65 /
28,43 / 131,23 ms en médiane de trois processus ; ils n’incluent pas la compilation
Vulkan. Les profils font 14 890 / 12 443 / 24 923 / 192 299 octets.

## Limites

Ce mécanisme évite la recompilation à la première utilisation d’un candidat
compatible **déjà préparé au démarrage** ; il n’élimine pas sa préparation initiale.
Un programme changé ou jamais utilisé peut donc avoir été préparé inutilement.
L’évaluation sur une vraie paire base/update et avec un pilote froid reste à faire.

Les localisateurs existants ou une copie locale vérifiée du programme sont
nécessaires. Le lecteur ne sait pas encore retrouver arbitrairement un programme
dans un conteneur réorganisé via le VFS monté. Les nouveaux shaders ne peuvent
pas être déduits du profil de base : ils sont compilés puis enregistrés.

Ni une taille maximale universelle de 100 ko, ni l’absence totale de stutters
ne sont garanties. Les détails chiffrés sont dans
[le rapport JSON](reports/cross-version-2026-10-24.json).

## Reproduction

Construire les tests natifs en Release, puis exécuter `ctest`, les tests Python
de `tools/shader-profile` et `cross-version-reuse`. Pour les 36 modules, configurer
`NXSHADER_APP_BUILD` vers un build complet de l’application et exécuter
`shader-compile-benchmark <dossier-sortie>`, puis valider chaque fichier avec
`spirv-val --target-env vulkan1.1`. Comparer les trois offsets pour chaque paire
NOP/workgroup et chaque mode avec ou sans branchement. Les scripts et sources
de test ne contiennent aucun shader extrait d’un jeu.
