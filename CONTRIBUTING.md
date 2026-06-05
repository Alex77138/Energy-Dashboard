# Contribuer à Dash Energy

Merci de votre intérêt pour le projet !

## Signaler un bug

Utilisez le [formulaire de bug report](../../issues/new?template=bug_report.md) en précisant :
- La version du firmware (`V1.x`)
- Le matériel utilisé (carte, appareils connectés)
- Les étapes pour reproduire le problème
- Le message d'erreur du moniteur série si disponible

## Proposer une fonctionnalité

Ouvrez une [issue feature request](../../issues/new?template=feature_request.md) avec une description claire de la fonctionnalité et du cas d'usage.

## Soumettre une Pull Request

1. Forkez le dépôt
2. Créez une branche : `git checkout -b feature/ma-fonctionnalite`
3. Respectez le style du code existant (C++, PlatformIO/Arduino)
4. Testez sur le matériel cible (JC8048W550 ou compatible)
5. Ajoutez une entrée dans `CHANGELOG.md`
6. Soumettez la PR en décrivant les changements et leur motivation

## Règles

- Licence : tout code soumis est placé sous **GPL v3** (même licence que le projet)
- Pas de credentials, IPs réelles ou tokens dans le code
- `src/config.h` est ignoré par git — ne pas le committer
