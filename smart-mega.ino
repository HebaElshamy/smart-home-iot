// ----------------- Arduino Mega (Master sensors + actuators) -----------------
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Servo.h>

#define SS_PIN 53
#define RST_PIN 5
#define SERVO_PIN 6

MFRC522 rfid(SS_PIN, RST_PIN);
Servo myServo;

#define MQ2_PIN A0
#define LED1_PIN 8
#define LED2_PIN 11
#define FAN1_PIN 9
#define FAN2_PIN 10
#define BUZZER_PIN 7
#define PIR_PIN 3
#define DHT_PIN 4
#define DHT_TYPE DHT11

LiquidCrystal_I2C lcd(0x27,16,2);
DHT dht(DHT_PIN, DHT_TYPE);

// variables for display and sensors
int gasValueGlobal = 0;
float lastTemp = NAN;
float lastHum  = NAN;
bool motionState = false;

// timing
unsigned long led2OnMillis = 0;
const unsigned long led2Timeout = 10000UL; // 10 seconds
bool led2On = false;

int prevPirState = LOW;
unsigned long lastSensorSend = 0;
const unsigned long sendInterval = 2000UL; // every 2s send sensor data

// gas threshold
int mqThreshold = 300;

// fan control hysteresis
float tempThreshold = 21.0;
float tempHysteresis = 1.5;
bool fan2State = false;

// servo control
bool servoOpen = false;
unsigned long servoOpenTime = 0;
const unsigned long servoOpenDuration = 3000UL; // 3s

// LED1 override (set by ESP command)
bool led1Override = false;
bool led1StateFromESP = false;

bool isAuthorized(byte *uid, byte size) {
  const byte authUID[] = { 0x83, 0x2F, 0x88, 0xF7 };
  if (size != 4) return false;
  for (byte i=0;i<4;i++) if (uid[i] != authUID[i]) return false;
  return true;
}

void setup() {
  Serial.begin(9600);    // monitor
  Serial1.begin(9600);   // communication to ESP (Serial1)
  SPI.begin();
  rfid.PCD_Init();

  myServo.attach(SERVO_PIN);
  myServo.write(0);

  pinMode(MQ2_PIN, INPUT);
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  pinMode(FAN1_PIN, OUTPUT);
  pinMode(FAN2_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(PIR_PIN, INPUT);

  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);
  digitalWrite(FAN1_PIN, LOW);
  digitalWrite(FAN2_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  dht.begin();
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("System Starting...");
  delay(1500);
  lcd.clear();
}

// update second LCD line: Motion + Temp + Hum
void updateLCDLine2() {
  char buf[17]; // 16 chars + null
  const char *motionText = motionState ? "Y" : "N";

  char tbuf[6], hbuf[5];
  if (!isnan(lastTemp)) {
    dtostrf(lastTemp, 2, 0, tbuf);
  } else {
    strcpy(tbuf, "--");
  }
  if (!isnan(lastHum)) {
    dtostrf(lastHum, 2, 0, hbuf);
  } else {
    strcpy(hbuf, "--");
  }

  // format: M:Y T:26C H:45%
  snprintf(buf, sizeof(buf), "M:%s T:%sC H:%s%%", motionText, tbuf, hbuf);

  int len = strlen(buf);
  if (len < 16) {
    for (int i = len; i < 16; i++) buf[i] = ' ';
    buf[16] = '\0';
  }

  lcd.setCursor(0,1);
  lcd.print(buf);
}

void loop() {
  // process incoming commands from ESP (Serial1)
  while (Serial1.available()) {
    String cmd = Serial1.readStringUntil('\n');
    cmd.trim();
    if (cmd.length()) handleCommand(cmd);
  }

  // RFID check (non-blocking)
  checkRFID();

  // gas sensor & local actions
  checkGasSensor();

  // PIR / LED2 handling
  checkMotionSensor();

  // servo auto-close
  if (servoOpen && millis() - servoOpenTime >= servoOpenDuration) {
    myServo.write(0);
    servoOpen = false;
    Serial.println("Servo closed automatically");
    Serial1.println("SERVO:CLOSED");
  }

  // periodic sensor sending to ESP (every sendInterval)
  if (millis() - lastSensorSend >= sendInterval) {
    lastSensorSend = millis();
    sendSensorsToESP();
  }
}

// command handler (from ESP)
void handleCommand(const String &cmd) {
  Serial.print("Cmd from ESP: ");
  Serial.println(cmd);

  if (cmd.startsWith("LED1:")) {
    int v = cmd.substring(5).toInt();
    led1Override = true;
    led1StateFromESP = (v != 0);
    digitalWrite(LED1_PIN, led1StateFromESP ? HIGH : LOW);
    Serial1.print("ACK:LED1:");
    Serial1.println(v);
  }
  else if (cmd.startsWith("LED2:")) {
    int v = cmd.substring(5).toInt();
    digitalWrite(LED2_PIN, v?HIGH:LOW);
    if (v) { led2On = true; led2OnMillis = millis(); }
    Serial1.print("ACK:LED2:"); Serial1.println(v);
  }
  else if (cmd.startsWith("FAN1:")) {
    int v = cmd.substring(5).toInt();
    digitalWrite(FAN1_PIN, v?HIGH:LOW);
    Serial1.print("ACK:FAN1:"); Serial1.println(v);
  }
  else if (cmd.startsWith("FAN2:")) {
    int v = cmd.substring(5).toInt();
    digitalWrite(FAN2_PIN, v?HIGH:LOW);
    fan2State = (v==1);
    Serial1.print("ACK:FAN2:"); Serial1.println(v);
  }
  else if (cmd == "SERVO:OPEN") {
    myServo.write(90);
    servoOpen = true;
    servoOpenTime = millis();
    Serial1.println("ACK:SERVO:OPEN");
  }
  else if (cmd == "SERVO:CLOSE") {
    myServo.write(0);
    servoOpen = false;
    Serial1.println("ACK:SERVO:CLOSE");
  }
  else if (cmd.startsWith("SERVO_ANGLE:")) {
    int a = cmd.substring(12).toInt();
    a = constrain(a, 0, 180);
    myServo.write(a);
    Serial1.print("ACK:SERVO_ANGLE:"); Serial1.println(a);
  }
  else if (cmd == "CLEAR_LED1_OVERRIDE") {
    led1Override = false;
    Serial1.println("ACK:LED1_OVERRIDE_CLEARED");
  }
}

// RFID
void checkRFID() {
  if (! rfid.PICC_IsNewCardPresent()) return;
  if (! rfid.PICC_ReadCardSerial()) return;
  Serial.print("UID: ");
  for (byte i=0;i<rfid.uid.size;i++) {
    Serial.print(rfid.uid.uidByte[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
  if (isAuthorized(rfid.uid.uidByte, rfid.uid.size)) {
    Serial.println("RFID: AUTHORIZED");
    myServo.write(90);
    servoOpen = true;
    servoOpenTime = millis();
    Serial1.println("RFID:AUTH");
  } else {
    Serial.println("RFID: DENIED");
    Serial1.println("RFID:DENIED");
  }
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

// gas sensor
void checkGasSensor() {
  int gasValue = analogRead(MQ2_PIN);
  gasValueGlobal = gasValue;

  if (gasValue > mqThreshold) {
    if (!led1Override) digitalWrite(LED1_PIN, HIGH);
    digitalWrite(FAN1_PIN, HIGH);
    digitalWrite(BUZZER_PIN, HIGH);
    Serial.print("Gas Alert! Value: "); Serial.println(gasValue);

    // Line 1: GAS ALERT
    lcd.setCursor(0,0);
    lcd.print("GAS ALERT!      ");
  } 
  else {
    if (!led1Override) digitalWrite(LED1_PIN, LOW);
    digitalWrite(FAN1_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);
    Serial.print("Gas Normal. Value: "); Serial.println(gasValue);

    // Line 1: GAS NORMAL
    lcd.setCursor(0,0);
    lcd.print("Gas Normal      ");
  }
}

void checkMotionSensor() {
  int pir = digitalRead(PIR_PIN);

  if (pir != prevPirState) {
    Serial.print("PIR state changed: ");
    Serial.println(pir);
  }

  if (pir == HIGH && prevPirState == LOW) {
    Serial.println("Motion detected (rising edge)");
    digitalWrite(LED2_PIN, HIGH);
    led2On = true;
    led2OnMillis = millis();

    motionState = true;
    updateLCDLine2();

    sendTempHumToESP();
  }

  if (pir == LOW && prevPirState == HIGH) {
    Serial.println("Motion ended (falling edge)");
  }

  if (led2On && (millis() - led2OnMillis >= led2Timeout)) {
    digitalWrite(LED2_PIN, LOW);
    led2On = false;
    motionState = false;
    updateLCDLine2();
    Serial.println("LED2 turned OFF after timeout");
  }

  prevPirState = pir;
}

// immediate read/send when PIR triggers
void sendTempHumToESP() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) {
    lastTemp = t;
    Serial1.print("TEMP:"); Serial1.println(t,1);
    Serial.print("TEMP:"); Serial1.println(t,1);
  } else {
    Serial1.println("TEMP:NAN");
  }
  if (!isnan(h)) {
    lastHum = h;
    Serial1.print("HUM:"); Serial1.println(h,1);
    Serial.print("HUM:"); Serial1.println(h,1);
  } else {
    Serial1.println("HUM:NAN");
  }

  updateLCDLine2();
}

void sendSensorsToESP() {
  int gas = analogRead(MQ2_PIN);
  gasValueGlobal = gas;
  Serial1.print("GAS:"); Serial1.println(gas);

  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) {
    lastTemp = t;
    Serial1.print("TEMP:"); Serial1.println(t, 1);
  } else {
    Serial1.println("TEMP:NAN");
  }
  if (!isnan(h)) {
    lastHum = h;
    Serial1.print("HUM:"); Serial1.println(h, 1);
  } else {
    Serial1.println("HUM:NAN");
  }

  updateLCDLine2();
}
