/*------------------------------------------------------------------------

   Kwiatonomous ver. 2 (Arduino nano)

  --------------------------------------------------------------------------*/

#include <EEPROM.h>
#include <LiquidCrystal_I2C.h>  // https://github.com/fdebrabander/Arduino-LiquidCrystal-I2C-library
#include "RTClib.h"             // https://adafruit.github.io/RTClib/
#include "DHT.h"                // https://github.com/adafruit/DHT-sensor-library


#define EEPROM_WATERING_AMOUNT_ADDR     0
#define EEPROM_WATERING_INTERVAL_ADDR   1
#define EEPROM_MOISTURE_LEVEL_ADDR      2
#define EEPROM_SCREEN_BRIGHTNESS_ADDR   3
#define EEPROM_AUTO_LCD_OFF_ADDR        4

#define JOYSTICK_X_PIN        A1
#define JOYSTICK_Y_PIN        A2
#define JOYSTICK_BUTTON_PIN   A3
#define MOISTURE_SENSOR_PIN   A7

#define DHT_11_PIN            2
#define RELAY_PIN             3
#define LCD_BRIGHTNESS_PIN    5

#define TIME_5_ML             300     // ms
#define TIME_15_ML            600     // ms
#define TIME_30_ML            1000    // ms
#define TIME_50_ML            1400    // ms
#define TIME_100_ML           2500    // ms

#define TIME_DRIFT_MS         11000   // 11 seconds drift per day
#define DEBOUNCE_TIME         35      // ms

//------------------------------------------------------------------------//

// Functions declaration
void updateDateTime();
bool checkJoystick();
void updateMenu();
void updateMoisture();
void checkForWatering();
void updateTemperature();
void waterPlant(int t);
void joystickEvent(int event);
void setScreenBrightness(int val);
void checkForAutoLcdOff();
void loadSettingsFromEEPROM();
void clearEEPROM();


// Date and time
RTC_DS1307 rtc;
DateTime now;
DateTime wateringDateTime;

String dayStamp;
String timeStamp;
unsigned long lastDebounceTime = 0;
unsigned long lastActionTime = 0;
unsigned long lastDatetimeUpdateTime = 0;
unsigned long lastSensorsUpdateTime = 0;
unsigned long lastMoistureBasedWateringTime = 0;

char daysOfTheWeek[7][13] = {
  "Niedziela", "Poniedzialek", "Wtorek",
  "Sroda", "Czwartek", "Piatek", "Sobota"
};


// DHT 11
DHT dht(DHT_11_PIN, DHT11);


// LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);
bool lcdEnabled = true;

// Custom characters
byte forwardArrowSign[] = {
  B00000, B00100, B00110, B11111,
  B11111, B00110, B00100, B00000
};
byte backwardsArrowSign[] = {
  B00000, B00100, B01100, B11111,
  B11111, B01100, B00100, B00000
};
byte temperatureSign[] = {
  B00100, B00110, B00100, B00110,
  B00100, B01110, B01110, B01110
};
byte humiditySign[] = {
  B00100, B00100, B01110, B01110,
  B11111, B11111, B11111, B01110
};


// Joystick
struct Joystick {
  int X = 0;
  int Y = 0;
  bool buttonState = LOW;
  bool lastButtonState = LOW;
  bool block = false;
};

struct Joystick joystick;

// Joystick event enum
enum {
  EVENT_LEFT,
  EVENT_RIGHT,
  EVENT_DOWN,
  EVENT_UP,
  EVENT_BUTTON_PRESSED
};


/*        MENU
  - DATA I GODZINA                  0
  - TEMPERATURA I WILGOTNOSC        1
  - WILG. GLEBY                     2
  - CZAS NASTEPNEGO PODLEWANIA      3
  - OPCJE                           4

          OPCJE
  - PODLEWANIE 1 (OFF/5ml/15ml/30ml, 50ml, 100ml) 0
  - PODLEWANIE 2 (AUTO, 6h, 12h, 24h, 48h, 168h)  1
  - WIGLOTNOSC (10%, 20%, 30%, 40%, 50%,
                60%, 70%, 80%, 90%, bagno)        2
  - JASNOSC LCD (OFF/25%/50%/75%/100%)            3
  - WYGASZANIE EKRANU (OFF/5s/10s/30s)            4
  - TEST PODLEWANIA                               5
  - GODZINA +1                                    6
  - GODZINA -1                                    7
  - MINUTA +1                                     8
  - MINUTA -1                                     9
*/

String menu[] = {
  "DATA I GODZINA",               // 0
  "TEMPERATURA I WILGOTNOSC",     // 1
  "WILG. GLEBY",                  // 2
  "CZAS NAST. PODLEW.",           // 3
  "OPCJE",                        // 4
};

String settings[] = {
  "PODLEWANIE 1",       // 0
  "PODLEWANIE 2",       // 1
  "WILG. GLEBY",        // 2
  "JASNOSC LCD",        // 3
  "WYGASZANIE EKR",     // 4
  "TEST PODLEWANIA",    // 5
  "GODZINA +1",         // 6
  "GODZINA -1",         // 7
  "MINUTA +1",          // 8
  "MINUTA -1",          // 9
};

int settingsWatering1[] = { // (amount)
  0, 5, 15, 30, 50, 100
};

int settingsWatering2[] = { // (interval)
  0,  6, 12, 24, 48, 168
};

int settingsMoisture[] = {
  0, 10, 20, 30, 40, 50,
  60, 70, 80, 90, 100
};

int settingsBrightness[] = {
  0, 25, 50, 75, 100
};

int settingsAutoLcdOff[] = {
  0,  5, 10, 30
};

uint8_t menuPage = 0;
uint8_t settingsPage = 0;
bool settingsOpened = false;


// Default settings
int wateringAmount = 30;
int wateringInterval = 24;
int moistureLevel = 50;
int screenBrightness = 100;
int autoLcdOff = 10;

uint8_t wateringAmountNo = 3;
uint8_t wateringIntervalNo = 2;
uint8_t moistureLevelNo = 4;
uint8_t screenBrightnessNo = 4;
uint8_t autoLcdOffNo = 2;


// Sensors readings
float airTemperature = 0.00;
float airHumidity = 0.00;
float soilMoisture = 0.00;

//------------------------------------------------------------------------//

void setup() {
  Serial.begin(9600);
  pinMode(JOYSTICK_BUTTON_PIN, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(MOISTURE_SENSOR_PIN, INPUT);

  // Setup LCD
  lcd.init();
  lcd.createChar(0, forwardArrowSign);
  lcd.createChar(1, backwardsArrowSign);
  lcd.createChar(2, temperatureSign);
  lcd.createChar(3, humiditySign);
  lcd.backlight();
  analogWrite(LCD_BRIGHTNESS_PIN, 200);

  // Setup RTC (DS1307)
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    lcd.print("RTC ERROR!");
    while (1);
  }

//  rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
//  rtc.adjust(DateTime(rtc.now().year(),
//                      rtc.now().month(),
//                      rtc.now().day(),
//                      rtc.now().hour(),
//                      rtc.now().minute(),
//                      rtc.now().second() + 15));

  //   Setup DHT11
  dht.begin();


  loadSettingsFromEEPROM();

  // Set next watering time
  wateringDateTime = rtc.now() + TimeSpan(0, wateringInterval, 0, 0);
}

void loop() {
  // Every 1s
  if (millis() - lastDatetimeUpdateTime >= 1000) {
    updateDateTime();
    checkForWatering();
    checkForAutoLcdOff();
    lastDatetimeUpdateTime = millis();
  }

  // Every 5s
  if (millis() - lastSensorsUpdateTime >= 5000) {
    updateTemperature();
    updateMoisture();
    lastSensorsUpdateTime = millis();
  }

  // refresh LCD only if action taken or options not opened
  if (checkJoystick()
      || !settingsOpened
      || (settingsOpened && settingsPage == 6)
      || (settingsOpened && settingsPage == 7)
      || (settingsOpened && settingsPage == 8)
      || (settingsOpened && settingsPage == 9)) {
    updateMenu();
  }

  //  Serial.println(joystick.X);
  //  Serial.println(joystick.Y);
  //  Serial.println();
}

void updateDateTime() {
  now = rtc.now();
  dayStamp = now.timestamp(DateTime::TIMESTAMP_DATE);
  timeStamp = now.timestamp(DateTime::TIMESTAMP_TIME);

  //   Adjust time
  if (now.hour() == 23 && now.minute() == 0 && now.second() == 0) {
    Serial.println("DRIFT CORRECTION ROUTINE");
    delay(TIME_DRIFT_MS + 1000);
    rtc.adjust(DateTime(now.year(),
                        now.month(),
                        now.day(),
                        23,          // hour
                        0,          // min
                        1));        // sec
  }
}

// Returns true if action was taken, else return false
bool checkJoystick() {
  bool action = false;

  joystick.X = analogRead(JOYSTICK_X_PIN);
  if (joystick.X >= 800 && joystick.block == false) {
    joystickEvent(EVENT_RIGHT);
    action = true;
  }
  else if (joystick.X <= 200 && joystick.block == false) {
    joystickEvent(EVENT_LEFT);
    action = true;
  }

  joystick.Y = analogRead(JOYSTICK_Y_PIN);
  if (joystick.Y >= 800 && joystick.block == false) {
    joystickEvent(EVENT_DOWN);
    action = true;
  }
  else if (joystick.Y <= 200 && joystick.block == false) {
    joystickEvent(EVENT_UP);
    action = true;
  }

  // Joystick released
  if (joystick.X < 800 && joystick.X > 200 &&
      joystick.Y < 800 && joystick.Y > 200 && joystick.block == true) {
    joystick.block = false;
  }


  // Check button (with debouncing)
  int reading = digitalRead(JOYSTICK_BUTTON_PIN);

  if (reading != joystick.lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > DEBOUNCE_TIME) {
    if (reading != joystick.buttonState) {
      joystick.buttonState = reading;

      if (joystick.buttonState == LOW) {
        // BUTTON PRESSED FOR SURE
        joystickEvent(EVENT_BUTTON_PRESSED);
        action = true;
      }
    }
  }

  joystick.lastButtonState = reading;
  return action;
}

void updateMenu() {
  if (!settingsOpened) {
    lcd.home();
    switch (menuPage) {
      case 0: {
          lcd.setCursor(3, 0);
          lcd.print(dayStamp);
          lcd.setCursor(4, 1);
          lcd.print(timeStamp);
          break;
        }
      case 1: {
          lcd.write(2);
          lcd.print(" " + String(airTemperature) + ((char)223) + 'C');
          lcd.setCursor(0, 1); lcd.write(3);
          lcd.setCursor(2, 1);
          lcd.print(airHumidity); lcd.print(" %");
          break;
        }
      case 2: {
          lcd.print("WILG. GLEBY");
          lcd.setCursor(0, 1);
          lcd.write(3);
          lcd.setCursor(2, 1);
          lcd.print(soilMoisture); lcd.print(" %");
          break;
        }
      case 3: {
          TimeSpan diff = wateringDateTime - now;
          long ts = diff.totalseconds();
          lcd.print("NAST. PODLEWANIE");
          lcd.setCursor(0, 1);

          if (wateringInterval == 0) {
            lcd.print("WILG. GLEBY <");
            lcd.print(moistureLevel);
            lcd.print("%");
          } else {
            lcd.print("ZA ");
            lcd.print(ts / 3600);
            lcd.print("h i ");
            lcd.print(diff.minutes(), DEC);
            lcd.print("min");
          }
          break;
        }
      case 4: {
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
          if (wateringInterval == 0) {
            lcd.print("AUTO");
          } else {
            lcd.print(wateringInterval); lcd.print("h");
          }
          break;
        }
      case 2: {
          if (moistureLevel == 90) {
            lcd.print("BAGNO");
          } else if (moistureLevel == 0) {
            lcd.print("PUSTYNIA");
          }
          else {
            lcd.print(moistureLevel); lcd.print("%");
          }
          break;
        }
      case 3: {
          if (screenBrightness == 0) {
            lcd.print("OFF");
          } else {
            lcd.print(screenBrightness); lcd.print("%");
          }
          break;
        }
      case 4: {
          if (autoLcdOff == 0) {
            lcd.print("OFF");
          } else {
            lcd.print(autoLcdOff); lcd.print("s");
          }
          break;
        }
      case 5: {
          lcd.setCursor(0, 1);
          lcd.print("OSTROZNIE!!!");
          break;
        }
      case 6: {
          lcd.setCursor(4, 1);
          lcd.print(timeStamp);
          break;
        }
      case 7: {
          lcd.setCursor(4, 1);
          lcd.print(timeStamp);
          break;
        }
      case 8: {
          lcd.setCursor(4, 1);
          lcd.print(timeStamp);
          break;
        }
      case 9: {
          lcd.setCursor(4, 1);
          lcd.print(timeStamp);
          break;
        }
      default: {
          lcd.print("BŁĄD :(");
        }
    }
  }
}

void updateMoisture() {
  int moistureReading = analogRead(MOISTURE_SENSOR_PIN);
  soilMoisture = map(moistureReading, 0, 1023, 100, 0);
  Serial.print("Soil moisture: "); Serial.print(soilMoisture);
  Serial.print("%\t(");
  Serial.print(moistureReading);
  Serial.println("/1023)");
  Serial.println("-------------------------------------");
}

void updateTemperature() {
  float at = dht.readTemperature();
  float ah = dht.readHumidity();

  if (isnan(ah) || isnan(at)) {
    Serial.println("Failed to read from DHT sensor!");
  } else {
    Serial.print("Air temperature: "); Serial.println(at);
    Serial.print("Air humidity: "); Serial.println(ah);
    airTemperature = at;
    airHumidity = ah;
  }
}

// Function that checks if it's time to water the plant
void checkForWatering() {
  // Check if watering is turned OFF
  if (wateringAmount == 0)
    return;

  // Watering is set to AUTO (based of soil moisture)
  if (wateringInterval == 0) {
    // Every 30min
    long moistureBasedWateringInterval = 1800000; // 30min
    if (millis() - lastMoistureBasedWateringTime >= moistureBasedWateringInterval) {
      if (soilMoisture != 0 && soilMoisture < moistureLevel) {
        waterPlant();
      }
      lastMoistureBasedWateringTime = millis();
    }
    // Watering has constant watering interval
  } else {
    if (wateringDateTime <= now) {
      waterPlant();
      wateringDateTime = rtc.now() + TimeSpan(0, wateringInterval, 0, 0);
    }
  }
}

// Function that activate water pump for time t
void waterPlant() {
  Serial.println("Watering plant!");

  int delayTime = 0;
  switch (wateringAmount) {
    case 5: {
        delayTime = TIME_5_ML;
        break;
      }
    case 15: {
        delayTime = TIME_15_ML;
        break;
      }
    case 30: {
        delayTime = TIME_30_ML;
        break;
      }
    case 50: {
        delayTime = TIME_50_ML;
        break;
      }
    case 100: {
        delayTime = TIME_100_ML;
        break;
      }
    default: {
        Serial.println("Error! Wrong wateringAmount value!");
      }
  }

  digitalWrite(RELAY_PIN, HIGH); // TURNS ON PUMP
  delay(delayTime);
  digitalWrite(RELAY_PIN, LOW); // TURNS OFF PUMP
}

void waterPlantTest() {
  digitalWrite(RELAY_PIN, HIGH);
  delay(TIME_15_ML);
  digitalWrite(RELAY_PIN, LOW);
}

void joystickEvent(int event) {
  joystick.block = true;
  if (autoLcdOff) lastActionTime = millis();
  if (!lcdEnabled) return;

  switch (event) {
    case EVENT_UP: {
        if (!settingsOpened) {
          if (menuPage > 0) menuPage--;
          else if (menuPage == 0) menuPage = 4;
        }
        else {
          if (settingsPage > 0) settingsPage--;
          else if (settingsPage == 0) settingsPage = 9;
        }
        break;
      }
    case EVENT_DOWN: {
        if (!settingsOpened) {
          if (menuPage < 4) menuPage++;
          else if (menuPage == 4) menuPage = 0;
        }
        else {
          if (settingsPage < 9) settingsPage++;
          else if (settingsPage == 9) settingsPage = 0;
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
        if (menuPage == 4 && !settingsOpened) {
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
                wateringAmount = settingsWatering1[wateringAmountNo];
                EEPROM.write(EEPROM_WATERING_AMOUNT_ADDR, wateringAmountNo);
                break;
              }
            case 1: {
                if (wateringIntervalNo < 5) wateringIntervalNo++;
                else if (wateringIntervalNo == 5) wateringIntervalNo = 0;
                wateringInterval = settingsWatering2[wateringIntervalNo];

                // Update next watering DateTime
                wateringDateTime = rtc.now() + TimeSpan(wateringInterval / 24,
                                                        wateringInterval % 24, 0, 0);
                Serial.print("Setting new watering time: ");
                Serial.println(wateringDateTime.timestamp(DateTime::TIMESTAMP_FULL));

                EEPROM.write(EEPROM_WATERING_INTERVAL_ADDR, wateringIntervalNo);
                break;
              }
            case 2: {
                if (moistureLevelNo < 9) moistureLevelNo++;
                else if (moistureLevelNo == 9) moistureLevelNo = 0;
                moistureLevel = settingsMoisture[moistureLevelNo];
                EEPROM.write(EEPROM_MOISTURE_LEVEL_ADDR, moistureLevelNo);
                break;
              }
            case 3: {
                if (screenBrightnessNo < 4) screenBrightnessNo++;
                else if (screenBrightnessNo == 4) screenBrightnessNo = 0;
                screenBrightness = settingsBrightness[screenBrightnessNo];
                setScreenBrightness(screenBrightness);
                EEPROM.write(EEPROM_SCREEN_BRIGHTNESS_ADDR, screenBrightnessNo);
                break;
              }
            case 4: {
                if (autoLcdOffNo < 3) autoLcdOffNo++;
                else if (autoLcdOffNo == 3) autoLcdOffNo = 0;
                autoLcdOff = settingsAutoLcdOff[autoLcdOffNo];
                EEPROM.write(EEPROM_AUTO_LCD_OFF_ADDR, autoLcdOffNo);
                break;
              }
            case 5: {
                waterPlantTest();
                break;
              }
            case 6: {
                rtc.adjust(DateTime(rtc.now().year(),
                                    rtc.now().month(),
                                    rtc.now().day(),
                                    rtc.now().hour() + 1,
                                    rtc.now().minute(),
                                    rtc.now().second()));
                break;
              }
            case 7: {
                rtc.adjust(DateTime(rtc.now().year(),
                                    rtc.now().month(),
                                    rtc.now().day(),
                                    rtc.now().hour() - 1,
                                    rtc.now().minute(),
                                    rtc.now().second()));
                break;
              }
            case 8: {
                rtc.adjust(DateTime(rtc.now().year(),
                                    rtc.now().month(),
                                    rtc.now().day(),
                                    rtc.now().hour(),
                                    rtc.now().minute() + 1,
                                    rtc.now().second()));
                break;
              }
            case 9: {
                rtc.adjust(DateTime(rtc.now().year(),
                                    rtc.now().month(),
                                    rtc.now().day(),
                                    rtc.now().hour(),
                                    rtc.now().minute() - 1,
                                    rtc.now().second()));
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
  Serial.print("Setting brightness: ");
  int mappedVal = map(val, 0, 100, 0, 255);
  Serial.println(mappedVal);
  analogWrite(LCD_BRIGHTNESS_PIN, mappedVal);
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


void loadSettingsFromEEPROM() {
  uint8_t temp_wateringAmountNo = EEPROM.read(EEPROM_WATERING_AMOUNT_ADDR);
  uint8_t temp_wateringIntervalNo = EEPROM.read(EEPROM_WATERING_INTERVAL_ADDR);
  uint8_t temp_moistureLevelNo = EEPROM.read(EEPROM_MOISTURE_LEVEL_ADDR);
  uint8_t temp_screenBrightnessNo = EEPROM.read(EEPROM_SCREEN_BRIGHTNESS_ADDR);
  uint8_t temp_autoLcdOffNo = EEPROM.read(EEPROM_AUTO_LCD_OFF_ADDR);

  if (temp_wateringAmountNo != 255) {
    wateringAmountNo = temp_wateringAmountNo;
    wateringAmount = settingsWatering1[wateringAmountNo];
  }

  if (temp_wateringIntervalNo != 255) {
    wateringIntervalNo = temp_wateringIntervalNo;
    wateringInterval = settingsWatering2[wateringIntervalNo];
  }

  if (temp_moistureLevelNo != 255) {
    moistureLevelNo = temp_moistureLevelNo;
    moistureLevel = settingsMoisture[moistureLevelNo];
  }

  if (temp_screenBrightnessNo != 255) {
    screenBrightnessNo = temp_screenBrightnessNo;
    screenBrightness = settingsBrightness[screenBrightnessNo];
    setScreenBrightness(screenBrightness);
  }

  if (temp_autoLcdOffNo != 255) {
    autoLcdOffNo = temp_autoLcdOffNo;
    autoLcdOff = settingsAutoLcdOff[autoLcdOffNo];
  }
}

void clearEEPROM() {
  for (int i = 0 ; i < EEPROM.length() ; i++) {
    EEPROM.write(i, 255);
  }
}
