# My_POC_All

Ce POC execute :

- une inference en clair `hard`
- une inference en clair `soft_global`
- une inference en clair `soft_adaptatif`
- une inference chiffree homomorphe du `soft_global`
- une inference chiffree homomorphe du `soft_adaptatif`

Pour chaque inference, le programme affiche :

- le nombre de bonnes predictions
- le total des echantillons testes
- le pourcentage de precision

Le pipeline travaille sur les `n` premiers samples du jeu de test.

## Commande d'execution

Depuis la racine du projet :

```powershell
.\run.ps1 -Dataset breast -SampleCount 10
```

Exemples :

```powershell
.\run.ps1 -Dataset iris -SampleCount 20
.\run.ps1 -Dataset cancer -SampleCount 15
.\run.ps1 -Dataset breast -SampleCount 10 -Rebuild
.\run.ps1 -Dataset spam -SampleCount 2
.\run.ps1 -Dataset spam2 -SampleCount 2
```

## Sortie attendue

En clair :

- `Hard (clair) : bonnes_predictions/total - pourcentage`
- `Soft global (clair) : bonnes_predictions/total - pourcentage`
- `Soft adaptatif (clair) : bonnes_predictions/total - pourcentage`

En chiffre :

- `HE Soft global : bonnes_predictions/total - pourcentage`
- `HE Soft adaptatif : bonnes_predictions/total - pourcentage`

## Fichiers de resultats

Apres chaque execution, le script enregistre automatiquement :

- un CSV cumulatif : `results/run_results_soft_adaptatif.csv`
- un log horodate : `results/logs/run_<dataset>_<timestamp>.log`
- un CSV detaille clair associe au log : `results/logs/run_<dataset>_<timestamp>_clear_predictions.csv`
- un CSV detaille CKKS associe au log : `results/logs/run_<dataset>_<timestamp>_he_predictions.csv`

Le CSV cumulatif contient notamment :

- les resultats `hard`, `soft_global`, `soft_adaptatif` en clair
- le resultat `soft_global` en HE
- le resultat `soft_adaptatif` en HE
- les chemins du log et des CSV detailles associes
- le chemin du fichier de log associe
