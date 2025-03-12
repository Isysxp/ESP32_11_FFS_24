#define _CRT_SECURE_NO_WARNINGS
#include <string>
#include <assert.h>
#include <cstdlib>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "avr11.h"
#include "kb11.h"
#include <FFat.h>
#include <ESP32Time.h>

KB11 cpu;
int kbdelay = 0;
int clkdelay = 0;
uint64_t systime,nowtime,clkdiv;
ESP32Time SystemTime;
int runFlag = 0, btcntr = 0;
float btlvl;
int RLTYPE;
// Timer0 Configuration Pointer (Handle)
hw_timer_t* Timer0_Cfg = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

void ARDUINO_ISR_ATTR clkint()
{
    cpu.unibus.kw11.tick();
}

void setup( char *rkfile, char *rlfile, int bootdev)
 {

	if (cpu.unibus.rk11.rk05)
		return;
  cpu.unibus.rk11.rk05 = FFat.open(rkfile,"rb+");
  cpu.unibus.rl11.rl02 = FFat.open(rlfile,"rb+");
  RLTYPE = 035;
  if (strcasestr(rlfile, ".rl02"))
      RLTYPE = 0235;
  clkdiv = (uint64_t)1000000 / (uint64_t)60;
  systime = SystemTime.getMillis();
	cpu.reset(02002,bootdev);
  Serial.printf("Ready\r\n");

  Timer0_Cfg = timerBegin(1000000);
  timerAttachInterrupt(Timer0_Cfg, &clkint);
  timerAlarm(Timer0_Cfg, 20000, true, 0);
}

jmp_buf trapbuf;

void loop0();

[[noreturn]] void trap(uint16_t vec) { longjmp(trapbuf, vec); }

void loop() {
    auto vec = setjmp(trapbuf);
    if (vec == 0) {
        loop0();
    } else {
        cpu.trapat(vec);
    }
}

void loop0() {
    while (true) {
        if ((cpu.itab[0].vec > 0) && (cpu.itab[0].pri > cpu.priority())) {
            cpu.trapat(cpu.itab[0].vec);
            cpu.popirq();
            return; // exit from loop to reset trapbuf
        }
        if (!cpu.wtstate)
           cpu.step();
        cpu.unibus.rk11.step();
        cpu.unibus.rl11.step();
        if (kbdelay++ == 5000) {
            cpu.unibus.cons.poll();
            cpu.unibus.dl11.poll();
            kbdelay = 0;
        }
    }
}

int startup( char *rkfile, char *rlfile, int bootdev)
{
    setup(rkfile,rlfile,bootdev);
    rgbLedWrite(DATA_PIN,0,0,0);
    while (1)
        loop();
}

void panic() {
    cpu.printstate();
    std::abort();
}
