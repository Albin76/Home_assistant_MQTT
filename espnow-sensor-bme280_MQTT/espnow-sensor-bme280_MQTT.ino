/*
 New MQTT version started 2019-04-24. To be used against MQTT instead of Thingspeak.
 
 ESP-NOW based sensor using a BME280 temperature/pressure/humidity sensor
 Sends readings every 15 minutes to a server with a fixed mac address
 Program based on ESPNOW example.
 Hardware based on a simplified Kevin Darrahs Trigboard V4 but with components used in JonathanCaes PCB in Easyeda to get more handable packages for Mosfets.
 April 16 2019: Needs esp8266 core 2.5 to get down to fast execution.
 
 To do: 
 x Battery monitoring
   x Add battery in struct
   x Add monitoring
 o Send to Hassio (MQTT) instead as alternative (New version)
 o Clean up serial print  
 o Test actual timing
 
*/
#include <ESP8266WiFi.h>
extern "C" {
  #include <espnow.h>
}
//#include "SparkFunBME280.h"
#include <forcedClimate.h>  // This library is really fast! Version 3.0 https://github.com/JVKran/Forced-BME280
#include "arduino_secrets.h"  // In Arduino_secret: MQTT_CLIENT_ID, MQTT_SENSOR_NO, MQTT_SENSOR_TOPIC

#define WIFI_CHANNEL 1
#define SLEEP_SECS 60 // 1 minute. Can be removed for TPL5111
#define SLEEP_SECS_SHORT 60 // 1 minute. Short due to sporadic internal errors. Used when failed BME280
#define SLEEP_SECS_LONG 300 // 5 minute. Longer due to external problems.       Used when failed wifi or failed espnow init
#define SEND_TIMEOUT 300    // 245 millis seconds timeout. 
//#define USING_BATTERY
#define USING_DEEPSLEEP
//#define USING_TPL5110
//#define USING_TPL5110_BARE

// keep in sync with slave struct
struct __attribute__((packed)) SENSOR_DATA {
    int   sensor;
    char  MQTT_sensor_topic[15];
    unsigned long   millis;
    float temp;
    float humidity;
    float pressure;
    float battery;
    int   spare1;
    float spare2;
} sensorData; 


//BME280 bme280;

// This is the MAC Address of the remote ESP server which receives these sensor readings
uint8_t remoteMac[] = {0x36, 0x33, 0x33, 0x33, 0x33, 0x33};

// for battery reading
const byte battLevelPin = A0;     //Used if Battery level is checked
const float batteryCorrection = 5.10/1023; // Needs to increase from 5.0 to 5.1 to get accurate reading
int sortValues[13]; // Used in sorting (Takes 13 readings and takes mean value)
float battLevelRaw; // needed in calculation 

// for callback handling
uint8_t result = 1; // Result from callback. 0 is OK and 1 is Error
volatile boolean callbackCalled;


#ifdef USING_TPL5110
const byte TPL5110DONEPIN = D1; // Used for TPL5110 (D1 for Wemos, 5 for esp8266
// have not beem able to get the TPL5110 to work with any pins other than D1 and D2 so I2C needed to move from those pins. Used Wire.begin(D5,D6) for that before starting sensor.
#endif
#ifdef USING_TPL5110_BARE
const byte TPL5110DONEPIN = 5; // Used for TPL5110 (D1 for Wemos, 5 for esp8266
// have not beem able to get the TPL5110 to work with any pins other than D1 and D2 so I2C needed to move from those pins. Used Wire.begin(D5,D6) for that before starting sensor.
#endif

ForcedClimate climateSensor = ForcedClimate(Wire, 0x76);

void setup() {
  #ifdef USING_TPL5110
    pinMode(TPL5110DONEPIN, OUTPUT);
    digitalWrite(TPL5110DONEPIN, LOW);
  #endif
  #ifdef USING_TPL5110_BARE
    pinMode(TPL5110DONEPIN, OUTPUT);
    digitalWrite(TPL5110DONEPIN, LOW);
  #endif  

 
// For new struct.
  sensorData.millis = 0;
  sensorData.spare1 = 0.0;
  sensorData.spare2 = 0.0;

  WiFi.persistent(false); // Shall save time See https://arduinodiy.wordpress.com/2020/02/06/very-deep-sleep-and-energy-saving-on-esp8266-part-5-esp-now/
  sensorData.sensor = MQTT_SENSOR_NO;
  //sensorData.channelID = 275152;
  strcpy(sensorData.MQTT_sensor_topic, MQTT_SENSOR_TOPIC);
  
  Serial.begin(115200); Serial.println();

  #ifdef USING_BATTERY
  Serial.printf("Before Battery reading: %i\n", millis());
  batteryreading();
  Serial.printf("Before BME280 read: %i\n", millis());
  #endif

  // read sensor first before awake generates heat
  //readBME280();  // Change to code made in ESP32
  readBME280forced();  // Forced
  checkreadings();
  Serial.printf("After BME280 read: %i\n", millis());

  // readded WIFI_STA and Disconnect 2020-12-22
  // https://randomnerdtutorials.com/esp-now-many-to-one-esp8266-nodemcu/
  WiFi.mode(WIFI_STA); // Station mode for esp-now sensor node. These are not in the minimal example. Takes long time. +60ms. Needed since not in minimal?
  WiFi.disconnect();
  //Serial.printf("After WIFI settings: %i\n", millis());
  
  Serial.printf("This mac: %s, ", WiFi.macAddress().c_str()); 
  Serial.printf("target mac: %02x%02x%02x%02x%02x%02x", remoteMac[0], remoteMac[1], remoteMac[2], remoteMac[3], remoteMac[4], remoteMac[5]); 
  Serial.printf(", channel: %i\n", WIFI_CHANNEL); 

  if (esp_now_init() != 0) {         
    Serial.println("*** ESP_Now init failed");  // If failed then go to sleep.
    gotoSleep(SLEEP_SECS_SHORT);
  }
  Serial.printf("Before Send: %i\n", millis());
  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  esp_now_add_peer(remoteMac, ESP_NOW_ROLE_SLAVE, WIFI_CHANNEL, NULL, 0);
  
  esp_now_register_send_cb([](uint8_t* mac, uint8_t result2){    // esp_now_register_send_cb to register sending callback function. 
    result=result2;  // 0 is OK, 1 is Error
    Serial.printf("Callback recieved with status = %i (0 is OK and 1 is Error). Time: %i \n", result, millis());  // 
    callbackCalled = true;  //Set to true ehen callback is called
  });

  callbackCalled = false; // Set to false in begining

  sensorData.millis=millis();
  uint8_t bs[sizeof(sensorData)];
  Serial.printf("Size of sensorData: ");
  Serial.println(sizeof(sensorData));
  memcpy(bs, &sensorData, sizeof(sensorData));
  Serial.printf("Sending the following data: ");  
//  Serial.printf("sensor=%i, channelID=%i, MQTT_sensor_topic=%s, temp=%01f, humidity=%01f, pressure=%01f, battery=%01f\n", sensorData.sensor, sensorData.channelID, sensorData.MQTT_sensor_topic, sensorData.temp, sensorData.humidity, sensorData.pressure, sensorData.battLevelNow);
  Serial.printf("sensor=%i, MQTT_sensor_topic=%s, millis=%i, temp=%01f, humidity=%01f, pressure=%01f, battery=%01f, spare1=%i, spare2=%01f, \n", sensorData.sensor, sensorData.MQTT_sensor_topic, sensorData.millis, sensorData.temp, sensorData.humidity, sensorData.pressure, sensorData.battery, sensorData.spare1, sensorData.spare2);
  
  esp_now_send(NULL, bs, sizeof(sensorData)); // NULL means send to all peers
  
  Serial.printf("After Send: %i\n", millis());
}

// sends and then goes into a loop and waits for callbackCalled = 1 or timeout.
void loop() {  // Ändrat
  if (callbackCalled || millis() > SEND_TIMEOUT ) {  // CallbackCalled true or SEND_TIMEOUT reached. 
    if (millis() > SEND_TIMEOUT) {                   // Timeout
      Serial.printf("Send timeout, short sleep \n"); 
      gotoSleep(SLEEP_SECS_SHORT);
      }
    else if (callbackCalled && result) {             // Callback recieved and result from callback is error (1).
      Serial.printf("Callback gave error, short sleep \n"); 
      gotoSleep(SLEEP_SECS_SHORT);
      }
    else {                                           // Not timeout and callback gave ok
      Serial.printf("Callback OK and no timeout, normal sleep \n"); 
      gotoSleep(SLEEP_SECS);
      }    
  }
}

/*
// if using adafruit
void readBME280() {
  #ifdef USING_TPL5110
  Wire.begin(D5,D6); // La till för att ändra D1, D2 till D4 och D5 så att D1 kan användas av TPL5110
  #endif
  #ifdef USING_TPL5110_BARE
  Wire.begin(14,12); // La till för att ändra D1, D2 till D4 och D5 så att D1 kan användas av TPL5110
  #endif

  
  bme280.settings.commInterface = I2C_MODE;
  bme280.settings.I2CAddress = 0x76;
  bme280.settings.runMode = 2; // Forced mode with deepSleep
  bme280.settings.tempOverSample = 1;
  bme280.settings.pressOverSample = 1;
  bme280.settings.humidOverSample = 1;

  if (bme280.beginI2C() == false) //Begin communication over I2C
  {
    Serial.println("The sensor did not respond. Please check wiring. Going to sleep.");
    gotoSleep(SLEEP_SECS_SHORT);
  }
  //Serial.print("bme280 init="); Serial.println(bme280.begin(), HEX);
  sensorData.temp = bme280.readTempC();
  sensorData.humidity = bme280.readFloatHumidity();
  sensorData.pressure = bme280.readFloatPressure() / 100.0;
  Serial.printf("temp=%01f, humidity=%01f, pressure=%01f\n", sensorData.temp, sensorData.humidity, sensorData.pressure);
}
*/

void readBME280forced() {
  climateSensor.begin();
  climateSensor.takeForcedMeasurement();
  
  sensorData.temp = climateSensor.getTemperatureCelcius();
  sensorData.humidity = climateSensor.getRelativeHumidity();
  sensorData.pressure = climateSensor.getPressure();
  Serial.printf("temp=%01f, humidity=%01f, pressure=%01f\n", sensorData.temp, sensorData.humidity, sensorData.pressure);
}

// Not complete yet!
void checkreadings(){
  int var = 0;
  while (var < 3) {
    if (isnan(sensorData.temp) || isnan(sensorData.humidity) || isnan(sensorData.pressure) || sensorData.temp>50 || sensorData.temp<-40 || sensorData.humidity>100 || sensorData.humidity<0 || sensorData.pressure>13000 || sensorData.pressure<700) {
      //Serial.println("ERROR: Unrealistic sensor readings. Retrying try no:");
      Serial.printf("ERROR: Unrealistic sensor readings. Retrying. Try no: %i\n", var);
      var++;
      //readBME280();
      readBME280forced();
      } else {
      return;
      }
    }
  gotoSleep(SLEEP_SECS_SHORT);
}

void batteryreading(){
  for (byte i=0;i<12;i++)
  {
    sortValues[i]=analogRead(battLevelPin);
  }  
  sort(sortValues,13); //Pass in the values and the size.
  battLevelRaw = sortValues[6]/1.0; // Divides with 1.0 to get float
  Serial.print("Batt Level Raw: ");
  Serial.print(battLevelRaw); // Needed to use float to set decimals since not using serial.print(battLevelNow,4) directly.  
  sensorData.battery = sortValues[6]*batteryCorrection;
  Serial.print(", Batt Level Now: ");
  Serial.println(String(sensorData.battery,4)); // Needed to use float to set decimals since not using serial.print(battLevelNow,4) directly.     
  Serial.printf("battery=%01f\n", sensorData.battery);

}

// Sort is used in batteryreading
void sort(int a[], int size) {
    for(int i=0; i<(size-1); i++) {
        for(int o=0; o<(size-(i+1)); o++) {
                if(a[o] > a[o+1]) {
                    int t = a[o];
                    a[o] = a[o+1];
                    a[o+1] = t;
                }
        }
    }
}

void gotoSleep(int sleepSecs) {

  #ifdef USING_DEEPSLEEP  
  Serial.printf("Up for %i ms, going to sleep for %i secs...\n", millis(), sleepSecs); 
  Serial.println(); 
  Serial.println();  
  ESP.deepSleep(sleepSecs * 1000000, RF_NO_CAL);
  delay(1000); // Give time to get to sleep
  //ESP.deepSleepInstant(sleepSecs * 1000000, RF_NO_CAL); // Längre tid och drog mer ström med core beta!!!
  #endif

  #ifdef USING_TPL5110  
  delay(200);
  
  Serial.printf("Up for %i ms, going to TPL-sleep \n", millis());
  Serial.println(); 
  Serial.println();      
  // toggle DONE so TPL knows to cut power!
  while (1) {
  digitalWrite(TPL5110DONEPIN, HIGH);
  delay(1);
  digitalWrite(TPL5110DONEPIN, LOW);
  delay(1);
  }
  #endif

  #ifdef USING_TPL5110_BARE  
  delay(200);
  
  Serial.printf("Up for %i ms, going to TPL-sleep \n", millis()); 
  // toggle DONE so TPL knows to cut power!
  while (1) {
  digitalWrite(TPL5110DONEPIN, HIGH);
  delay(1);
  digitalWrite(TPL5110DONEPIN, LOW);
  delay(1);
  }
  #endif
  
}
