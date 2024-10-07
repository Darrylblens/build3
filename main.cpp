/*
  bronnen:
    - canvas https://canvas.kdg.be/courses/49815/pages/webserver-op-esp32?module_item_id=1011961
    - randomnerdtutorials https://randomnerdtutorials.com/esp32-web-bluetooth/  
  Rui Santos & Sara Santos - Random Nerd Tutorials
  Complete project details at https://RandomNerdTutorials.com/esp32-web-bluetooth/
  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
*/
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Adafruit_Sensor.h>
#include <credentials.h>
#include <DHT.h>

BLEServer* pServer = NULL;
BLECharacteristic* pSensorCharacteristic = NULL;
BLECharacteristic* pLedCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

#define LEDPIN_ROOD D6 // Gebruik de juiste GPIO-pin voor jouw setup
#define LEDPIN_GROEN D7
#define DHTPIN D5
#define BUTTONPIN D4
#define DHTTYPE DHT22 

DHT dht(DHTPIN, DHTTYPE);
float temp;
int buttonState;
int buttonCount = 0;

unsigned long lastReadTime = 0;
unsigned long lastSendTime = 0;  // Nieuw voor het verzenden van temperatuur
const unsigned long readInterval = 1000;
const unsigned long sendInterval = 3000;  // Interval voor het verzenden van temperatuur

// Callback-functie voor server connect/disconnect
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
  };

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
  }
};

// Callback-functie voor LED-aansturing via BLE
class MyCharacteristicCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pLedCharacteristic) {
    std::string ledvalue  = pLedCharacteristic->getValue(); 
    String value = String(ledvalue.c_str());
    Serial.print("Characteristic event, written: ");
    Serial.println(static_cast<int>(value[0])); // Print de integer waarde

    int receivedValue = static_cast<int>(value[0]);
    if (receivedValue == 1) {
      digitalWrite(LEDPIN_ROOD, HIGH);
    } else {
      digitalWrite(LEDPIN_ROOD, LOW);
    }
  }
};

// Functie om de temperatuur te lezen
void getTemperature() {
  temp = dht.readTemperature();
  if (isnan(temp)) {
    Serial.println("Fout bij het lezen van de temperatuur!");
  } else {
    Serial.print("Temperatuur: ");
    Serial.print(temp);
    Serial.println(" °C");
  }
}

// Functie voor het verzenden van temperatuur via BLE
void sendTemperature() {
  getTemperature();
  if (!isnan(temp)) {  // Controleer of de temperatuur geldig is
    String tempStr = String(temp);
    pSensorCharacteristic->setValue(tempStr.c_str()); // Stuur de temperatuurwaarde
    pSensorCharacteristic->notify();  // Notificeer de verandering
    Serial.print("Nieuwe temperatuur genotificeerd: ");
    Serial.println(tempStr);
  }
}

// Functie voor het instellen van de LED's op basis van temperatuur
void setLightsBasedOnTemperature() {
  if (temp > 25) {
    digitalWrite(LEDPIN_ROOD, HIGH);
    digitalWrite(LEDPIN_GROEN, LOW);
    Serial.println("'t is warm");
  } else {
    digitalWrite(LEDPIN_ROOD, LOW);
    digitalWrite(LEDPIN_GROEN, HIGH);
    Serial.println("'t is koud");
  }
}

// Functie om de Bluetooth-advertenties te beheren
void manageAdvertising() {
  if (!deviceConnected && oldDeviceConnected) {
    Serial.println("Apparaat losgekoppeld.");
    delay(500); // Geef de Bluetooth-stack tijd om zich voor te bereiden
    pServer->startAdvertising(); // Start opnieuw met adverteren
    Serial.println("Start opnieuw met adverteren");
    oldDeviceConnected = deviceConnected;
  }

  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
    Serial.println("Apparaat verbonden");
  }
}

// Functie om knopindrukken te verwerken
void handleButtonPress() {
  buttonState = digitalRead(BUTTONPIN);
  if (buttonState == HIGH && buttonCount == 0) {
    buttonCount = 1;
    Serial.println("Knop ingedrukt!");
    sendTemperature();
  } else if (buttonState == LOW) {
    buttonCount = 0;
  }
}

// Setup-functie
void setup() {
  Serial.begin(115200);
  pinMode(LEDPIN_ROOD, OUTPUT);
  pinMode(LEDPIN_GROEN, OUTPUT);
  pinMode(BUTTONPIN, INPUT);

  dht.begin();
  
  // Maak het BLE-apparaat
  BLEDevice::init("ESP32Darryl");

  // Maak de BLE-server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Maak de BLE-service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Maak een BLE-kenmerk voor de sensor
  pSensorCharacteristic = pService->createCharacteristic(
                      SENSOR_CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_NOTIFY |
                      BLECharacteristic::PROPERTY_INDICATE
                    );

  // Maak het kenmerk voor de LED-aansturing
  pLedCharacteristic = pService->createCharacteristic(
                      LED_CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_WRITE
                    );

  // Registreer de callback voor de LED-aansturing
  pLedCharacteristic->setCallbacks(new MyCharacteristicCallbacks());

  // Voeg descriptoren toe voor de kenmerken
  pSensorCharacteristic->addDescriptor(new BLE2902());
  pLedCharacteristic->addDescriptor(new BLE2902());

  // Start de service
  pService->start();

  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x0);  // Zet waarde op 0x00 om deze parameter niet te adverteren
  BLEDevice::startAdvertising();
  Serial.println("Wachten op clientverbinding om te notificeren...");
}

// Loop-functie: Verplaatst logica naar functies
void loop() {
  unsigned long currentMillis = millis();

  // Verzend temperatuur als de tijd is verstreken
  if (deviceConnected && (currentMillis - lastSendTime >= sendInterval)) {
    lastSendTime = currentMillis;
    sendTemperature();
  }

  // Verwerk knopindrukken
  handleButtonPress();

  // Beheer connect/disconnect en advertenties
  manageAdvertising();

  // Stel de verlichting in op basis van de temperatuur
  if (currentMillis - lastReadTime >= readInterval) {
    lastReadTime = currentMillis;
    setLightsBasedOnTemperature();
  }
}
