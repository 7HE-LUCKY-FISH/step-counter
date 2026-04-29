#include <M5StickC.h>

int stepCount = 0;
int dailyGoal = 10000; // daily step goal
float accX, accY, accZ;
float magnitude = 0;
float threshold = 1.35; // sensitivity threshold

// Debounce variables
unsigned long lastStepTime = 0;
int stepDelay = 300; 

void setup() {
  M5.begin();
  M5.Imu.Init(); // initialize the MPU6886 sensor
  
  M5.Lcd.setRotation(3); // set screen horizontal (160x80 resolution)
  M5.Lcd.fillScreen(BLACK);
  
  // draw static UI elements (drawn once to save processing power)
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setCursor(5, 5);
  M5.Lcd.print("Steps:");
  
  // draw static outline for progress bar at the bottom
  M5.Lcd.drawRect(5, 60, 150, 15, WHITE);
  
  updateScreen();
}

void loop() {
  M5.update(); // update button states

  // read accelerometer data
  M5.Imu.getAccelData(&accX, &accY, &accZ);

  // calculate magnitude
  magnitude = sqrt((accX * accX) + (accY * accY) + (accZ * accZ));

  // peak detection logic
  if (magnitude > threshold) {
    if (millis() - lastStepTime > stepDelay) {
      stepCount++;
      lastStepTime = millis();
      updateScreen();
    }
  }

  // Button A (front button) resets the counter
  if (M5.BtnA.wasPressed()) {
    stepCount = 0;
    updateScreen();
  }

  delay(20); 
}

void updateScreen() {
  // update text
  // clear only the area where the number is so it doesn't flicker the whole screen
  M5.Lcd.fillRect(5, 25, 150, 30, BLACK); 
  
  M5.Lcd.setCursor(5, 25);
  M5.Lcd.setTextSize(4);
  M5.Lcd.setTextColor(TFT_GREEN);
  M5.Lcd.print(stepCount);
  
  // update progress bar
  // calculate completion percentage
  float progress = (float)stepCount / dailyGoal;
  if (progress > 1.0) progress = 1.0; // cap at 100% so it doesn't draw off-screen
  
  // map percentage to the pixel width inside our outline (max inner width is 146px)
  int barWidth = (int)(146 * progress);
  
  // fill the active portion of the progress bar with blue
  M5.Lcd.fillRect(7, 62, barWidth, 11, TFT_BLUE);
  
  // clear the remaining empty space in the bar (important if you reset the counter)
  if (barWidth < 146) {
    M5.Lcd.fillRect(7 + barWidth, 62, 146 - barWidth, 11, BLACK);
  }
}