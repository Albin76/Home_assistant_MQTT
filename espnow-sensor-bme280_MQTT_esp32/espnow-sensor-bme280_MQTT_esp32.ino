/*
 ESP-NOW based sensor using a BME280 temperature/pressure/humidity sensor
 Sends readings every 15 minutes to a server with a fixed mac address that sends via MQTT to Home Assistant
 
 Program based on ESPNOW example. (from esp-now-tx-rx/esp32_transmiter)
 Hardware based on a simplified Kevin Darrahs Trigboard V4 but with components used in JonathanCaes PCB in Easyeda to get more handable packages for Mosfets.
 April 16 2019: Needs esp8266 core 2.5 to get down to fast execution if using esp8266.
 Februari 2020: firebeetle ESP32. Takes long to wake from drrpsleep. 1.1sec in total. 200 ms for code. The rest is validation. Is it possible to reduce from Arduino IDE?
 
 To do: 
 x Battery monitoring
   x Add battery in struct
   x Add monitoring
 x Send to Hassio (MQTT) instead as alternative (New version)
 o Clean up serial print  
 o Test actual timing
 o Clean up code
 o get callback to work (Was working all along, esp_now_register_send_cb(OnDataSent);) 

Deep sleep current testing:
Base:       20.9uA
No BME280 20.7uA (but connected?)
utan cofig  22.0uA (but connected?)
If BME280 disconnected 10.8uA!!!! Try and control power with mosfets

3.3V to sensor 13,5uA (with mosfet A3401)
VBAT to sensor 15,1uA (with mosfet A3401)
3.3V to sensor 21uA (without mosfet A3401)
VBAT to sensor 16uA (without mosfet A3401)
VBAT to sensor 15,7uA (without transistor A3401 but with tips Savbee)
The mosfets does not help. Only low if GND is disconnected. Only low currnt if GND disconnected or if I2C is disconnected.
3.3V to sensor 11,7uA (without mosfet A3401 and BME280 with 3.3V regulator removed and power bypassed on BME280)

*/
#include <WiFi.h>
#include <esp_now.h>
#include "SparkFunBME280.h"
#include "arduino_secrets.h"  // In Arduino_secret: MQTT_CLIENT_ID, MQTT_SENSOR_NO, MQTT_SENSOR_TOPIC

// test start Savbee
//#include "driver/adc.h"  // used in Savbee tips for lower sleepcurrent
#include <esp_wifi.h> // used in Savbee tips for lower sleepcurrent
#include <esp_bt.h> // used in Savbee tips for lower sleepcurrent
// test end

#define WIFI_CHANNEL 1
#define SLEEP_SECS 600 // 10 minute. Can be removed for TPL5110
#define SLEEP_SECS_SHORT 60 // 1 minute. Used when failed wifi or failed espnow init
#define SEND_TIMEOUT 245  // 245 millis seconds timeout. This does not work. If set to 450 it takes that. Do not get response I think. Not configured at receiver?

//#define USING_DEEPSLEEP
#define USING_DEEPSLEEP_ESP32
//#define USING_TPL5110
//#define USING_TPL5110_BARE
#define USING_SENSOR_MOSFET

// keep in sync with slave struct
struct __attribute__((packed)) SENSOR_DATA {
    int   sensor;  // intiger of teh sensor number, ie 3 equals Sensor3
    int   channelID;  // not used any more
    char  MQTT_sensor_topic[15];  // uncertain if needs to be exact length (length + 1 before)
    float temp;
    float humidity;
    float pressure;
    float battLevelNow;
} sensorData;


uint8_t remoteMac[] = {0x36, 0x33, 0x33, 0x33, 0x33, 0x33}; // this is the MAC Address of the remote ESP server which receives these sensor readings

// from esp-now-tx-rx/esp32_transmiter
esp_now_peer_info_t slave;
const uint8_t maxDataFrameSize=200;
const esp_now_peer_info_t *peer = &slave;
uint8_t dataToSend[maxDataFrameSize];

BME280 bme280;

volatile boolean callbackCalled;

// constants for battery reading
const byte battLevelPin = A0;     //Used if Battery level is checked
const float batteryCorrection = 0.00176505333664306; // Needs to increase from 5.0 to 5.1 to get accurate reading
int sortValues[13]; // Used in sorting (Takes 13 readings and takes mean value)
float battLevelRaw; // needed in calculation 
int SENDSTATUS;

#ifdef USING_TPL5110
const byte TPL5110DONEPIN = D1; // Used for TPL5110 (D1 for Wemos, 5 for esp8266
// have not beem able to get the TPL5110 to work with any pins other than D1 and D2 so I2C needed to move from those pins. Used Wire.begin(D5,D6) for that before starting sensor.
#endif
#ifdef USING_TPL5110_BARE
const byte TPL5110DONEPIN = 5; // Used for TPL5110 (D1 for Wemos, 5 for esp8266
// have not beem able to get the TPL5110 to work with any pins other than D1 and D2 so I2C needed to move from those pins. Used Wire.begin(D5,D6) for that before starting sensor.
#endif


  
void setup() {
/*  pinMode(LED_BUILTIN, OUTPUT);  // test led
  digitalWrite(LED_BUILTIN, HIGH);   // turn the LED on (HIGH is the voltage level)
  delay(100);                       // wait for a second
  digitalWrite(LED_BUILTIN, LOW);    // turn the LED off by making the voltage LOW
  delay(20);                       // wait for a second
*/  
  #ifdef USING_TPL5110
    pinMode(TPL5110DONEPIN, OUTPUT);
    digitalWrite(TPL5110DONEPIN, LOW);
  #endif
  #ifdef USING_TPL5110_BARE
    pinMode(TPL5110DONEPIN, OUTPUT);
    digitalWrite(TPL5110DONEPIN, LOW);
  #endif  
  #ifdef USING_SENSOR_MOSFET
    pinMode(25, OUTPUT);  // D2
    digitalWrite(25, HIGH);  // D2
  #endif 
  //adc_power_on(); // used in Savbee tips for lower sleepcurrent
  sensorData.sensor = MQTT_SENSOR_NO;
  sensorData.channelID = 275152;
  strcpy(sensorData.MQTT_sensor_topic, MQTT_SENSOR_TOPIC);
  
  Serial.begin(115200); Serial.println();

  print_wakeup_reason();   //Print the wakeup reason for ESP32

  setsleepsettingsesp32();
  
  Serial.printf("[TIMING] Before Battery reading: %i\n", millis());
  batteryreading();
  Serial.printf("[TIMING] Before BME280 read: %i\n", millis());
  // read sensor first before awake generates heat
  //readBME280();
  readBME280test();  //ny. Worked 440uA to 22uA!!! 10,7uA with BME280 without 3.3V regulator 
  Serial.printf("[TIMING] After BME280 read: %i\n", millis());
  //WiFi.mode(WIFI_STA); // Station mode for esp-now sensor node // these are not in the minimal example. Takes long time. +60ms
  //WiFi.disconnect();
  //Serial.printf("After WIFI settings: %i\n", millis());

  Serial.printf("[ESPNOW] This mac: %s, ", WiFi.macAddress().c_str()); 
  Serial.printf("target mac: %02x%02x%02x%02x%02x%02x", remoteMac[0], remoteMac[1], remoteMac[2], remoteMac[3], remoteMac[4], remoteMac[5]); 
  Serial.printf(", channel: %i\n", WIFI_CHANNEL); 
/*
  if (esp_now_init() != 0) {         
    Serial.println("*** ESP_Now init failed");  // If failed then go to sleep.
    gotoSleep();
  }
  Serial.printf("Before Send: %i\n", millis());
*/
  WiFi.mode(WIFI_STA);  // needed for esp32
  //Serial.println( WiFi.softAPmacAddress() ); // Slightly different than mac. 
  WiFi.disconnect(); // needed for esp32
  if(esp_now_init() == ESP_OK)
  {
    Serial.println("[ESPNOW] ESP NOW INIT!");
  }
  else
  {
    Serial.println("[ESPNOW] ESP NOW INIT FAILED....");
    setshortsleepsettingsesp32();  // Change sleeptime to short time
    gotoSleep();
  }  
  
   memcpy( &slave.peer_addr, &remoteMac, 6 );
  slave.channel = WIFI_CHANNEL;
  slave.encrypt = 0;
  if( esp_now_add_peer(peer) == ESP_OK)
  {
    Serial.println("[ESPNOW] Added Peer!");
  }

  esp_now_register_send_cb(OnDataSent);
 
  
  
  
  
//  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
//  esp_now_add_peer(remoteMac, ESP_NOW_ROLE_SLAVE, WIFI_CHANNEL, NULL, 0);
//  SENDSTATUS = esp_now_register_send_cb([](uint8_t* mac, uint8_t sendStatus) {    // esp_now_register_send_cb to register sending callback function. 

    //It will return ESP_NOW_SEND_SUCCESS in sending callback function if the data is received successfully on MAC layer. Otherwise, it will return ESP_NOW_SEND_FAIL. 
//  Serial.printf("send_cb, send done, status = %i\n", sendStatus);
  Serial.printf("[TIMING] After Send register: %i\n", millis());
  //Was a go to sleep here before, but last part of message was cut. Probably possible to optimise so sleep is called at correct time. 
//  callbackCalled = true;
//  });

//  callbackCalled = false;  // Ändrat
  
  uint8_t bs[sizeof(sensorData)];
  Serial.printf("[ESPNOW] Size of sensorData: ");
  Serial.println(sizeof(sensorData));
  memcpy(bs, &sensorData, sizeof(sensorData));
  Serial.printf("[ESPNOW] Sending the following data: ");  
  Serial.printf("sensor=%i, channelID=%i, MQTT_sensor_topic=%s, temp=%01f, humidity=%01f, pressure=%01f, battery=%01f\n", sensorData.sensor, sensorData.channelID, sensorData.MQTT_sensor_topic, sensorData.temp, sensorData.humidity, sensorData.pressure, sensorData.battLevelNow);
  
  //esp_now_send(NULL, bs, sizeof(sensorData)); // NULL means send to all peers

 // this was the part before. removes the eso_ok check
  if( esp_now_send(NULL, bs, sizeof(sensorData)) == ESP_OK)// was slave.peer_addr maxDataFrameSize
  {
    Serial.printf("\r\n[ESPNOW] Success Sent Value->\t%d", bs[0]); // sista parematern kan vara fel
  }
  else
  {
    Serial.printf("\r\n[ESPNOW] DID NOT SEND....");
  }
 
  Serial.printf("After Send: %i\n", millis());
  gotoSleep();  // med denna aktiv väntar man ej på svar, men det har jag ändå ej fått till.
}

// sends and then goes into a loop and waits for callback=1 or timeout.
void loop() {  // Ändrat
  if (millis() > SEND_TIMEOUT) {  //sendStatus==1 || 
    Serial.printf("[LOOP] In loop sendStatus = %i\n", SENDSTATUS);
    Serial.print("[TIMING] Going to sleep: "); 
    Serial.println(millis()); 
    gotoSleep();
  }
}



// denna drar mer ström
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
    Serial.println("The sensor did not respond. Please check wiring.");
    gotoSleep();
  }
  //Serial.print("bme280 init="); Serial.println(bme280.begin(), HEX);
  sensorData.temp = bme280.readTempC();
  sensorData.humidity = bme280.readFloatHumidity();
  sensorData.pressure = bme280.readFloatPressure() / 100.0;
  Serial.printf("temp=%01f, humidity=%01f, pressure=%01f\n", sensorData.temp, sensorData.humidity, sensorData.pressure);
}


void readBME280test() {
  // denna drar markant mindre ström
  #ifdef USING_TPL5110
  Wire.begin(D5,D6); // La till för att ändra D1, D2 till D4 och D5 så att D1 kan användas av TPL5110
  #endif
  #ifdef USING_TPL5110_BARE
  Wire.begin(14,12); // La till för att ändra D1, D2 till D4 och D5 så att D1 kan användas av TPL5110
  #endif
  #ifdef USING_DEEPSLEEP_ESP32
  Wire.begin(); // Om ej TPL5110 och använder standard
  #endif

  bmeSetup();
  bmeForceRead();

  sensorData.temp = bme280.readTempC();
  sensorData.humidity = bme280.readFloatHumidity();
  sensorData.pressure = bme280.readFloatPressure() / 100.0;
  Serial.printf("[BME] Sensor readings:  temp=%01f, humidity=%01f, pressure=%01f\n", sensorData.temp, sensorData.humidity, sensorData.pressure);
}

void batteryreading(){
  for (byte i=0;i<12;i++)
  {
    sortValues[i]=analogRead(battLevelPin);
    Serial.print("[BATT] Batt Level: ");
    Serial.println(sortValues[i]); // Needed to use float to set decimals since not using serial.print(battLevelNow,4) directly.      
  }  
  sort(sortValues,13); //Pass in the values and the size.
  battLevelRaw = sortValues[6]/1.0; // Divides with 1.0 to get float
  Serial.print("[BATT] Batt Level Raw: ");
  Serial.print(battLevelRaw); // Needed to use float to set decimals since not using serial.print(battLevelNow,4) directly.  
  sensorData.battLevelNow = sortValues[6]*batteryCorrection;
  Serial.print(", Batt Level Now: ");
  Serial.println(String(sensorData.battLevelNow,4)); // Needed to use float to set decimals since not using serial.print(battLevelNow,4) directly.     
  Serial.printf("[BATT] Battery data to send=%01f\n", sensorData.battLevelNow);

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

/*
Method to print the reason by which ESP32
has been awaken from sleep
*/
void print_wakeup_reason(){
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("[WAKEUP] Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("[WAKEUP] Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("[WAKEUP] Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("[WAKEUP] Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("[WAKEUP] Wakeup caused by ULP program"); break;
    default : Serial.printf("[WAKEUP] Wakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
  }
}

void gotoSleep() {
  #ifdef USING_SENSOR_MOSFET
    digitalWrite(25, LOW);  // D2
    delay(10);
    pinMode(25, INPUT);  // D2
  #endif 


  #ifdef USING_DEEPSLEEP
  int sleepSecs = SLEEP_SECS; 
  Serial.printf("[TIMING] Up for %i ms, going to sleep for %i secs...\n", millis(), sleepSecs); 
  Serial.println(); 
  Serial.println();  
  //ESP.deepSleep(sleepSecs * 1000000, RF_NO_CAL);
  //ESP.deepSleepInstant(sleepSecs * 1000000, RF_NO_CAL); // Längre tid och drog mer ström med core beta!!!
  delay(10000);
  #endif

  #ifdef USING_DEEPSLEEP_ESP32
  /*
  Now that we have setup a wake cause and if needed setup the
  peripherals state in deep sleep, we can now start going to
  deep sleep.
  In the case that no wake up sources were provided but deep
  sleep was started, it will sleep forever unless hardware
  reset occurs.
  */
  Serial.println("[SLEEP] Going to sleep now");
  Serial.flush();

  // https://www.savjee.be/2019/12/esp32-tips-to-increase-battery-life/
  
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  btStop();

  //adc_power_off();
  esp_wifi_stop();
  esp_bt_controller_disable();  
  // end of Savjee tips
   
  esp_deep_sleep_start();
  Serial.println("[SLEEP] This will never be printed");
  #endif  

  #ifdef USING_TPL5110  
  delay(200);
  
  Serial.printf("[TIMING] Up for %i ms, going to TPL-sleep \n", millis());
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
  
  Serial.printf("[TIMING] Up for %i ms, going to TPL-sleep \n", millis()); 
  // toggle DONE so TPL knows to cut power!
  while (1) {
  digitalWrite(TPL5110DONEPIN, HIGH);
  delay(1);
  digitalWrite(TPL5110DONEPIN, LOW);
  delay(1);
  }
  #endif
  
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status)
{
  Serial.print("\r\n[ESPNOW] Last Packet Send Status:\t");
  Serial.print(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

void  setshortsleepsettingsesp32()
{
  // overrides the long sleep with a short
  int sleepSecsShort = SLEEP_SECS_SHORT;
  esp_sleep_enable_timer_wakeup(sleepSecsShort * 1000000);
  Serial.println("[SLEEP] Short sleep: Setup ESP32 to sleep for " + String(sleepSecsShort) +
  " Seconds");
}

void  setsleepsettingsesp32()
{
/*
  First we configure the wake up source
  We set our ESP32 to wake up every 5 seconds
  */
  int sleepSecs = SLEEP_SECS;
  esp_sleep_enable_timer_wakeup(sleepSecs * 1000000);
  Serial.println("[SLEEP] Setup ESP32 to sleep for every " + String(sleepSecs) +
  " Seconds");

// Added turning off additional components:
    // this is hibernate mode (deep sleep with additional components turned off)  // Kolla om dessa tillför något
  
  esp_sleep_pd_config(ESP_PD_DOMAIN_MAX, ESP_PD_OPTION_OFF);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_OFF);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);
  /*
  Next we decide what all peripherals to shut down/keep on
  By default, ESP32 will automatically power down the peripherals
  not needed by the wakeup source, but if you want to be a poweruser
  this is for you. Read in detail at the API docs
  http://esp-idf.readthedocs.io/en/latest/api-reference/system/deep_sleep.html
  Left the line commented as an example of how to configure peripherals.
  The line below turns off all RTC peripherals in deep sleep.
  */
  //esp_deep_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
  //Serial.println("Configured all RTC Peripherals to be powered down in sleep");
}

// https://bitbucket.org/xoseperez/weatherstation_moteino/src/master/code/src/main.ino
// https://tinkerman.cat/post/low-power-weather-station-bme280-moteino/

void bmeForceRead() {
    // https://tinkerman.cat/post/low-power-weather-station-bme280-moteino/
    // We set the sensor in "forced mode" to force a reading.
    // After the reading the sensor will go back to sleep mode.
    uint8_t value = bme280.readRegister(BME280_CTRL_MEAS_REG);
    value = (value & 0xFC) + 0x01;
    bme280.writeRegister(BME280_CTRL_MEAS_REG, value);

    // Measurement Time (as per BME280 datasheet section 9.1)
    // T_max(ms) = 1.25
    //  + (2.3 * T_oversampling)
    //  + (2.3 * P_oversampling + 0.575)
    //  + (2.4 * H_oversampling + 0.575)
    //  ~ 9.3ms for current settings
    delay(10);

}

void bmeSetup() {
    // https://tinkerman.cat/post/low-power-weather-station-bme280-moteino/
    bme280.settings.commInterface = I2C_MODE;
    bme280.settings.I2CAddress = 0x76;
    bme280.settings.runMode = 1;
    bme280.settings.tStandby = 0;
    bme280.settings.filter = 0;
    bme280.settings.tempOverSample = 1;
    bme280.settings.pressOverSample = 1;
    bme280.settings.humidOverSample = 1;

    // Make sure sensor had enough time to turn on. BME280 requires 2ms to start up
    delay(10);
  Serial.print("[BME] Begin output: ");
    Serial.println(bme280.begin(), HEX);

}
