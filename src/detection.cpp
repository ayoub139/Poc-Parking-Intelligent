#include <Arduino.h>
#include <WiFi.h>
#include <Adafruit_MQTT.h>
#include <Adafruit_MQTT_Client.h>
#include "esp_sleep.h"

// ========== CONFIGURATION WIFI & MQTT ==========
#define WIFI_SSID "OpenLab"
#define WIFI_PASS "MBlVst6WmEa&NjS4TNbmE3*"
#define AIO_SERVER      "io.adafruit.com"
#define AIO_SERVERPORT  1883
#define AIO_USERNAME    "Ihab_"

// ========== PINS ==========
#define SENSOR_PIN 36
#define GREEN_PIN 25
#define RED_PIN 26
#define BUTTON_PIN 33

// ========== CONSTANTES ==========
#define THRESHOLD_1 100   // premier seuil
#define THRESHOLD_2 200   // second seuil
#define MAX_CARS 2

// ========== VARIABLES RTC (persistent après deep sleep) ==========
RTC_DATA_ATTR bool calibrated = false;
RTC_DATA_ATTR int sensorMin = 4095;
RTC_DATA_ATTR int sensorMax = 0;
RTC_DATA_ATTR int carsDetected = 0;
RTC_DATA_ATTR bool place1Present = false;
RTC_DATA_ATTR bool place2Present = false;

// ========== OBJETS MQTT ==========
WiFiClient client;
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);

// Feeds Adafruit IO
Adafruit_MQTT_Publish carsCount     = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/parking-count");
Adafruit_MQTT_Publish parkingStatus = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/parking-status");
Adafruit_MQTT_Publish place1Status  = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/parking-place1");
Adafruit_MQTT_Publish place2Status  = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/parking-place2");

// Prototypes
void connectWiFi();
void connectMQTT();
void calibrateSensor();

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  analogReadResolution(12);

  digitalWrite(RED_PIN, LOW);
  digitalWrite(GREEN_PIN, LOW);

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  if (!calibrated) {
    calibrateSensor();
    calibrated = true;
  }

  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("\n========================================");
    Serial.println(">>> Réveil par le bouton ! <<<");

    int raw = analogRead(SENSOR_PIN);
    int val = map(raw, sensorMin, sensorMax, 0, 255);
    val = constrain(val, 0, 255);

    Serial.print("Valeur capteur : ");
    Serial.print(raw);
    Serial.print(" (→ ");
    Serial.print(val);
    Serial.println(")");

    // ===== Détection place 1 =====
    bool newPlace1 = (val > THRESHOLD_1);
    bool newPlace2 = (val > THRESHOLD_2);

    // Gestion place 1
    if (newPlace1 && !place1Present) {
      Serial.println("[PLACE 1] Voiture détectée !");
      carsDetected++;
      place1Present = true;
    } else if (!newPlace1 && place1Present) {
      Serial.println("[PLACE 1] Voiture partie.");
      carsDetected--;
      place1Present = false;
    }

    // Gestion place 2
    if (newPlace2 && !place2Present) {
      Serial.println("[PLACE 2] Voiture détectée !");
      carsDetected++;
      place2Present = true;
    } else if (!newPlace2 && place2Present) {
      Serial.println("[PLACE 2] Voiture partie.");
      carsDetected--;
      place2Present = false;
    }

    // Sécurité bornes
    if (carsDetected < 0) carsDetected = 0;
    if (carsDetected > MAX_CARS) carsDetected = MAX_CARS;

    Serial.print("Nombre total de voitures : ");
    Serial.println(carsDetected);

    // LEDs
    if (carsDetected < MAX_CARS) { // au moins une place libre
    digitalWrite(GREEN_PIN, HIGH);
    digitalWrite(RED_PIN, LOW);
    } else { // parking complet
    digitalWrite(GREEN_PIN, LOW);
    digitalWrite(RED_PIN, HIGH);
    }


    // ===== Publication MQTT =====
    connectWiFi();
    if (WiFi.status() == WL_CONNECTED) {
      connectMQTT();
      if (mqtt.connected()) {
        carsCount.publish(carsDetected);
        parkingStatus.publish(carsDetected >= MAX_CARS ? 1 : 0);
        delay(200);
        place1Status.publish(place1Present ? 1 : 0);
        delay(200);
        place2Status.publish(place2Present ? 1 : 0);
        Serial.println("Publication MQTT envoyée !");
      }
    }

    delay(2000);
    Serial.println("Retour en deep sleep...");
  } else {
    Serial.println("Premier démarrage ou reset manuel");
  }

  if (WiFi.status() == WL_CONNECTED) {
    mqtt.disconnect();
    WiFi.disconnect(true);
  }

  esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON_PIN, 0);
  esp_deep_sleep_start();
}

void loop() {}

// ======================================================
// Calibration du capteur unique
// ======================================================
void calibrateSensor() {
  Serial.println("\n=== Calibration du capteur (5 secondes) ===");
  unsigned long start = millis();
  while (millis() - start < 5000) {
    int v = analogRead(SENSOR_PIN);
    if (v < sensorMin) sensorMin = v;
    if (v > sensorMax) sensorMax = v;
  }
  Serial.println("Calibration terminée !");
  Serial.print("Min = "); Serial.print(sensorMin);
  Serial.print("  Max = "); Serial.println(sensorMax);
  Serial.println("=============================================");
}

// ======================================================
// Connexion WiFi et MQTT
// ======================================================
void connectWiFi() {
  Serial.print("Connexion WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED)
    Serial.println(" Connecté !");
  else
    Serial.println(" Échec WiFi !");
}

void connectMQTT() {
  if (mqtt.connected()) return;
  int8_t ret;
  uint8_t retries = 3;
  while ((ret = mqtt.connect()) != 0 && retries--) {
    Serial.println(mqtt.connectErrorString(ret));
    mqtt.disconnect();
    delay(2000);
  }
  if (ret == 0)
    Serial.println("Connecté MQTT !");
}
