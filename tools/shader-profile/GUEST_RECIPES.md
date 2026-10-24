# Recettes invitées — étape de validation, 22 octobre 2026

## Résultat

Le prototype conserve les combinaisons invitées avant l'application des capacités
de l'hôte, puis reconstruit la clé locale avec ces capacités. Il ne remplace pas
les clés utilisées pour rendre le jeu. Le cache interne reste en version 20.

Sur Mario Kart, avec l'update appliquée, une session automatique de 90 secondes
sur RTX 5070 Ti a donné :

| Mesure | Résultat |
|---|---:|
| États graphiques observés et comparés | 1 214 202 |
| Divergences de projection | 0 |
| Recettes distinctes | 967 |
| Clés locales distinctes observées | 823 |
| Combinaisons de hashes de stages | 343 |
| Hashes de shaders distincts | 498 |
| Observations perdues / erreur de sonde | 0 / aucune |
| Export JSON / gzip | 1 045 900 / 24 334 octets |
| Temps CPU cumulé de la sonde | 1,309 s |

Les 823 clés sont celles observées au point de capture, avant certaines
normalisations de dépendances du chemin lent. Ce nombre ne compte pas les
compilations natives. Les 967 recettes ne prouvent pas non plus 967 variantes
de shader à compiler. Une recette contient un état graphique et un contexte ;
plusieurs recettes peuvent partager des résultats de compilation.

La session utilise un répertoire portable neuf, sans cache NXEmu initial.
Elle vérifie la projection et ne mesure pas un gain de compilation : le cache
du pilote n'a pas été isolé pour cette étape. Les démos aléatoires ne permettent
pas une comparaison fiable des stutters. Le parcours ne couvre pas toutes les
courses. L'absence d'un shader d'une course non visitée est attendue.

## Implémentation

`FixedPipelineState::RefreshFromRegisters` expose la logique existante, avec les
mêmes transitions des quatre dirty flags. `Refresh` conserve son interface.
`GuestPipelineState::Capture` enregistre l'état avec tous les champs statiques,
sans options GPU ; `Project` applique les capacités du destinataire. Le masque
des streams activés permet notamment de reconstruire le workaround de stride.

`Words` écrit chaque champ explicitement ; `FromWords` le relit et refuse les
versions inconnues, tailles incorrectes, options hôte et valeurs tronquées.
Il n'y a ni dump mémoire de struct, ni pointeur, ni adresse GPU, ni instruction
shader. Les mots sont néanmoins encore liés au schéma expérimental des champs
du renderer : ce n'est pas l'ABI publique V2 définitive.

La sonde compare la projection à la clé réellement calculée par le renderer,
y compris sur les succès du cache. C'est indispensable : ne capturer que les
nouveaux pipelines manquerait des combinaisons masquées par les états dynamiques.
Les recettes sont dédupliquées par égalité complète des valeurs, pas par hash
seul. Une limite de 16 384 recettes borne les tables ; toute perte est signalée.
L'écriture du rapport a lieu à l'arrêt.

La sonde est désactivée par défaut. Pour un diagnostic, définir
`NXEMU_SHADER_RECIPE_PROBE` avec le chemin de sortie JSON avant le lancement.
Elle effectue alors volontairement une capture complète à chaque observation :
environ 1,08 µs par observation sur cette session. Ce mode ne constitue pas la
collecte optimisée destinée aux joueurs. Sans cette variable, aucune de ces
captures ou tables n'est créée. L'absence de régression de performances du
chemin normal reste à mesurer sur un parcours reproductible.

## Validation reproductible

Depuis la racine du dépôt :

```powershell
cmake -S tools/shader-profile/tests -B build/shader-recipe-poc/tests -G 'Visual Studio 17 2022' -A x64
cmake --build build/shader-recipe-poc/tests --config Release
ctest --test-dir build/shader-recipe-poc/tests -C Release --output-on-failure
```

Les tests comparent 128 jeux de capacités × 64 transitions, soit 8 192 cas,
à une copie figée de l'ancienne logique. Ils vérifient l'état complet, les dirty
flags, la projection, l'export/relecture, les combinaisons de sommets/blending
masquées par un hôte dynamique, le dual-source blending et les changements de
formats de cible. Les capacités artificielles ne remplacent pas des essais sur
du matériel Mac, AMD ou Intel.

Le runtime de test est compilé dans le checkout séparé
`build/shader-profile-poc/master`, basé sur upstream/master
`e1faa8c99c5571884d934a23e30d4fab9a539c0f`, avec le prototype. Le master de travail
contenant d'autres modifications n'a pas été fusionné ou réinitialisé.
`run_recipe_probe.py` est un assistant propre à cette machine : il lance le jeu
et son update depuis la configuration locale et attend l'arrêt automatique du
checkout de mesure. Il ne doit pas être distribué avec ses données privées.

```powershell
python tools/shader-profile/run_recipe_probe.py
python tools/shader-profile/analyze_recipe_probe.py build/shader-recipe-poc/mariokart-probe/guest-recipes.json
```

Le premier assistant refuse d'écraser un test existant. Le rapport condensé est
dans `build/shader-recipe-poc/mariokart-probe/summary.json`, avec le hash SHA-256
du diagnostic compressé. Celui-ci finit par `.nxshader-probe.json.gz` pour éviter
de le confondre avec un profil complet accepté par l'importeur précédent.

## Suite recommandée et accélération

1. Compléter les recettes avec les réponses de l'environnement de compilation,
   les références aux ressources locales et l'identité exacte jeu/update.
   Les tailles de constant buffers et les hashes des six stages sont déjà
   enregistrés par la sonde, mais ne suffisent pas à cet environnement complet.
   Les anciens profils macOS ont déjà perdu certains champs dynamiques : on ne
   peut pas les déduire fidèlement de leur seule clé ; une nouvelle capture sera
   nécessaire pour les combinaisons absentes.
2. Remplacer la capture diagnostique complète par un instantané invité mis à jour
   par groupes dirty. Capturer aussi les variantes sur les cache hits, puis ne
   sérialiser que les nouvelles recettes. Mesurer son coût sur le même parcours.
3. Reconstruire depuis le manifeste et les ressources du jeu, comparer les clés
   au chemin normal, puis tester matériellement Windows → Mac et Mac → Windows.
4. Optimiser séparément le compilateur, avec compteurs de traductions, émissions
   SPIR-V, modules et créations de pipelines natifs, plus temps CPU/attente.

Pour l'étape 4, conserver trois niveaux : identité du shader, variante compilée,
puis pipeline complet. Une variante ne dépend pas seulement du hash du code :
elle peut dépendre des réponses de l'environnement, des constantes, du profil
hôte, de l'état invité utilisé par le compilateur, des interfaces entre stages,
des bindings et de la version du compilateur. Le renderer courant relance
`TranslateProgram` dans plusieurs constructions de pipelines, mais ce constat
ne prouve pas encore que leurs entrées de compilation sont identiques.

La première optimisation à instrumenter est un travail partagé pour les requêtes
concurrentes strictement identiques : un résultat ou une compilation en cours
par clé complète, en distinguant explicitement échecs et annulations. Ensuite,
un cache local des variantes peut réutiliser les résultats validés entre
pipelines. L'IR actuel est mutable et utilise des pools réinitialisés ; conserver
ses pointeurs serait incorrect. Toute réutilisation d'IR exige un instantané
indépendant ou une copie correctement possédée. Les produits compilés restent
locaux ; le manifeste communautaire contient seulement leurs recettes.

**Décision : GO pour poursuivre ce prototype ; pas encore de GO pour publier
un format communautaire V2.** La projection graphique est validée dans le
périmètre observé. La reconstruction complète inter-GPU, la collecte optimisée
et un gain de temps de compilation ou de stutters restent à démontrer.
