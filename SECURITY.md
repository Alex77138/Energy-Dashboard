# Politique de sécurité

## Versions supportées

| Version | Support sécurité |
|---|---|
| V1.x (dernière) | ✅ Supportée |
| < V1.0 | ❌ Non supportée |

## Signaler une vulnérabilité

Si vous découvrez une vulnérabilité de sécurité, **ne pas ouvrir une issue publique**.

Signalez-la via le système
[GitHub Private Vulnerability Reporting](../../security/advisories/new)
(onglet **Security** → **Report a vulnerability**).

Vous recevrez une réponse dans les 7 jours. Si la vulnérabilité est confirmée,
un correctif sera publié dans la prochaine version.

## Contexte de sécurité

Dash Energy est conçu pour un usage sur **réseau local uniquement** :

- Pas d'authentification sur l'interface web (ne pas exposer sur Internet)
- Les mises à jour OTA ne sont pas signées (réseau de confiance uniquement)
- Les credentials Wi-Fi sont stockés en NVS non chiffré sur l'ESP32
