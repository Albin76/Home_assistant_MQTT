/**
 * ESP-NOW to Serial 
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
extern "C" {
  #include <espnow.h>
  #include "user_interface.h"
}

/* Set a private Mac Address
 *  http://serverfault.com/questions/40712/what-range-of-mac-addresses-can-i-safely-use-for-my-virtual-machines
 * Note: the point of setting a specific MAC is so you can replace this Gateway ESP8266 device with a new one
 * and the new gateway will still pick up the remote sensors which are still sending to the old MAC 
 */
uint8_t mac[] = {0x36, 0x33, 0x33, 0x33, 0x33, 0x33};
void initVariant() {
// new code as of 2020-12-22
// https://randomnerdtutorials.com/get-change-esp32-esp8266-mac-address-arduino/
// and
// https://randomnerdtutorials.com/esp-now-many-to-one-esp8266-nodemcu/
  WiFi.mode(WIFI_STA);
  wifi_set_macaddr(STATION_IF, &mac[0]);
  WiFi.disconnect();  
}

void setup() {
  Serial.begin(9600); Serial.println();
  Serial.println("This is the ESP-NOW side ");
  Serial.print("This node STA mac: "); Serial.println(WiFi.macAddress());

  initEspNow();
}

int heartBeat;

void loop() {
  if (millis()-heartBeat > 30000) {
    Serial.println("Waiting for ESP-NOW messages...");
    heartBeat = millis();
  }
}

void initEspNow() {
  if (esp_now_init()!=0) {
    Serial.println("*** ESP_Now init failed");
    ESP.restart();
  }

// Old code before 2020-12-22
//  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);

// new code as of 2020-12-22
// https://randomnerdtutorials.com/esp-now-many-to-one-esp8266-nodemcu/
  esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);

  esp_now_register_recv_cb([](uint8_t *mac, uint8_t *data, uint8_t len) {

    // Investigate: There's little doc on what can be done within this method. If its like an ISR
    // then it should not take too long or do much I/O, but writing to Serial does appear to work ok

    Serial.print("$$"); // $$ just an indicator that this line is a received ESP-Now message
    Serial.write(mac, 6); // mac address of remote ESP-Now device
    Serial.write(len);
    Serial.write(data, len); 
    
  });
}
