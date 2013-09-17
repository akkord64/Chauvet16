//////////////////////////////////////////////////////
//   CHAUVET16 Program for the CHAUVET16 CONTROLLER //
//   (c) 2013 and beyond by Jerry Sy aka d0ughb0y   //
//   make sure to edit config.h first before        //
//   uploading to the mega.                         //
//////////////////////////////////////////////////////
#include <OneWire.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Time.h>
#include <DS1307RTC.h>
#include <SdFat.h>
#include <SPI.h>
#include <Ethernet.h>
#include <Flash.h>
#include <stdlib.h>
#include "TinyWebServer.h"
#include "config.h"

static uint32_t currentTime = 0;
static uint32_t previousTime = 0;
static uint16_t deltaTime = 0;

SdFat sd;
SdFile file_;
uint32_t debug1=0;
uint32_t debug2=0;
LiquidCrystal_I2C lcd(LCD_ADDR,LCD_EN,LCD_RW,LCD_RS,LCD_D4,LCD_D5,LCD_D6,LCD_D7,LCD_BACKLIGHT,NEGATIVE);

enum outlet {OUTLETDEFS, Feeder=20, End=255};

volatile uint8_t _ActiveMacro = 4;
volatile time_t _MacroTime = 0;
volatile time_t _MacroCountDown = 0;
volatile conf_t conf;
volatile int tz = -8;
  
volatile OutLogRec_t _outlog[OUTLOGSZ];
volatile uint8_t _head = 0;
volatile uint8_t _tail = 0;
volatile boolean alarm = false;
volatile boolean testDst = false;
volatile boolean emailFlag = false;
volatile boolean logalarmFlag = false;
volatile boolean logSensorsFlag = false;
//volatile boolean netCheckFlag = false;
volatile uint16_t sonaravg = 0;
volatile uint8_t displaymode = 0;

void setup() {
  setupTimers();
  initializeEEPROM();
  #ifdef _FEEDER
  initFeeder();
  #endif
  initATO();
  initIO();
  #ifdef _X10
  initX10();
  #endif
  #ifdef _SONAR
  initSonar();
  #endif
  enablePCINT();
  beep();
  initUtils(); //init led, i2c pullup, lcd, SD
  lightOn();
  initNetwork(); //ethernet, clock, webserver, start logging here
  initSensors(); //temp and ph
  initOutlets();
  initPWM();
  lightOff();
  startOutletTimers(); 
  #ifdef _SONAR
  for (int i=0;i<255;i++) {
    updateSonar();
    delay(2);
    if (getSonar()>0) {
      logMessage(F("ATO water level "),getSonarPct());
      break;
    }
  }  
  #endif
  p(F("Boot Completed. "));
  logMessage(F("Initialization Completed."));
  logMessage(freeRam());
  delay(1000);
  beep2();
  lcd.clear();
}


void loop() {
  uint32_t time1 = micros();
  currentTime = millis();
  deltaTime = currentTime - previousTime;
  if (deltaTime > 16) { //60hz ~ every 16ms
    time_t timenow = now();
    static time_t lasttimenow = 0;
    if (timenow!=lasttimenow) {
      ptime(timenow); //print time once a second
      lasttimenow = timenow;
    }
    static uint8_t lastdisplaymode = 255;
    if (displaymode!=lastdisplaymode) {
      switch (displaymode) { //switch 2nd line every 5 secs
        case 0: psensors();
                break;
        case 1: poutlets();
                break;
        case 2: pinputs();
                break;
        case 3: pdebug();
                break;
      }
      lastdisplaymode = displaymode;
    }
    if (testDst) {
      autoDST(timenow);
      testDst = false;
    }
    if (logSensorsFlag) { //every 10 minutes only
      logSensorsFlag = false;
      logSensors();
    }
    if (emailFlag) {
      sendEmail();
      emailFlag=false;
    }
    if (logalarmFlag) {
      logAlarm();
      logalarmFlag=false;
    }
//    if (netCheckFlag) {
//      netCheck();
//      netCheckFlag=false; 
//    }
    updateTemp();
    updatePh();
    updateSonar();
    logOutlet();
    previousTime = currentTime;
    //debug1 and debug2 can be any number you want to display on lcd for debugging purposes
    //here I display the current and average execution time in milliseconds
    debug2 = micros()-time1;
    static uint32_t sum = 0;
    if (debug2 < 5000000) {
      if (sum) {
        sum = (sum-debug1)+debug2;
      } else {
        if (debug2>0)
          sum = debug2 * 60;
      }
      debug1=sum/60;
    }
  }
  //do every cycle  
  webprocess(); 
}

