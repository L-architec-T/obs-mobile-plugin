# OBS Mobile — plugin pour OBS Studio

Transforme ton téléphone Android en caméra et en micro pour OBS, avec l'app **OBS Mobile**.

- **Connexion directe** : OBS s'annonce sur le Wi-Fi, le téléphone le trouve seul. Pas de serveur
  WebSocket à activer, pas de mot de passe ni de QR code : tu acceptes le téléphone dans OBS, une
  seule fois.
- **Caméra à faible latence** : l'image du téléphone arrive dans une vraie source OBS, placée dans
  la scène « OBS Mobile · Caméras » (plein écran, côte à côte ou en vignette).
- **Retour vidéo** : le téléphone affiche ce qui est à l'antenne, en direct.
- **Panneau « Téléphones »** (menu *Docks*) : demandes de connexion, téléphones connectés et
  mémorisés, « Tout déconnecter ».

Le plugin ne touche jamais aux réglages de sortie d'OBS (stream, enregistrement) ni aux sources
qu'il n'a pas créées.

## Installation (Windows)

1. Télécharge `obs-mobile-…-windows-x64-installer.exe` dans la dernière version des
   [Releases](https://github.com/L-architec-T/obs-mobile-plugin/releases).
2. Lance-le et suis les étapes. Il te demande de fermer OBS s'il est ouvert, installe le plugin et
   ouvre ses ports dans le pare-feu Windows (réseaux privés seulement).
3. Rouvre OBS.
4. Sur le téléphone, ouvre OBS Mobile : ton PC apparaît avec le badge « PLUGIN ✓ ».

Pour le retirer : *Paramètres → Applications → OBS Mobile (plugin OBS) → Désinstaller*.

Sans installateur : décompresse `obs-mobile-…-windows-x64.zip` dans
`C:\ProgramData\obs-studio\plugins` (OBS fermé), pour obtenir
`C:\ProgramData\obs-studio\plugins\obs-mobile\bin\64bit\obs-mobile.dll`.

OBS Studio 31 ou plus récent, 64 bits.

## Réseau

| Port | Protocole | Rôle |
|------|-----------|------|
| 4460 | TCP | canal de contrôle (appairage, état) |
| 4461 | TCP | retour vidéo vers les téléphones |
| 9711 et suivants | UDP (SRT) | caméra et micro de chaque téléphone |

## Compiler

Basé sur [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate) : voir son wiki.
Sous Linux : `libobs-dev`, `qt6-base-dev`, `libavahi-client-dev`, FFmpeg avec SRT, puis
`cmake --preset ubuntu-x86_64 && cmake --build build_x86_64`.

## Licence

GPL v2 ou ultérieure, comme OBS Studio : voir [LICENSE](LICENSE).
