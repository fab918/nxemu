# NXEmu — audit ciblé des bornes du cache shader

23 octobre 2026. Audit des sources locales et proposition isolée, sans modifier
`src/yuzu_video_core/shader_environment.cpp` dans le checkout principal.

## Défaut confirmé

`TryFindSize()` retourne le décalage de la branche terminale, pas la taille
totale. `Analyze()` conserve ce décalage comme borne haute incluse.
`CachedSizeBytes()` ajoute donc correctement huit octets. Mais `SetCachedSize()`
et `SetCachedCode()` reçoivent ensuite cette taille totale et l'emploient comme
un décalage de dernière instruction : le calcul suivant ajoute huit octets de
trop. `ShareCachedCode()` passe par `SetCachedSize()`.

Le code partagé, la lecture mémoire et la sérialisation sont concernés. Le POC
a observé quinze blobs dont le mot supplémentaire empêchait la correspondance
avec les ressources ; deux ne différaient que par ce mot. Cela prouve une
distinction artificielle entre contenus sérialisés, pas une nouvelle clé native.

## Effets à distinguer

| Élément | Constat dans ce chemin |
|---|---|
| Snapshot partagé et code sérialisé | Un mot de trop |
| Identité SHA-256 des blobs du POC | Peut dépendre du mot supplémentaire |
| `ShaderInfo::unique_hash`, chemin `Analyze()` | Calculé avant le partage ; n'inclut pas ce mot |
| `ShaderInfo::size_bytes` | Taille correcte, terminal inclus une fois |
| `Register()` et comptage des pages surveillées | Reçoivent cette taille correcte ; élargissement non établi |
| Dédoublonnage natif / recompilations en jeu | Aucun gain démontré par cette correction seule |

Le hash rapide existant exclut même la branche terminale de sa plage. Ce choix
historique est conservé pour éviter une modification des identités de pipeline.
Le chemin lent de calcul de taille/hash n'est pas refondu par cette proposition.

## Correctif préparé

`build/shader-cache-bounds-fix/cache-bounds-fix.patch` propose :

1. Conserver la convention inclusive existante et calculer la borne haute comme
   `start_address + size_bytes - sizeof(u64)` dans les deux setters.
2. Accepter cette borne dans `ReadInstruction()` avec `address <= cached_highest`,
   y compris sans snapshot gelé. Sinon la branche terminale devient une lecture
   hors cache après réduction de la plage, et interdit la sérialisation.
3. Vérifier en assertion que la taille contient au moins une instruction et est
   alignée sur huit octets. Aucun changement d'algorithme de hash ni de layout.

Les anciens enregistrements gardent leur structure et leur propre borne ; une
migration de format ne découle pas de ce changement. La compatibilité de replay
avec un exécutable intégrant le correctif reste toutefois à valider en entier.
Il ne faut pas tronquer automatiquement tout cache existant : seules les plages
prouvées par le POC ont été normalisées.

## Régression C++

`prepare_cache_bounds_fix.py` extrait les corps des méthodes concernées depuis
la source réelle et les compile avec une mémoire GPU simulée. Il génère une
copie corrigée et le patch sous `build/`, sans appliquer le patch au renderer.

Les huit cas couvrent départ nul/non nul et tailles 8, 104, 4 096, 4 104 octets :
analyse, taille partagée, volume de lecture exact, dernier mot terminal, variant
partagé, chemin compute, rejet après borne en mode gelé et marquage des lectures
hors plage en mode vivant. La plage historique du hash est également contrôlée.

Trois exécutables servent au contrôle : l'original doit échouer, le correctif
complet doit réussir, et le correctif de taille sans correction de lecture doit
échouer. Ce sont des tests des corps de méthodes, pas un build de l'émulateur
complet. CityHash est remplacé par une sonde de taille ; l'algorithme lui-même,
le serializer complet, l'invalidation et les créations Vulkan ne sont pas
exécutés par ce banc.

```powershell
python tools/shader-profile/prepare_cache_bounds_fix.py
```

La suite nécessaire avant intégration est une recompilation isolée de NXEmu,
une capture fraîche, le replay des anciens caches puis la comparaison des clés,
IR/SPIR-V et créations natives sur le corpus. Les 555 validations précédentes
portent sur l'adaptateur de manifeste et ne constituent pas cette validation
d'intégration du nouveau patch.
