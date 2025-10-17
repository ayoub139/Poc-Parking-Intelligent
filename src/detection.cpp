#include <Arduino.h>
#include "esp_sleep.h"

#define SENSOR_PIN 36
#define RED_PIN 25
#define BUTTON_PIN 33
#define THRESHOLD 150

RTC_DATA_ATTR int sensorMin = 4095;
RTC_DATA_ATTR int sensorMax = 0;
RTC_DATA_ATTR bool calibrated = false;

void calibrateSensor() {
  Serial.println("Calibration en cours...");
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
  digitalWrite(RED_PIN, LOW);  // LED éteinte au démarrage

  pinMode(BUTTON_PIN, INPUT_PULLUP);  // bouton poussoir vers GND
  analogReadResolution(12);

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  if (!calibrated) {
    // Premier démarrage → calibration
    calibrateSensor();
  } else if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
    // Réveil par appui sur le bouton
    Serial.println("Réveil par le bouton !");
    int val = analogRead(SENSOR_PIN);
    val = map(val, sensorMin, sensorMax, 0, 255);
    val = constrain(val, 0, 255);
    Serial.print("Valeur capteur : "); Serial.println(val);

    if (val > THRESHOLD) {
      digitalWrite(RED_PIN, HIGH);
      Serial.println("Voiture détectée !");
      delay(2000);
      digitalWrite(RED_PIN, LOW);
    } else {
      Serial.println("Aucune voiture détectée.");
    }
  }

  esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON_PIN, 0);  // LOW = appui
  esp_deep_sleep_start();
}

void loop() {
  
}
