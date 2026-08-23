
#define USE_FLASH 1
#include <ESP32Time.h>
#include <ESP.h>
#include <Arduino.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ESPTelnetStream.h"
#include "FS.h"
#ifdef USE_FLASH
#include <FFat.h>   	// Enable to use Flash rather then SDCard
#else
#include "SD_MMC.h"		// Disable to use Flash
#endif
#include <HardwareSerial.h>


#define NUM_LEDS 1
//#define DATA_PIN RGB_BUILTIN
#define DATA_PIN 48
const int SD_MISO_PIN = 39;  // GPIO for MISO
const int SD_MOSI_PIN = 44;  // GPIO for MOSI
const int SD_SCK_PIN = 43;   // GPIO for SCK
const int SD_CS_PIN = 42;    // GPIO for Chip Select (CS)

extern void TStart();

int startup(char* rkfile, char* rlfile, int bootdev);
using namespace std;
char* ReadLine(bool fullDuplex = true, char lineBreak = '\n');

String Fnames[128];
int SelFile, cntr = 0;
int lbright = 0;
HardwareSerial MySerial(2);

// List contents of SDCard.

void listDir(fs::FS& fs, const char* dirname, uint8_t levels) {
	Serial.printf("Listing directory: %s\r\n", dirname);

	File root = fs.open(dirname);
	if (!root) {
		//Serial.println("- failed to open directory");
		return;
	}
	if (!root.isDirectory()) {
		Serial.println(" - not a directory");
		return;
	}
	File file = root.openNextFile();
	while (file) {
		if (file.isDirectory()) {
			Serial.print("DIR : ");
			Serial.println(file.name());
			if (levels) {
				listDir(fs, file.name(), levels - 1);
			}
		}
		else {
			Serial.printf(" File:%3d ", cntr + 1);
			Serial.print(file.name());
			Fnames[cntr++] = file.name();
			Serial.print("\tSIZE: ");
			Serial.println(file.size());
		}
		file = root.openNextFile();
	}
}

void setup() {
	char* bfr, rkfile[32], rlfile[32];
	int bootdev = 0;

	rgbLedWrite(DATA_PIN, 0, 10, 0);

	Serial.begin(115200);
	while (!Serial)
	{
		yield();
	}
	MySerial.begin(115200, SERIAL_8N1, SOC_RX0, SOC_TX0);
	Serial.println("Startup.....");
	MySerial.println("DL0 online");
#ifdef USE_FLASH
	if (!FFat.begin()) {
	 	Serial.println("FatFS Mount Failed");
		Serial.flush();
     rgbLedWrite(DATA_PIN, 0, 0, 10);
	 	while (1);
	 }
	uint64_t cardSize = FFat.totalBytes() / 1024;
	Serial.printf("FFS Size: %llu KByte\r\n", cardSize);
	Serial.printf("FFS free: %d KByte\r\n", FFat.freeBytes() / 1024);
#else
	    if(! SD_MMC.setPins(SD_SCK_PIN, SD_MOSI_PIN, SD_MISO_PIN)){
       Serial.println("Pin change failed!");
       return;
}
  if (!SD_MMC.begin()) {
    Serial.println("Card Mount Failed");
		while(1)
			yield();
    return;
  }
#endif
	Serial.printf("Total heap: %d\r\n", ESP.getHeapSize());
	Serial.printf("Free heap: %d\r\n", ESP.getFreeHeap());
	Serial.printf("Alloc heap: %d\r\n", ESP.getMaxAllocHeap());
#ifdef USE_FLASH
	listDir(FFat, "/", 3);
#else
	listDir(SD_MMC, "/", 3);
#endif
	Serial.printf("Enter index of RK05 image:");
	bfr = ReadLine(true, '\r');
	sscanf(bfr, "%d", &SelFile);
	strcpy(rkfile, "/");
	strcat(rkfile, Fnames[SelFile - 1].c_str());
	Serial.printf("\r\nEnter index of RL01/2 image:");
	bfr = ReadLine(true, '\r');
	sscanf(bfr, "%d", &SelFile);
	strcpy(rlfile, "/");
	strcat(rlfile, Fnames[SelFile - 1].c_str());
	Serial.printf("\r\nBoot: RK/RL:");
	bfr = ReadLine(true, '\r');
	if (bfr[1] == 'l' || bfr[1] == 'L')
		bootdev = 1;
	if (bootdev)
		Serial.printf("\r\nBooting file:%s on RL0:\r\n", rlfile);
	else
		Serial.printf("\r\nBooting file:%s on RK0:\r\n", rkfile);
	TStart();
	rgbLedWrite(DATA_PIN, 0, 0, 0);
	startup(rkfile, rlfile, bootdev);
}
