/*
   Kwiatonomous ver. 1
*/

#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>


#define JOYSTICK_X_PIN        32
#define JOYSTICK_Y_PIN        35
#define JOYSTICK_BUTTON_PIN   33  // 34, 35, 36 and 39 can't be pulled up!
#define LCD_BRIGHTNESS_PIN    25


// Constants
const char* ssid              = "FunBox-7B3E";
const char* password          = "czaja121";
const uint8_t debounceDelay   = 35;           // ms
const int datetimeUpdateDelay = 500;          // ms


// Functions declaration
void updateDateTime();
void checkJoystick();
void updateMenu();
void joystickEvent(int event);
void setScreenBrightness(int val);
void checkForAutoLcdOff();


// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

// Date and time
String formattedDate;
String dayStamp;
String timeStamp;
unsigned long lastDebounceTime = 0;
unsigned long lastActionTime = 0;
unsigned long lastDatetimeUpdateTime = 0;

// LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);
bool lcdEnabled = true;

// Custom signs
byte forwardArrowSign[] = {B00000, B00100, B00110, B11111, B11111, B00110, B00100, B00000};
byte backwardsArrowSign[] = {B00000, B00100, B01100, B11111, B11111, B01100, B00100, B00000};
byte temperatureSign[] = {B00100, B00110, B00100, B00110, B00100, B01110, B01110, B01110};
byte humiditySign[] = {B00100, B00100, B01110, B01110, B11111, B11111, B11111, B01110};

// JOYSTICK
struct Joystick {
  int X = 0;
  int Y = 0;
  bool buttonState = LOW;
  bool lastButtonState = LOW;
  bool block = false;
};

struct Joystick joystick;

enum {
  EVENT_LEFT,
  EVENT_RIGHT,
  EVENT_DOWN,
  EVENT_UP,
  EVENT_BUTTON_PRESSED
};

/*        MENU
  - DATA I GODZINA
  - TEMPERATURA I WILGOTNOSC
  - OPCJE
    - PODLEWANIE
        - OFF/50ml/100ml/150ml
    - JASNOSC
        - OFF/25%/50%/75%/100%
    - WYGASZANIE EKRANU
        - OFF/5s/10s/30s
    - ZAPIS NA SD
        - ON/OFF
*/
uint8_t menuPage = 0;
uint8_t settingsPage = 0;
bool settingsOpened = false;

String menu[] = {
  "DATA I GODZINA",             // 0
  "TEMPERATURA I WILGOTNOSC",   // 1
  "OPCJE",                      // 2
};

String settings[] = {
  "PODLEWANIE",       // 0
  "JASNOSC",          // 1
  "WYGASZANIE EKR",   // 2
  "ZAPIS NA SD",      // 3
};

int settingsWatering[] = {
  0,
  25,
  50,
  100,
  150,
  200
};

int settingsBrightness[] = {
  0,
  25,
  50,
  75,
  100
};

int settingsAutoLcdOff[] = {
  0,
  5,
  10,
  30
};

// Settings               TODO: Save it in SD and retrieve in setup.
int wateringAmount = 100;
int wateringAmountNo = 3;
int screenBrightness = 100;
int screenBrightnessNo = 4;
int autoLcdOff = 10;
int autoLcdOffNo = 2;
bool sdLogging = false;

// Sensors readings
float airTemperature = 26.5;
float airHumidity = 74.2;

void setup() {
  Serial.begin(115200);
  pinMode(JOYSTICK_BUTTON_PIN, INPUT_PULLUP);
  ledcSetup(0, 500, 8);
  ledcAttachPin(LCD_BRIGHTNESS_PIN, 0);
  ledcWrite(0, 255);

  // Setup WiFi
  Serial.print("Connecting to "); Serial.print(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected.");
  Serial.print("IP address: "); Serial.println(WiFi.localIP());

  // Setup time
  timeClient.begin();
  timeClient.setTimeOffset(3600); // GMT+1 (czas zimowy)

  // Setup LCD
  lcd.init();
  lcd.createChar(0, forwardArrowSign);
  lcd.createChar(1, backwardsArrowSign);
  lcd.createChar(2, temperatureSign);
  lcd.createChar(3, humiditySign);
  lcd.backlight();
}


void loop() {
  if (millis() - lastDatetimeUpdateTime >= datetimeUpdateDelay) {
    updateDateTime();
  }

  checkJoystick();
  updateMenu();
  checkForAutoLcdOff();
}

void updateDateTime() {
  while (!timeClient.update()) {
    timeClient.forceUpdate();
  }
  formattedDate = timeClient.getFormattedDate(); // 2018-05-28T16:00:13Z

  // Extract date
  int splitT = formattedDate.indexOf("T");
  dayStamp = formattedDate.substring(0, splitT);

  // Extract time
  timeStamp = formattedDate.substring(splitT + 1, formattedDate.length() - 1);

  lastDatetimeUpdateTime = millis();
}

void checkJoystick() {
  joystick.X = analogRead(JOYSTICK_X_PIN);
  if (joystick.X >= 3800 && joystick.block == false) {
    joystickEvent(EVENT_RIGHT);
  }
  else if (joystick.X <= 200 && joystick.block == false) {
    joystickEvent(EVENT_LEFT);
  }

  joystick.Y = analogRead(JOYSTICK_Y_PIN);
  if (joystick.Y >= 3800 && joystick.block == false) {
    joystickEvent(EVENT_DOWN);
  }
  else if (joystick.Y <= 200 && joystick.block == false) {
    joystickEvent(EVENT_UP);
  }

  // Joystick released
  if (joystick.X < 3800 && joystick.X > 200 &&
      joystick.Y < 3800 && joystick.Y > 200 && joystick.block == true) {
    joystick.block = false;
  }


  // Check button (with debouncing)
  int reading = digitalRead(JOYSTICK_BUTTON_PIN);

  if (reading != joystick.lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != joystick.buttonState) {
      joystick.buttonState = reading;

      if (joystick.buttonState == LOW) {
        // BUTTON PRESSED FOR SURE
        joystickEvent(EVENT_BUTTON_PRESSED);
      }
    }
  }

  joystick.lastButtonState = reading;
}

void updateMenu() {
  if (!settingsOpened) {
    lcd.home();
    switch (menuPage) {
      case 0: {
          lcd.print(dayStamp);
          lcd.setCursor(0, 1);
          lcd.print(timeStamp);
          break;
        }
      case 1: {
          lcd.write(2);
          lcd.setCursor(2, 0);
          lcd.print(airTemperature); lcd.print(" "); lcd.print((char)223); lcd.print('C');
          lcd.setCursor(0, 1); lcd.write(3);
          lcd.setCursor(2, 1);
          lcd.print(airHumidity); lcd.print(" "); lcd.print('%');
          break;
        }
      case 2: {
          lcd.print("OPCJE");
          lcd.setCursor(15, 0); lcd.write(0);
          break;
        }
      default: {
          lcd.print("BŁĄD :(");
        }
    }
  } else {
    lcd.setCursor(0, 0); lcd.write(1);
    lcd.setCursor(2, 0);
    lcd.print(settings[settingsPage]);
    lcd.setCursor(0, 1);

    switch (settingsPage) {
      case 0: {
          if (wateringAmount == 0) {
            lcd.print("OFF");
          } else {
            lcd.print(wateringAmount); lcd.print("ml");
          }
          break;
        }
      case 1: {
          if (screenBrightness == 0) {
            lcd.print("OFF");
          } else {
            lcd.print(screenBrightness); lcd.print("%");
          }
          break;
        }
      case 2: {
          if (autoLcdOff == 0) {
            lcd.print("OFF");
          } else {
            lcd.print(autoLcdOff); lcd.print("s");
          }
          break;
        }
      case 3: {
          if (sdLogging) lcd.print("ON");
          else lcd.print("OFF");
          break;
        }
      default: {
          lcd.print("BŁĄD :(");
        }
    }
  }
}

void joystickEvent(int event) {
  joystick.block = true;
  if (autoLcdOff) lastActionTime = millis();
  if (!lcdEnabled) return;

  switch (event) {
    case EVENT_DOWN: {
        if (!settingsOpened) {
          if (menuPage > 0) menuPage--;
          else if (menuPage == 0) menuPage = 2;
        }
        else {
          if (settingsPage > 0) settingsPage--;
          else if (settingsPage == 0) settingsPage = 3;
        }
        break;
      }
    case EVENT_UP: {
        if (!settingsOpened) {
          if (menuPage < 2) menuPage++;
          else if (menuPage == 2) menuPage = 0;
        }
        else {
          if (settingsPage < 3) settingsPage++;
          else if (settingsPage == 3) settingsPage = 0;
        }
        break;
      }
    case EVENT_LEFT: {
        if (settingsOpened) {
          settingsOpened = false;
        }
        break;
      }
    case EVENT_RIGHT: {
        if (menuPage == 2 && !settingsOpened) {
          settingsOpened = true;
        }
        break;
      }
    case EVENT_BUTTON_PRESSED: {
        if (settingsOpened) {
          switch (settingsPage) {
            case 0: {
                if (wateringAmountNo < 5) wateringAmountNo++;
                else if (wateringAmountNo == 5) wateringAmountNo = 0;
                wateringAmount = settingsWatering[wateringAmountNo];
                break;
              }
            case 1: {
                if (screenBrightnessNo < 4) screenBrightnessNo++;
                else if (screenBrightnessNo == 4) screenBrightnessNo = 0;
                screenBrightness = settingsBrightness[screenBrightnessNo];
                setScreenBrightness(screenBrightness);
                break;
              }
            case 2: {
                if (autoLcdOffNo < 3) autoLcdOffNo++;
                else if (autoLcdOffNo == 3) autoLcdOffNo = 0;
                autoLcdOff = settingsAutoLcdOff[autoLcdOffNo];
                break;
              }
            case 3: {
                sdLogging = !sdLogging;
                break;
              }
          }
        }
        break;
      }
  }

  lcd.clear();
}

void setScreenBrightness(int val) {
  ledcWrite(0, map(val, 0, 100, 0, 255));
}

void checkForAutoLcdOff() {
  if (autoLcdOff) {
    unsigned long diff = millis() - lastActionTime;
    if (diff >= autoLcdOff * 1000 && lcdEnabled) {
      Serial.println("Turning LCD off");
      lcdEnabled = false;
      lcd.noDisplay();
      lcd.noBacklight();
    } else if (diff < autoLcdOff * 1000 && !lcdEnabled) {
      Serial.println("Turning LCD on");
      lcdEnabled = true;
      lcd.display();
      lcd.backlight();
    }
  }
}
