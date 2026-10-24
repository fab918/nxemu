# Bissection du gel en Unlocked

Lancer `Lancer-Bisect.cmd` une fois le build prêt et NXEmu fermé. Le lanceur
vérifie le commit et les empreintes de tous les modules pour éviter de tester
un mélange de builds. Le worktree, son build, ses réglages, ses logs et ses
caches sont séparés du checkout principal. Les dépendances sources identiques
et les données du jeu (dont NAND/sauvegardes) sont partagées ; éviter de lancer
deux instances simultanément. Les shaders d'Odyssey ont été copiés initialement.

Les étapes utilisent le même MSBuild/compilateur que le build qui bloque
(Release x64, v145 14.51). RmlUI est explicitement activé, même aux commits
où son activation par défaut n'était pas encore intégrée. Ni trace dans le
renderer, ni validation, ni correctif conditionnel ne sont ajoutés au code testé.
Le watchdog externe capture les threads après un gel de dix secondes.

La borne `good` initiale b027d97d provient de la stabilité rapportée de la PR #421
dont le renderer Vulkan est identique. Cette borne reste à reconfirmer avec
ce compilateur et ce frontend si les résultats la contredisent. Le point `bad`
687cab47 a bloqué plusieurs fois. Git bisect est actif uniquement dans le worktree.

Pour chaque essai, rapporter dans le chat le nombre de relances en Unlocked,
la durée et la présence ou l'absence du même gel. Un gel avec piles concordantes
constitue un résultat `bad`. Une compilation cassée est un `skip`, pas un `bad`.
Une seule relance sans gel, ou même sept, n'est pas une preuve de stabilité.

L'absence de gel doit être enregistrée avec l'exposition et rester provisoire
avant de marquer `good`. Par exemple, si le risque hypothétique était de 10 %
par essai indépendant, même 20 essais sans gel auraient encore une probabilité
de 12 % ; le risque réel et l'indépendance des essais ne sont pas connus.
Alterner relances du jeu et démarrages à froid suivant le scénario observé,
en les consignant séparément. Ne pas automatiser `git bisect good` sur un timer.

L'état et les journaux sont conservés dans `build/gpu-bisect/` du checkout
principal ; les captures du watchdog sont dans `build/vulkan-diagnostic/`
du worktree. Aucun rebase ni reset du checkout principal.
