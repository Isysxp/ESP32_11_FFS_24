/* ------------------------------------------------- */

#include "ESPTelnetStream.h"
#include <WiFi.h>

/* ------------------------------------------------- */

#define SERIAL_SPEED  115200
#define INFRA_SSID    "<Your SSID>"
#define INFRA_PSWD    "<Your Password>"
//const char noecho[]={0xFF,0xFD,0x2D,0}; // IAC DO SUPPRESS-LOCAL-ECHO Required for Windows telnet client.
const uint8_t noecho[]={0xFF,0xFB,0x01,0};   // IAC WILL ECHO Seems to work for most clients
/*
IAC DO SUPPRESS-LOCAL-ECHO

        The sender of this command, generally a host computer, REQUESTS that a
client NVT terminal suspend the local echoing of text typed on its
keyboard. This request makes good sense only when the NVT and host are
operating in an asymmetric, half-duplex terminal mode with a
co-operating host. The command should have no effect on an NVT terminal
operating in full-duplex mode.
*/

/* ------------------------------------------------- */

ESPTelnetStream telnet;

/* ------------------------------------------------- */

void telnetConnected(String ip) {
  Serial.print(ip);
  Serial.println(" connected.");
  telnet.print((char *)noecho);
}

void telnetDisconnected(String ip) {
  Serial.print(ip);
  Serial.println(" disconnected.");
}

void telnetReconnect(String ip) {
  Serial.print(ip);
  Serial.println(" reconnected.");
}

/* ------------------------------------------------- */

void TStart() {
 uint8_t wifiAttempts = 0;

  Serial.println("ESP Telnet server");
  WiFi.disconnect(false,true);
  //Serial.flush();
  delay(2000);
  WiFi.setAutoReconnect(false);
  WiFi.useStaticBuffers(true);
  WiFi.setHostname("EspPDP11");
  WiFi.mode(WIFI_STA);
  WiFi.setMinSecurity(WIFI_AUTH_WPA2_PSK);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.begin(INFRA_SSID, INFRA_PSWD);

  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(1000);
    if(wifiAttempts == 20)
    {
      WiFi.disconnect();//Switch off the wifi on making 10 attempts and start again.
      WiFi.begin(INFRA_SSID, INFRA_PSWD);
      wifiAttempts = 0;
    }
    wifiAttempts++;
  }

  telnet.onConnect(telnetConnected);
  telnet.onDisconnect(telnetDisconnected);
  telnet.onReconnect(telnetReconnect);

  Serial.print("Telnet.begin: ");
  if(telnet.begin()) {
    Serial.println("Successful");
  } else {
    Serial.println("Failed");
  }
  IPAddress ip = WiFi.localIP();
  Serial.print("IP:");
  Serial.print(ip);
  Serial.println("/EspPDP11");
}

/* ------------------------------------------------- */

void Tloop() {
  telnet.loop();
/*
  if(Serial.available() > 0) {
    telnet.write(Serial.read());
  }
  if (telnet.available() > 0) {    
    Serial.print(char(telnet.read()));
  }
*/
}

/* ------------------------------------------------- */
