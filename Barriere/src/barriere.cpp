#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <ESP32Servo.h>
#include <LiquidCrystal_I2C.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

// ========== CONFIGURATION WIFI & MQTT ==========
#define WLAN_SSID "OpenLab"
#define WLAN_PASS "MBlVst6WmEa&NjS4TNbmE3*"
#define MQTT_SERVER "io.adafruit.com"
#define MQTT_SERVERPORT 1883
#define MQTT_USERNAME "Ihab_"


WiFiClient client;
Adafruit_MQTT_Client mqtt(&client, MQTT_SERVER, MQTT_SERVERPORT, MQTT_USERNAME, MQTT_KEY);

// ========== MQTT FEEDS ==========
// Setup feeds for publishing (état de la barrière)
Adafruit_MQTT_Publish barrier_state_feed = Adafruit_MQTT_Publish(&mqtt, MQTT_USERNAME "/feeds/parking.barrier-state");
Adafruit_MQTT_Publish barrier_count_feed = Adafruit_MQTT_Publish(&mqtt, MQTT_USERNAME "/feeds/parking.cycle-count");

// Setup feeds for subscribing (bouton ON/OFF + statut parking)
Adafruit_MQTT_Subscribe barrier_control_feed = Adafruit_MQTT_Subscribe(&mqtt, MQTT_USERNAME "/feeds/parking.barrier-control");
Adafruit_MQTT_Subscribe parking_status_feed = Adafruit_MQTT_Subscribe(&mqtt, MQTT_USERNAME "/feeds/parking-status");
Adafruit_MQTT_Subscribe parking_count_feed = Adafruit_MQTT_Subscribe(&mqtt, MQTT_USERNAME "/feeds/parking-count");
Adafruit_MQTT_Subscribe barrier_count_sub = Adafruit_MQTT_Subscribe(&mqtt, MQTT_USERNAME "/feeds/parking.cycle-count");

// ========== PINS ==========
#define SERVO_PIN 16

// ========== CONSTANTES ==========
#define SERVO_UP 90      // Barrière levée
#define SERVO_DOWN 0     // Barrière baissée
#define MAX_PARKING_SPOTS 2  // Nombre total de places

// ========== OBJETS ==========
LiquidCrystal_I2C lcd(0x27, 20, 4);
Servo barriere;

// ========== VARIABLES GLOBALES ==========
bool barrierUp = false;
int openCount = 0;  // Compteur d'ouvertures
int currentCarCount = 0;  // Nombre actuel de voitures dans le parking

// ========== PROTOTYPES ==========
void MQTT_connect();
void updateCarCountDisplay(int count);
void moveBarrier(bool up);
void updateParkingStatus(bool isFull);

// ========== SETUP ==========
void setup() {
    Serial.begin(115200);
    delay(10);

    Serial.println(F("\n========================================"));
    Serial.println(F("   CONTROLE BARRIERE via MQTT          "));
    Serial.println(F("   Bouton ON/OFF Adafruit IO           "));
    Serial.println(F("========================================\n"));

    // ===== INITIALISATION LCD =====
    Serial.println(F("Initialisation LCD..."));
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("PARKING LIBRE   ");
    lcd.setCursor(0, 1);
    lcd.print("Places: 0/");
    lcd.print(MAX_PARKING_SPOTS);
    lcd.print(" (  0%)  ");
    lcd.setCursor(0, 2);
    lcd.print("Synchro MQTT... ");
    lcd.setCursor(0, 3);
    lcd.print("Barriere: BAISSE");
    Serial.println(F("LCD OK"));

    // ===== INITIALISATION SERVO =====
    Serial.println(F("Initialisation Servo..."));
    barriere.attach(SERVO_PIN);
    barriere.write(SERVO_DOWN);  // Position basse initiale
    barrierUp = false;
    delay(1000);
    Serial.println(F("Servo OK (position basse)"));

    // ===== CONNEXION WIFI =====
    Serial.print(F("Connecting to "));
    Serial.println(WLAN_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WLAN_SSID, WLAN_PASS);

    int wifi_retry = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        wifi_retry++;
        if (wifi_retry > 40) {
            Serial.println(F("\n[ERROR] WiFi timeout! Verifiez SSID/Password et que le reseau est en 2.4GHz"));
            Serial.print(F("SSID tente: "));
            Serial.println(WLAN_SSID);
            while(1) { delay(1000); }
        }
    }
    Serial.println();

    Serial.print("WiFi connected. IP address: ");
    Serial.println(WiFi.localIP());

    // ===== SOUSCRIPTION MQTT =====
    mqtt.subscribe(&barrier_control_feed);
    mqtt.subscribe(&parking_status_feed);
    mqtt.subscribe(&parking_count_feed);
    mqtt.subscribe(&barrier_count_sub);

    // ===== CONNEXION MQTT =====
    MQTT_connect();

    // ===== RÉCUPÉRATION DE L'ÉTAT INITIAL DU PARKING =====
    Serial.println(F("\n[INIT] Récupération état du parking..."));

    // Attendre les messages initiaux (dernières valeurs des feeds)
    unsigned long startWait = millis();
    bool receivedCount = false;
    bool receivedStatus = false;
    bool receivedBarrierCount = false;

    while (millis() - startWait < 5000 && (!receivedCount || !receivedStatus || !receivedBarrierCount)) {
        Adafruit_MQTT_Subscribe *subscription;
        while ((subscription = mqtt.readSubscription(100))) {
            if (subscription == &parking_count_feed) {
                String countStr = String((char*)parking_count_feed.lastread);
                currentCarCount = countStr.toInt();

                Serial.print(F("[INIT] Count reçu: "));
                Serial.println(currentCarCount);

                updateCarCountDisplay(currentCarCount);

                receivedCount = true;
            }
            else if (subscription == &parking_status_feed) {
                String status = String((char*)parking_status_feed.lastread);

                Serial.print(F("[INIT] Status reçu: "));
                Serial.println(status);

                updateParkingStatus(status == "1");

                receivedStatus = true;
            }
            else if (subscription == &barrier_count_sub) {
                String countStr = String((char*)barrier_count_sub.lastread);
                openCount = countStr.toInt();

                Serial.print(F("[INIT] Barrier count reçu: "));
                Serial.println(openCount);

                receivedBarrierCount = true;
            }
        }
    }

    if (!receivedCount || !receivedStatus || !receivedBarrierCount) {
        Serial.println(F("[WARN] Timeout - Utilisation valeurs par défaut"));
        if (!receivedBarrierCount) {
            Serial.println(F("[WARN] Barrier count non reçu - démarrage à 0"));
            openCount = 0;
        }
        // Forcer l'affichage des valeurs par défaut
        updateParkingStatus(false);
        updateCarCountDisplay(0);
        lcd.setCursor(0, 2);
        lcd.print("Auto: LIBRE     ");
    } else {
        Serial.println(F("[OK] État du parking synchronisé !"));
        lcd.setCursor(0, 2);
        lcd.print("Auto: LIBRE     ");
    }

    Serial.println(F("\n========================================"));
    Serial.println(F("  SYSTEME PRET"));
    Serial.println(F("========================================"));
    Serial.println(F("Commandes MQTT disponibles :"));
    Serial.println(F("  - ON/1/UP : Lever barriere"));
    Serial.println(F("  - OFF/0/DOWN : Baisser barriere"));
    Serial.println(F("========================================\n"));
}

// ========== LOOP ==========
void loop() {
    // Maintenir connexion MQTT
    MQTT_connect();

    // ===== TRAITEMENT COMMANDES MQTT =====
    Adafruit_MQTT_Subscribe *subscription;
    while ((subscription = mqtt.readSubscription(100))) {
        // === CONTROLE MANUEL DE LA BARRIERE ===
        if (subscription == &barrier_control_feed) {
            String command = String((char*)barrier_control_feed.lastread);
            command.trim();
            command.toUpperCase();

            Serial.print(F("\n[MQTT] Commande recue: "));
            Serial.println(command);

            if (command == "UP" || command == "1" || command == "ON") {
                Serial.println(F("[CMD] LEVER BARRIERE"));

                lcd.setCursor(0, 2);
                lcd.print("MQTT: ON        ");

                moveBarrier(true);

                openCount++;  // Incrémenter le compteur

                barrier_state_feed.publish("Barriere levee");
                barrier_count_feed.publish(openCount);

                Serial.print(F("[OK] Barriere levee - Ouvertures: "));
                Serial.println(openCount);
            }
            else if (command == "DOWN" || command == "0" || command == "OFF") {
                Serial.println(F("[CMD] BAISSER BARRIERE"));

                lcd.setCursor(0, 2);
                lcd.print("MQTT: OFF       ");

                moveBarrier(false);

                barrier_state_feed.publish("Barriere baissee");
                Serial.println(F("[OK] Barriere baissee"));
            }

            Serial.println();
        }
        // === NOMBRE DE VOITURES (MISE A JOUR COUNT) ===
        else if (subscription == &parking_count_feed) {
            String countStr = String((char*)parking_count_feed.lastread);
            countStr.trim();
            currentCarCount = countStr.toInt();

            Serial.print(F("\n[PARKING] Nombre de voitures: "));
            Serial.print(currentCarCount);
            Serial.print("/");
            Serial.println(MAX_PARKING_SPOTS);

            updateCarCountDisplay(currentCarCount);

            Serial.println();
        }
        // === STATUT DU PARKING (AUTOMATIQUE) ===
        else if (subscription == &parking_status_feed) {
            String status = String((char*)parking_status_feed.lastread);
            status.trim();

            Serial.print(F("\n[PARKING] Statut recu: "));
            Serial.println(status);

            if (status == "1") {
                // PARKING COMPLET - Baisser la barrière automatiquement
                Serial.println(F("[AUTO] PARKING COMPLET - BAISSE DE LA BARRIERE"));

                updateParkingStatus(true);
                lcd.setCursor(0, 2);
                lcd.print("Auto: BAISSE    ");

                // Baisser la barrière si elle était levée
                if (barrierUp) {
                    moveBarrier(false);
                    barrier_state_feed.publish("Barriere baissee (parking complet)");
                    Serial.println(F("[OK] Barriere baissee automatiquement"));
                }
            }
            else if (status == "0") {
                // PARKING LIBRE
                Serial.println(F("[AUTO] PARKING DISPONIBLE"));

                updateParkingStatus(false);
                lcd.setCursor(0, 2);
                lcd.print("Auto: LIBRE     ");
            }

            Serial.println();
        }
    }

    delay(100);
}

// ========== FONCTION CONNEXION MQTT ==========
// Function to connect and reconnect as necessary to the MQTT server.
// Should be called in the loop function and it will take care if connecting.
void MQTT_connect() {
    int8_t ret;

    // ===== VERIFICATION WIFI D'ABORD =====
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\n[WARN] WiFi disconnected! Reconnecting...");
        lcd.setCursor(0, 2);
        lcd.print("WiFi perdu...   ");

        WiFi.reconnect();
        int timeout = 0;
        while (WiFi.status() != WL_CONNECTED && timeout < 20) {
            delay(500);
            Serial.print(".");
            timeout++;
        }

        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("\n[ERROR] WiFi reconnection failed. Restarting ESP32...");
            lcd.setCursor(0, 2);
            lcd.print("Redemarrage...  ");
            delay(2000);
            ESP.restart();
        }

        Serial.println("\n[OK] WiFi reconnected!");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
    }

    // Stop if already connected.
    if (mqtt.connected()) {
        return;
    }

    Serial.print("Connecting to MQTT... ");

    uint8_t retries = 3;
    while ((ret = mqtt.connect()) != 0) { // connect will return 0 for connected
        Serial.println(mqtt.connectErrorString(ret));
        Serial.println("Retrying MQTT connection in 5 seconds...");
        mqtt.disconnect();
        delay(5000);  // wait 5 seconds
        retries--;
        if (retries == 0) {
            Serial.println("[ERROR] MQTT connection failed after 3 retries. Restarting ESP32...");
            lcd.setCursor(0, 2);
            lcd.print("MQTT echec...   ");
            delay(2000);
            ESP.restart();
        }
    }
    Serial.println("MQTT Connected!");
}

// ========== FONCTION AFFICHAGE COMPTEUR VOITURES ==========
void updateCarCountDisplay(int count) {
    // Effacer la ligne complètement
    lcd.setCursor(0, 1);
    lcd.print("                    ");  // 20 espaces

    // Afficher les nouvelles valeurs
    lcd.setCursor(0, 1);
    lcd.print("Places: ");
    lcd.print(count);
    lcd.print("/");
    lcd.print(MAX_PARKING_SPOTS);

    // Calculer le pourcentage
    int percent = (count * 100) / MAX_PARKING_SPOTS;
    lcd.print(" (");
    if (percent < 100) lcd.print(" ");
    if (percent < 10) lcd.print(" ");
    lcd.print(percent);
    lcd.print("%)  ");  // Espaces supplémentaires pour effacer
}

// ========== FONCTION DEPLACEMENT BARRIERE ==========
void moveBarrier(bool up) {
    if (up) {
        // Lever la barrière
        for (int pos = barriere.read(); pos <= SERVO_UP; pos++) {
            barriere.write(pos);
            delay(15);
        }
        barrierUp = true;
        lcd.setCursor(0, 3);
        lcd.print("Barriere: LEVEE ");
    } else {
        // Baisser la barrière
        for (int pos = barriere.read(); pos >= SERVO_DOWN; pos--) {
            barriere.write(pos);
            delay(15);
        }
        barrierUp = false;
        lcd.setCursor(0, 3);
        lcd.print("Barriere: BAISSE");
    }
}

// ========== FONCTION MISE A JOUR STATUT PARKING ==========
void updateParkingStatus(bool isFull) {
    lcd.setCursor(0, 0);
    if (isFull) {
        lcd.print("PARKING COMPLET ");
    } else {
        lcd.print("PARKING LIBRE   ");
    }
}
