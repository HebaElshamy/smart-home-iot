#include <SPI.h>
#include <MFRC522.h>

#define SS_PIN 53
#define RST_PIN 5

MFRC522 mfrc(SS_PIN, RST_PIN);

void setup() {
  Serial.begin(9600);
  SPI.begin();
  mfrc.PCD_Init();
  Serial.println("MFRC522 reader ready");
  Serial.println("Tap an RFID card...");
}

void loop() {
  if (!mfrc.PICC_IsNewCardPresent()) {
    delay(50);
    return;
  }
  if (!mfrc.PICC_ReadCardSerial()) {
    delay(50);
    return;
  }

  Serial.print("UID: ");
  for (byte i = 0; i < mfrc.uid.size; i++) {
    if (mfrc.uid.uidByte[i] < 0x10) Serial.print("0");
    Serial.print(mfrc.uid.uidByte[i], HEX);
    Serial.print(" ");
  }
  Serial.println();

  Serial.print("As byte array: { ");
  for (byte i = 0; i < mfrc.uid.size; i++) {
    Serial.print("0x");
    if (mfrc.uid.uidByte[i] < 0x10) Serial.print("0");
    Serial.print(mfrc.uid.uidByte[i], HEX);
    if (i < mfrc.uid.size - 1) Serial.print(", ");
  }
  Serial.println(" }");

  mfrc.PICC_HaltA();
  mfrc.PCD_StopCrypto1();

  delay(300);
}
