# ESP32_PDP11_FFS - A PDP11/40 with RK05 disk in flash.
<br>
This is a modified version of a PDP11/40 emulation at https://github.com/Isysxp/PDP11-on-the-M5-Core<br>
This app requires an ESP32 processor with at least 512K of PSRAM.<br>
The current build is targeted for the Adafruit QT Py ESP32-S3 board.<br>
This provide a complete PDP11/40 with an OS on a board the size of a postage stamp!<br>
https://learn.adafruit.com/adafruit-qt-py-esp32-s3/overview<br>
The build enviroment is for the current Arduino IDE V 2.3.1 or the VisualMicro plugin for Visual Studio.<br>
The critical libraries required are:<br>
ESPTelnetStream (https://github.com/LennartHennigs/ESPTelnet)<br>
and Espressif Arduino-esp32 (https://github.com/espressif/arduino-esp32) V 2.0.17.<br>
Disk images: I have included a 'reasonably' complete build of RT11 V5.03 on an RK05 image.<br>
This can be loaded into the Flash File System as a FAT image. The upload tool can be found<br>
in either Arduino IDE 1.8 or via the VisualMicro plugin for Visual Studio.<br>
The app provides 1 telnet stream linked to a DL11 serial port mapped at 2 Unibus addresses: 0775610 and 0776500 Vector:0300.<br>
(Do not try to use both at once!)<br>
<br>
Ian Schofield April 2024<br>
<br>
Note: With refernce to the flash partition table (Should be set to No OTA 1MB app 3MB flash).<br>
Using ESP-IDF release >= 3.0 results in an app size > 1048576 bytes. This will overflow the ESP32 flash.<br>
Please use ESP-IDF release 2.0.17 to correct this error.<br>
In addition, you will need to change the c standard in C:\Users\<username></username>\AppData\Local\Arduino15\packages\esp32\hardware\esp32\2.0.17\platform.txt<br>
from -std=gnu++11 to -std=gnu++17 to allow for template objects.
<br>
NB: Change the SSID and Password in ESP_Telnet.cpp to match your router. <br>
NNB: If your are using the Arduino IDE, rename the root folder to ESP32_11_FFS for the app to load correctly. <br>
<br>

Ian Schofield August 2024<br>


