# ESP32 PoC Parking intelligent

Détection de véhicules (capteur + bouton + deep sleep) et gestion de barrière automatique (servo + LCD) via MQTT (Adafruit IO)

Notice : On a pas pu pusher le MQTT_KEY et AIO_KEY (github protection)

## 1. Description du projet

Ce projet met en œuvre un système de parking intelligent basé sur deux microcontrôleurs ESP32 :

| Module | Fonction |
|--------|----------|
| ESP32 Détection | Détecte la présence de véhicules grâce à un capteur analogique + bouton de réveil. Publie les informations sur Adafruit IO (MQTT) et passe automatiquement en deep sleep pour économiser l'énergie. |
| ESP32 Barrière | Reçoit l'état du parking, contrôle un servomoteur (barrière), affiche les informations sur un écran LCD I2C, gère un compteur d'ouvertures et peut être commandé manuellement via MQTT. |

## 2. Structure du projet (Poc-Parking-Intelligent/)

```
Poc-Parking-Intelligent/
├─ platformio.ini
├─ src/                     # Détection
│  └─ detection.cpp
└─ Barriere/
   └─ src/                 # Barrière
      └─ barriere.cpp
```

## 3. Matériel requis

### 3.1. ESP32 Détection

| Composant | Pin |
|-----------|-----|
| Capteur analogique | GPIO 36 |
| Bouton réveil (wake) | GPIO 33 (EXT0, niveau bas) |
| LED verte | GPIO 25 |
| LED rouge | GPIO 26 |

### 3.2. ESP32 Barrière

| Composant | Pin |
|-----------|-----|
| Servomoteur (signal) | GPIO 16 |
| LCD I2C SDA | GPIO 21 |
| LCD I2C SCL | GPIO 22 |
| LCD Adresse | 0x27 (par défaut) |

## 4. Feeds Adafruit IO utilisés

### Publiés par ESP32 Détection

| Feed | Valeur | Description |
|------|--------|-------------|
| parking-count | 0–2 | Nombre de voitures dans le parking |
| parking-status | 0 = libre / 1 = complet | Parking complet (2 voitures) |
| parking-place1 | 0/1 | État place n°1 |
| parking-place2 | 0/1 | État place n°2 |

### Publiés / Souscrits par ESP32 Barrière

| Type | Feed | Rôle |
|------|------|------|
| Subscribe | parking.barrier-control | UP/ON/1 = lever, DOWN/OFF/0 = baisser |
| Subscribe | parking-status | Parking libre ou complet |
| Subscribe | parking-count | Nombre de voitures |
| Subscribe | parking.cycle-count | Lecture du compteur d'ouvertures |
| Publish | parking.barrier-state | Texte : "Barriere levee", "Barriere baissee" |
| Publish | parking.cycle-count | Compteur d'ouvertures (incrémenté à chaque levée) |

## 5. Configuration WiFi + MQTT

### ESP32 Détection (src/main.cpp)

```cpp
#define WIFI_SSID "OpenLab"
#define WIFI_PASS "MBlVst6WmEa&NjS4TNbmE3*"
#define AIO_SERVER "io.adafruit.com"
#define AIO_SERVERPORT 1883
#define AIO_USERNAME "Ihab_"
```

### ESP32 Barrière (Barriere/src/main.cpp)

```cpp
#define WLAN_SSID "OpenLab"
#define WLAN_PASS "MBlVst6WmEa&NjS4TNbmE3*"
#define MQTT_SERVER "io.adafruit.com"
#define MQTT_SERVERPORT 1883
#define MQTT_USERNAME "Ihab_"
```

## 6. Téléchargement et installation (PlatformIO)

```bash
git clone https://github.com/ayoub139/Poc-Parking-Intelligent.git
cd Poc-Parking-Intelligent
```

### Flasher l'ESP32 Détection

```bash
pio run --target upload
pio device monitor
```

### Flasher l'ESP32 Barrière

(ouvrir PlatformIO > sélectionner Barriere/src/ comme dossier source, ou créer un environnement dédié dans platformio.ini)

```bash
pio run --target upload
pio device monitor
```

## 7. Fonctionnement de la détection (src/main.cpp)

1. Calibration automatique du capteur sur 5 secondes au premier démarrage.
2. Réveil uniquement par bouton (GPIO 33 via esp_sleep_enable_ext0_wakeup).
3. Lecture du capteur, comparaison à deux seuils (THRESHOLD_1 et THRESHOLD_2) :
   - place 1
   - place 2
4. Mise à jour du compteur (carsDetected, max 2 voitures).
5. Publication MQTT des feeds :
   - parking-place1
   - parking-place2
   - parking-count
   - parking-status (1 = complet si 2 voitures)
6. Reconnexion WiFi/MQTT si nécessaire, puis retour en deep sleep.

## 8. Fonctionnement de la barrière (Barriere/src/main.cpp)

1. En démarrage, synchronise l'état initial des feeds :
   - parking-count
   - parking-status
   - parking.cycle-count

2. Affichage LCD :
   - Ligne 0 : "PARKING LIBRE" ou "PARKING COMPLET"
   - Ligne 1 : "Places : X/2 (YY%)"
   - Ligne 2 : "Auto/MQTT: …"
   - Ligne 3 : "Barriere: LEVEE/BAISSE"

3. Mouvement barrière (servo) :
   - SERVO_UP = 90, SERVO_DOWN = 0

4. Commande manuelle via parking.barrier-control :
   - UP, ON, 1 → lever barrière + incrémenter parking.cycle-count
   - DOWN, OFF, 0 → baisser barrière

5. Automatique :
   - Si parking-status = 1 (complet), barrière forcée en position baissée.

## 9. Création d'un Dashboard Adafruit IO

1. Se connecter à https://io.adafruit.com
2. Menu → Dashboards → New Dashboard → Nommer Parking-System
3. Ajouter des "Blocks" pour visualiser/contrôler le parking :

| Block | Feed associé | Utilité |
|-------|-------------|---------|
| Gauge ou Text | parking-count | Affiche nombre de voitures |
| Toggle Button | parking.barrier-control | Contrôle manuel de la barrière |
| Text | parking.barrier-state | État actuel de la barrière |
| Indicator / Toggle | parking-status | Parking libre/complet |
| Toggle / Indicator | parking-place1 et parking-place2 | Occupation places |

## 10. Configuration d'un Email Trigger (parking complet)

### 10.1. Étapes

1. Aller dans Actions → Create a New Action
2. Trigger Type : Feed
3. Configurer :
   - Feed : parking-place2
   - Condition : gets data matching
   - Valeur : 1 (place 2 occupée → parking complet)
4. Action → Email

Exemple :
- **Subject** : Alerte : Parking complet
- **Body** :
  ```
  Parking complet (2/2 places)
  La barrière est automatiquement fermée.
  ```

## 11. Dépannage

| Problème | Cause possible | Solution |
|----------|---------------|----------|
| MQTT non connecté | AIO_KEY manquant ou incorrect | Vérifier #define AIO_KEY / MQTT_KEY |
| LCD n'affiche rien | Mauvaise adresse I2C | Tester 0x27 / 0x3F |
| Servo ne bouge pas | Alimentation insuffisante | Utiliser 5V séparé + GND commun |
| Pas de données sur dashboard | Feeds non créés ou mal nommés | Vérifier noms exacts listés dans ce README |
| ESP32 Détection ne se réveille pas | Mauvais câblage bouton ou mauvaise config esp_sleep_enable_ext0_wakeup | Vérifier GPIO 33 tiré à GND pour réveil |

## 12. Licence

Projet à but éducatif. Libre de réutilisation avec mention de l'auteur.