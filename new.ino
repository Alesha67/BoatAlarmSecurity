#include <TFT_eSPI.h>
#include <Preferences.h>
#include "Back.h"

#define BUTTON_DOWN   0
#define BUTTON_SELECT 14

#define PIN_RED    1
#define PIN_GREEN  2
#define PIN_BLUE   3
#define BUZZER_PIN 10

TFT_eSPI tft = TFT_eSPI();
Preferences prefs;

const int rectX = 15;
const int rectYStart = 125;
const int stepY = 32;
const int rectWidth = 135;
const int rectHeight = 25;
const int cornerRadius = 6;
int currentPos = 0;

unsigned long lastPress = 0;
const int debounce = 200;

bool alarmActive = false;
unsigned long lastCheckTime = 0;
const unsigned long checkInterval = 3000;

bool redConnected = false;
bool greenConnected = false;
bool blueConnected = false;

bool buzzerActive = false;
unsigned long lastBuzzerChange = 0;

float resistanceMin = 500.0;
float resistanceMax = 2500.0;

bool inThresholdMenu = false;
bool adjustingMin = true;

void drawBackground() {
  tft.pushImage(0, 0, 170, 320, (uint16_t*)Back);
}

void eraseRoundedOutline(int pos) {
  int y = rectYStart + pos * stepY;
  for (int dy = -2; dy < rectHeight + 2; dy++) {
    for (int dx = -2; dx < rectWidth + 2; dx++) {
      int px = rectX + dx;
      int py = y + dy;
      if (px >= 0 && px < 170 && py >= 0 && py < 320) {
        uint16_t bgColor = pgm_read_word(&Back[py * 170 + px]);
        tft.drawPixel(px, py, bgColor);
      }
    }
  }
}

void drawRoundedOutline(int pos, uint16_t color) {
  int y = rectYStart + pos * stepY;
  tft.drawRoundRect(rectX, y, rectWidth, rectHeight, cornerRadius, color);
}

void animateRoundedOutline(int pos, uint16_t color) {
  int y = rectYStart + pos * stepY;
  for (int thickness = 1; thickness <= 3; thickness++) {
    tft.drawRoundRect(rectX - thickness + 1, y - thickness + 1,
                      rectWidth + (thickness - 1) * 2,
                      rectHeight + (thickness - 1) * 2,
                      cornerRadius + thickness / 2, color);
    delay(30);
  }
}

void drawStatusText() {
  for (int y = 220; y < 280; y++) {
    for (int x = 0; x < 170; x++) {
      uint16_t color = pgm_read_word(&Back[y * 170 + x]);
      tft.drawPixel(x, y, color);
    }
  }
}

void drawThresholdMenu() {
  drawStatusText();

  tft.setTextColor(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1); // увеличенный шрифт

  tft.drawString("RESISTANCE", 85, 230);

  tft.setTextColor(adjustingMin ? TFT_BLACK : TFT_BLACK);
  tft.drawString("Min: " + String((int)resistanceMin), 50, 260);

  tft.setTextColor(!adjustingMin ? TFT_BLACK : TFT_BLACK);
  tft.drawString("Max: " + String((int)resistanceMax), 110, 260);
}

void activateBuzzer() {
  if (!buzzerActive) {
    buzzerActive = true;
    ledcAttachPin(BUZZER_PIN, 3);
    ledcSetup(3, 500, 10);
    ledcWriteTone(3, 500);
    lastBuzzerChange = millis();
  }
}

void updateBuzzer() {
  if (!buzzerActive) return;

  static bool rising = true;
  static unsigned long lastUpdate = 0;
  static int freq = 500;

  if (millis() - lastUpdate > 10) {
    lastUpdate = millis();

    if (rising) {
      freq += 2;
      if (freq >= 1800) rising = false;
    } else {
      freq -= 2;
      if (freq <= 500) rising = true;
    }
    ledcWriteTone(3, freq);
  }

  static bool lightState = false;
  static unsigned long lastBlink = 0;

  if (millis() - lastBlink > 200) {
    lastBlink = millis();
    lightState = !lightState;

    digitalWrite(PIN_RED, lightState ? HIGH : LOW);
    digitalWrite(PIN_BLUE, lightState ? LOW : HIGH);
  }
}

void deactivateBuzzer() {
  buzzerActive = false;
  ledcWriteTone(3, 0);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(PIN_RED, LOW);
  digitalWrite(PIN_BLUE, LOW);
}

void checkChannel(uint8_t pin, const char* label, bool& connected) {
  int adcValue;

  pinMode(pin, OUTPUT);
  digitalWrite(pin, HIGH);
  delay(150);

  pinMode(pin, INPUT);
  delay(20);
  adcValue = analogRead(pin);

  float voltage = adcValue * (3.3 / 4095.0);
  float resistance = (voltage > 0.01) ? (3.3 - voltage) * 1000 / voltage : 9999.0;
  connected = (voltage > 0.1);

  Serial.printf("%s_ADC: %d | Voltage: %.2f V | Resistance: %.2f kOhm (%s)\n",
                label, adcValue, voltage, resistance / 1000.0, connected ? "OK" : "--");

  if (resistance < resistanceMin || resistance > resistanceMax) {
    activateBuzzer();
  }

  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
}

void checkAllChannels() {
  checkChannel(PIN_RED,   "🔴 R", redConnected);
  checkChannel(PIN_GREEN, "🟢 G", greenConnected);
  checkChannel(PIN_BLUE,  "🔵 B", blueConnected);
  drawStatusText();
}

void setup() {
  Serial.begin(115200);

  pinMode(BUTTON_DOWN, INPUT_PULLUP);
  pinMode(BUTTON_SELECT, INPUT_PULLUP);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  prefs.begin("alarm", false);
  resistanceMin = prefs.getFloat("resMin", 500.0);
  resistanceMax = prefs.getFloat("resMax", 2500.0);

  tft.init();
  tft.setRotation(0);
  tft.setSwapBytes(true);
  tft.fillScreen(TFT_BLACK);

  drawBackground();
  animateRoundedOutline(currentPos, TFT_RED);
  drawStatusText();
}

void loop() {
  if (alarmActive && millis() - lastCheckTime >= checkInterval) {
    lastCheckTime = millis();
    checkAllChannels();
  }

  updateBuzzer();

  if (millis() - lastPress > debounce) {

    if (inThresholdMenu) {
      if (digitalRead(BUTTON_DOWN) == LOW) {
        lastPress = millis();
        if (adjustingMin) {
          resistanceMin += 100;
          if (resistanceMin > 2500) resistanceMin = 100;
        } else {
          resistanceMax += 100;
          if (resistanceMax > 7500) resistanceMax = 2500;
        }
        drawThresholdMenu();
      }

      if (digitalRead(BUTTON_SELECT) == LOW) {
        lastPress = millis();
        if (adjustingMin) {
          adjustingMin = false;
        } else {
          // Сохраняем значения в Flash
          prefs.putFloat("resMin", resistanceMin);
          prefs.putFloat("resMax", resistanceMax);
          adjustingMin = true;
          inThresholdMenu = false;
          drawStatusText();
        }
        drawThresholdMenu();
      }
      return;
    }

    if (digitalRead(BUTTON_DOWN) == LOW) {
      lastPress = millis();
      eraseRoundedOutline(currentPos);
      currentPos = (currentPos + 1) % 3;
      animateRoundedOutline(currentPos, TFT_RED);
    }

    if (digitalRead(BUTTON_SELECT) == LOW) {
      lastPress = millis();
      animateRoundedOutline(currentPos, TFT_GREEN);
      delay(300);
      animateRoundedOutline(currentPos, TFT_RED);

      if (currentPos == 0) {
        alarmActive = true;
        lastCheckTime = millis();
        tone(BUZZER_PIN, 1500, 250);
      } else if (currentPos == 1) {
        alarmActive = false;
        digitalWrite(PIN_RED, LOW);
        digitalWrite(PIN_GREEN, LOW);
        digitalWrite(PIN_BLUE, LOW);
        deactivateBuzzer();
      } else if (currentPos == 2) {
        inThresholdMenu = true;
        adjustingMin = true;
        drawThresholdMenu();
      }
    }
  }
}
