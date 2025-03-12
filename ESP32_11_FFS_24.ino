
#include <FFat.h>
#include <ESP32Time.h>
#include <ESP.h>
#include <Arduino.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ESPTelnetStream.h"

#define NUM_LEDS 1
#define DATA_PIN RGB_BUILTIN
//#define DATA_PIN 39

extern void TStart();

int startup(char* rkfile, char* rlfile, int bootdev);
using namespace std;
char* ReadLine(bool fullDuplex = true, char lineBreak = '\n');

String Fnames[128];
int SelFile, cntr = 0;
int lbright = 0;

// List contents of SDCard.

void listDir(fs::FS& fs, const char* dirname, uint8_t levels) {
	Serial.printf("Listing directory: %s\r\n", dirname);

	File root = fs.open(dirname);
	if (!root) {
		Serial.println("- failed to open directory");
		return;
	}
	if (!root.isDirectory()) {
		Serial.println(" - not a directory");
		return;
	}
	File file = root.openNextFile();
	while (file) {
		if (file.isDirectory()) {
			Serial.print("  DIR : ");
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
	Serial.println("Startup.....");

	if (!FFat.begin()) {
		Serial.println("FatFS Mount Failed");
    rgbLedWrite(DATA_PIN, 0, 0, 10);
		while (1);
	}

	uint64_t cardSize = FFat.totalBytes() / 1024;
	Serial.printf("FFS Size: %llu KByte\r\n", cardSize);
	Serial.printf("FFS free: %d KByte\r\n", FFat.freeBytes() / 1024);
	Serial.printf("Total heap: %d\r\n", ESP.getHeapSize());
	Serial.printf("Free heap: %d\r\n", ESP.getFreeHeap());
	Serial.printf("Alloc heap: %d\r\n", ESP.getMaxAllocHeap());
	listDir(FFat, "/", 3);
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
	startup(rkfile, rlfile, bootdev);
}
