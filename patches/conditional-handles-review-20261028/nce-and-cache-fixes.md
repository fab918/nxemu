**Correctifs après la première session END​ER MAGNOLIA**

Le cas NCE a été reproduit sur les vrais mappings Mach, avant correction. Il ne
s'agissait pas uniquement d'un message trop sévère : les opérations modifiaient
les pages invitées et la table de pointeurs, puis s'arrêtaient au premier trou
entre les réservations. Une fenêtre suivante pouvait conserver des permissions
hôtes obsolètes. Le test avant correction échoue sur les mappings et permissions
réels, et pas seulement sur le texte des logs.

Chaque opération Map/Unmap/Protect/SetAccessMask traite désormais séparément les
intersections avec les fenêtres réservées. Le décalage relatif du backing reste
correct après clipping. Les trous ne sont plus parcourus page par page. Les
plages qui débordent U64 sont rejetées avant toute mutation, avec une vraie erreur
conservée. Les protections hôtes et la table canonique sont mises à jour pour
chaque intersection.

Dans une reproduction avec un petit mapping à chaque bord du trou, Protect
passait environ 197 ms à parcourir le trou et laissait le second mapping hôte
incorrect ; l'opération corrigée traite les deux mappings en environ 1,2–1,3 µs.
Cette mesure décrit ce cas synthétique et ne constitue pas un gain de FPS.

Cette branche indépendante part de upstream/master et exclut la reconstruction
des handles testée séparément. Le cache Vulkan passe de version 20 à 22 pour le
nouveau layout des push constants décrit dans scaling-layout-audit.md. Le cache
OpenGL reste inchangé : la correction de viewport ne modifie pas ses shaders.

La validation ciblée porte sur la disposition SPIR-V, les viewports/scissors,
le modèle NCE de 4 000 pages aléatoires, les mappings natifs avec 1 000 cycles NRO
et la régression NCE des plages tronquées.

Les logs nomment désormais les opcodes SPIR-V qui échouent. Le traçage du premier
accès GPU non mappé est activable sur macOS avec
`NXEMU_TRACE_UNMAPPED_DEVICE_READ=1`. Ces diagnostics ne remplacent aucun opcode
manquant par une valeur factice.

Les portraits Mario Party restent cassés en ×2 dans la dernière capture.
GlobalAtomicIAdd32 et le registre spécial 15 restent non implémentés ; leur lien
avec ce défaut visuel n'est pas établi.

Autre observation dans le journal de la session Mario Party : environ 19 000
avertissements identiques de storage buffer nul (adresse et taille zéro),
ainsi que les limites annoncées par MoltenVK (geometryShader, shaderCullDistance
et sampler_filter_minmax). Le spam peut avoir un coût de journalisation ; ni
sa cause ni son lien avec les portraits ne sont établis. Aucun changement de
sémantique ni suppression de ces avertissements n'est inclus dans cette branche.

Mise à jour après récupération du correctif Windows 4a44687f : le seuil de mise à
l'échelle des petites profondeurs suit celui des couleurs. Le nouveau test
ImageInfo passe aussi sur macOS. Le suivi de pointeurs invariants dans une boucle
est corrigé séparément ; voir tests/unit/storage_pointer_tracking/README.md pour
la reproduction, les limites du rejeu et l'erreur de registre 15 restante. Les
caches OpenGL/Vulkan passent à 13/23 car la liste des storage descriptors change.
