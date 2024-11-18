#include <GxEPD2_BW.h>         // Библиотека для e-ink экрана
#include <NimBLEDevice.h>      // Библиотека для работы с BLE
#include <QRCode.h>            // Библиотека для генерации QR-кодов
#include <mbedtls/aes.h>       // Библиотека для расшифровки AES

// Параметры e-ink экрана
GxEPD2_BW<GxEPD2_213_B72, GxEPD2_213_B72::HEIGHT> display(GxEPD2_213_B72(SS, 17, 16, 4)); // Замена пинов под ваш экран

const int relayPin = 5;        // Пин для управления реле
String deviceId = "DEVICE_123"; // Уникальный идентификатор устройства
String blePassword = "secureBLEPass"; // Пароль для BLE

// Переменные для BLE
NimBLEServer* pServer = nullptr;
NimBLECharacteristic* pKeyCharacteristic = nullptr;
NimBLECharacteristic* pTimeCharacteristic = nullptr;
bool deviceConnected = false;

// Параметры времени работы реле
unsigned long relayEndTime = 0;
bool relayActive = false;
String encryptionKey; // Ключ шифрования, полученный по BLE
void displayQRCode(const String& data);
int decryptTime(const String& encryptedTime);
void activateRelay(int duration);
void displayTimeLeft(int secondsLeft);
// Callback для обработки соединения BLE
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

// Callback для обработки данных BLE
class KeyCharacteristicCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic) {
        std::string value = pCharacteristic->getValue();
        if (value.length() > 0) {
            encryptionKey = String(value.c_str());
            Serial.println("Received encryption key: " + encryptionKey);
        }
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
    
    // Инициализация e-ink экрана
    display.init(115200);
    display.setRotation(1);
    display.setTextColor(GxEPD_BLACK);

    // Генерация QR-кода для идентификатора устройства
    displayQRCode(deviceId);

    // Инициализация BLE
    NimBLEDevice::init("ESP32_Device");
    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    // Создание сервисов и характеристик BLE
    NimBLEService *pService = pServer->createService("12345678-1234-1234-1234-1234567890AB");
    pKeyCharacteristic = pService->createCharacteristic(
                         "abcd1234-1234-1234-1234-1234567890AB",
                          NIMBLE_PROPERTY::WRITE);
    pTimeCharacteristic = pService->createCharacteristic(
                         "dcba4321-1234-1234-1234-1234567890AB",
                          NIMBLE_PROPERTY::WRITE);

    pService->start();
    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID("12345678-1234-1234-1234-1234567890AB");
    pAdvertising->start();

    // Установка пина реле
    pinMode(relayPin, OUTPUT);
    digitalWrite(relayPin, LOW);
}

void loop() {
    if (deviceConnected) {
        // Проверка активации реле и времени окончания работы
        if (relayActive && millis() >= relayEndTime) {
            digitalWrite(relayPin, LOW); // Выключаем реле
            relayActive = false;
            displayQRCode(deviceId); // Обновляем QR-код после выключения реле
        }
    }
}

// Функция для генерации QR-кода устройства
void displayQRCode(const String& data) {
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setCursor(0, 10);
        display.println("Device ID:");

        // Генерация QR-кода
        QRCode qrcode;
        uint8_t qrcodeData[qrcode_getBufferSize(3)];
        qrcode_initText(&qrcode, qrcodeData, 3, 0, data.c_str());

        // Отображение QR-кода
        for (int y = 0; y < qrcode.size; y++) {
            for (int x = 0; x < qrcode.size; x++) {
                if (qrcode_getModule(&qrcode, x, y)) {
                    display.drawPixel(30 + x, 30 + y, GxEPD_BLACK);
                }
            }
        }
    } while (display.nextPage());
}

// Функция для активации реле на определенное время
void activateRelay(int duration) {
    digitalWrite(relayPin, HIGH);          // Включаем реле
    relayEndTime = millis() + duration * 1000; // Устанавливаем время окончания работы
    relayActive = true;
    
    // Отображаем оставшееся время работы на e-ink экране
    displayTimeLeft(duration);
}

// Функция для отображения времени на e-ink экране
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

// Функция для расшифровки времени работы
int decryptTime(const String& encryptedTime) {
    // Инициализация AES
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);

    // Установка ключа шифрования
    uint8_t key[16]; // Преобразование encryptionKey в массив байтов
    for (int i = 0; i < 16; i++) {
        key[i] = encryptionKey[i];
    }
    mbedtls_aes_setkey_dec(&aes, key, 128);

    // Декодирование времени работы
    uint8_t input[16];
    uint8_t output[16];
    for (int i = 0; i < 16; i++) {
        input[i] = encryptedTime[i];
    }

    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, input, output);
    mbedtls_aes_free(&aes);

    // Преобразуем результат обратно в int
    int decryptedTime = 0;
    for (int i = 0; i < 4; i++) {
        decryptedTime = (decryptedTime << 8) + output[i];
    }

    return decryptedTime;
}

