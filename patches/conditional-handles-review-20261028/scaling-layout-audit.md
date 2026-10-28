**Audit complémentaire ×1 / ×2 / ×3**

La correction des viewports Vulkan n'a pas rétabli les portraits sur la capture suivante. Elle corrige une incohérence réelle du chemin pixel-space, mais n'est pas une preuve que ce chemin explique la scène de Mario Party Superstars.

Deux autres corrections sont apportées :

- Le viewport OpenGL pixel-space applique maintenant le même facteur que son chemin normal. Ses scissors appliquaient déjà le facteur.
- Les dimensions logiques de rendu partageaient l'offset zéro des push constants Vulkan avec les masques de résolution. Le chemin normal écrasait les premiers masques par memcpy, et le chemin à table de textures faisait deux écritures au même emplacement. Les dimensions occupent désormais la plage 32–47, après les données de résolution. Quand un même shader utilise les deux informations, il déclare un seul bloc PushConstant SPIR-V. Le paquet du chemin normal reste envoyé en une seule commande.

Les dimensions de normalisation des positions restent logiques : les multiplier aussi appliquerait le facteur deux fois. Les chemins de taille des cibles, blits et invalidation des états lors du changement de facteur examinés appliquent déjà leur mise à l'échelle.

Les tests vérifient les offsets réellement déclarés dans le SPIR-V, le nombre de blocs par shader, le contenu du paquet transmis (masques, facteur inverse, lectures d'attachments, dimensions), en ×1/×2/×3 et avec/sans masque de résolution. Les assertions du test existant sont aussi actives en Release. La validation de cette branche couvre cinq tests ciblés et la compilation de video_core, y compris OpenGL et Vulkan.

Le cache Vulkan passe de 20 à 22 parce que les offsets dans les shaders changent. Une reconstruction initiale du cache est donc attendue. Le shader GlobalAtomicIAdd32 non implémenté reste un problème distinct, désormais nommé explicitement dans les logs ; son erreur n'est pas masquée.

Le rendu des portraits doit encore être vérifié dans le jeu. Ces tests valident les incohérences de layout et d'échelle, pas la disparition du défaut visuel.
