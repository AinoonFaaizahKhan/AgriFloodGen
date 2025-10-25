#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "DHT.h"
#include <WiFi.h>
#include <HTTPClient.h>

// ---------------- WiFi & ThingSpeak ----------------
const char* WIFI_SSID = "";
const char* WIFI_PASSWORD = "";
String THINGSPEAK_API_KEY = ""; 
String THINGSPEAK_SERVER = "";

// ----------------- Pin Config -----------------
#define DHTPIN 4
#define DHTTYPE DHT22
#define SOIL1_PIN 34
#define SOIL2_PIN 35
#define TRIG_PIN 5
#define ECHO_PIN 18
#define GREEN_LED 25
#define RED_LED 26
#define RELAY_PIN 27   // Relay for submersible motor

// ----------------- Objects -----------------
DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);
WiFiClient client;
HTTPClient http;

// ----------------- WiFi -----------------
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting Wi-Fi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    Serial.print(".");
    delay(500);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWi-Fi connected!");
  } else {
    Serial.println("\nWi-Fi failed to connect, retrying later.");
  }
}

// ----------------- Publish to ThingSpeak -----------------
void publishThingSpeak(float temp, float hum, int soil1, int soil2, float distance) {
  if (WiFi.status() != WL_CONNECTED) return;

  String serverPath = THINGSPEAK_SERVER + "?api_key=" + THINGSPEAK_API_KEY
                      + "&field1=" + String(temp)
                      + "&field2=" + String(hum)
                      + "&field3=" + String(soil1)
                      + "&field4=" + String(soil2)
                      + "&field5=" + String(distance);

  http.begin(client, serverPath);
  int httpResponseCode = http.GET();
  if (httpResponseCode > 0) {
    Serial.println("Data sent to ThingSpeak!");
    Serial.println("HTTP Response code: " + String(httpResponseCode));
  } else {
    Serial.println("Error sending to ThingSpeak. Code: " + String(httpResponseCode));
  }
  http.end();
}

// ----------------- Setup -----------------
void setup() {
  Serial.begin(115200);

  // LCD initialization
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0,0);
  lcd.print("Flood Monitor");
  delay(2000);
  lcd.clear();

  // Sensors
  dht.begin();
  pinMode(SOIL1_PIN, INPUT);
  pinMode(SOIL2_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);

  digitalWrite(RELAY_PIN, LOW); // Relay OFF initially

  connectWiFi(); // try Wi-Fi early
}

// ----------------- Loop -----------------
void loop() {
  connectWiFi(); // reconnect if Wi-Fi lost

  // --- Sensors ---
  float temp = dht.readTemperature();
  float hum = dht.readHumidity();

  if (isnan(temp) || isnan(hum)) {
    Serial.println("DHT read failed, retrying...");
    delay(2000);
    temp = dht.readTemperature();
    hum = dht.readHumidity();
  }

  if (isnan(temp)) temp = 0.0;
  if (isnan(hum)) hum = 0.0;

  // Soil readings
  int soil1 = analogRead(SOIL1_PIN);
  int soil2 = analogRead(SOIL2_PIN);
  int soil1Pct = map(soil1, 0, 4095, 100, 0);
  int soil2Pct = map(soil2, 0, 4095, 100, 0);
  int avgSoil = (soil1Pct + soil2Pct) / 2;

  // Ultrasonic sensor
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 30000); // 30ms timeout
  float distance = duration * 0.034 / 2; // cm

  // Flood risk level
  String risk = "LOW";
  if ((distance <= 15 && distance > 10) && (avgSoil > 60 && avgSoil <= 80)) risk = "MED";
  if (distance <= 10 && avgSoil > 80) risk = "HIGH";

  // LED indicators
  if (risk == "LOW") { digitalWrite(GREEN_LED,HIGH); digitalWrite(RED_LED,LOW); }
  else if (risk == "HIGH") { digitalWrite(GREEN_LED,LOW); digitalWrite(RED_LED,HIGH); }
  else { digitalWrite(GREEN_LED,LOW); digitalWrite(RED_LED,LOW); }

  // --- Pump control (Relay) ---
  // Maintain relay ON if distance ≤ 2cm, OFF only when ≥ 8cm
  static bool relayState = false;

  if (distance <= 4.2) {
    relayState = true;
    Serial.println("Gap <= 3cm → Relay ON (motor active)");
  } 
  else if (distance >= 8) {
    relayState = false;
    Serial.println("Gap >= 8cm → Relay OFF (motor stopped)");
  }

  // Apply relay state
  digitalWrite(RELAY_PIN, relayState ? HIGH : LOW);

  // Debug info
  Serial.printf("Temp: %.1fC Hum: %.1f%% Soil1:%d Soil2:%d Avg:%d Dist:%.1fcm Risk:%s Relay:%s\n",
                temp, hum, soil1Pct, soil2Pct, avgSoil, distance, risk.c_str(), 
                relayState ? "ON" : "OFF");

  // --- LCD Display ---
  // Screen 1: Flood Risk
  /*lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Flood Risk Level:");
  lcd.setCursor(0,1);
  lcd.print(risk);
  delay(2000);*/

  // Screen 2: Temp & Humidity
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Temp:");
  lcd.print(temp,1);
  lcd.print("C");
  lcd.setCursor(0,1);
  lcd.print("Hum:");
  lcd.print(hum,0);
  lcd.print("%");
  delay(2000);

  // Screen 3: Soil Moisture
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("S1:");
  lcd.print(soil1Pct);
  lcd.print("% S2:");
  lcd.print(soil2Pct);
  lcd.print("%");
  lcd.setCursor(0,1);
  lcd.print("Avg:");
  lcd.print(avgSoil);
  lcd.print("%");
  delay(2000);

  // Screen 4: Distance Detected
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Water Level Dist:");
  lcd.setCursor(0,1);
  lcd.print(distance,1);
  lcd.print(" cm");
  delay(2000);

  // --- Publish to ThingSpeak ---
  publishThingSpeak(temp, hum, soil1Pct, soil2Pct, distance);

  delay(15000); // ThingSpeak rate limit
}
