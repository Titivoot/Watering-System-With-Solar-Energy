#include <LiquidCrystal_I2C.h>
#include "CMBMenu.hpp"
#include <EEPROM.h>
#include <Wire.h>
#include <ThreeWire.h>
#include <RtcDS1302.h>

#define RELAY_PIN (int[]){3, 4, 5, 6} // Relay Pin
#define RTC_PIN (int[]){11, 10, 12} // Time Module Pin (IO, SCLK, CE)
#define BUTTON_PIN (int[]){7, 8, 9} // Button (setTime button pin, Increase button pin, Decrease button pin)

#define countof(a) (sizeof(a) / sizeof(a[0]))

int relayPinSize = countof(RELAY_PIN);
int buttonPinSize = countof(BUTTON_PIN);

struct RelayTime {
    int hour;
    int minute;
};
int eepromAddress = 0;


struct RelayTime rt1;
struct RelayTime rt2;
struct RelayTime rt3;

ThreeWire RTCWire(RTC_PIN[0], RTC_PIN[1], RTC_PIN[2]); // IO, SCLK, CE
RtcDS1302<ThreeWire> Rtc(RTCWire);

RtcDateTime dt;

int s, m, h; //time sec, min, Hour
int D, M, Y; //Date, Month
int hourupg, minupg, secupg;
unsigned long HybridTimeStart;
int HybridRefresh = 120;
int hF, mF, sF;

int relayOffTime = 5;
unsigned long relayTime = 0;
int rtNum;

bool RelayState = false;

LiquidCrystal_I2C lcd(0x27, 16, 2);

byte ArrowLeft[] = {
  B00001,
  B00011,
  B00111,
  B01111,
  B01111,
  B00111,
  B00011,
  B00001
};

byte ArrowRight[] = {
  B10000,
  B11000,
  B11100,
  B11110,
  B11110,
  B11100,
  B11000,
  B10000
};

byte EnterChar[] = {
  B00001,
  B00001,
  B00001,
  B00101,
  B01101,
  B11111,
  B01100,
  B00100
};

int menu = 0;
int page = 0;

// define text to display
const char g_MenuAdjustTime_pc[] PROGMEM = {"Adjust Time"};
const char g_MenuRelaySettings_pc[] PROGMEM = {"Relay Settings"};
const char g_MenuExit_pc[] PROGMEM = {"Exit"};

const char g_MenuRelaySettingsCloseTime_pc[] PROGMEM = {"Close Time"};
const char g_MenuRelaySettingsOne_pc[] PROGMEM = {"Time 1"};
const char g_MenuRelaySettingsTwo_pc[] PROGMEM = {"Time 2"};
const char g_MenuRelaySettingsThree_pc[] PROGMEM = {"Time 3"};
const char g_MenuBack_pc[] PROGMEM = {"Back"};

const char g_MenuNull_pc[] PROGMEM = {""};
enum MenuFID {
  MenuDummy,
  MenuAdjustTime,
  MenuRelaySettings,
  MenuExit,
  MenuRelaySettingsCloseTime,
  MenuRelaySettingsOne,
  MenuRelaySettingsTwo,
  MenuRelaySettingsThree,
  MenuBack
};

enum KeyType {
  KeyNone, // no key is pressed
  KeyLeft,
  KeyRight,
  KeyEnter
};

CMBMenu<100> g_Menu;


void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);

  // get Value from ROM
  EEPROM.get(0, relayOffTime);
  if (relayOffTime == -1) {
    relayOffTime = 5;
    EEPROM.put(0, relayOffTime);
  }
  EEPROM.get(sizeof(relayOffTime), rt1);
  EEPROM.get(sizeof(rt1), rt2);
  EEPROM.get(sizeof(rt2), rt3);

  // pin init
  for (int pinNum = 0; pinNum < relayPinSize; pinNum++) {
    pinMode(RELAY_PIN[pinNum], OUTPUT);
    digitalWrite(RELAY_PIN[pinNum], LOW);
  }
  delay(5000);
  for (int pinNum = 0; pinNum < relayPinSize; pinNum++) {
    digitalWrite(RELAY_PIN[pinNum], HIGH);
  }

  for (int pinNum = 0; pinNum < buttonPinSize; pinNum++) {
    pinMode(BUTTON_PIN[pinNum], INPUT);
  }

  Rtc.Begin();
  RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
  HybridTimeStart = millis();
  RTCread();

  if (!Rtc.IsDateTimeValid()) 
  {
      // Common Causes:
      //    1) first time you ran and the device wasn't running yet
      //    2) the battery on the device is low or even missing

      Serial.println("RTC lost confidence in the DateTime!");
      Rtc.SetDateTime(compiled);
  }

  if (Rtc.GetIsWriteProtected())
  {
      Serial.println("RTC was write protected, enabling writing now");
      Rtc.SetIsWriteProtected(false);
  }

  if (!Rtc.GetIsRunning())
  {
      Serial.println("RTC was not actively running, starting now");
      Rtc.SetIsRunning(true);
  }

  RtcDateTime now (Y, M, D, h, m, s);
  if (now < compiled) 
  {
      Serial.println("RTC is older than compile time!  (Updating DateTime)");
      Rtc.SetDateTime(compiled);
  }
  else if (now > compiled) 
  {
      Serial.println("RTC is newer than compile time. (this is expected)");
  }
  else if (now == compiled) 
  {
      Serial.println("RTC is the same as compile time! (not expected but all is fine)");
  }

  // lcd
  lcd.begin();
  lcd.backlight();

  lcd.createChar(0, ArrowLeft);
  lcd.createChar(1, ArrowRight);
  lcd.createChar(2, EnterChar);

  g_Menu.addNode(0, g_MenuAdjustTime_pc , MenuAdjustTime);
  g_Menu.addNode(0, g_MenuRelaySettingsCloseTime_pc , MenuRelaySettingsCloseTime);
  g_Menu.addNode(0, g_MenuRelaySettingsOne_pc , MenuRelaySettingsOne);
  g_Menu.addNode(0, g_MenuRelaySettingsTwo_pc , MenuRelaySettingsTwo);
  g_Menu.addNode(0, g_MenuRelaySettingsThree_pc , MenuRelaySettingsThree);
  g_Menu.addNode(0, g_MenuExit_pc , MenuExit);
  g_Menu.addNode(0, g_MenuNull_pc, MenuDummy);

  const char* info;
  g_Menu.buildMenu(info);
  g_Menu.printMenu();

  // printMenuEntry(info);
  lcd.clear();
}

void loop() {
  // put your main code here, to run repeatedly:
  int fid = 0;
  const char* info;
  bool layerChanged = false;

  GetTime();
  RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
  RtcDateTime now (Y, M, D, h, m, s);
  Serial.println(menu);
  if (menu == 0) {
    hourupg = h;
    minupg = m;
    secupg = s;
    // show current time in lcd
    char timeString[9];
    snprintf_P(timeString,
                countof(timeString),
                PSTR("%02u:%02u:%02u"),
                h,
                m,
                s
              );
    lcd.setCursor(1, 0);
    lcd.print("Time: ");
    lcd.print(timeString);
    lcd.setCursor(1, 1);
    lcd.print("CT: ");
    lcd.print(relayOffTime);
    if (rtNum != 0) {
      lcd.setCursor(8,1);
      lcd.print("RT: ");
      lcd.print(rtNum);
    } else {
      lcd.setCursor(8,1);
      lcd.print("        ");
    }
    if (digitalRead(BUTTON_PIN[2])) {
      menu = 1;
    }
  } else if (menu == 1) {
    KeyType key = getKey();
    switch(key) {
      case KeyEnter:
        g_Menu.enter(layerChanged);
        break;
      case KeyRight:
        g_Menu.right();
        break;
      case KeyLeft:
        g_Menu.left();
        break;
      default:
        break;
    }

    if (KeyNone != key) {
      fid = g_Menu.getInfo(info);
      printMenuEntry(info);
    }

    printMenuEntry(info);

    if ((0 != fid) && (KeyEnter == key) && (!layerChanged)) {
      switch (fid) {
        case MenuDummy: break;
        case MenuAdjustTime:
          lcd.clear();
          menu = 2;
          break;
        case MenuExit:
          lcd.clear();
          menu = 0;
          break;
        case MenuRelaySettingsCloseTime:
          menu = 3;
          break;
        case MenuRelaySettingsOne:
          menu = 4;
          break;
        case MenuRelaySettingsTwo:
          menu = 5;
          break;
        case MenuRelaySettingsThree:
          menu = 6;
          break;
        default:
          break;
      }
    }
  } else if (menu == 2) {
    if (digitalRead(BUTTON_PIN[2])) {
      if (page < 2)
        page += 1;
      else
        page = 0;
    }
    if (page == 0) {
      lcd.clear();
      if(digitalRead(BUTTON_PIN[1])) { // +
        if (hourupg==23) {
          hourupg=0;
        } else {
          hourupg=hourupg+1;
        }
      }
      if(digitalRead(BUTTON_PIN[0])) { // -
        if (hourupg==23) {
          hourupg=0;
        } else {
          hourupg=hourupg-1;
        }
      }
      lcd.setCursor(0,0);
      lcd.print("Set Hour:");
      lcd.setCursor(0,1);
      lcd.print(hourupg,DEC);
    } else if (page == 1) {
      lcd.clear();
      if(digitalRead(BUTTON_PIN[1]) == HIGH) { // +
        if (minupg==59) {
          minupg=0;
        } else {
          minupg=minupg+1;
        }
      }
      if(digitalRead(BUTTON_PIN[0]) == HIGH) { // -
        if (minupg==59) {
          minupg=0;
        } else {
          minupg=minupg-1;
        }
      }
      lcd.setCursor(0,0);
      lcd.print("Set Minutes:");
      lcd.setCursor(0,1);
      lcd.print(minupg,DEC);
    } else {
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("SAVING IN");
      lcd.setCursor(0,1);
      lcd.print("PROGRESS");
      Rtc.SetDateTime(RtcDateTime(now.Year(),now.Month(),now.Day(),hourupg,minupg,0));
      delay(500);
      RTCread();
      menu = 0;
      page = 0;
      lcd.clear();
    }
  } else if (menu == 3) {
    if (digitalRead(BUTTON_PIN[2])) {
      if (page < 2)
        page += 1;
      else
        page = 0;
    }
    if (page == 0) {
      lcd.clear();
      if(digitalRead(BUTTON_PIN[1])) { // +
        if (relayOffTime==59) {
          relayOffTime=0;
        } else {
          relayOffTime=relayOffTime+1;
        }
      }
      if(digitalRead(BUTTON_PIN[0])) { // -
        if (relayOffTime==59) {
          relayOffTime=0;
        } else {
          relayOffTime=relayOffTime-1;
        }
      }
      lcd.setCursor(0,0);
      lcd.print("Set Close Time:");
      lcd.setCursor(0,1);
      lcd.print(relayOffTime,DEC);
    } else if (page == 1) {
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("SAVING IN");
      lcd.setCursor(0,1);
      lcd.print("PROGRESS");
      EEPROM.put(0, relayOffTime);
      delay(500);
      menu = 0;
      page = 0;
      lcd.clear();
    }
  } else if (menu == 4) { // Time 1
    if (digitalRead(BUTTON_PIN[2])) {
      if (page < 2)
        page += 1;
      else
        page = 0;
    }
    if (page == 0) {
      lcd.clear();
      if(digitalRead(BUTTON_PIN[1])) { // +
        if (rt1.hour==23) {
          rt1.hour=0;
        } else {
          rt1.hour=rt1.hour+1;
        }
      }
      if(digitalRead(BUTTON_PIN[0])) { // -
        if (rt1.hour==23) {
          rt1.hour=0;
        } else {
          rt1.hour=rt1.hour-1;
        }
      }
      lcd.setCursor(0,0);
      lcd.print("Set T1 Hour:");
      lcd.setCursor(0,1);
      lcd.print(rt1.hour,DEC);
    } else if (page == 1) {
      lcd.clear();
      if(digitalRead(BUTTON_PIN[1]) == HIGH) { // +
        if (rt1.minute==59) {
          rt1.minute=0;
        } else {
          rt1.minute=rt1.minute+1;
        }
      }
      if(digitalRead(BUTTON_PIN[0]) == HIGH) { // -
        if (rt1.minute==59) {
          rt1.minute=0;
        } else {
          rt1.minute=rt1.minute-1;
        }
      }
      lcd.setCursor(0,0);
      lcd.print("Set T1 Minutes:");
      lcd.setCursor(0,1);
      lcd.print(rt1.minute,DEC);
    } else {
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("SAVING IN");
      lcd.setCursor(0,1);
      lcd.print("PROGRESS");
      EEPROM.put(sizeof(relayOffTime), rt1);
      delay(500);
      menu = 0;
      page = 0;
      lcd.clear();
    }
  } else if (menu == 5) { // Time 2
    if (digitalRead(BUTTON_PIN[2])) {
      if (page < 2)
        page += 1;
      else
        page = 0;
    }
    if (page == 0) {
      lcd.clear();
      if(digitalRead(BUTTON_PIN[1])) { // +
        if (rt2.hour==23) {
          rt2.hour=0;
        } else {
          rt2.hour=rt2.hour+1;
        }
      }
      if(digitalRead(BUTTON_PIN[0])) { // -
        if (rt2.hour==23) {
          rt2.hour=0;
        } else {
          rt2.hour=rt2.hour-1;
        }
      }
      lcd.setCursor(0,0);
      lcd.print("Set T2 Hour:");
      lcd.setCursor(0,1);
      lcd.print(rt2.hour,DEC);
    } else if (page == 1) {
      lcd.clear();
      if(digitalRead(BUTTON_PIN[1]) == HIGH) { // +
        if (rt2.minute==59) {
          rt2.minute=0;
        } else {
          rt2.minute=rt2.minute+1;
        }
      }
      if(digitalRead(BUTTON_PIN[0]) == HIGH) { // -
        if (rt2.minute==59) {
          rt2.minute=0;
        } else {
          rt2.minute=rt2.minute-1;
        }
      }
      lcd.setCursor(0,0);
      lcd.print("Set T2 Minutes:");
      lcd.setCursor(0,1);
      lcd.print(rt2.minute,DEC);
    } else {
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("SAVING IN");
      lcd.setCursor(0,1);
      lcd.print("PROGRESS");
      EEPROM.put(sizeof(rt1), rt2);
      delay(500);
      menu = 0;
      page = 0;
      lcd.clear();
    }
  } else if (menu == 6) { // Time 3
    if (digitalRead(BUTTON_PIN[2])) {
      if (page < 2)
        page += 1;
      else
        page = 0;
    }
    if (page == 0) {
      lcd.clear();
      if(digitalRead(BUTTON_PIN[1])) { // +
        if (rt3.hour==23) {
          rt3.hour=0;
        } else {
          rt3.hour=rt3.hour+1;
        }
      }
      if(digitalRead(BUTTON_PIN[0])) { // -
        if (rt3.hour==23) {
          rt3.hour=0;
        } else {
          rt3.hour=rt3.hour-1;
        }
      }
      lcd.setCursor(0,0);
      lcd.print("Set T3 Hour:");
      lcd.setCursor(0,1);
      lcd.print(rt3.hour,DEC);
    } else if (page == 1) {
      lcd.clear();
      if(digitalRead(BUTTON_PIN[1]) == HIGH) { // +
        if (rt3.minute==59) {
          rt3.minute=0;
        } else {
          rt3.minute=rt3.minute+1;
        }
      }
      if(digitalRead(BUTTON_PIN[0]) == HIGH) { // -
        if (rt3.minute==59) {
          rt3.minute=0;
        } else {
          rt3.minute=rt3.minute-1;
        }
      }
      lcd.setCursor(0,0);
      lcd.print("Set T3 Minutes:");
      lcd.setCursor(0,1);
      lcd.print(rt3.minute,DEC);
    } else {
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("SAVING IN");
      lcd.setCursor(0,1);
      lcd.print("PROGRESS");
      EEPROM.put(sizeof(rt2), rt3);
      delay(500);
      menu = 0;
      page = 0;
      lcd.clear();
    }
  }

  if (RelayState == false) {
      if (rt1.hour == h && rt1.minute == m) {
        RelayState = true;
        rtNum = 1;
        for (int RelayNum = 0; RelayNum < relayPinSize; RelayNum++) {
          digitalWrite(RELAY_PIN[RelayNum], LOW);
        }
      }
      if (rt2.hour == h && rt2.minute == m) {
        RelayState = true;
        rtNum = 2;
        for (int RelayNum = 0; RelayNum < relayPinSize; RelayNum++) {
          digitalWrite(RELAY_PIN[RelayNum], LOW);
        }
      }
      if (rt3.hour == h && rt3.minute == m) {
        RelayState = true;
        rtNum = 3;
        for (int RelayNum = 0; RelayNum < relayPinSize; RelayNum++) {
          digitalWrite(RELAY_PIN[RelayNum], LOW);
        }
      }
  } else {
    if (millis() - relayTime > (unsigned long) relayOffTime * 1000 * 60) {
      relayTime = millis();
      RelayState = false;
      rtNum = 0;
      for (int RelayNum = 0; RelayNum < relayPinSize; RelayNum++) {
        digitalWrite(RELAY_PIN[RelayNum], HIGH);
      }
    }
  }
  delay(200);
}

void printMenuEntry(const char* f_Info)
{
  String info_s;
  MBHelper::stringFromPgm(f_Info, info_s);

  // print on LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(info_s);

  // you can print here additional infos into second line of LCD
  lcd.setCursor(4, 1);
  lcd.write(0);
  lcd.setCursor(8, 1);
  lcd.write(1);
  lcd.setCursor(12, 1);
  lcd.write(2);
}

KeyType getKey()
{
  KeyType key = KeyNone;

  // here for demonstration: get "pressed" key from terminal
  // replace code when using push buttons
  if (digitalRead(BUTTON_PIN[0]) == HIGH) {
      key = KeyLeft;
  }

  if (digitalRead(BUTTON_PIN[1]) == HIGH) {
      key = KeyRight;
  }

  if (digitalRead(BUTTON_PIN[2]) == HIGH) {
      key = KeyEnter;
  }

 return key;
}


void GetTime() {
  static unsigned long TotalSec;
  if ((HybridTimeStart + (HybridRefresh * 1000)) < millis()) {
    RTCread();
    HybridTimeStart = millis();
    Serial.print("Hybrid Update");
  }

  s = sF;
  m = mF;
  h = hF;

  long xtrasec;
  xtrasec = byte((millis() - HybridTimeStart) / 1000);
  TotalSec = h * 3600L + m * 60 + s + xtrasec;
  s = TotalSec % 60;
  h = TotalSec / 3600;
  m = ((TotalSec - ((TotalSec / 3600) * 3600)) / 60);

  if (h >= 24) h = h - 24;
}

void RTCread() {
  dt = Rtc.GetDateTime();
  s = dt.Second();
  m = dt.Minute();
  h = dt.Hour();
  D = dt.Day();
  M = dt.Month();
  Y = dt.Year();

  sF = s; //fix sec for hybrid use
  mF = m;
  hF = h;
}