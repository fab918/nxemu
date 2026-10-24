# Export automatique depuis le contexte capture — 24 octobre 2026

Base : feature-nxshad 30e24f49 (dernier commit recupere), modifications locales du workflow.

Le cache conserve deja une empreinte exacte du couple base/update : active-content.txt, journal par contexte et associations du vulkan.nxc. Cette empreinte ne donne pas directement le nom du fichier de l'update. L'ancien dialogue demandait donc une selection manuelle et ne reprenait pas le contexte automatiquement.

Le clic Export ouvre maintenant directement la boite d'enregistrement du fichier. Ensuite le dialogue affiche la progression et le bilan. Les boutons de selection d'update et l'ancien nxshader-update.txt ne sont plus utilises. La fenetre de progression reste necessaire pendant la recherche initiale des ressources.

L'exporteur identifie la source par la taille et le SHA-256 des en-tetes des NCA programme. Il utilise le contexte effectivement capture, pas le numero de version affiche dans la bibliotheque ni le fichier le plus recent. Une base sans update reste sans update meme si une update est disponible. En cas de fichier absent ou non identifiable, l'export echoue avec un diagnostic.

Le backend hors ligne lit des PFS0 et des HFS0 de la partition secure des cartes DXCI/XCI en clair. Les offsets exportes sont des offsets physiques du conteneur original, pas des adresses GPU. Il prend en charge la base et son update dans un seul fichier. Les NCA de patch peuvent porter le meme Title ID que la base : la table indirecte distingue alors les roles. Les ressources chiffrees ne sont pas prises en charge par ce backend.

Aucune collecte supplementaire pendant les draws et aucun changement du format NXSHBIN4/Zstd.

Validation :
- Compilation Windows de l'application et des outils C++ reussie.
- 14/14 tests CTest : dont selection exacte d'une ancienne update, base seule, update integree DXCI, NCA patch avec Title ID de base, annulation et entrees invalides.
- Export reel du cache Odyssey 1.3.0 : 1934 programmes, 2162 environnements, 2705 recettes graphiques et 26 compute. 284 observations non resolues, aucun programme manquant dans les recettes exportees.
- Manifeste : 216809 octets. Premier export : 31.6987 secondes, 4939 ressources examinees.
- Reconstruction hors ligne avec seulement le manifeste et le DXCI local : 2731 recettes, aucun manque/incompatibilite/skip, 2116 records locaux uniques. 2.018 secondes dans cette mesure. Ce controle ne cree pas de pipelines Vulkan natifs et ne remplace pas le test Mac.

Exécutable prepare : build/nxshader-native-workflow/nxshader-workflow-30e24f49/nxemu.exe. Copie autonome des donnees utilisateur de la derniere session ON, avec modules reconstruits; donnees originales conservees.

Profil produit : build/nxshader-native-workflow/odyssey-auto-export-30e24f49/0100000000010000.nxshader.

L'import automatique depuis le dossier shader et la notification dediee de premiere detection restent une etape d'interface distincte; cette modification porte sur l'export automatique et les conteneurs integres.

## Finalisation : provenance, progression et remplacement

Le chargeur fournit la NCA de l'update effectivement selectionnee via GetActiveProgramContent, y compris une ancienne version parmi plusieurs installees. ABI loader 0137 : reconstruire et distribuer ensemble application et modules. Les mods actifs restent non identifies. Les empreintes du contexte ne changent pas.

Au lancement, un petit fichier content-<context>.json conserve base_version, update_version, update_available, update_enabled et les empreintes des programmes. Aucun travail supplementaire par draw. L'export lit la provenance du contexte capture au lieu de reprendre la version affichee dans la bibliotheque. Les anciens caches sans cette information affichent une provenance textuelle indisponible; leurs empreintes restent utilisables.

Verification reelle dans une copie utilisateur : Odyssey avec update active -> base 1.0.0, update 1.3.0, contexte 100e98f4b88549ab1e11fdcba1d3caf6c1b7417f028877b5b45295d897ad8cfe. Update desactivee -> base 1.0.0, update_program vide, contexte 27366abd215fa41494bf8cdcebe4ae0871b5469c2d541d43744904dd073c0657. Configuration de la copie retablie avec update active ensuite.

Pendant une phase sans total connu : barre indeterminee animee, temps ecoule et message indiquant que l'operation peut prendre plusieurs minutes. Les ressources examinees et programmes localises sont des compteurs d'activite, pas une estimation du temps restant. La verification, l'assemblage, la compression et l'ecriture sont des phases distinctes.

Si le fichier choisi existe, la confirmation de remplacement precede le travail. NSSavePanel confirme sur macOS, la question NXEmu confirme sur Windows/Linux. Le nouvel export est produit dans un fichier frere, puis remplace l'ancien atomiquement apres succes. Annulation et echec laissent le precedent intact. Le backend CLI conserve sa protection contre les ecrasements implicites.

Validation finale : application Windows compilee; 14/14 tests C++ et 27/27 regressions de workflow natif. Test additionnel du remplacement reussi et de la preservation du precedent en cas d'entree temporaire absente. Verification UI du dialogue de confirmation a effectuer lors du prochain export utilisateur.
