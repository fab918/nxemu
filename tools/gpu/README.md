# Dump GPU Vulkan (Windows)

Le lanceur utilise GFXReconstruct et fonctionne avec le `nxemu.exe` compile depuis
la branche courante. La capture est active uniquement dans le processus lance.

1. Placer `VkLayer_gfxreconstruct.dll` et son manifeste
   `VkLayer_gfxreconstruct.json` dans `gpu-capture-layer`, a cote de `nxemu.exe`.
   Utiliser une version de GFXReconstruct avec `capture_trigger_frames`.
2. Fermer NXEmu, puis lancer `Lancer-Dump-GPU.cmd` dans le dossier du build
   (ou `tools/gpu/Lancer-Dump-GPU.cmd` depuis le depot).
3. Lancer le jeu avec Vulkan et rejoindre la scene en 2C qui pose probleme.
4. Appuyer sur **F8** : deux images sont capturees, puis la capture s'arrete
   automatiquement. La sauvegarde initiale de l'etat GPU peut figer le jeu.
5. Recuperer le dossier indique dans la console, sous `user/dump/gpu/` :
   fichier `.gfxr`, journal de la couche, configuration au lancement et
   `launch.json` avec les empreintes de l'executable, des modules et de la couche.

Pour un defaut intermittent, `Lancer-Dump-GPU.cmd -Frames 10` capture dix images.
`-Continuous` permet de commencer et terminer avec deux pressions sur F8 ; les
fichiers peuvent alors devenir tres volumineux. Chaque nouvelle pression apres
une capture terminee permet une autre capture.

Options PowerShell : `-TargetExe`, `-LayerDirectory`, `-OutputRoot`, `-GamePath`,
`-Frames` (1 a 120), `-Continuous`, `-Wait` et `-ValidateOnly`.
`-ValidateOnly` verifie les chemins et ecrit les metadonnees sans lancer NXEmu ;
il ne valide pas une capture en jeu.

Conserver avec le dump une capture d'ecran du defaut et les etapes pour atteindre
la scene. Pour comparer les resolutions, creer deux captures separees dans la
meme scene en indiquant le facteur de resolution utilise.
