
#include <Wire.h>
#include <DHT.h>

// ===== 腳位設定 =====
#define SOIL_PIN 2
#define DHT_PIN 21
#define DHT_TYPE DHT22
#define LED_PIN 19

// ===== 建立 DHT22 =====
DHT dht(DHT_PIN, DHT_TYPE);

// ===== GY-302 位址 =====
byte lightAddress = 0;

// ===== LED 控制變數 =====
bool ledState = false;
unsigned long lastLedTime = 0;

// ===== 感測器控制變數 =====
unsigned long lastSensorTime = 0;

void setup() {
  Serial.begin(115200);

  // 啟動 DHT22
  dht.begin();

  // 初始化 LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // 啟動 I2C
  Wire.begin();

  Serial.println("系統啟動！");

  // ===== 尋找 GY-302 =====
  Wire.beginTransmission(0x23);

  if (Wire.endTransmission() == 0) {
    lightAddress = 0x23;
  } else {
    Wire.beginTransmission(0x5C);

    if (Wire.endTransmission() == 0) {
      lightAddress = 0x5C;
    }
  }

  // ===== 初始化 GY-302 =====
  if (lightAddress != 0) {

    Wire.beginTransmission(lightAddress);
    Wire.write(0x10);

    if (Wire.endTransmission() == 0) {
      Serial.println("GY-302 初始化成功");
    } else {
      Serial.println("GY-302 初始化失敗");
      lightAddress = 0;
    }

  } else {
    Serial.println("找不到 GY-302");
  }
}

void loop() {

  // ===== 1. LED 持續閃爍 =====
  if (millis() - lastLedTime >= 500) {

    lastLedTime = millis();

    // 切換 LED 狀態
    ledState = !ledState;

    digitalWrite(LED_PIN, ledState);
  }

  // ===== 2. 每 2 秒讀取感測器 =====
  if (millis() - lastSensorTime >= 2000) {

    lastSensorTime = millis();

    Serial.println("----------------------");

    // 讀取土壤濕度
    int soilValue = analogRead(SOIL_PIN);

    Serial.print("土壤濕度原始值：");
    Serial.println(soilValue);

    // 讀取 DHT22
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();

    if (isnan(temperature) || isnan(humidity)) {
      Serial.println("DHT22 讀取失敗");
    } else {
      Serial.print("溫度：");
      Serial.print(temperature);
      Serial.println(" °C");

      Serial.print("濕度：");
      Serial.print(humidity);
      Serial.println(" %");
    }

    // 讀取 GY-302
    if (lightAddress != 0) {

      Wire.requestFrom(lightAddress, (uint8_t)2);

      if (Wire.available() == 2) {

        int highByte = Wire.read();
        int lowByte = Wire.read();

        int rawLight = (highByte << 8) | lowByte;

        float lux = rawLight / 1.2;

        Serial.print("光照度：");
        Serial.print(lux);
        Serial.println(" lux");

      } else {
        Serial.println("GY-302 讀取失敗");
      }

    } else {
      Serial.println("GY-302 未初始化");
    }

    Serial.println("----------------------");
  }
}
