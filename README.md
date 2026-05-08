# Step Counter

Arduino-based step counter for the **M5StickC** that uses the onboard IMU to detect steps and display activity stats directly on the device screen.

This version goes beyond a basic step count and adds a multi-screen interface with **cadence tracking**, **activity classification**, **hourly step history**, a **daily goal progress bar**, a **calorie estimate**, and a **live web dashboard** accessible from any browser on the same network.

## Overview

This project reads acceleration data from the M5StickC IMU, smooths it with a rolling average, and uses a peak/valley detection method to count steps. The device then updates a compact on-screen dashboard so you can monitor your movement in real time.

## Features

- Real-time step counting using the M5StickC IMU
- Rolling-average + peak/valley step detection algorithm
- Cadence calculation in **steps per minute**
- Activity classification:
    - **Idle**
    - **Walking**
    - **Running**
- **4-screen UI** on the M5StickC display
- **10,000-step daily goal** progress bar
- **Estimated calories burned** display
- **12-hour hourly step history** bar chart
- Button controls for **screen switching** and **session reset**
- Lightweight on-device interface with periodic UI refresh
- **WiFi dashboard** served from the device, viewable in any browser on the same network

## Screens

### 1. Steps Screen

Shows:

- Total step count
- Current activity badge (**IDLE / WALK / RUN**)
- Progress bar toward the daily goal

### 2. Cadence Screen

Shows:

- Current cadence in **steps/min**
- Total step count
- Estimated calories burned

### 3. History Screen

Shows:

- Bar chart of step counts across the last **12 tracked hourly buckets**
- Highlight for the current active hour
- Current hour step total

### 4. Wi-Fi Screen

Shows:

- WiFi connection status
- Connected network name
- Device IP address for accessing the web dashboard

## Web Dashboard

When connected to WiFi, the M5StickC hosts a web dashboard at its local IP address (visible on the Wi-Fi screen). Open that address in any browser on the same network to view a live stats page that updates every 3 seconds.

The dashboard shows:

- Total step count and progress toward the daily goal
- Current cadence and activity state
- Estimated calories burned
- Current hour step count
- Hourly history bar chart

### WiFi Setup

WiFi credentials are configured at runtime using **WiFiManager**. On first boot, the device broadcasts a hotspot called `M5StickC-Step Counter`. Connect to it from your phone or laptop and a setup page will appear where you can select your network and enter the password. Credentials are saved to flash and used automatically on future boots.

To reconfigure WiFi (e.g. when switching networks), hold **BtnA** while the device boots to clear the saved credentials and re-enter setup mode.

## Controls

| Button   | Action                                   |
| -------- | ---------------------------------------- |
| **BtnA** | Reset steps, cadence, and hourly history |
| **BtnB** | Cycle between the 4 screens              |

## Hardware and Software Requirements

### Hardware

- M5StickC
- USB cable for programming/power

### Software

- Arduino IDE
- M5StickC board/package support in Arduino, version 2.0.17

#### Required Arduino Libraries

- M5StickC by M5Stack, version 0.3.0
- WiFiManager by tzapu, version 2.0.17

## Installation and Upload

1. Install the **Arduino IDE**.
2. Install the **M5Stack board manager/package, version 2.0.17** and select **M5StickC** as the target board.
3. Install the [required Arduino libraries](#required-arduino-libraries).
4. Clone this repository or download the source.
5. Open `stepcounter.ino` in Arduino IDE.
6. Connect your M5StickC by USB.
7. Select the correct board and serial port.
8. Compile and upload the sketch.

## How It Works

### Step Detection

The sketch reads accelerometer values `(ax, ay, az)` from the IMU and computes the acceleration magnitude. It then:

1. Stores recent magnitude readings in a rolling window
2. Computes a rolling average
3. Measures the difference between the current reading and the average
4. Uses a **peak/valley state machine** to detect a valid step
5. Enforces a minimum time between steps to reduce false positives

### Cadence Tracking

Each detected step is timestamped. Recent timestamps are used to estimate cadence over a short moving window and convert it into **steps per minute (SPM)**.

### Activity Classification

Activity state is determined from cadence:

- **Idle**: below walking threshold
- **Walking**: moderate cadence
- **Running**: high cadence

### Hourly History

The app stores step totals in hourly buckets and rotates through a fixed-size history buffer so recent activity can be shown as a compact bar chart.

## Current Tuning Values

These values are defined near the top of the sketch and can be adjusted to tune detection sensitivity and UI behavior:

| Parameter           |   Value | Purpose                                   |
| ------------------- | ------: | ----------------------------------------- |
| `WINDOW_SIZE`       |    `20` | Rolling average buffer size               |
| `PEAK_THRESHOLD`    |  `0.12` | Minimum peak needed to consider a step    |
| `MIN_STEP_MS`       |   `300` | Minimum time between valid steps          |
| `VALLEY_RESET`      |  `0.06` | Rearming threshold for the detector       |
| `PEAK_MIN_MS`       |    `60` | Minimum duration a peak must be sustained |
| `CADENCE_WINDOW_MS` |  `5000` | Time window used for cadence calculation  |
| `WALKING_CADENCE`   |    `60` | Walking threshold in steps/min            |
| `RUNNING_CADENCE`   |   `140` | Running threshold in steps/min            |
| `DAILY_GOAL`        | `10000` | Daily step goal                           |
| `HOURS_TRACKED`     |    `12` | Number of hourly history buckets          |
| `UI_UPDATE_MS`      |   `200` | UI refresh interval                       |

## Project Structure

```text
step-counter/
├── README.md
├── stepcounter.ino
└── wifi_server.ino
```
