#include <Arduino.h>
#include <M5StickC.h>
#include "DHT12.h"
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include <Wire.h>
#include "BLEDevice.h"
#include "BLEServer.h"
#include "BLEUtils.h"
#include <WiFi.h>

enum HAT {
  HAT_NONE = 0,
  HAT_ENV
};
enum UNIT {
  UNIT_NONE = 0,
  UNIT_ENV,
  UNIT_IR
};
enum ROLE {
  ROLE_NONE = 0,
  ROLE_ADV,
  ROLE_SCAN
};
struct Profile {
  int index;
  uint8_t* mac;
  HAT hat;
  UNIT unit;
  ROLE role;
};
#include "secret.h"
Profile* profile = NULL;

DHT12 dht12;
Adafruit_BMP280 bmp280;
#define LED_BUILTIN 10
RTC_DATA_ATTR static uint8_t seq = 0;

void set_led_red(bool on){
  digitalWrite(LED_BUILTIN, on ? LOW : HIGH);
}

String mac_string(const uint8_t *mac)
{
  char macStr[18] = {0};
  sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(macStr);
}

void setAdvData(BLEAdvertising *pAdvertising, float temperature, float humidity, float pressure, float vbat) {
    BLEAdvertisementData oAdvertisementData = BLEAdvertisementData();

    oAdvertisementData.setFlags(ESP_BLE_ADV_FLAG_LIMIT_DISC|ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);

    int16_t t = temperature *  100;
    int16_t h = humidity    *  100;
    int16_t p = pressure    *   10;
    int16_t v = vbat;

    std::string strServiceData = "";
    strServiceData += (char)0x0c;   // 長さ
    strServiceData += (char)0xff;   // AD Type 0xFF: Manufacturer specific data
    strServiceData += (char)0xff;   // Test manufacture ID low byte
    strServiceData += (char)0xff;   // Test manufacture ID high byte
    strServiceData += (char)seq;                // シーケンス番号
    strServiceData += (char)( t & 0xff);        // 温度の下位バイト
    strServiceData += (char)((t >> 8) & 0xff);  // 温度の上位バイト
    strServiceData += (char)( h & 0xff);        // 湿度の下位バイト
    strServiceData += (char)((h >> 8) & 0xff);  // 湿度の上位バイト
    strServiceData += (char)( p & 0xff);        // 気圧の下位バイト
    strServiceData += (char)((p >> 8) & 0xff);  // 気圧の上位バイト
    strServiceData += (char)( v & 0xff);        // 電池電圧の下位バイト
    strServiceData += (char)((v >> 8) & 0xff);  // 電池電圧の上位バイト

    oAdvertisementData.addData(strServiceData);
    pAdvertising->setAdvertisementData(oAdvertisementData);

    printf("seq=%d temperature=%4.1f humidity=%4.1f pressure=%6.1f vbat=%6.1f\n", seq, temperature, humidity, pressure, vbat);
}

void advertise_and_sleep() {
  if(0){
    M5.begin();
  }else{
    M5.Axp.begin();
    M5.Axp.ScreenBreath(0);
    M5.Lcd.begin();
    M5.Rtc.begin();
    M5.Lcd.fillScreen(BLACK);
  }

  pinMode(LED_BUILTIN, OUTPUT);
  set_led_red(false);

  if(profile->hat==HAT_ENV){
    Wire.begin(0, 26); // HAT-pin G0,G26. For dht12 and bme.
  }
  //Wire.begin(26, 32); // M5Atom Grove 26,32
  if(profile->unit==UNIT_ENV){
    Wire.begin(32, 33); // GROVE. For ENV Unit
  }

  float temperature = -273.15;
  float humidity    = -1;
  float pressure    = -1;

  for(int i=0; i<1000; i++){
    if(bmp280.begin(0x76)){
      pressure = bmp280.readPressure() / 100.0; // hPa
      break;
    }
    // delay(10);
  }

  for(int i=0; i<10; i++){
    if (dht12.read() == DHT12_OK)
    {
      temperature = dht12.temperature;
      humidity    = dht12.humidity;
      break;
    }
    delay(10);
  }

  BLEDevice::init("env");
  BLEServer *pServer = BLEDevice::createServer();
  BLEAdvertising *pAdvertising = pServer->getAdvertising();
  setAdvData(pAdvertising, temperature, humidity, pressure, M5.Axp.GetBatVoltage()*1000);
  pAdvertising->start();
  int advertise_sec = 5;
  delay(advertise_sec * 1000);
  pAdvertising->stop();
  seq++;
  delay(10);
  int sleep_sec = 55;
  esp_deep_sleep(1000000LL * sleep_sec);
}

void scan()
{
  static int count = 0;
  BLEDevice::init("");
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(false);
  BLEScanResults foundDevices = pBLEScan->start(3);
  for (int i=0; i<foundDevices.getCount(); i++) {
    BLEAdvertisedDevice d = foundDevices.getDevice(i);
    if (!d.haveManufacturerData()) {
      continue;
    }
    std::string data = d.getManufacturerData();
    int manu = data[1] << 8 | data[0];
    if (manu == 0xffff) {
      seq = data[2];
      float temperature = (float)(data[ 4] << 8 | data[3]) /  100.0;
      float humidity    = (float)(data[ 6] << 8 | data[5]) /  100.0;
      float pressure    = (float)(data[ 8] << 8 | data[7]) /   10.0;
      float vbat        = (float)(data[10] << 8 | data[9]);

      M5.Lcd.fillScreen(BLACK);
      //M5.Lcd.setTextFont(1);
      //M5.Lcd.setTextSize(1);
      M5.Lcd.setCursor(0, 0);
      M5.Lcd.printf("count: %d\n", count);
      M5.Lcd.printf("seq: %d\n", seq);
      M5.Lcd.printf("temp: %4.1f'C\n",  temperature);
      M5.Lcd.printf("humid:%4.1f%%\n",  humidity);
      M5.Lcd.printf("press:%4.0fhPa\n", pressure);
      M5.Lcd.printf("vbat: %6.1fmV\n",    vbat);
      printf("count=%d seq=%d temperature=%4.1f humidity=%4.1f pressure=%6.1f vbat=%6.1f\n", count, seq, temperature, humidity, pressure, vbat);
      count++;
    }
  }
}

void setup() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  printf("macAddress=%s\n", mac_string(mac).c_str());

  for(int i=0; i<PROFILE_COUNT; i++){
    if(memcmp(mac, profiles[i].mac, 6)==0){
      profile = &profiles[i];
      break;
    }
  }
  if(!profile){
    printf("profile not found.");
    esp_restart();
  }

  switch(profile->role){
  case ROLE_ADV:
    advertise_and_sleep();
    break;
  case ROLE_SCAN:
    M5.begin();
    M5.Lcd.setRotation(1);
    M5.Axp.ScreenBreath(9);
    break;
  default:
    break;
  }
}

void loop() {
  switch(profile->role){
  case ROLE_SCAN:
    scan();
    break;
  }
}
