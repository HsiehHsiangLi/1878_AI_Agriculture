#include <Wire.h>
#include <DHT.h>
#include <WiFi.h>
#include <WiFiSSLClient.h>
#include <math.h>

// ==================================================
// 請填入你的 Wi-Fi 和 ThingSpeak 資訊
// ==================================================

char wifiName[] = "AJI";
char wifiPassword[] = "aji940417";

const char writeApiKey[] = "49W0KCST453YTC4X";

// ==================================================
// 腳位設定：沿用你原本的接線
// ==================================================

#define SOIL_PIN 2
#define DHT_PIN 21
#define DHT_TYPE DHT22
#define LED_PIN 22

// ==================================================
// 建立感測器和網路物件
// ==================================================

DHT dht(DHT_PIN, DHT_TYPE);
WiFiSSLClient client;

// ThingSpeak 伺服器
const char server[] = "api.thingspeak.com";
const int HTTPS_PORT = 443;

// GY-302 I2C 位址
byte lightAddress = 0;

// ==================================================
// 時間設定
// ==================================================

// 每 2 秒讀取一次感測器
const unsigned long SENSOR_INTERVAL = 2000;

// 每次上傳流程結束後，等待至少 20 秒再上傳
const unsigned long UPLOAD_INTERVAL = 20000;

// LED 亮或暗各維持 200 毫秒
const unsigned long LED_INTERVAL = 200;

unsigned long lastSensorTime = 0;
unsigned long lastUploadTime = 0;

// ==================================================
// 感測器資料
// ==================================================

int soilValue = 0;
float temperature = NAN;
float humidity = NAN;
float lux = NAN;

// ==================================================
// LED 閃爍控制
// ==================================================

bool ledBlinking = false;
bool ledState = false;

int blinkCount = 0;
const int TOTAL_BLINKS = 10;

unsigned long lastLedTime = 0;

// 開始閃爍：先亮起，接著由 updateLedBlink() 控制
void startLedBlink() {
  blinkCount = 0;
  ledBlinking = true;
  ledState = true;

  digitalWrite(LED_PIN, HIGH);
  lastLedTime = millis();

  Serial.println("LED 開始閃爍 10 下");
}

// 更新 LED 狀態，每完成一次亮到暗就算閃爍一下
void updateLedBlink() {
  if (!ledBlinking) {
    return;
  }

  unsigned long now = millis();

  if (now - lastLedTime >= LED_INTERVAL) {
    lastLedTime = now;

    if (ledState) {
      // 由亮轉暗，完成一下
      ledState = false;
      digitalWrite(LED_PIN, LOW);
      blinkCount++;

      if (blinkCount >= TOTAL_BLINKS) {
        ledBlinking = false;
        Serial.println("LED 已閃爍 10 下");
      }
    } else {
      // 由暗轉亮
      ledState = true;
      digitalWrite(LED_PIN, HIGH);
    }
  }
}

// 上傳流程提前結束時，讓 LED 繼續閃完 10 下
void finishLedBlink() {
  while (ledBlinking) {
    updateLedBlink();
    delay(10);
  }

  digitalWrite(LED_PIN, LOW);
}

// ==================================================
// 連接 Wi-Fi
// ==================================================

bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  Serial.println("正在連接 Wi-Fi...");

  int status = WiFi.begin(wifiName, wifiPassword);

  if (status == WL_CONNECTED) {
    Serial.println("Wi-Fi 連線成功");

    Serial.print("IP 位址：");
    Serial.println(WiFi.localIP());

    return true;
  }

  Serial.println("Wi-Fi 連線失敗");
  return false;
}

// ==================================================
// 尋找並初始化 GY-302
// ==================================================

void setupLightSensor() {
  // 先嘗試位址 0x23
  Wire.beginTransmission(0x23);

  if (Wire.endTransmission() == 0) {
    lightAddress = 0x23;
  } else {
    // 再嘗試位址 0x5C
    Wire.beginTransmission(0x5C);

    if (Wire.endTransmission() == 0) {
      lightAddress = 0x5C;
    }
  }

  if (lightAddress == 0) {
    Serial.println("找不到 GY-302");
    return;
  }

  // 0x10：連續高解析度模式
  Wire.beginTransmission(lightAddress);
  Wire.write(0x10);

  if (Wire.endTransmission() == 0) {
    Serial.println("GY-302 初始化成功");
  } else {
    Serial.println("GY-302 初始化失敗");
    lightAddress = 0;
  }
}

// ==================================================
// 讀取感測器
// ==================================================

void readSensors() {
  Serial.println("----------------------");

  // 讀取土壤濕度原始值
  soilValue = analogRead(SOIL_PIN);

  Serial.print("土壤濕度原始值：");
  Serial.println(soilValue);

  // 讀取 DHT22
  temperature = dht.readTemperature();
  humidity = dht.readHumidity();

  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("DHT22 讀取失敗");
  } else {
    Serial.print("溫度：");
    Serial.print(temperature);
    Serial.println(" °C");

    Serial.print("空氣濕度：");
    Serial.print(humidity);
    Serial.println(" %");
  }

  // 先清除舊值，避免讀取失敗時繼續使用舊資料
  lux = NAN;

  if (lightAddress != 0) {
    Wire.requestFrom(lightAddress, (uint8_t)2);

    if (Wire.available() == 2) {
      int highByte = Wire.read();
      int lowByte = Wire.read();

      unsigned int rawLight =
        ((unsigned int)highByte << 8) |
        (unsigned int)lowByte;

      lux = rawLight / 1.2f;

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

// ==================================================
// 上傳資料到 ThingSpeak
// ==================================================

void uploadToThingSpeak() {
  Serial.println("開始 ThingSpeak 上傳流程");

  // 每次嘗試上傳時，開始閃爍 10 下
  startLedBlink();

  // 如果 Wi-Fi 斷線，先嘗試重新連線
  if (WiFi.status() != WL_CONNECTED) {
    if (!connectWiFi()) {
      Serial.println("Wi-Fi 未連線，本次上傳失敗");
      finishLedBlink();
      return;
    }
  }

  // 組合要上傳的欄位
  String body = "";

  // field1：溫度
  if (!isnan(temperature)) {
    body += "field1=";
    body += String(temperature, 2);
  }

  // field2：空氣濕度
  if (!isnan(humidity)) {
    if (body.length() > 0) {
      body += "&";
    }

    body += "field2=";
    body += String(humidity, 2);
  }

  // field3：光照度
  if (!isnan(lux)) {
    if (body.length() > 0) {
      body += "&";
    }

    body += "field3=";
    body += String(lux, 2);
  }

  // field4：土壤濕度原始值
  if (body.length() > 0) {
    body += "&";
  }

  body += "field4=";
  body += String(soilValue);

  Serial.println("正在連接 ThingSpeak...");

  // 連線至 ThingSpeak HTTPS
  if (!client.connect(server, HTTPS_PORT)) {
    Serial.println("ThingSpeak 連線失敗");
    client.stop();
    finishLedBlink();
    return;
  }

  // 使用 GET 上傳四個欄位
  // body 已包含前面組合好的 field1～field4
  client.print("GET /update?api_key=");
  client.print(writeApiKey);
  client.print("&");
  client.print(body);
  client.println(" HTTP/1.1");

  client.print("Host: ");
  client.println(server);

  client.println("Connection: close");
  client.println();

  Serial.println("資料已送出，正在等待 ThingSpeak 回應：");

  // 等待伺服器回應，最多等待 10 秒。
  // 等待時同時更新 LED 閃爍。
  unsigned long responseStart = millis();

  while (millis() - responseStart < 10000) {
    updateLedBlink();

    while (client.available()) {
      char c = client.read();
      Serial.write(c);

      // 收到資料後，重新計算等待時間
      responseStart = millis();

      updateLedBlink();
    }

    if (!client.connected() && !client.available()) {
      break;
    }

    delay(10);
  }

  client.stop();

  Serial.println();
  Serial.println("ThingSpeak 回應接收結束");

  // 若回應很快結束，LED 繼續閃完剩餘次數
  finishLedBlink();

  Serial.println("本次上傳流程結束");
}

// ==================================================
// setup：只執行一次
// ==================================================
void setup() {
  Serial.begin(115200);

  // LED 初始化，預設熄滅
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // 啟動 DHT22
  dht.begin();

  // 啟動 I2C
  Wire.begin();

  Serial.println("系統啟動！");

  // 初始化 GY-302
  setupLightSensor();

  // 連接 Wi-Fi
  connectWiFi();

  // 啟動後先讀取一次感測器
  readSensors();

  lastSensorTime = millis();
  lastUploadTime = millis();
}

// ==================================================
// loop：持續執行
// ==================================================

void loop() {
  unsigned long now = millis();

  // 每 2 秒讀取一次感測器
  if (now - lastSensorTime >= SENSOR_INTERVAL) {
    lastSensorTime = now;
    readSensors();
  }

  // 每次上傳流程結束後，至少等待 20 秒
  if (now - lastUploadTime >= UPLOAD_INTERVAL) {
    uploadToThingSpeak();

    // 從上傳流程結束後開始計時
    lastUploadTime = millis();
  }
}