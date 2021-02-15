
// From version EspNowFromSerialGatewayV2 2019-04-24
/**
 * ESP-NOW from serial Gateway Example 
 * 
 * This shows how to use an ESP8266/Arduino as an ESP-Now Gateway by having one
 * ESP8266 receive ESP-Now messages and write them to Serial and have another
 * ESP8266 receive those messages over Serial and send them over WiFi. This is to
 * overcome the problem of ESP-Now not working at the same time as WiFi.
 * 
 * Author: Anthony Elder
 * License: Apache License v2
 */
#include <ESP8266WiFi.h>
#include <SoftwareSerial.h>

#include <SPI.h>
#include <PubSubClient.h>  // 2021-01-24 was 2.6.0, now 2.8.0
#include <ArduinoJson.h> // V6

#include "arduino_secrets.h" // In Arduino_secret: WIFI_SSID, WIFI_PASSWORD, MQTT_CLIENT_ID, MQTT_SENSOR_TOPIC, MQTT_USER, MQTT_PASSWORD

#define BAUD_RATE 115200
#define SWSERIAL_BAUD_RATE 9600
#define MQTT_VERSION MQTT_VERSION_3_1_1

//SoftwareSerial swSer(14, 12, false, 1024);
//SoftwareSerial swSer(D5, D6, false, 1024);
//SoftwareSerial swSer(D5, D6); // test 20200122
//SoftwareSerial swSer(D5, D6, false, 1024);  // Ändrade tillbaka 2020-12-25 för att etsta om det är detta som är problemet med att det tappas info i serielkommunikationen. Rätt fram till det sänds seriellt.
//Får fel vid compilering med ovan
SoftwareSerial swSer;


DynamicJsonDocument doc(256);  // version 6

//-------- Customise these values -----------

const PROGMEM char* MQTT_SERVER_IP = "192.168.1.200";
const PROGMEM uint16_t MQTT_SERVER_PORT = 1883;

//-------- Customise the above values --------

WiFiClient wifiClient;
PubSubClient client(wifiClient); 

String deviceMac;

/*
// keep in sync with ESP_NOW sensor struct
struct __attribute__((packed)) SENSOR_DATA {
    int   sensor;
    int   channelID;
    char  MQTT_sensor_topic[15];
    float temp;
    float humidity;
    float pressure;
    float battLevelNow;    
} sensorData;
*/

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

// MQTT: topic. Har ej från sensor innan jag mottagit!!!!
//const PROGMEM char* MQTT_SENSOR_TOPIC = "office/sensor3"; // In Arduino Secrets now.

volatile boolean haveReading = false;

void setup() {
  Serial.begin(115200); Serial.println();
  Serial.println("This is the WiFi side ");

  //swSer.begin(BAUD_RATE); Innan 2020-12-25
  swSer.begin(SWSERIAL_BAUD_RATE, SWSERIAL_8N1,D5,D6,false,1024); // test med annan seriel. Verkar ha ändrat syntax
  
  wifiConnect();

  // init the MQTT connection
  client.setServer(MQTT_SERVER_IP, MQTT_SERVER_PORT);
  client.setKeepAlive(60);  // Home assistant has 60 s. PubSubClient has 15s as default. Was disconnecting often.  
  client.setCallback(callback);
  
}

int heartBeat;

void loop() {
  if (millis()-heartBeat > 30000) {
    Serial.println("Waiting for ESP-NOW messages...");
    heartBeat = millis();
  }

 client.loop();  // Call the loop continuously to establish connection to the server.
 
  if (!client.connected()) {
    reconnect();
  }
  
  // client.loop();  // Call the loop continuously to establish connection to the server. //flyttade på test till efter kontroll. Flyttade tillbaka 2020-06-24 12:40

  while (swSer.available()) {
    if ( swSer.read() == '$' ) {
      while ( ! swSer.available() ) { delay(1); }
      if ( swSer.read() == '$' ) {
        readSerial();
      }
    }
  }
}

void readSerial() {

    deviceMac = "";

    while (swSer.available() < 6) { delay(1); }
    deviceMac += String(swSer.read(), HEX);
    deviceMac += String(swSer.read(), HEX);
    deviceMac += String(swSer.read(), HEX);
    deviceMac += String(swSer.read(), HEX);
    deviceMac += String(swSer.read(), HEX);
    deviceMac += String(swSer.read(), HEX);

    while (swSer.available() < 1) { delay(1); }
    byte len =  swSer.read();

    while (swSer.available() < len) { delay(1); }
    swSer.readBytes((char*)&sensorData, len);

    sendSensorData();
}

void sendSensorData() {
   //Serial.print("deviceMac: ");  
   //Serial.println(deviceMac);
   //Serial.printf("sensor=%i, channelID=%i, MQTT_sensor_topic=%s, temp=%01f, humidity=%01f, pressure=%01f, battery=%01f\r\n", sensorData.sensor, sensorData.channelID, sensorData.MQTT_sensor_topic, sensorData.temp, sensorData.humidity, sensorData.pressure, sensorData.battLevelNow);
   //publishData(sensorData.sensor, sensorData.temp, sensorData.humidity, sensorData.pressure, sensorData.battLevelNow);
   Serial.printf("sensor=%i, MQTT_sensor_topic=%s, millis=%lu, temp=%01f, humidity=%01f, pressure=%01f, battery=%01f, spare1=%i, spare2=%01f\r\n", sensorData.sensor, sensorData.MQTT_sensor_topic, sensorData.millis, sensorData.temp, sensorData.humidity, sensorData.pressure, sensorData.battery, sensorData.spare1, sensorData.spare2);
   publishData(sensorData.sensor, sensorData.millis, sensorData.temp, sensorData.humidity, sensorData.pressure, sensorData.battery, sensorData.spare1, sensorData.spare2);

}

void wifiConnect() {

  //WiFi.hostname(MQTT_CLIENT_ID); // Sketch menu ==> Tool ==> LwIP Variant xxxxx==> "V1.4 Higher Bandwidth"  // ej testat ännu (2020-06-13) 
  Serial.printf("Default hostname: %s\n", WiFi.hostname().c_str());
  WiFi.hostname(MQTT_CLIENT_ID);
  Serial.printf("New hostname: %s\n", WiFi.hostname().c_str());
  
  WiFi.mode(WIFI_STA);
  Serial.print("Connecting to "); Serial.print(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
     delay(250);
     Serial.print(".");
  }  
  Serial.print("\nWiFi connected, IP address: "); Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

void publishData(int sensor, unsigned long p_millis, float p_temperature, float p_humidity, float p_pressure, float p_battery, int p_spare1, float p_spare2) {

  // create a JSON object
  // doc : https://github.com/bblanchon/ArduinoJson/wiki/API%20Reference

  doc["sensor"] = sensor;
  doc["millis"] = p_millis;  
  doc["temperature"] = p_temperature;
  doc["humidity"] = p_humidity;
  doc["pressure"] = p_pressure;
  doc["battery"] = p_battery;
  doc["spare1"] = p_spare1;
  doc["spare2"] = p_spare2;  
  
  serializeJsonPretty(doc, Serial);

  char data[200];
  serializeJson(doc, data); 
  client.publish(sensorData.MQTT_sensor_topic, data, true);
  yield();
}

void reconnect() 
{
  // Loop until reconnected.
  while (!client.connected()) 
  {
    Serial.print("Attempting MQTT connection...");
    // Connect to the MQTT broker
    if (client.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD)) 
    {
      Serial.print("Connected with Client ID:  ");
      Serial.println(String(MQTT_CLIENT_ID));  
    } else 
    {
      Serial.print("failed, rc=");
      // Print to know why the connection failed.
      // See https://pubsubclient.knolleary.net/api.html#state for the failure code explanation.
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}
