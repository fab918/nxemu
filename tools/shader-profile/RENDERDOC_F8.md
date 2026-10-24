# Capture GPU avec F8 (Windows)

La version normale ne charge pas RenderDoc. Le lanceur de diagnostic attache la
version portable officielle avant l'initialisation Vulkan et configure F8 via
l'API publique RenderDoc. Aucun changement du chemin de rendu n'est requis.

1. Telecharger le ZIP Windows x64 depuis https://renderdoc.org/builds et l'extraire.
2. Lancer `launch_renderdoc_f8.ps1` avec les arguments `-Executable`,
   `-RenderDocDirectory`, `-CaptureDirectory` et, facultativement, `-Game`.
3. Verifier l'overlay RenderDoc dans le jeu. Au moment du defaut, appuyer une
   seule fois sur F8, puis attendre l'enregistrement du fichier `.rdc`.

Les variables de couche Vulkan sont limitees au processus enfant; le lanceur
ne modifie pas le registre et retablit son environnement. Le fichier
`capture-ready.txt` confirme l'attachement de l'API et la configuration F8;
le log `Attached debugging tool: RenderDoc` confirme la couche Vulkan.

Un `.rdc` contient les commandes GPU, shaders et ressources necessaires a la
frame capturee. Il reste un diagnostic local, distinct du manifeste communautaire
sans instructions de shader. Une capture de la bibliotheque ou du chargement ne
remplace pas celle de la scene defectueuse. RenderDoc peut modifier les capacites
exposees et les timings, et donc masquer un defaut; une reproduction n'est pas
garantie. F8 ne doit pas etre utilise en meme temps que le moniteur de courses.

La frame peut etre inspectee et rejouee apres fermeture du jeu avec qrenderdoc.
Le dump doit contenir le defaut : il ne permet pas de reconstruire retrospectivement
une frame corrompue qui n'a pas ete capturee.

## Verification locale du lanceur

Version portable officielle testee : RenderDoc 1.46 Windows x64. En-tete API
provenant du tag v1.46, licence MIT conservee. Application Windows compilee avec
le dernier code de feature-nxshad. Le lancement de validation confirme dans le
log Vulkan `Attached debugging tool: RenderDoc` et l'overlay affiche `F8 to capture`.
Le lanceur traite correctement l'identifiant de controle non nul que
renderdoccmd renvoie lors d'un lancement reussi.

Le workflow NXShader integre a cette version passe 14/14 tests C++ et 37/37
regressions natives apres integration des commits distants cd59e184 et 0020644b.

Copie locale preparee : build/nxshader-renderdoc-f8. Lanceur utilisateur :
Lancer-Odyssey-F8.cmd. Dossier des captures utilisateur : captures/.
La session de validation deja ouverte utilise captures-validation/.
La sauvegarde d'Odyssey est conservee, l'update 1.3.0 activee, et le cache de
shaders initial de cette copie est mis a part dans odyssey-cache-before-diagnostic/.
Le cache original et les sauvegardes originales ne sont pas modifies.
