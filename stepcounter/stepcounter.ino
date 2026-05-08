#include <M5StickC.h>
#include <math.h>
#include <WiFi.h>
#include "esp_sleep.h"

// Step algorithm tuning
#define WINDOW_SIZE       20
#define PEAK_THRESHOLD    0.12f
#define MIN_STEP_MS       300
#define VALLEY_RESET      0.06f
#define PEAK_MIN_MS       60

// Cadence / activity thresholds
#define CADENCE_WINDOW_MS 5000
#define RUNNING_CADENCE   140
#define WALKING_CADENCE   60

// Screen IDs
#define SCREEN_COUNT    4
#define SCREEN_STEPS    0
#define SCREEN_CADENCE  1
#define SCREEN_HISTORY  2
#define SCREEN_WIFI     3

#define DAILY_GOAL      10000
#define HOURS_TRACKED   12
#define UI_UPDATE_MS    200
#define IDLE_SLEEP_MS   15000UL

// Colors
#define COLOR_BG        TFT_BLACK
#define COLOR_ACCENT    0x07FF
#define COLOR_RUN       0xF800
#define COLOR_WALK      0x07E0
#define COLOR_IDLE      0x8410
#define COLOR_BAR_EMPTY 0x2104
#define COLOR_GOAL      0xFFE0
#define COLOR_WHITE     TFT_WHITE
#define COLOR_DIM       0x7BEF

// MPU6886 registers for wake-on-motion
#define MPU6886_ADDR    0x68
#define MPU6886_WOM_THR 0x1F
#define MPU6886_MOT_DET 0x69
#define MPU6886_INT_EN  0x38

float magBuffer[WINDOW_SIZE];
int   magIndex    = 0;
float magSum      = 0.0f;
bool  bufferReady = false;

float peakMag        = 0.0f;
bool  armed          = true;
unsigned long lastStepMs     = 0;
unsigned long peakStartMs    = 0;
bool          aboveThreshold = false;

int stepCount  = 0;
int cadenceSPM = 0;

#define CADENCE_BUF_SIZE 20
unsigned long stepTimes[CADENCE_BUF_SIZE];
int stepTimeHead   = 0;
int stepTimeFilled = 0;

int hourlySteps[HOURS_TRACKED];
int currentHour = 0;
unsigned long hourStartMs = 0;

int  currentScreen   = SCREEN_STEPS;
bool needsFullRedraw = true;
unsigned long lastUiMs = 0;

unsigned long lastMotionMs = 0;
bool screenOn = true;

enum Activity { IDLE, WALKING, RUNNING };
Activity currentActivity = IDLE;


void imuWriteReg(uint8_t reg, uint8_t val) {
  Wire1.beginTransmission(MPU6886_ADDR);
  Wire1.write(reg);
  Wire1.write(val);
  Wire1.endTransmission();
}

void enableMotionInterrupt() {
  imuWriteReg(MPU6886_WOM_THR, 10);   // ~39mg threshold
  imuWriteReg(MPU6886_MOT_DET, 0xC0);
  imuWriteReg(MPU6886_INT_EN,  0x40);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_36, 1);
}

void enterLightSleep() {
  M5.Lcd.fillScreen(COLOR_BG);
  M5.Axp.SetLDO2(false);
  screenOn = false;

  esp_light_sleep_start();

  M5.Axp.SetLDO2(true);
  screenOn        = true;
  lastMotionMs    = millis();
  needsFullRedraw = true;
}


void setup() {
  Serial.begin(115200);
  M5.begin();
  M5.Imu.Init();
  Wire1.begin(21, 22);

  M5.Lcd.setRotation(3);
  M5.Lcd.fillScreen(COLOR_BG);

  memset(magBuffer,   0, sizeof(magBuffer));
  memset(hourlySteps, 0, sizeof(hourlySteps));
  memset(stepTimes,   0, sizeof(stepTimes));

  hourStartMs  = millis();
  lastMotionMs = millis();

  enableMotionInterrupt();
  drawFullScreen();
  startWiFiServer();
}


void loop() {
  M5.update();

  if (M5.BtnB.wasPressed()) {
    currentScreen = (currentScreen + 1) % SCREEN_COUNT;
    needsFullRedraw = true;
    lastMotionMs = millis();
  }

  if (M5.BtnA.wasPressed() && currentScreen != SCREEN_WIFI) {
    stepCount      = 0;
    cadenceSPM     = 0;
    stepTimeFilled = 0;
    stepTimeHead   = 0;
    memset(hourlySteps, 0, sizeof(hourlySteps));
    currentHour  = 0;
    hourStartMs  = millis();
    lastMotionMs = millis();
    needsFullRedraw = true;
  }

  if (millis() - hourStartMs >= 3600000UL) {
    currentHour = (currentHour + 1) % HOURS_TRACKED;
    hourlySteps[currentHour] = 0;
    hourStartMs = millis();
  }

  processAccelerometer();
  handleWiFiClient();

  if (millis() - lastMotionMs >= IDLE_SLEEP_MS) {
    enterLightSleep();
  }

  if (needsFullRedraw) {
    drawFullScreen();
    needsFullRedraw = false;
    lastUiMs = millis();
  } else if (millis() - lastUiMs >= UI_UPDATE_MS) {
    updateDynamic();
    lastUiMs = millis();
  }

  delay(10);
}


void processAccelerometer() {
  float ax, ay, az;
  M5.Imu.getAccelData(&ax, &ay, &az);

  float mag = sqrtf(ax*ax + ay*ay + az*az);

  magSum -= magBuffer[magIndex];
  magBuffer[magIndex] = mag;
  magSum += mag;
  magIndex = (magIndex + 1) % WINDOW_SIZE;
  if (magIndex == 0) bufferReady = true;

  if (!bufferReady) return;

  float avg   = magSum / WINDOW_SIZE;
  float delta = mag - avg;
  unsigned long now = millis();

  if (delta > PEAK_THRESHOLD * 0.5f) {
    lastMotionMs = now;
  }

  if (armed) {
    if (delta > PEAK_THRESHOLD && !aboveThreshold) {
      aboveThreshold = true;
      peakStartMs    = now;
    }
    if (!aboveThreshold && delta <= PEAK_THRESHOLD) {
      peakMag = 0.0f;
    }

    if (aboveThreshold) {
      if (delta > peakMag) peakMag = delta;

      if (peakMag > PEAK_THRESHOLD && delta < peakMag - VALLEY_RESET) {
        if (now - peakStartMs >= PEAK_MIN_MS && now - lastStepMs > MIN_STEP_MS) {
          recordStep(now);
        }
        armed          = false;
        aboveThreshold = false;
        peakMag        = 0.0f;
      }
    }
  } else {
    if (delta < VALLEY_RESET) {
      armed          = true;
      aboveThreshold = false;
    }
  }

  updateCadence(now);
  classifyActivity();
}

void recordStep(unsigned long now) {
  stepCount++;
  hourlySteps[currentHour]++;

  stepTimes[stepTimeHead] = now;
  stepTimeHead = (stepTimeHead + 1) % CADENCE_BUF_SIZE;
  if (stepTimeFilled < CADENCE_BUF_SIZE) stepTimeFilled++;

  lastStepMs   = now;
  lastMotionMs = now;
}

void updateCadence(unsigned long now) {
  if (stepTimeFilled < 2) {
    cadenceSPM = 0;
    return;
  }
  int recentSteps = 0;
  for (int i = 0; i < stepTimeFilled; i++) {
    if (now - stepTimes[i] <= CADENCE_WINDOW_MS) recentSteps++;
  }
  cadenceSPM = (int)((recentSteps * 60000.0f) / CADENCE_WINDOW_MS);
}

void classifyActivity() {
  if (cadenceSPM >= RUNNING_CADENCE)      currentActivity = RUNNING;
  else if (cadenceSPM >= WALKING_CADENCE) currentActivity = WALKING;
  else                                    currentActivity = IDLE;
}


void drawBattery() {
  float batV   = M5.Axp.GetBatVoltage();
  int   batPct = (int)constrain((batV - 3.0f) / (4.2f - 3.0f) * 100.0f, 0.0f, 100.0f);

  M5.Lcd.fillRect(0, 0, 40, 10, COLOR_BG);
  M5.Lcd.setTextSize(1);

  uint16_t col;
  if      (batPct > 50) col = COLOR_WALK;
  else if (batPct > 20) col = COLOR_GOAL;
  else                  col = COLOR_RUN;

  M5.Lcd.setTextColor(col);
  M5.Lcd.setCursor(0, 2);
  M5.Lcd.print(batPct);
  M5.Lcd.print("%");
}


void drawFullScreen() {
  M5.Lcd.fillScreen(COLOR_BG);
  switch (currentScreen) {
    case SCREEN_STEPS:   drawStepsChrome();   break;
    case SCREEN_CADENCE: drawCadenceChrome(); break;
    case SCREEN_HISTORY: drawHistoryChrome(); break;
    case SCREEN_WIFI:    drawWifiChrome();    break;
  }
  updateDynamic();
}

void drawStepsChrome() {
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(COLOR_DIM);
  M5.Lcd.setCursor(44, 4);
  M5.Lcd.print("STEPS TODAY");
  drawScreenDots();
  M5.Lcd.drawRect(5, 62, 140, 10, COLOR_DIM);
  M5.Lcd.setCursor(148, 64);
  M5.Lcd.print("G");
}

void drawCadenceChrome() {
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(COLOR_DIM);
  M5.Lcd.setCursor(44, 4);
  M5.Lcd.print("CADENCE");
  M5.Lcd.setCursor(90, 4);
  M5.Lcd.print("steps/min");
  drawScreenDots();
  M5.Lcd.drawFastHLine(0, 50, 160, COLOR_DIM);
  M5.Lcd.setCursor(5, 55);
  M5.Lcd.print("TOTAL");
  M5.Lcd.setCursor(90, 55);
  M5.Lcd.print("CALORIES");
}

void drawHistoryChrome() {
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(COLOR_DIM);
  M5.Lcd.setCursor(44, 4);
  M5.Lcd.print("HOURLY HISTORY");
  drawScreenDots();
  M5.Lcd.drawFastHLine(5, 68, 150, COLOR_DIM);
}

void drawWifiChrome() {
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(COLOR_DIM);
  M5.Lcd.setCursor(44, 4);
  M5.Lcd.print("WI-FI");
  drawScreenDots();
  M5.Lcd.drawFastHLine(0, 14, 160, COLOR_DIM);
}

void drawScreenDots() {
  for (int i = 0; i < SCREEN_COUNT; i++) {
    uint16_t col = (i == currentScreen) ? COLOR_ACCENT : COLOR_DIM;
    M5.Lcd.fillCircle(147 + i * 5, 4, 1, col);
  }
}


void updateDynamic() {
  drawBattery();
  switch (currentScreen) {
    case SCREEN_STEPS:   updateStepsDynamic();   break;
    case SCREEN_CADENCE: updateCadenceDynamic(); break;
    case SCREEN_HISTORY: updateHistoryDynamic(); break;
    case SCREEN_WIFI:    updateWifiDynamic();    break;
  }
}

void updateStepsDynamic() {
  M5.Lcd.fillRect(100, 0, 44, 12, COLOR_BG);

  uint16_t badgeColor;
  const char* label;
  switch (currentActivity) {
    case RUNNING: badgeColor = COLOR_RUN;  label = "RUN";  break;
    case WALKING: badgeColor = COLOR_WALK; label = "WALK"; break;
    default:      badgeColor = COLOR_IDLE; label = "IDLE"; break;
  }
  M5.Lcd.fillRoundRect(100, 1, 44, 10, 3, badgeColor);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(COLOR_BG);
  M5.Lcd.setCursor(104, 3);
  M5.Lcd.print(label);

  M5.Lcd.fillRect(5, 16, 145, 40, COLOR_BG);
  M5.Lcd.setTextSize(4);
  M5.Lcd.setTextColor((stepCount >= DAILY_GOAL) ? COLOR_GOAL : COLOR_ACCENT);
  M5.Lcd.setCursor(5, 18);
  M5.Lcd.print(stepCount);

  float pct    = constrain((float)stepCount / DAILY_GOAL, 0.0f, 1.0f);
  int   filled = (int)(138 * pct);
  M5.Lcd.fillRect(6, 63, 138, 8, COLOR_BAR_EMPTY);
  if (filled > 0) {
    M5.Lcd.fillRect(6, 63, filled, 8, (pct >= 1.0f) ? COLOR_GOAL : COLOR_ACCENT);
  }
}

void updateCadenceDynamic() {
  M5.Lcd.fillRect(5, 14, 155, 32, COLOR_BG);

  uint16_t cadColor;
  switch (currentActivity) {
    case RUNNING: cadColor = COLOR_RUN;  break;
    case WALKING: cadColor = COLOR_WALK; break;
    default:      cadColor = COLOR_IDLE; break;
  }
  M5.Lcd.setTextSize(3);
  M5.Lcd.setTextColor(cadColor);
  M5.Lcd.setCursor(5, 16);
  M5.Lcd.print(cadenceSPM);

  M5.Lcd.fillRect(5, 60, 155, 18, COLOR_BG);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(COLOR_WHITE);
  M5.Lcd.setCursor(5, 62);
  M5.Lcd.print(stepCount);

  int calories = (int)(stepCount * 0.04f);
  M5.Lcd.setCursor(90, 62);
  M5.Lcd.print(calories);
  M5.Lcd.print(" kcal");
}

void updateHistoryDynamic() {
  int maxVal = 1;
  for (int i = 0; i < HOURS_TRACKED; i++) {
    if (hourlySteps[i] > maxVal) maxVal = hourlySteps[i];
  }

  int barAreaTop    = 15;
  int barAreaBottom = 67;
  int barHeight     = barAreaBottom - barAreaTop;
  int barW          = 150 / HOURS_TRACKED;

  for (int i = 0; i < HOURS_TRACKED; i++) {
    int x = 5 + i * barW;
    int h = (int)((float)hourlySteps[i] / maxVal * barHeight);
    if (hourlySteps[i] > 0 && h < 2) h = 2;

    M5.Lcd.fillRect(x, barAreaTop, barW - 1, barHeight, COLOR_BAR_EMPTY);
    if (h > 0) {
      uint16_t col = (i == currentHour) ? COLOR_ACCENT : COLOR_WALK;
      M5.Lcd.fillRect(x, barAreaBottom - h, barW - 1, h, col);
    }
  }

  M5.Lcd.fillRect(5, 70, 100, 10, COLOR_BG);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(COLOR_DIM);
  M5.Lcd.setCursor(5, 71);
  M5.Lcd.print("now:");
  M5.Lcd.setTextColor(COLOR_ACCENT);
  M5.Lcd.print(hourlySteps[currentHour]);
}

void updateWifiDynamic() {
  M5.Lcd.fillRect(0, 16, 160, 64, COLOR_BG);
  M5.Lcd.setTextSize(1);

  if (WiFi.status() == WL_CONNECTED) {
    M5.Lcd.setTextColor(COLOR_WALK);
    M5.Lcd.setCursor(5, 20);
    M5.Lcd.print("Connected");

    M5.Lcd.setTextColor(COLOR_DIM);
    M5.Lcd.setCursor(5, 35);
    M5.Lcd.print("Network:");
    M5.Lcd.setTextColor(COLOR_WHITE);
    M5.Lcd.setCursor(5, 45);
    M5.Lcd.print(WiFi.SSID());

    M5.Lcd.setTextColor(COLOR_DIM);
    M5.Lcd.setCursor(5, 58);
    M5.Lcd.print("Dashboard:");
    M5.Lcd.setTextColor(COLOR_ACCENT);
    M5.Lcd.setCursor(5, 68);
    M5.Lcd.print(WiFi.localIP().toString());
  } else {
    M5.Lcd.setTextColor(COLOR_RUN);
    M5.Lcd.setCursor(5, 20);
    M5.Lcd.print("Not connected");

    M5.Lcd.setTextColor(COLOR_DIM);
    M5.Lcd.setCursor(5, 38);
    M5.Lcd.print("Hold A on boot to");
    M5.Lcd.setCursor(5, 50);
    M5.Lcd.print("reconfigure WiFi.");
  }
}
