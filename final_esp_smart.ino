// ---------------- ESP32 (Blynk bridge to Mega) ----------------
#define BLYNK_TEMPLATE_ID "TMPL67OjaFgJV"
#define BLYNK_TEMPLATE_NAME "Smart Home"
#define BLYNK_AUTH_TOKEN "3Amk6tr51DL7Wy9cmJVt72FnTpSKpZI_"

#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>

#define ESP_RX_PIN 16   // connect to Mega TX1 (via level shifter)
#define ESP_TX_PIN 17   // connect to Mega RX1

char auth[] = BLYNK_AUTH_TOKEN;
char ssid[] = "Yonova";
char pass[] = "12345@2025";

void sendToMega(const String &s) {
  Serial2.print(s);
  Serial2.print('\n');
  Serial.print("Sent to Mega: ");
  Serial.println(s);
}

// Blynk virtual callbacks
BLYNK_WRITE(V1) { int v = param.asInt(); if (v) sendToMega("LED1:1"); else sendToMega("LED1:0"); }
BLYNK_WRITE(V2) { int v = param.asInt(); if (v) sendToMega("LED2:1"); else sendToMega("LED2:0"); }
BLYNK_WRITE(V3) { int v = param.asInt(); if (v) sendToMega("SERVO:OPEN"); else sendToMega("SERVO:CLOSE"); }
// optional slider for angle on V7
BLYNK_WRITE(V7) {
  int angle = param.asInt();
  String s = "SERVO_ANGLE:" + String(angle);
  sendToMega(s);
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, ESP_RX_PIN, ESP_TX_PIN); // to Mega
  delay(200);

  Blynk.begin(auth, ssid, pass);
  Serial.println("Blynk connected, ESP passthrough ready");
}

void loop() {
  Blynk.run();

  // read lines from Mega
  while (Serial2.available()) {
    String line = Serial2.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    Serial.print("From Mega: ");
    Serial.println(line);

    // Parse and publish to Blynk
    if (line.startsWith("GAS:")) {
      String val = line.substring(4);
      int gas = val.toInt();
      Blynk.virtualWrite(V4, gas);
    }
    else if (line.startsWith("TEMP:")) {
      String val = line.substring(5);
      float t = val.toFloat();
      Blynk.virtualWrite(V0, t);
    }
    else if (line.startsWith("HUM:")) {
      String val = line.substring(4);
      float h = val.toFloat();
      Blynk.virtualWrite(V5, h);
    }
    else {
      // optional: forward other messages to V6 terminal if you create it
      // Serial.println("Other: " + line);
    }
  }
}
