# Proposition d'enchainement musical par analyse audio

Ce document explique, en termes simples, ce que fait le script `propose_song_sequence.py`.

## A quoi sert ce script ?

Ce script aide a construire une playlist cohérente a partir des morceaux présents dans `works/audio/`.

Son but n'est pas de dire quel morceau est "meilleur" qu'un autre. Il cherche plutot a répondre a cette question :

> "Dans quel ordre ces morceaux peuvent-ils s'enchainer de façon fluide et crédible a l'écoute ?"

L'idée est de produire une proposition d'ordre qui limite les cassures trop brutales entre deux titres voisins.

## Ce que le script analyse

Pour chaque morceau, le script écoute et résume plusieurs aspects musicaux :

- la couleur globale du son
- l'énergie
- la brillance / densité du spectre
- certaines caractéristiques rythmiques
- certaines caractéristiques harmoniques
- le début du morceau
- la fin du morceau

Autrement dit, il ne regarde pas seulement le morceau "en moyenne" : il tient aussi compte de l'intro et de l'outro, car ce sont souvent elles qui comptent le plus pour l'enchainement.

## Ce qu'est un "embedding" ici

Le mot *embedding* désigne simplement un portrait numérique du morceau.

Le script transforme chaque titre en une liste de nombres qui résume son identité sonore :

- timbre
- texture
- énergie
- rythme
- harmonie
- comportement de l'intro
- comportement de l'outro

Deux morceaux qui ont des embeddings proches sont considérés comme musicalement voisins.

## Comment il propose un ordre

Une fois tous les morceaux transformés en embeddings, le script :

1. compare les morceaux entre eux
2. place les morceaux sur un axe unique
3. trie cet axe pour que des morceaux voisins restent proches

Cet axe peut être vu comme une ligne continue qui va, en gros, d'une zone plus calme / plus douce vers une zone plus intense / plus énergique.

Le résultat n'est pas "la vérité". C'est une proposition de parcours.

## Ce que veut dire "harmonieux" dans ce contexte

Ici, "harmonieux" ne veut pas dire :

- meme tonalité exacte
- meme BPM exact
- meme style exact

Cela veut plutot dire :

- transition plus naturelle entre morceaux voisins
- continuité de texture et d'énergie
- sensation de progression plus fluide

Le script essaie donc d'éviter qu'un morceau très éloigné dans son caractère sonore arrive juste après un morceau qui n'a rien a voir.

## Si je veux choisir le premier morceau

C'est possible.

Tu peux imposer le tout premier titre avec l'option :

```powershell
python src/bin/tools/propose_song_sequence.py --overwrite --first-track cosmowave_chaosnet_willbe
```

Dans ce cas, le script conserve sa logique de voisinage musical, mais force l'ouverture de la playlist avec le morceau demandé.

## Fichiers produits

Le script écrit ses résultats dans `works/audio/analysis/`.

Les fichiers principaux sont :

- `audio_embeddings.json`
  contient le portrait numérique détaillé de chaque morceau
- `embedding_axis.csv`
  contient la projection sur l'axe unique, donc la base du tri
- `playlist_proposal.lua`
  contient une proposition d'ordre directement réutilisable dans le manifeste Lua
- `playlist_report.txt`
  contient un résumé lisible de la méthode et de l'ordre proposé

## Exemple de commande

Analyse standard :

```powershell
python src/bin/tools/propose_song_sequence.py --overwrite
```

Analyse avec un premier morceau imposé :

```powershell
python src/bin/tools/propose_song_sequence.py --overwrite --first-track cosmowave_chaosnet_willbe
```

## Comment utiliser le résultat en pratique

La meilleure façon de s'en servir est la suivante :

1. lancer le script
2. regarder `playlist_proposal.lua`
3. comparer cet ordre avec tes spectrogrammes
4. écouter surtout les transitions entre voisins
5. garder, corriger ou casser certaines liaisons selon ton intention artistique

Le script est donc un assistant de composition / séquençage, pas un décideur final.

## Limites

Il faut garder en tête que le script ne comprend pas :

- le récit que tu veux construire
- la signification émotionnelle d'un titre dans l'album
- la place d'un climax voulu
- une contrainte narrative ou visuelle externe

Il est bon pour suggérer une continuité sonore.
Il ne remplace pas une intention artistique.

## En résumé

Ce script sert a :

- analyser les morceaux audio
- générer un embedding pour chacun
- placer les morceaux sur un axe unique
- proposer un ordre de playlist plus fluide
- éventuellement forcer le premier morceau

Le plus utile est de le voir comme un outil de pré-ordonnancement :
il donne une base rationnelle, que le musicien peut ensuite affiner a l'oreille.
