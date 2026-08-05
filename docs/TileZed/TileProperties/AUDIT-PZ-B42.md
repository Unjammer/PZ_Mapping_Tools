# Audit des TileDefs Project Zomboid (sources B42)

## Portée

Cet audit compare la définition d'interface de TileZed (`TileProperties.txt`)
avec une copie locale externe des sources Java et Lua de B42. Les sources Java étant
décompilées, le registre du jeu prouve l'existence d'une clé, mais ne suffit pas
toujours à documenter son contrat.

Sources de référence :

- `core/properties/TilePropertyKey.java` : nouveau registre des clés ;
- `core/properties/IsoPropertyType.java` : propriétés historiques avec valeur ;
- `iso/SpriteDetails/IsoFlagType.java` : drapeaux historiques ;
- `core/properties/PropertyContainer.java` et `core/TilePropertyAliasMap.java` :
  stockage et résolution des propriétés ;
- usages directs dans les sources Java et Lua.

## Résultat après la première tranche d'enrichissement

| Source | Nombre | Rôle |
| --- | ---: | --- |
| Contrôles dans `TileProperties.txt` | 208 | Interface visible |
| Noms effectivement écrits par ces contrôles | 249 | Inclut les enums avec `ValueAsPropertyName` |
| `IsoPropertyType` | 228 | Ancien registre de propriétés |
| `IsoFlagType` | 111 | Ancien registre de drapeaux (hors `MAX`) |
| `TilePropertyKey` | 278 | Nouveau registre B42 |
| Clés `TilePropertyKey` encore absentes de l'interface | 37 | Comparaison exacte après résolution des enums UI |

Les 208 contrôles ont tous un `ToolTip` anglais. Les nouveaux champs indiquent
également leur clé moteur, leur format et les risques d'une saisie manuelle.

## Propriétés maintenant exposées

| Clé(s) | Contrôle | Justification |
| --- | --- | --- |
| `SpriteGridPos` | chaîne libre `x,y` ou `x,y,z` | supprime l'ancienne liste limitée à 8x6 et permet les grands Jumbos |
| `SpriteGridLevel` | entier optionnel sous forme de chaîne | préserve la différence entre absence et niveau `0` explicite |
| `Noffset`, `Woffset`, `Soffset`, `Eoffset` | entiers signés | utilisés par le script des moveables pour retrouver les quatre orientations |
| `LinkedOffset`, `LinkedLocIs` | entier signé et chaîne | lus dans les métadonnées des moveables ; marqués « Advanced B42 » |
| `EntityScript`, `EntityScriptName` | booléen et chaîne | branche le nouveau pipeline d'entités ; normalement généré par `SpriteConfigManager` |
| `halfheight` | booléen | modifie les tests de murs, de vision et le placement vertical |
| `unlit` | booléen | rend le sprite à pleine luminosité sans créer de source lumineuse |
| `FasciaEdgeReversible` | booléen | autorise le repli d'attachement des fascias nord/ouest |
| `UnbreakableWindowN/W/NW` | enum directionnel exclusif | construit les variantes de fenêtres incassables sans combinaisons contradictoires |

`EntityScript` et `EntityScriptName` restent explicitement avancés : une clé de
script inconnue provoque une erreur de création d'entité dans
`GameEntityFactory`.

## Propriétés du fichier de géométrie, pas des `.tiles`

Les clés suivantes existent dans `TilePropertyKey`, mais les usages trouvés les
lisent dans `TileGeometryFile` et l'éditeur Lua `TileGeometryEditor`. Les ajouter
à `TileProperties.txt` donnerait un contrôle visuellement valide mais sans effet
garanti sur la géométrie de profondeur :

- `CurtainOffset` : trois composantes `x y z` ;
- `OpaquePixelsOnly` : limite la profondeur aux pixels opaques ;
- `Translucent` : active le traitement translucide de la géométrie ;
- `UseObjectDepthTexture` : utilise la texture de profondeur de l'objet.

Ces propriétés devront être intégrées dans une future interface de géométrie de
tiles, ou exportées dans son fichier dédié.

## Clés à documenter avant exposition

- `DoorWallNW`, `DoorWallNWTrans` ;
- `windowFN`, `windowFW` ;
- `attachtostairs`, `blocksight`, `blueprint`, `canPathN`, `canPathW` ;
- `connectX`, `connectY` ;
- `hidewalls`, `interior`, `isMoveAbleObject`, `tableN`.

`FloorHeightOneThird` et `FloorHeightTwoThirds` ne doivent pas être ajoutées en
double : elles sont déjà dérivées par le jeu depuis le contrôle structuré
`FloorHeight=OneThird/TwoThirds`.

## États internes ou générés à ne pas exposer par défaut

- `burning`, `burntOut`, `forcedLocked`, `open`, `smoke` ;
- `HasRaindrop`, `HasRainSplashes` ;
- `name`, `noStart` ;
- `jukebox`, `radio` (héritage ou spécialisation à confirmer) ;
- `unflammable` ;
- `transparentN`, `transparentW` quand ils sont produits par les contrôles de
  murs et fenêtres.

## Incohérences de casse

Le nouveau `ResourceLocation` normalise les identifiants en minuscules, mais
l'ancien `TilePropertyAliasMap` conserve une table de chaînes. Deux divergences
restent visibles :

- `AttachedFloor` dans `TilePropertyKey`, contre `attachedFloor` dans l'ancien
  enum et dans l'éditeur ;
- `firerequirement` dans `TilePropertyKey`, contre `FireRequirement` dans
  l'éditeur et `fireRequirement` dans l'ancien enum.

Ces chaînes ne doivent pas être modifiées sans test de compatibilité avec les
anciens `.tiles` et le chargeur de la version du jeu ciblée.

## Drapeaux historiques hors du nouveau registre

Ces 17 noms existent encore dans `IsoFlagType` sans entrée équivalente dans
`TilePropertyKey` :

`floorE`, `floorS`, `ontable`, `openAir`, `pushable`, `sheetCurtains`, `shelfE`,
`shelfS`, `SpriteConfig`, `tableE`, `tableNE`, `tableNW`, `tableS`, `tableSE`,
`tableSW`, `tableW`, `unflamable`.

Une migration automatique basée uniquement sur `TilePropertyKey` ferait donc
perdre des drapeaux historiques encore compris par le moteur.

## Liste exhaustive des 37 clés encore absentes

`AttachedFloor`, `attachtostairs`, `blocksight`, `blueprint`, `burning`,
`burntOut`, `canPathN`, `canPathW`, `connectX`, `connectY`, `CurtainOffset`,
`DoorWallNW`, `DoorWallNWTrans`, `firerequirement`, `FloorHeightOneThird`,
`FloorHeightTwoThirds`, `forcedLocked`, `HasRaindrop`, `HasRainSplashes`,
`hidewalls`, `interior`, `isMoveAbleObject`, `jukebox`, `name`, `noStart`,
`OpaquePixelsOnly`, `open`, `radio`, `smoke`, `tableN`, `Translucent`,
`transparentN`, `transparentW`, `unflammable`, `UseObjectDepthTexture`,
`windowFN`, `windowFW`.

## Suite recommandée

1. Tester cette première tranche sur des `.tiles` B41 et B42 réels.
2. Recenser les valeurs effectivement rencontrées pour `LinkedLocIs`, les coins
   de portes et les types de fenêtres internes.
3. Ajouter une interface dédiée au fichier de géométrie pour les quatre clés de
   profondeur, au lieu de les mélanger aux TileDefs classiques.
4. Conserver les contrôles structurés comme source unique des drapeaux dérivés.
