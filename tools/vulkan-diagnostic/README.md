# Diagnostic des blocages Vulkan (Windows)

## Captures mémoire étendues

Après le premier freeze détecté, le lanceur conserve désormais un dump complet
de la mémoire du processus dans `hang-*/extended/`, les modules réellement chargés
et leurs empreintes, les PDB NxEmu disponibles et les événements GPU Windows récents.
Deux minidumps supplémentaires suivent la capture complète. Le dump complet est
limité à une tentative par session et nécessite une réserve d'espace disque ;
`-NoFullMemory` permet de conserver uniquement les captures légères.
La ligne `Device loss occurred!` déclenche aussi une capture, même si la fenêtre
répond encore. Le journal est lu par incréments bornés à 64 Kio par seconde ; les
anciennes lignes d'erreur ne redéclenchent pas une capture à chaque vérification.

Cette collecte fonctionne aussi avec `-NoTrace` et n'ajoute aucune sonde au renderer.
La mémoire et les métadonnées sont lues seulement après la détection du freeze.
Le dump complet peut prendre quelques secondes et plusieurs Gio ; laisser NXEmu
ouvert jusqu'au message `Capture etendue sauvegardee`.

Pour capturer un processus déjà bloqué sans relancer le jeu :

```powershell
./tools/vulkan-diagnostic/Capture-Now.ps1 -ProcessId <PID> -OutputDirectory build/vulkan-diagnostic/capture-manuelle
```

Le dossier de destination doit être neuf. Les dumps complets restent locaux et
peuvent contenir les données présentes en mémoire du jeu et de l'application.

Sur le build diagnostique qui le supporte, `-RetainCommandHistory` active
`NXEMU_VK_RETAIN_CHUNKS=1` uniquement pour NXEmu. Le scheduler conserve les 128
derniers chunks de commandes CPU dans un anneau d'environ 4 Mio, avec leur
ordre, taille et buffers de commande associés. Il retarde le recyclage des chunks
sans copier les commandes, ajouter de soumission GPU ou ajouter d'attente.
Le coût CPU et l'effet sur les FPS restent à mesurer. Désactivé par défaut.
Les captures possédant des pointeurs vers des allocations libérées ne sont pas
des données de replay ; leurs scalaires et adresses sont des indices à analyser
avec les PDB, les valeurs timeline et les autres captures.

## Test de durée de vie des sémaphores d'acquisition

Le test `tests/acquire_lifetime.cpp` maintient une attente Vulkan en cours via un
sémaphore timeline contrôlé depuis le CPU. Il vérifie que la réutilisation prématurée
d'un sémaphore d'acquisition déclenche `01779`, puis que la réutilisation après
la fence du submit consommateur passe. Le callback de validation rejette l'appel
invalide avant son envoi au pilote. Ce test ne reproduit pas le freeze du jeu.
Il est également conservé dans le commit `712c2fa8` de la branche
`codex/fix-acquire-semaphore-lifetime` (`tools/vulkan-acquire-lifetime/`).

Compiler normalement `nxemu.sln` en Release x64, puis fermer NXEmu et lancer
`Lancer-Diagnostic.cmd`. Le lanceur active `NXEMU_VK_TRACE=1` pour son processus
enfant uniquement et enlève les variables héritées de validation/capture Vulkan.
Le lancement normal de NXEmu laisse le traceur désactivé.

Garder la console ouverte, lancer le jeu, passer en **Unlocked**, puis arrêter
et relancer le jeu autant de fois que nécessaire. Le réglage Unlocked peut
revenir à son état verrouillé lors d'une relance : le remettre manuellement.
Le traceur reste actif dans les deux modes pour enregistrer la transition.
Le message `Historique Vulkan connecte` confirme la connexion au module instrumenté.

Pour comparer sans recompiler, lancer `Lancer-Temoin-Sans-Trace.cmd` après avoir
fermé NXEmu. Ce témoin utilise exactement le même binaire avec `NXEMU_VK_TRACE=0` :
les sondes mémoire sont désactivées et les minidumps automatiques restent actifs.
Le fichier launch.json indique `vulkanTrace: false`. Une absence de blocage sur
quelques essais ne démontre pas une correction. Si le témoin bloque et la trace
active ne bloque pas sur des essais comparables, la collecte influence probablement
le timing. Si aucun ne bloque, comparer ensuite avec le module antérieur conservé
pour distinguer l'effet du nouveau build de la variabilité du problème.

Sur ce poste, `Lancer-Temoin-Initial.cmd` utilise le module vidéo exact sauvegardé
lors du blocage du 21 octobre à 16:41, avant instrumentation. Les empreintes du
module, de son PDB et de l'exécutable sont vérifiées. Fermer NXEmu avant de le
lancer et garder la console ouverte. Le module actuellement installé est sauvegardé,
remplacé pendant l'essai, puis restauré à la fermeture de NXEmu. Les réglages et
l'exécutable restent identiques ; les minidumps restent actifs. Ce lanceur est lié
aux binaires conservés dans build/, il refusera de démarrer si la référence manque
ou si l'exécutable a changé. Si la console est fermée brutalement, la restauration
peut ne pas avoir lieu ; le module précédent reste dans baseline-backup-*.

**Candidat conditionnel écarté** : le gel s'est reproduit avec la même signature,
et une baisse d'environ 250 à 200 FPS a été rapportée (comparaison non contrôlée).
Le changement a été retiré du code courant ; le patch et le binaire sont gardés
pour analyse. Le diagnostic de référence reste `Lancer-Diagnostic.cmd`.

`Lancer-Correctif-Conditionnel.cmd` conserve, uniquement pour comparaison, le candidat compilé
dans `build/vulkan-diagnostic/conditional-scope-candidate`. Il ferme la passe de
rendu courante avant de commencer/terminer un bloc conditionnel, avec une portée
tout entière hors passe. L'historique mémoire reste actif et la validation Vulkan
reste désactivée en jeu. Le candidat corrige l'erreur de validation 01986 ; son
effet sur le gel intermittent et son coût en jeu ne sont pas encore établis.
La fermeture de passe peut réduire le regroupement des commandes lors de
changements de prédicat, donc une mesure en jeu reste nécessaire.

## Reproduction synthétique de la portée conditionnelle

Le programme autonome `tests/conditional_scope.cpp` requiert le SDK Vulkan et
la couche Khronos. Il enregistre la séquence invalide sans la soumettre au GPU,
puis soumet les variantes valides avec une fence et un délai maximal de 2 s.
Il teste l'erreur de portée indépendamment d'Odyssey ; il ne reproduit pas le gel.
Il ne mesure pas la performance des passes du jeu.

```powershell
cmake -S tools/vulkan-diagnostic/tests -B build/vulkan-scope-test -A x64
cmake --build build/vulkan-scope-test --config Release
$env:VK_LAYER_PATH = "$env:VULKAN_SDK\Bin"
& build/vulkan-scope-test/Release/conditional-repro.exe
```

L'historique contient les 65 536 derniers événements dans environ 8 Mio de mémoire
partagée. Il ne soumet aucune commande supplémentaire au GPU, n'interroge pas
ses compteurs et n'écrit pas de journal à chaque frame. Les enregistrements
contiennent le QPC, le thread, les objets, sémaphores, fences, valeurs timeline,
résultats Vulkan, attentes de mutex de queue, acquisitions et présentations.
Les créations/destructions de renderer et de swapchain repèrent les relances.
Le lanceur conserve le mapping même pendant le rechargement du module vidéo.

Après les 20 premières secondes, une fenêtre qui ne répond plus pendant dix
secondes déclenche deux instantanés de l'historique et deux minidumps, espacés
d'une seconde. Les fichiers sont dans `build/vulkan-diagnostic/session-*/hang-*`.
Une longue compilation de shaders peut aussi déclencher la capture ; si la
fenêtre répond de nouveau, le diagnostic se réarme et conserve les anciennes
captures dans des dossiers distincts. Un dernier historique est sauvegardé à la
sortie du processus. Aucun arrêt forcé de NXEmu.

La capture d'un minidump peut perturber brièvement le processus après détection.
Le tampon circulaire peut remplacer des événements anciens ; une écriture
concurrente au snapshot peut donner un enregistrement partiel, rejeté par le
décodeur. Les attentes sans fin correspondante sont des indices à interpréter
avec les piles et les deux captures, pas une preuve de la cause du blocage.
Ce diagnostic vise le gel de fenêtre observé ; un crash qui tue brutalement
le processus peut conserver l'historique via le lanceur sans fournir de minidump.

Décodage, avec Python 3 :

```powershell
python tools/vulkan-diagnostic/decode_trace.py <chemin-vers-vulkan-trace-1.bin>
```

Le décodeur produit un historique `.jsonl` et un `.summary.json` avec les dernières
opérations et les débuts d'attente/appel sans fin correspondante.
