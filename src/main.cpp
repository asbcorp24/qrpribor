#include <GxEPD2_BW.h>         // Библиотека для e-ink экрана
#include <NimBLEDevice.h>      // Библиотека для работы с BLE
#include <QRCode.h>            // Библиотека для генерации QR-кодов
#include <mbedtls/aes.h>       // Библиотека для расшифровки AES
#include <Preferences.h>       // Библиотека для работы с Preferences

// Параметры e-ink экрана
GxEPD2_BW<GxEPD2_213_B72, GxEPD2_213_B72::HEIGHT> display(GxEPD2_213_B72(SS, 17, 16, 4)); // Настройте под ваш экран

const char defaultDeviceId[] = "DEVICE_123";         // Статичный идентификатор устройства
const char defaultEncryptionKey[] = "MySecureKey12"; // Статичный ключ шифрования (12 символов)
String deviceId;
char encryptionKey[16];

Preferences preferences;  // Экземпляр для работы с Preferences

const int relayPin = 5;        // Пин для управления реле
unsigned long relayEndTime = 0;
bool relayActive = false;

// Переменные для BLE
NimBLEServer* pServer = nullptr;
NimBLECharacteristic* pTimeCharacteristic = nullptr;
bool deviceConnected = false;
void displayTimeLeft(int secondsLeft);
void activateRelay(int duration);
int decryptTime(const String& encryptedTime);
void displayQRCode(const String& data);
// Инициализация BLE
class MyServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) {
        deviceConnected = true;
        Serial.println("Device connected");
    }

    void onDisconnect(NimBLEServer* pServer) {
        deviceConnected = false;
        Serial.println("Device disconnected");
    }
};

class TimeCharacteristicCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic) {
        std::string value = pCharacteristic->getValue();
        if (value.length() > 0) {
            int workDuration = decryptTime(String(value.c_str()));
            activateRelay(workDuration);
        }
    }
};

void setup() {
    Serial.begin(115200);

    // Инициализация Preferences
    preferences.begin("device-config", false);
    
    // Получение deviceId из памяти
    if (preferences.isKey("deviceId")) {
        deviceId = preferences.getString("deviceId", defaultDeviceId);
    } else {
        deviceId = defaultDeviceId;
        preferences.putString("deviceId", deviceId);
    }

    // Получение encryptionKey из памяти
    if (preferences.isKey("encryptionKey")) {
        String storedKey = preferences.getString("encryptionKey", defaultEncryptionKey);
        strncpy(encryptionKey, storedKey.c_str(), 16);
    } else {
        strncpy(encryptionKey, defaultEncryptionKey, 16);
        preferences.putString("encryptionKey", encryptionKey);
    }

    Serial.println("Device ID: " + deviceId);
    Serial.print("Encryption Key: ");
    Serial.println(encryptionKey);

    // Инициализация e-ink экрана
    display.init(115200);
    display.setRotation(1);
    display.setTextColor(GxEPD_BLACK);

    // Генерация QR-кода
    displayQRCode(deviceId);

    // Настройка BLE
    NimBLEDevice::init("ESP32_Device");
    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    NimBLEService *pService = pServer->createService("12345678-1234-1234-1234-1234567890AB");
    pTimeCharacteristic = pService->createCharacteristic(
                          "dcba4321-1234-1234-1234-1234567890AB",
                          NIMBLE_PROPERTY::WRITE);
    pTimeCharacteristic->setCallbacks(new TimeCharacteristicCallbacks());

    pService->start();
    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID("12345678-1234-1234-1234-1234567890AB");
    pAdvertising->start();

    // Настройка пина реле
    pinMode(relayPin, OUTPUT);
    digitalWrite(relayPin, LOW);
}

void loop() {
    if (deviceConnected) {
        // Проверка времени окончания работы реле
        if (relayActive && millis() >= relayEndTime) {
            digitalWrite(relayPin, LOW);
            relayActive = false;
            displayQRCode(deviceId);
        }
    }
}

// Генерация QR-кода
void displayQRCode(const String& data) {
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setCursor(0, 10);
        display.println("Device ID:");

        QRCode qrcode;
        uint8_t qrcodeData[qrcode_getBufferSize(3)];
        qrcode_initText(&qrcode, qrcodeData, 3, 0, data.c_str());

        for (int y = 0; y < qrcode.size; y++) {
            for (int x = 0; x < qrcode.size; x++) {
                if (qrcode_getModule(&qrcode, x, y)) {
                    display.drawPixel(30 + x, 30 + y, GxEPD_BLACK);
                }
            }
        }
    } while (display.nextPage());
}

// Включение реле на определенное время
void activateRelay(int duration) {
    digitalWrite(relayPin, HIGH);
    relayEndTime = millis() + duration * 1000;
    relayActive = true;
    displayTimeLeft(duration);
}

// Отображение оставшегося времени на e-ink
void displayTimeLeft(int secondsLeft) {
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setCursor(10, 20);
        display.println("Remaining time:");
        display.setCursor(10, 50);
        display.print(secondsLeft);
        display.println(" seconds");
    } while (display.nextPage());
}

// Расшифровка времени работы
int decryptTime(const String& encryptedTime) {
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);

    uint8_t key[16];
    memcpy(key, encryptionKey, 16);
    mbedtls_aes_setkey_dec(&aes, key, 128);

    uint8_t input[16] = {0};
    uint8_t output[16] = {0};
    memcpy(input, encryptedTime.c_str(), encryptedTime.length());

    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, input, output);
    mbedtls_aes_free(&aes);

    int decryptedTime = 0;
    for (int i = 0; i < 4; i++) {
        decryptedTime = (decryptedTime << 8) + output[i];
    }

    return decryptedTime;
}
