#include <Arduino.h>
#include <WiFi.h>
#include <Adafruit_MQTT.h>
#include <Adafruit_MQTT_Client.h>

// WiFi
#define WIFI_SSID "Numericable-ca2a"
#define WIFI_PASS "u5km459i6ygz"

// Adafruit IO
#define AIO_SERVER      "io.adafruit.com"
#define AIO_SERVERPORT  1883
#define AIO_USERNAME    "Ihab_"

// Capteur et LED
#define SENSOR_PIN 36
#define GREEN_PIN 25
#define RED_PIN 26
#define THRESHOLD 150
#define READ_INTERVAL 500  

RTC_DATA_ATTR int sensorMin = 4095;
RTC_DATA_ATTR int sensorMax = 0;
RTC_DATA_ATTR bool calibrated = false;
RTC_DATA_ATTR int carsDetected = 0;
#define MAX_CARS 1

WiFiClient client;
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);
Adafruit_MQTT_Publish parkingStatus = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/parking-status");
Adafruit_MQTT_Publish carsCount = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/parking-count");

void connectWiFi() {
  Serial.print("Connexion WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" connecté !");
    Serial.print("IP : "); Serial.println(WiFi.localIP());
  } else {
    Serial.println(" ⚠️ Échec WiFi !");
  }
}

void connectMQTT() {
  if (mqtt.connected()) return;
  int8_t ret;
  uint8_t retries = 3;
  while ((ret = mqtt.connect()) != 0 && retries--) {
    Serial.print("Erreur MQTT : "); Serial.println(mqtt.connectErrorString(ret));
    mqtt.disconnect();
    delay(2000);
  }
  if (ret == 0) Serial.println("Connecté MQTT !");
  else Serial.println("⚠️ Échec MQTT !");
}

void calibrateSensor() {
  Serial.println("Calibration (5 sec)...");
  unsigned long start = millis();
  while (millis() - start < 5000) {
    int val = analogRead(SENSOR_PIN);
    if (val < sensorMin) sensorMin = val;
    if (val > sensorMax) sensorMax = val;
  }
  calibrated = true;
  Serial.println("Calibration terminée !");
  Serial.print("Min = "); Serial.println(sensorMin);
  Serial.print("Max = "); Serial.println(sensorMax);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  digitalWrite(RED_PIN, LOW);
  digitalWrite(GREEN_PIN, LOW);
  analogReadResolution(12);

  if (!calibrated) calibrateSensor();

  connectWiFi();
  if (WiFi.status() == WL_CONNECTED) connectMQTT();
}


void loop() {
  static bool lastStateFull = false;  
  static unsigned long lastRead = 0;

  if (millis() - lastRead < READ_INTERVAL) return;
  lastRead = millis();

  int val = analogRead(SENSOR_PIN);
  val = map(val, sensorMin, sensorMax, 0, 255);
  val = constrain(val, 0, 255);
  Serial.print("Valeur capteur : "); Serial.println(val);

  bool carDetected = (val > THRESHOLD);

  if (carDetected) {
    digitalWrite(GREEN_PIN, HIGH);
    digitalWrite(RED_PIN, LOW);
    if (carsDetected < MAX_CARS) carsDetected = MAX_CARS;
  } else {
    digitalWrite(GREEN_PIN, LOW);
    digitalWrite(RED_PIN, HIGH);
    if (carsDetected > 0) carsDetected = 0;
  }

  if (carDetected != lastStateFull) {
    lastStateFull = carDetected;
    if (WiFi.status() != WL_CONNECTED) connectWiFi();
    if (mqtt.connected() || WiFi.status() == WL_CONNECTED) connectMQTT();

    if (mqtt.connected()) {
      carsCount.publish(carsDetected);
      parkingStatus.publish(carDetected ? 1 : 0);
      Serial.println(carDetected ? "Parking complet" : "Parking libre");
    }
  }
}