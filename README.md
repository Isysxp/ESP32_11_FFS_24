# ESP32_PDP11_FFS - A PDP11/24 with RL02 disk in flash. - Ultrix 3.1
<br>
This is a modified version of a PDP11/40 emulation at https://github.com/Isysxp/PDP11-on-the-M5-Core<br>
This app requires an ESP32 processor with at least 8Mb of PSRAM.<br>
The current build is targeted for the ESP32S3 Dev Module clones eg: ESP32 S3 DevKitC 1 ESP32 S3 WROOM1 N16R8 Development Board.<br>
This provides a complete PDP11/24 with an OS on a quite a small module.<br>
The build enviroment is for the current Arduino IDE V 2.3.1 or the VisualMicro plugin for Visual Studio.<br>
The critical libraries required are:<br>
ESPTelnetStream (https://github.com/LennartHennigs/ESPTelnet)<br>
and Espressif Arduino-esp32 (https://github.com/espressif/arduino-esp32) V 2.0.17.<br>
Disk images: I have included a 'reasonably' complete build of Ultrix 3.1 on an emulated RL02 drive.<br>
This can be loaded into the Flash File System as a FAT image. The upload tool can be found<br>
in either Arduino IDE 1.8 or via the VisualMicro plugin for Visual Studio.<br>
The app provides 1 telnet stream linked to a DL11 serial port mapped at 2 Unibus addresses: 0775610 and 0776500 Vector:0300.<br>
(Do not try to use both at once!)<br>
The root login does not need a password. There is a user account niss1 PW:dodk5133 ... contains example programmes.<br>
<br>
Ian Schofield April 2024<br>
<br>
Note: Select ESP32 Dev Module with config:<br>
CDC on boot: disabled<br>
Flash size: 16Mb<br>
PSRAM: OPI PSRAM<br>
USB mode: Hardware CDC and JTAG<br>
Upload mode: UART0<br>
Partition scheme: 16M flash (2MB app / 12.5MB FATFS)<br>
Please use ESP-IDF release 3.1.3.
In addition, you will need to change the c standard in C:\Users\<username></username>\AppData\Local\Arduino15\packages\esp32\hardware\esp32\3.1.3\platform.txt<br>
from -std=gnu++11 to -std=gnu++17 to allow for template objects.
<br>
NB: Change the SSID and Password in ESP_Telnet.cpp to match your router. <br>
NNB: If your are using the Arduino IDE, rename the root folder to ESP32_11_FFS for the app to load correctly. <br>
NNNNNB: I strongly suggest you read up about Ultrix 3.1. This was the last of the none split I/D PDP11 operating systems.<br>
Also, you will need to solder a bridge on the top of the board marked 'RGB' then when the blue LED flashes you can rest assured that the flash in being trashed!!!!
<br>

Ian Schofield March 2025<br>


