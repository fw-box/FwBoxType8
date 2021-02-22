//
// Copyright (c) 2020 Fw-Box (https://fw-box.com)
// Author: Hartman Hsieh
//
// Description :
//   None
//
// Connections :
//
// Required Library :
//   https://github.com/beegee-tokyo/DHTesp
//   https://github.com/claws/BH1750
//

#include "FwBox.h"
#include "DHTesp.h" // DHT11 or DHT22
#include <BH1750.h> // Light Sensor (BH1750)
#include "FwBox_UnifiedLcd.h"
#include <U8g2lib.h>
#include <WiFiUdp.h> // For NTP function
#include "time.h" // ESP8266 time function support
#include <TimeLib.h> // Library for Time function

#define DEVICE_TYPE 8
#define FIRMWARE_VERSION "1.1"

//
// Debug definitions
//
#define FW_BOX_DEBUG 1

#if FW_BOX_DEBUG == 1
  #define DBG_PRINT(VAL) Serial.print(VAL)
  #define DBG_PRINTLN(VAL) Serial.println(VAL)
  #define DBG_PRINTF(FORMAT, ARG) Serial.printf(FORMAT, ARG)
#else
  #define DBG_PRINT(VAL)
  #define DBG_PRINTLN(VAL)
  #define DBG_PRINTF(FORMAT, ARG)
#endif

//
// PIN settings
//
#define PIN_DHT 0 // D3 // 溫溼度感測器 (DHT11 or DHT22)
#define PIN_BUTTON 2 // D4 // 按鍵
#define PIN_POT A0 // 旋鈕

//
// 函式定義
//

time_t getNtpTime();
void sendNtpPacket(IPAddress &address);

//
// Global variable
//

//
// NTP 設定
//
//static const char NTP_SERVER[] = "us.pool.ntp.org";
static const char NTP_SERVER[] = "tw.pool.ntp.org";
//static const char NTP_SERVER[] = "pool.ntp.org";
const int TIME_ZONE = 8; // Taipei Time
const unsigned int UDP_LOCAL_PORT = 8888;  // local port to listen for UDP packets
const char* WEEK_DAY_NAME[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"}; // Days Of The Week 

WiFiUDP Udp;

//
// LCD 1602
//
FwBox_UnifiedLcd* Lcd = 0;

//
// OLED 128x128
//
U8G2_SSD1327_MIDAS_128X128_1_HW_I2C* u8g2 = 0;

//
// DHT11 or DHT22
//
DHTesp SensorDht;

//
// Light Sensor (BH1750)
//
BH1750 SensorLight;

//
// The sensor's values
//
float HumidityValue = 0.0;
float TemperatureValue = 0.0;
float LightValue = 0.0;

unsigned long ReadingTime = 0;

int DisplayMode = 0;

void setup()
{
  Wire.begin();  // Join IIC bus for Light Sensor (BH1750).
  Serial.begin(9600);

  pinMode(LED_BUILTIN, OUTPUT);

  //
  // Initialize the fw-box core (early stage)
  //
  fbEarlyBegin(DEVICE_TYPE, FIRMWARE_VERSION);

  //
  // Initialize the LCD1602
  //
  Lcd = new FwBox_UnifiedLcd(16, 2);
  if(Lcd->begin() != 0) {
    //
    // LCD1602 doesn't exist, delete it.
    //
    delete Lcd;
    Lcd = 0;
    DBG_PRINT("LCD1602 initialization failed.");
  }

  //
  // Detect the I2C address of OLED.
  //
  Wire.beginTransmission(0x78>>1);
  uint8_t data8 = Wire.endTransmission();
  if (data8 == 0) {
    //
    // Initialize OLED
    //
    u8g2 = new U8G2_SSD1327_MIDAS_128X128_1_HW_I2C(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);  /* Uno: A4=SDA, A5=SCL, add "u8g2.setBusClock(400000);" into setup() for speedup if possible */
    u8g2->begin();
    u8g2->enableUTF8Print();
    u8g2->setFont(u8g2_font_unifont_t_chinese1);  // use chinese2 for all the glyphs of "你好世界"
  }
  else {
    DBG_PRINT("U8G2_SSD1327_MIDAS_128X128_1_HW_I2C is not found.");
    u8g2 = 0;
  }

  //
  // Display the screen
  //
  display(analogRead(A0)); // Read 'A0' to change the display mode.

  //
  // Hard code the default unit strings.
  // If no internet, device will use the strings to display.
  //
  FwBoxIns.setValUnit(0, "°C");
  FwBoxIns.setValUnit(1, "%");
  FwBoxIns.setValUnit(2, "Lux");

  //
  // Initialize the fw-box core
  //
  fbBegin(DEVICE_TYPE, FIRMWARE_VERSION);

  //
  // Setup the MQTT subscribe callback
  //
  setRcvValueCallback(onReceiveValue);

  //
  // Initialize the DHT Sensor
  //
  SensorDht.setup(PIN_DHT, DHTesp::AUTO_DETECT);

  //
  // Initialize the Light Sensor
  //
  SensorLight.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);

  //
  // Update the reading time.
  //
  ReadingTime = millis();

  //
  // 設定 NTP 時間同步
  //
  DBG_PRINTLN("Starting UDP");
  Udp.begin(UDP_LOCAL_PORT);
  DBG_PRINT("Local port: ");
  DBG_PRINTLN(Udp.localPort());
  DBG_PRINTLN("waiting for sync");
  setSyncProvider(getNtpTime); // Set the external time provider
  setSyncInterval(36000); // Set the number of seconds between re-sync

} // void setup()

void loop()
{
  if((millis() - ReadingTime) > 2000) {
    DBG_PRINT("Device UUID is ");
    DBG_PRINTLN(FwBoxIns.getDeviceConfig()->Uuid);
    DBG_PRINT("Device Type is ");
    DBG_PRINTLN(FwBoxIns.getDeviceConfig()->Type);

    //
    // Read the sensors
    //
    read();

    //
    // Check if any reads failed.
    //
    if(isnan(HumidityValue) || isnan(TemperatureValue)) {
    }
    else {
      //
      // Filter the wrong values.
      //
      if( (TemperatureValue > 1) &&
          (TemperatureValue < 70) && 
          (HumidityValue > 10) &&
          (HumidityValue < 95) ) {
  
        FwBoxIns.setValue(0, TemperatureValue);
        FwBoxIns.setValue(1, HumidityValue);
      }
    }

    if(LightValue > 0) {
      FwBoxIns.setValue(2, LightValue);
    }

    ReadingTime = millis();
  }

  //
  // LED status
  //
  FwBoxIns.setValue(3, digitalRead(LED_BUILTIN));

  //
  // Display the screen
  //
  display(analogRead(A0)); // Read 'A0' to change the display mode.

  //
  // Run the handle
  //
  fbHandle();

} // END OF "void loop()"

void read()
{
  TempAndHumidity th = SensorDht.getTempAndHumidity();

  //
  // Check if any reads failed.
  //
  if(SensorDht.getStatus() == 0) {
    TemperatureValue = th.temperature;
    HumidityValue = th.humidity;
    DBG_PRINTF("Temperature : %f C\n", TemperatureValue);
    DBG_PRINTF("Humidity : %f %%\n", HumidityValue);
  }
  else {
    DBG_PRINTLN("DHT11 error status : " + String(SensorDht.getStatusString()));
  }

  //
  // Read the Light level (Unit:Lux)
  //
  LightValue = SensorLight.readLightLevel();
  if(LightValue > 0) {
    DBG_PRINTF("Light : %f Lux\n", LightValue);
  }
}

void display(int analogValue)
{
  //
  // Change the display mode according to the value of PIN - 'A0'.
  //
  DisplayMode = getDisplayMode(5, analogValue);

  //
  // Draw the LCD1602
  //
  if(Lcd != 0) {
    switch(DisplayMode) { 
      case 1:
        LcdDisplayType1();
        break;
      case 2:
        LcdDisplayType2();
        break;
      case 3:
        LcdDisplayType3();
        break;
      case 4:
        LcdDisplayType4();
        break;
      case 5:
        LcdDisplayType5();
        break;
      default:
        LcdDisplayType1();
        break;
    }
  }

  //
  // Draw the OLED
  //
  if(u8g2 != 0) {
    u8g2->setFont(u8g2_font_unifont_t_chinese2);
    u8g2->firstPage();
    do {
      String line0 = String(TemperatureValue) + " " + FwBoxIns.getValUnit(0);
      u8g2->setCursor(5, 20);
      u8g2->print(line0);

      String line1 = String(HumidityValue) + " " + FwBoxIns.getValUnit(1);
      u8g2->setCursor(5, 55);
      u8g2->print(line1);

      String line2 = String(LightValue) + " " + FwBoxIns.getValUnit(2);
      u8g2->setCursor(5, 90);
      u8g2->print(line2);
    } while ( u8g2->nextPage() );
  }
}

int getDisplayMode(int pageCount,int analogValue)
{
  int page_width = 1024 / pageCount;

  for (int i = 0; i < pageCount; i++) {
    if (i == 0) {
      if (analogValue < (page_width*(i+1))-5) { // The value - '5' is for debouncing.
        return i + 1;
      }
    }
    else if (i == (pageCount - 1)) {
      if (analogValue >= (page_width*i)+5) { // The value - '5' is for debouncing.
        return i + 1;
      }
    }
    else {
      if ((analogValue >= (page_width*i)+5) && (analogValue < (page_width*(i+1))-5)) { // The value - '5' is for debouncing.
        return i + 1;
      }
    }
  }

  return 1; // default page
}

//
// Display Date, Time, Humidity and Temperature
// 顯示日期，時間，溼度與溫度。
//
void LcdDisplayType1()
{
  ComfortState cf;
  float cr = SensorDht.getComfortRatio(cf, TemperatureValue, HumidityValue);

  //
  // Skip the wrong values.
  //
  if(TemperatureValue == 0 || HumidityValue == 0) {
    cf = Comfort_OK;
  }

  //
  // Print YEAR-MONTH-DAY
  //
  Lcd->setCursor(0, 0);
  //Lcd->print(year());
  //Lcd->print("-");   
  PrintLcdDigits(month());
  Lcd->print("-");   
  PrintLcdDigits(day());
  Lcd->print(" ");
  Lcd->print(WEEK_DAY_NAME[weekday() - 1]);
  Lcd->print("  ");
  Lcd->setCursor(16 - 5, 0); // Align right
  Lcd->printf("%2.1f", TemperatureValue);
  Lcd->print("C");

  //
  // Print HOUR:MIN:SEC WEEK
  //
  Lcd->setCursor(0, 1);
  PrintLcdDigits(hour());
  Lcd->print(":");
  PrintLcdDigits(minute());
  Lcd->print(":");    
  PrintLcdDigits(second());
  Lcd->print(" ");
  if((cf == Comfort_TooDry) || (cf == Comfort_HotAndDry) || (cf == Comfort_ColdAndDry))
    Lcd->print("DRY ");
  else if((cf == Comfort_TooHumid) || (cf == Comfort_HotAndHumid) || (cf == Comfort_ColdAndHumid))
    Lcd->print("WET ");
  else
    Lcd->print("    ");
  Lcd->setCursor(16 - 3, 1); // Align right
  Lcd->printf("%2d", (int)HumidityValue);
  Lcd->print("%");
}

//
// Display Date and Time
// 顯示日期與時間。
//
void LcdDisplayType2()
{
  //
  // Print YEAR-MONTH-DAY
  //
  Lcd->setCursor(0, 0);
  Lcd->print("   ");
  Lcd->print(year());
  Lcd->print("-");   
  PrintLcdDigits(month());
  Lcd->print("-");   
  PrintLcdDigits(day());
  Lcd->print("   ");

  //
  // Print HOUR:MIN:SEC WEEK
  //
  Lcd->setCursor(0, 1);
  Lcd->print("  ");
  PrintLcdDigits(hour());
  Lcd->print(":");
  PrintLcdDigits(minute());
  Lcd->print(":");    
  PrintLcdDigits(second());
  Lcd->print(" ");
  Lcd->print(WEEK_DAY_NAME[weekday() - 1]);
  Lcd->print("  ");
}

//
// Display Humidity and Temperature
// 顯示溼度與溫度。
//
void LcdDisplayType3()
{
  Lcd->setCursor(0, 0);
  Lcd->print("Temp:      ");
  Lcd->print(TemperatureValue, 1);
  Lcd->print("C");
  Lcd->setCursor(0, 1);
  Lcd->print("Humidity:    ");
  Lcd->print(HumidityValue, 0);
  Lcd->print("%");
}

//
// Display light level
// 顯示環境亮度。
//
void LcdDisplayType4()
{
  //
  // Print HOUR:MIN:SEC WEEK
  //
  Lcd->setCursor(0, 0);
  Lcd->print("  ");
  PrintLcdDigits(hour());
  Lcd->print(":");
  PrintLcdDigits(minute());
  Lcd->print(":");    
  PrintLcdDigits(second());
  Lcd->print(" ");
  Lcd->print(WEEK_DAY_NAME[weekday() - 1]);
  Lcd->print("  ");

  Lcd->setCursor(0, 1);
  Lcd->print("Light:");
  Lcd->printf("%7d LX", (int)LightValue); // Align right
}

//
// Display the information
//
void LcdDisplayType5()
{
  //
  // Print the device ID and the firmware version.
  //
  Lcd->setCursor(0, 0);
  Lcd->print("ID:");
  Lcd->print(FwBoxIns.getSimpleChipId());
  Lcd->print("     v");
  Lcd->print(FIRMWARE_VERSION);
  Lcd->print("    ");

  //
  // Display the local IP address.
  //
  Lcd->setCursor(0, 1);
  if (WiFi.status() == WL_CONNECTED) {
    String ip = WiFi.localIP().toString();
    int space_len = 16 - ip.length();
    Lcd->print(ip);

    //
    // Fill the char space at the end.
    //
    for(int i = 0; i < space_len; i++)
      Lcd->print(" ");
  }
  else {
    Lcd->print("                ");
  }
}

void onReceiveValue(int valueIndex, String* payload)
{
  DBG_PRINT("onReceiveValue valueIndex = ");
  DBG_PRINTLN(valueIndex);
  DBG_PRINT("onReceiveValue *payload = ");
  DBG_PRINTLN(*payload);

  if(valueIndex == 3) {
    payload->toUpperCase();
    if(payload->equals("ON") == true)
    {
      digitalWrite(LED_BUILTIN, LOW);
    }
    else
    {
      digitalWrite(LED_BUILTIN, HIGH);
    }
  }
}


void PrintLcdDigits(int digits)
{
  if (digits < 10)
    Lcd->print('0');
  Lcd->print(digits);
}


//
// NTP code
//
const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime()
{
  IPAddress ntpServerIP; // NTP server's ip address

  for (int i = 0; i < 6; i++) { // Retry 6 times if "No NTP Response"
    while (Udp.parsePacket() > 0) ; // discard any previously received packets
    DBG_PRINTLN("Transmit NTP Request");
    // get a random server from the pool
    WiFi.hostByName(NTP_SERVER, ntpServerIP);
    DBG_PRINT(NTP_SERVER);
    DBG_PRINT(": ");
    DBG_PRINTLN(ntpServerIP);
    sendNtpPacket(ntpServerIP);
    uint32_t beginWait = millis();
    while (millis() - beginWait < 1500) {
      int size = Udp.parsePacket();
      if (size >= NTP_PACKET_SIZE) {
        DBG_PRINTLN("Receive NTP Response");
        Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
        unsigned long secsSince1900;
        // convert four bytes starting at location 40 to a long integer
        secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
        secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
        secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
        secsSince1900 |= (unsigned long)packetBuffer[43];
        return secsSince1900 - 2208988800UL + TIME_ZONE * SECS_PER_HOUR;
      }
    }
    DBG_PRINTLN("No NTP Response :-(");
    delay(500);
  }
  return 0; // return 0 if unable to get the time
}

//
// send an NTP request to the time server at the given address
//
void sendNtpPacket(IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}
