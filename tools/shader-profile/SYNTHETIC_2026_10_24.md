# Validation synthétique et coûts NXShader — 24 octobre 2026

Référence : `344f7fc8`. Machine : Apple M4 Pro, macOS arm64, build Release.
Les données sont synthétiques et ne contiennent aucun shader de jeu.
Zstd reste au **niveau 3**. Le benchmark livré compare uniquement 1, 3 et 6.

## Validation

- 12 tests natifs et 73 tests Python réussis ; 5 tests sous AddressSanitizer et UndefinedBehaviorSanitizer réussis. La détection des fuites est indisponible avec ce runtime Apple et a été désactivée.
- 3 000 mutations du payload, 398 troncatures du profil, 158 mutations de l’enveloppe, 398 troncatures du cache et 287 corruptions de données de frames.
- 1 200 appends aléatoires, 800 appends concurrents et 256 variantes compute. Vérification des ensembles exacts de recettes, du partage des programmes, des collisions et de l’expiration des propriétaires mémorisés.
- Ressources absentes/tronquées, updates séparées, conteneur nommé face à un leurre de même préfixe, programmes locaux mal étiquetés, reprise sans accès au jeu.
- 8 704 recettes distinctes dans quatre charges d’import. Trois imports par charge ; aucune recette perdue. Les payloads, profils Zstd 3, caches reconstruits et identités de recettes sont identiques octet par octet à la référence.
- 366 passages dans le vrai CFG/traducteur/optimiseur/émetteur SPIR-V, avec des programmes NOP/EXIT écrits pour ce test. Sorties déterministes et tailles de workgroup vérifiées ; six modules validés par `spirv-val --target-env vulkan1.1`.
- Compilation complète de NxEmu et des modules réussie.

## Import et cache local

Les durées d’import sont les médianes de trois processus complets : démarrage de l’outil, lecture, validation, reconstruction, écriture et relecture du cache. Elles ne comprennent pas de création de pipeline Vulkan.

| Charge | Recettes | Avant (ms) | Après (ms) | Taille NXShader (octets) |
|---|---:|---:|---:|---:|
| shared-code | 2048 | 401.90 | 63.15 | 14890 |
| mixed-variants | 2048 | 159.57 | 66.55 | 12443 |
| unique-programs | 512 | 54.09 | 28.50 | 24923 |
| large-profile | 4096 | 267.98 | 137.32 | 192299 |

| Cache local | Lecture avant (ms) | Lecture après (ms) |
|---|---:|---:|
| 16 Mio de programmes uniques | 41.68 | 11.24 |
| 64 Mio de programmes uniques | 166.19 | 33.72 |

Corrections appliquées : SHA-256 accéléré pour les frames locales ; empreinte mémorisée par propriétaire de programme immuable ; assemblage des métadonnées sans recopier les instructions ; validation du hash moteur une fois par programme ; identité d’environnement préparée une fois ; localisateurs préparés une fois avant tri ; ouverture d’un conteneur raw une fois pour ses programmes.
Le writer invalide aussi son index si la date du fichier change, même à taille identique. Un test reproduisait une omission de pipeline après remplacement du cache ; il passe avec le correctif. Même contrôle pour l’index de contexte.

## Suivi NXShader sur les draws

Le même driver de benchmark et les mêmes options compilent les headers de référence puis les headers corrigés. Cinq passages, dont un échauffement, sans horloge par draw. Les ensembles exacts des journaux sont vérifiés à chaque passage. Une référence sans suivi et une référence avec marqueurs seuls sont mesurées séparément.

| Scénario | Avant (ns/draw) | Après (ns/draw) | Allocations sur le thread principal après |
|---|---:|---:|---:|
| unchanged (64 entrées connues) | 10.73 | 8.92 | 0 |
| mostly_unchanged (64 entrées connues) | 10.39 | 9.05 | 0 |
| known_transitions (64 entrées connues) | 66.68 | 65.00 | 0 |
| prefix_only (64 entrées connues) | 65.24 | 64.52 | 0 |
| untracked_writes (64 entrées connues) | 10.83 | 9.21 | 0 |
| discovery (64 entrées connues) | 548.10 | 447.02 | 562 |
| unchanged (512 entrées connues) | 10.51 | 8.95 | 0 |
| mostly_unchanged (512 entrées connues) | 10.66 | 9.27 | 0 |
| known_transitions (512 entrées connues) | 76.39 | 77.60 | 0 |
| prefix_only (512 entrées connues) | 156.89 | 91.22 | 0 |
| untracked_writes (512 entrées connues) | 10.18 | 9.04 | 0 |
| discovery (512 entrées connues) | 478.76 | 467.90 | 562 |

Le chemin inchangé est court et peut être inliné. Une modification du seul préfixe réutilise la capture canonique précédente si les registres, le canal, la topologie et les capacités sont identiques. Les changements masqués par les états dynamiques du GPU source restent suivis.
La table de préfixes passe de 1 024 à 4 096 slots ; le mémo total passe de 3 416 620 à 4 744 492 octets. Ce compromis améliore les revisites de nombreuses combinaisons et augmente la mémoire de 1 327 872 octets. Les collisions comparent toujours les données complètes ; une éviction ne supprime pas de recette.
Les écritures de registres seules sont également mesurées sur 2 millions de writes, avec 0 %, 10 % et 100 % de registres pertinents. Une barrière de compilation empêche de déplacer artificiellement le masque hors de la boucle. Les différences subnanosecondes sont sensibles au bruit et ne constituent pas une mesure de FPS.
Les recettes connues n’allouent rien dans ces tests. La découverte alloue les nouveaux nœuds et les batches envoyés au worker. Les SHA-256 du journal et les accès disque restent sur le worker ; son drainage est mesuré séparément dans le JSON.

## Taille et limites

Le niveau 6 gagne sur certaines tables mais peut produire un fichier plus gros que le niveau 3 sur d’autres : le niveau 3 est conservé. Aucun hash de validation ni recette n’a été retiré pour réduire artificiellement la taille.
Le profil synthétique à 4 096 programmes dépasse 100 ko : leurs seules empreintes SHA-256 représentent déjà 131 072 octets peu compressibles. Un plafond de 100 ko ne peut donc pas être garanti pour une couverture arbitraire avec cette représentation.
Ces tests ne prouvent pas un surcoût toujours négligeable en jeu. Les draws stables sont peu coûteux ; des changements à chaque draw et la découverte ont un coût supérieur. Ils ne mesurent ni les FPS, ni les stutters, ni le compilateur du pilote Vulkan. Le corpus de compilation NOP/EXIT reste beaucoup plus simple qu’un shader de jeu.
Le code IR et SPIR-V dépend aussi des constantes, types de textures, états projetés, bindings et capacités hôte. Une éventuelle réutilisation intermédiaire devra intégrer ces dépendances et être mesurée sur un corpus représentatif.

## Reproduction

Construire `tools/shader-profile/tests` en Release, puis exécuter :

```sh
ctest --test-dir build/nxshader-tests --output-on-failure
NXSHADER_READER="$PWD/build/nxshader-tests/nxshader-reader" \
NXSHADER_EXPORTER="$PWD/build/nxshader-tests/nxshader-export" \
python3 tools/shader-profile/synthetic_benchmark.py --output build/synthetic.json
build/nxshader-tests/recorder-hotpath build/recorder-narrow
build/nxshader-tests/recorder-hotpath build/recorder-wide wide
build/nxshader-tests/register-tracking-benchmark
```

Les dossiers du benchmark recorder doivent être neufs. Pour comparer les headers, configurer `NXSHADER_RECORDER_REFERENCE_INCLUDE` vers leur copie figée, puis utiliser `recorder-hotpath-reference`. Pour le compilateur CPU, configurer `NXSHADER_APP_BUILD` vers un build complet de l’application et utiliser `shader-compile-benchmark <dossier-sortie>`. Les sanitizers sont activés par les flags C/C++ usuels `-fsanitize=address,undefined` ; exécuter au minimum `synthetic-robustness`, `cache-store`, `profile-hash`, `recorder-replay` et `recorder-input-cache`.

Résultats détaillés : [reports/synthetic-2026-10-24.json](reports/synthetic-2026-10-24.json).
