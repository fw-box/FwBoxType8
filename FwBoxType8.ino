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

#define DEVICE_TYPE 8
#define FIRMWARE_VERSION "1.1.6"

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
// Global variable
//

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

void setup()
{
  Wire.begin();  // Join IIC bus for Light Sensor (BH1750).
  Serial.begin(115200);

  pinMode(LED_BUILTIN, OUTPUT);

  //
  // Initialize the fw-box core (early stage)
  //
  fbEarlyBegin(DEVICE_TYPE, FIRMWARE_VERSION);

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
