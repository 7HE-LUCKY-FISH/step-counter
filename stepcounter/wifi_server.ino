#include <WiFiManager.h>
#include <WebServer.h>

WebServer server(80);
bool serverStarted = false;

#define WIFI_RETRY_MS 30000
unsigned long lastWifiRetryMs = 0;

extern int stepCount;
extern int cadenceSPM;
extern int hourlySteps[];
extern int currentHour;
extern Activity currentActivity;

void startWiFiServer() {
  WiFiManager wm;

  // Hold Button A on boot to forget saved credentials
  M5.update();
  if (M5.BtnA.isPressed()) {
    wm.resetSettings();
    M5.Lcd.fillScreen(TFT_BLACK);
    M5.Lcd.setTextColor(TFT_RED);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(5, 30);
    M5.Lcd.print("WiFi credentials cleared.");
    M5.Lcd.setCursor(5, 45);
    M5.Lcd.print("Release button to setup.");
    delay(2000);
  }

  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextColor(TFT_WHITE);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(5, 25);
  M5.Lcd.print("Connecting to WiFi...");
  M5.Lcd.setCursor(5, 40);
  M5.Lcd.print("Continuing in 10s if");
  M5.Lcd.setCursor(5, 52);
  M5.Lcd.print("no network found.");

  // Try to connect using saved credentials only
  wm.setConnectTimeout(10);
  wm.setConfigPortalTimeout(0);
  bool connected = wm.autoConnect("M5StickC-Step Counter");

  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextSize(1);

  if (connected) {
    beginServer();
    M5.Lcd.setTextColor(TFT_GREEN);
    M5.Lcd.setCursor(5, 20);
    M5.Lcd.print("Connected!");
    M5.Lcd.setTextColor(TFT_WHITE);
    M5.Lcd.setCursor(5, 35);
    M5.Lcd.print("Dashboard at:");
    M5.Lcd.setTextColor(0x07FF);
    M5.Lcd.setCursor(5, 50);
    M5.Lcd.print(WiFi.localIP().toString());
    delay(3000);
  } else {
    M5.Lcd.setTextColor(0x8410);
    M5.Lcd.setCursor(5, 30);
    M5.Lcd.print("No WiFi. Tracking");
    M5.Lcd.setCursor(5, 45);
    M5.Lcd.print("steps offline.");
    delay(2000);
  }

  lastWifiRetryMs = millis();
}

void beginServer() {
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();
  serverStarted = true;
}

void handleWiFiClient() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!serverStarted) {
      // WiFi came up after boot, start the server now
      beginServer();
    }
    server.handleClient();
  } else {
    serverStarted = false;

    // Periodically try to reconnect
    if (millis() - lastWifiRetryMs >= WIFI_RETRY_MS) {
      lastWifiRetryMs = millis();
      WiFi.reconnect();
    }
  }
}

void handleRoot() {
  String html = R"(<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Step Counter Dashboard</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      background: #0a0a0a;
      color: #fff;
      min-height: 100vh;
      padding: 24px 16px;
    }
    h1 {
      font-size: 1.1rem;
      font-weight: 500;
      color: #666;
      margin-bottom: 24px;
      letter-spacing: 0.1em;
      text-transform: uppercase;
    }
    .grid {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 12px;
      max-width: 480px;
      margin: 0 auto 24px;
    }
    .card {
      background: #161616;
      border-radius: 12px;
      padding: 16px;
    }
    .card.wide { grid-column: span 2; }
    .label {
      font-size: 0.7rem;
      color: #555;
      text-transform: uppercase;
      letter-spacing: 0.08em;
      margin-bottom: 6px;
    }
    .value {
      font-size: 2.2rem;
      font-weight: 600;
      color: #00ffff;
      line-height: 1;
    }
    .value.goal { color: #ffe000; }
    .unit {
      font-size: 0.75rem;
      color: #444;
      margin-top: 4px;
    }
    .badge {
      display: inline-block;
      padding: 3px 10px;
      border-radius: 20px;
      font-size: 0.75rem;
      font-weight: 600;
    }
    .badge.RUNNING { background: #f80000; color: #fff; }
    .badge.WALKING { background: #07e000; color: #000; }
    .badge.IDLE    { background: #333;    color: #888; }
    .progress-wrap {
      background: #222;
      border-radius: 6px;
      height: 10px;
      margin-top: 10px;
      overflow: hidden;
    }
    .progress-bar {
      height: 100%;
      border-radius: 6px;
      background: #00ffff;
      transition: width 0.4s ease;
    }
    .bars {
      display: flex;
      align-items: flex-end;
      gap: 3px;
      height: 80px;
      margin-top: 10px;
    }
    .bar {
      flex: 1;
      background: #07e000;
      border-radius: 3px 3px 0 0;
      min-height: 2px;
    }
    .bar.current { background: #00ffff; }
    .footer {
      text-align: center;
      font-size: 0.7rem;
      color: #333;
      max-width: 480px;
      margin: 0 auto;
    }
  </style>
</head>
<body>
  <h1>Step Counter</h1>
  <div class="grid" id="grid">
    <div class="card wide">
      <div class="label">Steps Today</div>
      <div class="value" id="steps">--</div>
      <div class="progress-wrap">
        <div class="progress-bar" id="progress" style="width:0%"></div>
      </div>
      <div class="unit" id="goal-label">-- / 10000</div>
    </div>
    <div class="card">
      <div class="label">Cadence</div>
      <div class="value" id="cadence">--</div>
      <div class="unit">steps / min</div>
    </div>
    <div class="card">
      <div class="label">Activity</div>
      <div id="activity-badge" class="badge IDLE">IDLE</div>
    </div>
    <div class="card">
      <div class="label">Calories</div>
      <div class="value" id="calories">--</div>
      <div class="unit">kcal (est.)</div>
    </div>
    <div class="card">
      <div class="label">This Hour</div>
      <div class="value" id="this-hour">--</div>
      <div class="unit">steps</div>
    </div>
    <div class="card wide">
      <div class="label">Hourly History</div>
      <div class="bars" id="bars"></div>
    </div>
  </div>
  <div class="footer" id="footer">Updating...</div>

  <script>
    async function update() {
      try {
        const res = await fetch('/data');
        const d = await res.json();

        const goal = 10000;
        const pct = Math.min(d.steps / goal * 100, 100).toFixed(1);

        document.getElementById('steps').textContent = d.steps.toLocaleString();
        document.getElementById('steps').className = 'value' + (d.steps >= goal ? ' goal' : '');
        document.getElementById('progress').style.width = pct + '%';
        document.getElementById('progress').style.background = d.steps >= goal ? '#ffe000' : '#00ffff';
        document.getElementById('goal-label').textContent = d.steps.toLocaleString() + ' / ' + goal.toLocaleString();
        document.getElementById('cadence').textContent = d.cadence;
        document.getElementById('calories').textContent = d.calories;
        document.getElementById('this-hour').textContent = d.thisHour.toLocaleString();

        const badge = document.getElementById('activity-badge');
        badge.textContent = d.activity;
        badge.className = 'badge ' + d.activity;

        const bars = document.getElementById('bars');
        bars.innerHTML = '';
        const maxVal = Math.max(...d.hourly, 1);
        d.hourly.forEach((val, i) => {
          const div = document.createElement('div');
          div.className = 'bar' + (i === d.currentHour ? ' current' : '');
          div.style.height = Math.max(val / maxVal * 100, val > 0 ? 3 : 0) + '%';
          bars.appendChild(div);
        });

        document.getElementById('footer').textContent =
          'Last updated ' + new Date().toLocaleTimeString();
      } catch(e) {
        document.getElementById('footer').textContent = 'Connection lost. Retrying...';
      }
    }

    update();
    setInterval(update, 3000);
  </script>
</body>
</html>)";

  server.send(200, "text/html", html);
}

void handleData() {
  String activityStr;
  switch (currentActivity) {
    case RUNNING: activityStr = "RUNNING"; break;
    case WALKING: activityStr = "WALKING"; break;
    default:      activityStr = "IDLE";    break;
  }

  String hourlyJson = "[";
  for (int i = 0; i < 12; i++) {
    hourlyJson += hourlySteps[i];
    if (i < 11) hourlyJson += ",";
  }
  hourlyJson += "]";

  int calories = (int)(stepCount * 0.04f);

  String json = "{";
  json += "\"steps\":"      + String(stepCount)    + ",";
  json += "\"cadence\":"    + String(cadenceSPM)   + ",";
  json += "\"activity\":\"" + activityStr           + "\",";
  json += "\"calories\":"   + String(calories)     + ",";
  json += "\"thisHour\":"   + String(hourlySteps[currentHour]) + ",";
  json += "\"currentHour\":" + String(currentHour) + ",";
  json += "\"hourly\":"     + hourlyJson;
  json += "}";

  server.send(200, "application/json", json);
}
