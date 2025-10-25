#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>
#include <base64.h>   // For encoding SID:AUTH into base64

// ---------------- Wi-Fi & ThingSpeak ----------------
const char* WIFI_SSID = "";
const char* WIFI_PASSWORD = "";
const char* THINGSPEAK_FEEDS_URL = "";

// ---------------- Twilio Credentials ----------------
const char* TWILIO_SID = "";
const char* TWILIO_AUTH = "";
const char* TWILIO_FROM = "";   // Twilio number
const char* MY_PHONE = "";      // Your phone number

// ---------------- Hardware Pins ----------------
#define GREEN_LED 25
#define RED_LED 26

// ---------------- Objects ----------------
LiquidCrystal_I2C lcd(0x27, 16, 2);
WiFiClientSecure client;
HTTPClient http;

// ---------------- Helper Flags ----------------
bool thingSpeakConnected = false;
String lastRisk = "LOW"; // To prevent duplicate SMS alerts

// ---------------- Connect to Wi-Fi ----------------
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting Wi-Fi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Wi-Fi connected!");
    Serial.print("IP Address: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("Wi-Fi failed to connect!");
  }
}

// ---------------- Compute Flood Risk ----------------
int computeFloodRisk(float soil1, float soil2, float distance) {
  float SM_sum = (soil1 + soil2) / 2.0;
  float SS_sum = distance;
  float AWQ_sum = 0; // placeholder
  float R = 0.6 * SM_sum + 0.3 * SS_sum + 0.1 * AWQ_sum;

  if (R < 50) return 0;      // LOW
  else if (R < 80) return 1; // MED
  else return 2;             // HIGH
}

// ---------------- Send SMS via Twilio ----------------
void sendTwilioSMS(String message) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi not connected. SMS not sent.");
    return;
  }

  WiFiClientSecure smsClient;
  smsClient.setInsecure();  // Skip SSL certificate verification
  HTTPClient smsHttp;

  String url = "https://api.twilio.com/2010-04-01/Accounts/" + String(TWILIO_SID) + "/Messages.json";

  // Encode SID:AUTH into base64 for Basic Auth
  String auth = String(TWILIO_SID) + ":" + String(TWILIO_AUTH);
  String authHeader = "Basic " + base64::encode(auth);

  // Form data for Twilio API
  String body = "To=" + String(MY_PHONE) +
                "&From=" + String(TWILIO_FROM) +
                "&Body=" + message;

  smsHttp.begin(smsClient, url);
  smsHttp.addHeader("Authorization", authHeader);
  smsHttp.addHeader("Content-Type", "application/x-www-form-urlencoded");

  int httpResponseCode = smsHttp.POST(body);

  if (httpResponseCode > 0) {
    Serial.print("SMS sent successfully! HTTP Response code: ");
    Serial.println(httpResponseCode);
    Serial.println(smsHttp.getString());
  } else {
    Serial.print("Error sending SMS: ");
    Serial.println(httpResponseCode);
  }

  smsHttp.end();
}

// ---------------- Setup ----------------
void setup() {
  Serial.begin(115200);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0,0);
  lcd.print("Receiver ESP32");
  delay(2000);
  lcd.clear();

  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);

  connectWiFi();
  client.setInsecure();
}

// ---------------- Loop ----------------
void loop() {
  connectWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    http.begin(client, THINGSPEAK_FEEDS_URL);
    int httpResponseCode = http.GET();

    if (httpResponseCode > 0) {
      if (!thingSpeakConnected) {
        Serial.println("Connected to ThingSpeak!");
        thingSpeakConnected = true;
      }

      String payload = http.getString();
      Serial.println("Payload: " + payload);

      // Parse JSON (last.json returns a single object)
      DynamicJsonDocument doc(1024);
      DeserializationError error = deserializeJson(doc, payload);
      if (error) {
        Serial.print("JSON parse error: "); Serial.println(error.c_str());
        http.end();
        delay(5000);
        return;
      }

      // Read values directly
      float temp  = atof(doc["field1"].as<const char*>());
      float hum   = atof(doc["field2"].as<const char*>());
      float soil1 = atof(doc["field3"].as<const char*>());
      float soil2 = atof(doc["field4"].as<const char*>());
      float dist  = atof(doc["field5"].as<const char*>());
      float avgSoil = (soil1 + soil2) / 2.0;

      // --- Run Mini GA ---
      int riskLevel = computeFloodRisk(soil1, soil2, dist);
      String risk;
      if (riskLevel == 0) { 
        risk = "LOW"; 
        digitalWrite(GREEN_LED, HIGH); 
        digitalWrite(RED_LED, LOW); 
      } else if (riskLevel == 1) { 
        risk = "HIGH"; 
        digitalWrite(GREEN_LED, LOW); 
        digitalWrite(RED_LED, HIGH); 
      }

      // Serial Print
      Serial.printf("Temp: %.1fC Hum: %.1f%% Soil1: %.1f Soil2: %.1f AvgSoil: %.1f Dist: %.1f Risk: %s\n",
                    temp, hum, soil1, soil2, avgSoil, dist, risk.c_str());

      // --- LCD Display ---
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("Flood Risk:");
      lcd.setCursor(0,1);
      lcd.print(risk);
      delay(2000);

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

      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("S1:");
      lcd.print(soil1);
      lcd.print(" S2:");
      lcd.print(soil2);
      lcd.setCursor(0,1);
      lcd.print("Avg:");
      lcd.print(avgSoil);
      delay(2000);

      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("Distance:");
      lcd.setCursor(0,1);
      lcd.print(dist);
      delay(2000);

      // --- SMS Alert ---
      if ((risk == "MED" || risk == "HIGH") && risk != lastRisk) {
        String alertMessage = "🚨 ALERT: Flood Risk " + risk + 
                              "! Temp=" + String(temp,1) + 
                              "C, Hum=" + String(hum,0) + "%, AvgSoil=" + String(avgSoil,0) + 
                              ", Dist=" + String(dist,0) + "cm.";
        sendTwilioSMS(alertMessage);
      }
      lastRisk = risk;

    } else {
      Serial.println("Error fetching ThingSpeak data");
      thingSpeakConnected = false;
    }

    http.end();
  }

  delay(15000); // ThingSpeak rate limit
}
