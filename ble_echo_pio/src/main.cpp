#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

static const char *kServiceUuid = "8b1c0001-5b2a-4f3e-9c1d-6a7b8c9d0e01";
static const char *kCharacteristicUuid = "8b1c0002-5b2a-4f3e-9c1d-6a7b8c9d0e01";

static BLEServer *server = nullptr;
static volatile bool clientConnected = false;
static volatile bool restartAdvertising = false;

class ServerCallbacks : public BLEServerCallbacks {
 public:
  void onConnect(BLEServer *connectedServer) override {
    (void)connectedServer;
    clientConnected = true;
    Serial.println("BLE: connected");
  }

  void onDisconnect(BLEServer *disconnectedServer) override {
    (void)disconnectedServer;
    clientConnected = false;
    Serial.println("BLE: disconnected");
    restartAdvertising = true;
  }
};

class EchoCallbacks : public BLECharacteristicCallbacks {
 public:
  void onWrite(BLECharacteristic *characteristic) override {
    // Arduino-ESP32 3.x getValue()/setValue() use String. Core 2.0.17
    // still uses std::string, so copy through the shared c_str()/length() API.
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    String value = characteristic->getValue();
#else
    const std::string raw = characteristic->getValue();
    String value(raw.c_str(), raw.length());
#endif
    Serial.print("BLE RX: ");
    Serial.println(value);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    characteristic->setValue(value);
#else
    characteristic->setValue(std::string(value.c_str(), value.length()));
#endif
    characteristic->notify();
    Serial.print("BLE TX: ");
    Serial.println(value);
  }
};

void setup() {
  Serial.begin(115200);
  unsigned long start = millis();
  while (!Serial && millis() - start < 2000) {
    delay(10);
  }

  BLEDevice::init("XIAO-BLE-ECHO");
  server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService *service = server->createService(kServiceUuid);
  BLECharacteristic *characteristic = service->createCharacteristic(
      kCharacteristicUuid,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE |
          BLECharacteristic::PROPERTY_NOTIFY);
  characteristic->addDescriptor(new BLE2902());
  characteristic->setCallbacks(new EchoCallbacks());
  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(kServiceUuid);
  advertising->setScanResponse(true);
  BLEDevice::startAdvertising();
}

void loop() {
  if (restartAdvertising && !clientConnected) {
    delay(500);
    server->startAdvertising();
    restartAdvertising = false;
  }
}
