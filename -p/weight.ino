#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>
#include "HX711.h"
// --- Pin Definitions ---
#define TRIG_PIN 5
#define ECHO_PIN 17
#define LED_PIN 2
const int buzzerPin = 18;
// Servo Motor Pin
#define SERVO_PIN 16

// --- HX711 Pins ---
#define DT_PIN 32   // Data pin
#define SCK_PIN 33  // Clock pin
HX711 scale;        // HX711 object



// --- OLED Configuration ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET 4
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Global States ---
Servo servoMotor;
bool radarState = true;  // Overall radar system ON/OFF (controls sweep and detection)
bool ledState = false;
bool buzzerState = false;
bool servoState = false;  // Servo motor power ON/OFF
bool animationState = true;
String displayText = ".";


// ---------- Session & Water ----------
float glassFullWeight = 0;         // baseline for full glass
float previousWeight = 0;          // for tracking drinking
float totalDrank = 0;              // total water consumed in session
const float SESSION_TARGET = 950;  // target per 3-hour session

bool glassRemoved = false;


// Timing
unsigned long sessionStart;
const unsigned long SESSION_DURATION = 3UL * 60UL * 60UL * 1000UL;  // 3 hours in ms
const unsigned long MINI_BREAK_INTERVAL = 45UL * 60UL * 1000UL;     // 45 min break

unsigned long lastBreakAlert = 0;


// --- WiFi Configuration ---
const char *ssid = "ESP32-Access-Point";
const char *password = "12345678";
WiFiServer server(80);



// --- Debounce/Timing ---
unsigned long lastActionTime = 0;
const unsigned long debounceTime = 200;
unsigned long previousBlinkTime = 0;
int blinkInterval = 500;  // default 500ms for LED

// --- Sensor/Animation Data ---
#define NUMFLAKES 5
int flakeX[NUMFLAKES];
int flakeY[NUMFLAKES];
int flakeSpeed[NUMFLAKES];

int servoAngle = 0;  // Current servo angle for sweep (0-180)
long distance = 0;   // Raw distance value from sensor
long cm = 0;         // Controlled distance value for UI/Logic

// --- HX711 Weight Data ---
float weight = 0.0;                // Current measured weight
float calibration_factor = -7050;  // ⚖️ Adjust this by calibration

// --- OLED Snow Setup ---
void setupFlakes() {
  for (int i = 0; i < NUMFLAKES; i++) {
    flakeX[i] = random(0, SCREEN_WIDTH - 10);
    flakeY[i] = random(0, SCREEN_HEIGHT - 10);
    flakeSpeed[i] = random(1, 4);
  }
}

// --- Setup ---
void setup() {
  Serial.begin(115200);
  Serial.println("Starting ESP32 IoT Dashboard...");

  pinMode(LED_PIN, OUTPUT);
  pinMode(buzzerPin, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Initialize OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 allocation failed");
    for (;;)
      ;
  }

  // WiFi AP Setup
  Serial.println("Setting up WiFi Access Point...");
  WiFi.softAP(ssid, password);
  delay(2000);
  IPAddress apIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(apIP);

  server.begin();
  Serial.println("Server started on port 80");

  setupFlakes();

  // Attach Servo immediately but set initial state to OFF (detached)
  servoMotor.attach(SERVO_PIN);
  servoMotor.detach();  // Start detached to save power and prevent jitter
  servoState = false;
  Serial.println("Servo initialized and detached (OFF)");


  // HX711 Init
  scale.begin(DT_PIN, SCK_PIN);
  scale.set_scale(calibration_factor);
  if (scale.is_ready()) {
    scale.tare();
    Serial.println("HX711 Initialized & Tared");
  } else {
    Serial.println("HX711 not found! Skipping taring.");
  }

  // --- Measure baseline full glass ---
  Serial.println("Place a full glass on the scale...");
  while (glassFullWeight <= 0) {  // wait until glass is placed
    if (scale.is_ready()) {
      glassFullWeight = scale.get_units(5);
      if (glassFullWeight < 0) glassFullWeight = 0;
    }
    delay(500);
  }
  previousWeight = glassFullWeight;  // initialize previousWeight
  Serial.print("Baseline full glass weight: ");
  Serial.println(glassFullWeight);

  totalDrank = 0;
  glassRemoved = false;
  sessionStart = millis();
  lastBreakAlert = millis();
}

// --- Distance Reading Function ---
long readDistance() {
  // Clears the trigPin condition
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  // Sets the trigPin HIGH for 10 microsecond
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // Reads the echoPin, returns the sound wave travel time in microseconds
  long duration = pulseIn(ECHO_PIN, HIGH, 25000);  // 25ms timeout for max range ~400cm
  // Calculating the distance (speed of sound = 0.034 cm/us)
  long dist = duration * 0.034 / 2;
  return dist;
}

// --- HTTP Helper Function ---
String getRequestParam(String header, String paramName) {
  int index = header.indexOf(paramName + "=");
  if (index == -1)
    return "";
  int start = index + paramName.length() + 1;
  int end = header.indexOf('&', start);
  if (end == -1)
    end = header.indexOf(' ', start);
  if (end == -1)
    end = header.length();
  String value = header.substring(start, end);
  value.replace('+', ' ');
  return value;
}

// --- HTTP Client Handler ---
void handleClient(WiFiClient client) {
  String currentLine = "";
  String request = "";

  // Read request until end of header or connection closed
  while (client.connected()) {
    if (client.available()) {
      char c = client.read();
      if (c == '\n') {
        if (currentLine.length() == 0) {
          // This is the end of the HTTP request header
          break;
        } else {
          // Store the first line (GET request)
          if (request.length() == 0) {
            request = currentLine;
          }
          currentLine = "";
        }
      } else if (c != '\r') {
        currentLine += c;
      }
    }
  }

  if (request.length() == 0) {
    client.stop();
    return;
  }

  // --- 1. Handle AJAX Data Request (Non-blocking update for UI) ---
  if (request.indexOf("GET /data") >= 0) {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-type:application/json");
    client.println("Connection: close");
    client.println("Access-Control-Allow-Origin: *");
    client.println();

    // If radar is off, send -1 for distance to indicate no reading
    long distanceForUI = radarState ? cm : -1;

    // JSON response for the front-end radar
    String jsonResponse = "{\"angle\":" + String(servoAngle) + ", \"distance\":" + String(distanceForUI) + ", \"radarState\":" + (radarState ? "true" : "false") + ", \"weight\":" + String((int)weight) + "}";
    client.println(jsonResponse);

    client.stop();
    return;  // Exit handler
  }

  // --- 2. Handle Control Commands ---
  if (millis() - lastActionTime > debounceTime) {
    if (request.indexOf("/LEDON") >= 0) {
      ledState = true;
      digitalWrite(LED_PIN, HIGH);
    } else if (request.indexOf("/LEDOFF") >= 0) {
      ledState = false;
      digitalWrite(LED_PIN, LOW);
    } else if (request.indexOf("/BUZZERON") >= 0) {
      buzzerState = true;
      tone(buzzerPin, 1000);
    } else if (request.indexOf("/BUZZEROFF") >= 0) {
      buzzerState = false;
      noTone(buzzerPin);
    } else if (request.indexOf("/ANIMATIONON") >= 0) {
      animationState = true;
    } else if (request.indexOf("/ANIMATIONOFF") >= 0) {
      animationState = false;
    } else if (request.indexOf("/RADARON") >= 0) {
      radarState = true;
    } else if (request.indexOf("/RADAROFF") >= 0) {
      radarState = false;
      // Stop radar-controlled devices
      noTone(buzzerPin);
      if (!ledState) digitalWrite(LED_PIN, LOW);  // Only turn off if not manually ON
    } else if (request.indexOf("/SERVOON") >= 0) {
      if (!servoState) {
        servoMotor.attach(SERVO_PIN);
        servoState = true;
      }
    } else if (request.indexOf("/SERVOOFF") >= 0) {
      if (servoState) {
        servoMotor.detach();
        servoState = false;
      }
    } else if (request.indexOf("/SETTEXT") >= 0) {
      String newText = getRequestParam(request, "text");
      if (newText.length() > 0) {
        displayText = newText;
      }
    }
    lastActionTime = millis();
  }

  // --- 3. Serve Main HTML Page (Always redirect to the main page after command) ---
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html");
  client.println("Connection: close");
  client.println();

  // Start modern mobile-friendly HTML
  client.println("<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'>");

  // --- CSS for Professional Sci-Fi Dashboard Look ---
  client.println("<style>");
  client.println("body{font-family:'Inter', sans-serif; text-align:center; background:#0a0a1a; color:#e0e0e0; margin:0; padding:25px;}");
  client.println(".card{max-width:480px; margin:0 auto; padding:30px 20px; background:#181829; border-radius:30px; box-shadow:0 15px 40px rgba(0,0,0,0.8);}");
  client.println("h1{color:#fff; font-size:2rem; margin-bottom:10px; font-weight:700;}");
  client.println("p.subtitle{color:#94a3b8; margin-bottom:30px; font-size:0.9rem;}");
  client.println(".control-group{background:#2a384b; border-radius:18px; padding:15px; margin-bottom:20px; transition:box-shadow 0.3s, background 0.3s; border:1px solid #334155;}");
  client.println(".control-group.active{box-shadow:0 0 15px #06b6d4, 0 0 8px #06b6d4 inset; background:#1f2937;}");
  client.println(".ctrl-label{display:flex; justify-content:space-between; align-items:center; font-size:1.1rem; font-weight:600; color:#fff; margin-bottom:15px;}");
  client.println(".ctrl-title{display:flex; align-items:center;}");
  client.println(".icon{margin-right:10px; font-size:1.4rem;}");
  client.println(".status-indicator{font-size:0.8rem; font-weight:800; padding:4px 10px; border-radius:12px;}");
  client.println(".status-on{background:#06b6d4; color:#0a0a1a;}");
  client.println(".status-off{background:#3f3f46; color:#a1a1aa;}");
  client.println(".btn-row{display:flex; justify-content:space-between; gap:10px;}");
  client.println("a{text-decoration:none; flex-grow:1;}");
  client.println(".btn{width:100%; padding:10px 0; font-size:15px; cursor:pointer; border:1px solid #475569; border-radius:10px; color:#fff; font-weight:500; transition:all 0.2s; background:transparent;}");
  client.println(".btn-active{background:#06b6d4; border-color:#06b6d4; color:#0a0a1a; font-weight:700;}");
  client.println(".btn:active{transform:scale(0.98);}");
  client.println(".form-group{margin-top:20px; padding: 0 10px;}");
  client.println(".text-input{width:100%; box-sizing:border-box; padding:12px; font-size:16px; border:1px solid #475569; border-radius:10px; background:#1e293b; color:#f8fafc; margin-bottom:15px; text-align:left;}");
  client.println(".text-input:focus{border:1px solid #06b6d4; outline:none;}");
  client.println(".submit-btn{padding:12px 20px; font-size:16px; background:#06b6d4; color:#0a0a1a; border:none; border-radius:10px; cursor:pointer; font-weight:700; width:100%; transition:background-color 0.2s;}");
  client.println(".submit-btn:hover{background:#0ea5e9;}");

  // --- RADAR SPECIFIC STYLES ---
  client.println(".radar-container { position: relative; width: 100%; max-width: 400px; margin: 20px auto; aspect-ratio: 1 / 1; border-radius: 50%; background: #111; overflow: hidden; border: 2px solid #06b6d4; box-shadow: 0 0 15px rgba(6, 182, 212, 0.5);}");
  client.println("#radarCanvas { width: 100%; height: 100%; display: block; background: #000; }");
  client.println(".radar-info { position: absolute; top: 10px; left: 10px; color: #fff; text-align: left; font-size: 0.9rem; z-index: 10; font-weight: 600; text-shadow: 0 0 5px #06b6d4;}");
  client.println(".distance-display { font-size: 1.5rem; color: #facc15; margin-top: 5px; }");
  client.println(".distance-display.alert { color: #f87171; animation: pulse 0.5s infinite alternate; }");
  client.println("@keyframes pulse { from { transform: scale(1); opacity: 1; } to { transform: scale(1.05); opacity: 0.8; } }");
  client.println(".radar-status { position: absolute; bottom: 10px; right: 10px; font-size: 0.8rem; font-weight: 700; }");
  client.println("</style></head><body>");

  // --- HTML Body ---
  client.println("<div class='card'>");
  client.println("<h1>ESP32 Advanced IoT Dashboard</h1>");
  client.println("<p class='subtitle'>Controller IP: ");
  client.println(WiFi.softAPIP());
  client.println("</p>");

  // --- RADAR VISUALIZATION SECTION ---
  client.println("<div class='control-group' style='background:#0a0a1a; padding:10px; border:none;'>");
  client.println("<div class='radar-container'>");
  client.println("<canvas id='radarCanvas'></canvas>");
  client.println("<div class='radar-info'>");
  client.println("ANGLE: <span id='angleDisplay'>0°</span><br>");
  client.println("DIST: <span id='distanceDisplay' class='distance-display'>0 cm</span>");
  client.println("</div>");
  client.println("<div class='radar-status'><span id='radarStatus'>ACTIVE</span></div>");
  client.println("</div>");
  client.println("</div>");
  // --- END RADAR SECTION ---

  // --- Control Groups ---

  // --- LED Control Group ---
  client.print("<div class='control-group");
  if (ledState) client.print(" active");
  client.println("'>");
  client.print("<div class='ctrl-label'><div class='ctrl-title'><span class='icon'>💡</span> LED Pin 2</div> <span id='ledIndicator' class='status-indicator status-");
  client.print(ledState ? "on'>ON" : "off'>OFF");
  client.println("</span></div>");
  client.print("<div class='btn-row'>");
  client.print("<a href='/LEDON'><button class='btn");
  if (ledState) client.print(" btn-active");
  client.println("'>ON</button></a>");
  client.print("<a href='/LEDOFF'><button class='btn");
  if (!ledState) client.print(" btn-active");
  client.println("'>OFF</button></a>");
  client.println("</div></div>");

  // --- Buzzer Control Group ---
  client.print("<div class='control-group");
  if (buzzerState) client.print(" active");
  client.println("'>");
  client.print("<div class='ctrl-label'><div class='ctrl-title'><span class='icon'>🔊</span> Buzzer Pin 18</div> <span class='status-indicator status-");
  client.print(buzzerState ? "on'>ON" : "off'>OFF");
  client.println("</span></div>");
  client.print("<div class='btn-row'>");
  client.print("<a href='/BUZZERON'><button class='btn");
  if (buzzerState) client.print(" btn-active");
  client.println("'>ON</button></a>");
  client.print("<a href='/BUZZEROFF'><button class='btn");
  if (!buzzerState) client.print(" btn-active");
  client.println("'>OFF</button></a>");
  client.println("</div></div>");

  // --- Radar Control Group (Master Switch) ---
  client.print("<div class='control-group");
  if (radarState) client.print(" active");
  client.println("'>");
  client.print("<div class='ctrl-label'><div class='ctrl-title'><span class='icon'>📡</span> Detection Radar (HCT+Servo)</div> <span class='status-indicator status-");
  client.print(radarState ? "on'>ON" : "off'>OFF");
  client.println("</span></div>");
  client.print("<div class='btn-row'>");
  client.print("<a href='/RADARON'><button class='btn");
  if (radarState) client.print(" btn-active");
  client.println("'>ON</button></a>");
  client.print("<a href='/RADAROFF'><button class='btn");
  if (!radarState) client.print(" btn-active");
  client.println("'>OFF</button></a>");
  client.println("</div></div>");

  // --- Servo Control Group (Power Switch) ---
  client.print("<div class='control-group");
  if (servoState) client.print(" active");
  client.println("'>");
  client.print("<div class='ctrl-label'><div class='ctrl-title'><span class='icon'>⚙️</span> Servo Motor Power</div> <span class='status-indicator status-");
  client.print(servoState ? "on'>ON' title='PWM Active'" : "off'>OFF' title='Detached'");
  client.println("</span></div>");
  client.print("<div class='btn-row'>");
  client.print("<a href='/SERVOON'><button class='btn");
  if (servoState) client.print(" btn-active");
  client.println("'>ON</button></a>");
  client.print("<a href='/SERVOOFF'><button class='btn");
  if (!servoState) client.print(" btn-active");
  client.println("'>OFF</button></a>");
  client.println("</div></div>");

  // --- OLED Animation Group ---
  client.print("<div class='control-group");
  if (animationState) client.print(" active");
  client.println("'>");
  client.print("<div class='ctrl-label'><div class='ctrl-title'><span class='icon'>✨</span> OLED Snow Animation</div> <span class='status-indicator status-");
  client.print(animationState ? "on'>ON" : "off'>OFF");
  client.println("</span></div>");
  client.print("<div class='btn-row'>");
  client.print("<a href='/ANIMATIONON'><button class='btn");
  if (animationState) client.print(" btn-active");
  client.println("'>ON</button></a>");
  client.print("<a href='/ANIMATIONOFF'><button class='btn");
  if (!animationState) client.print(" btn-active");
  client.println("'>OFF</button></a>");
  client.println("</div></div>");

  // --- Text Form Group ---
  client.println("<form action='/SETTEXT' class='form-group'>");
  client.println("<div class='ctrl-label' style='margin-bottom: 5px;'><div class='ctrl-title'><span class='icon'>✍️</span> OLED Text (Current: " + displayText + ")</div></div>");
  client.print("<input class='text-input' type='text' name='text' placeholder='Enter new text...' value='" + displayText + "'>");
  client.println("<input type='submit' class='submit-btn' value='Update OLED Text'>");
  client.println("</form>");

  client.println("</div>");  // Close card div

  // --- JAVASCRIPT FOR RADAR VISUALIZATION ---
  client.println("<script>");
  client.println("const canvas = document.getElementById('radarCanvas');");
  client.println("const ctx = canvas.getContext('2d');");
  client.println("const angleDisplay = document.getElementById('angleDisplay');");
  client.println("const distanceDisplay = document.getElementById('distanceDisplay');");
  client.println("const radarStatus = document.getElementById('radarStatus');");
  client.println("let currentAngle = 0;");
  client.println("let currentDistance = 0;");
  client.println("let maxRange = 30; // Max distance for visual radar in cm");

  // Set canvas size dynamically for responsiveness
  client.println("function resizeCanvas() {");
  client.println("  const container = canvas.parentElement;");
  client.println("  const size = container.clientWidth;");
  client.println("  canvas.width = size;");
  client.println("  canvas.height = size;");
  client.println("  drawRadar();");
  client.println("}");

  // Radar Drawing Logic
  client.println("function drawRadar() {");
  client.println("  const size = Math.min(canvas.width, canvas.height);");
  client.println("  const center = size / 2;");
  client.println("  const radius = size * 0.9 / 2;");

  client.println("  ctx.clearRect(0, 0, size, size);");

  // Background
  client.println("  ctx.fillStyle = '#111';");
  client.println("  ctx.fillRect(0, 0, size, size);");

  // Draw Grid Lines (Rings and Radials)
  client.println("  ctx.strokeStyle = '#004455';");
  client.println("  ctx.lineWidth = 1;");

  // Range Rings (180 degree view)
  client.println("  for (let i = 1; i <= 3; i++) {");
  client.println("    ctx.beginPath();");
  client.println("    ctx.arc(center, center, radius * i / 3, Math.PI, 2 * Math.PI); // Draw bottom half of circle (0 to 180 degrees)");
  client.println("    ctx.stroke();");
  client.println("  }");

  // Radial Lines (0°, 45°, 90°, 135°, 180°)
  client.println("  for (let i = 0; i <= 180; i += 45) {");
  client.println("    const angle = i * Math.PI / 180;");
  client.println("    ctx.beginPath();");
  client.println("    ctx.moveTo(center, center);");
  // Angle correction: Servo 0-180 maps to 0 to -180 degrees (clockwise from 12 o'clock)
  client.println("    const x = center + radius * Math.cos(angle - Math.PI/2);");
  client.println("    const y = center + radius * Math.sin(angle - Math.PI/2);");
  client.println("    ctx.lineTo(x, y);");
  client.println("    ctx.stroke();");
  client.println("  }");

  // Draw detected object (Target)
  client.println("  if (currentDistance > 0 && currentDistance <= maxRange) {");
  client.println("    const distRatio = currentDistance / maxRange;");
  client.println("    const distPixels = radius * distRatio;");

  // Map servo angle (0=right, 180=left) to canvas angle (90=right, 270=left)
  // Angle: 180-currentAngle gives angle from 180 (left side)
  client.println("    const radarAngle = (180 - currentAngle) + 90; // Map 0->180 to 270->90 degrees for bottom sweep");
  client.println("    const radians = radarAngle * Math.PI / 180;");

  client.println("    const x = center + distPixels * Math.cos(radians);");
  client.println("    const y = center + distPixels * Math.sin(radians);");

  client.println("    ctx.beginPath();");
  client.println("    ctx.arc(x, y, 6, 0, 2 * Math.PI);");
  client.println("    ctx.fillStyle = '#4ade80'; // Bright Green object");
  client.println("    ctx.shadowBlur = 15;");
  client.println("    ctx.shadowColor = '#4ade80';");
  client.println("    ctx.fill();");
  client.println("    ctx.shadowBlur = 0;");
  client.println("  }");

  // Draw Sweep Line (The professional part - Gradient)
  client.println("  const sweepAngle = (180 - currentAngle) + 90; // Current Angle mapped to canvas degrees");
  client.println("  const sweepRadians = sweepAngle * Math.PI / 180;");

  client.println("  ctx.beginPath();");
  client.println("  ctx.moveTo(center, center);");
  client.println("  const lineX = center + radius * Math.cos(sweepRadians);");
  client.println("  const lineY = center + radius * Math.sin(sweepRadians);");
  client.println("  ctx.lineTo(lineX, lineY);");

  // Gradient for a glowing effect
  client.println("  const lineGradient = ctx.createLinearGradient(center, center, lineX, lineY);");
  client.println("  lineGradient.addColorStop(0, 'rgba(6, 182, 212, 1)');");
  client.println("  lineGradient.addColorStop(0.5, 'rgba(6, 182, 212, 0.5)');");
  client.println("  lineGradient.addColorStop(1, 'rgba(6, 182, 212, 0)');");

  client.println("  ctx.strokeStyle = lineGradient;");
  client.println("  ctx.lineWidth = 3;");
  client.println("  ctx.shadowBlur = 10;");
  client.println("  ctx.shadowColor = '#06b6d4';");
  client.println("  ctx.stroke();");
  client.println("  ctx.shadowBlur = 0;");

  // Draw Center Hub
  client.println("  ctx.beginPath();");
  client.println("  ctx.arc(center, center, 8, 0, 2 * Math.PI);");
  client.println("  ctx.fillStyle = '#06b6d4';");
  client.println("  ctx.fill();");

  client.println("}");

  // AJAX Function to fetch data periodically
  client.println("async function fetchRadarData() {");
  client.println("  try {");
  client.println("    const response = await fetch('/data');");
  client.println("    const data = await response.json();");

  client.println("    currentAngle = data.angle;");
  client.println("    currentDistance = data.distance;");

  client.println("    angleDisplay.textContent = currentAngle + '°';");
  client.println("    if (data.radarState) {");
  client.println("      radarStatus.textContent = 'ACTIVE';");
  client.println("      radarStatus.style.color = '#06b6d4';");
  client.println("      distanceDisplay.textContent = currentDistance > 0 ? currentDistance + ' cm' : 'Scanning...';");
  client.println("      if (currentDistance > 0 && currentDistance < 20) {");
  client.println("        distanceDisplay.classList.add('alert');");
  client.println("      } else {");
  client.println("        distanceDisplay.classList.remove('alert');");
  client.println("      }");
  client.println("    } else {");
  client.println("      radarStatus.textContent = 'OFFLINE';");
  client.println("      radarStatus.style.color = '#f87171';");
  client.println("      distanceDisplay.textContent = '--- cm';");
  client.println("      distanceDisplay.classList.remove('alert');");
  client.println("    }");

  client.println("    drawRadar();");
  client.println("  } catch (error) {");
  client.println("    console.error('Error fetching radar data:', error);");
  client.println("    radarStatus.textContent = 'ERROR';");
  client.println("    radarStatus.style.color = '#f87171';");
  client.println("  }");
  client.println("}");

  client.println("window.addEventListener('resize', resizeCanvas);");
  client.println("resizeCanvas();");                    // Initial setup
  client.println("setInterval(fetchRadarData, 100);");  // Update UI 10 times per second

  client.println("</script>");

  client.println("</body></html>");
  client.println();

  client.stop();
}

// --- Loop Function (Runs continuously on ESP32) ---
int sweepDirection = 1;  // 1 for increasing angle, -1 for decreasing

void loop() {
  // Check for new client connections
  WiFiClient client = server.available();
  if (client) handleClient(client);


  // --- HX711 Weight Reading ---
  if (scale.is_ready()) {

    weight = (int)round((scale.get_units(5) - 96.7) * 10.1);
    Serial.printf("Glass current weight is: %.2f g\n", weight);



    // weight = scale.get_units(5) - 96.7;  // Average of 5 readings
    // weight = scale.get_units(5) + 353.7;  // Average of 5 readings
    // if (weight < 0) weight = 0;   // Ignore negative drift
  }
  // --- Detect glass removal ---
  if (!glassRemoved && weight < 0) {
    glassRemoved = true;
    Serial.println("Glass picked up, waiting for replacement...");
    // Play buzzer sound
    tone(buzzerPin, 1000);  // 1000 Hz tone
    delay(500);             // Play for 0.5 second
    noTone(buzzerPin);      // Stop the buzzer
  }
  // --- Detect glass replaced ---
  if (glassRemoved && weight >= 0) {
    float consumed = previousWeight - weight;
    if (consumed < 0) consumed = 0;  // safety check
    totalDrank += consumed;
    previousWeight = weight;
    glassRemoved = false;

    Serial.print("Water consumed this sip: ");
    Serial.print(consumed);
    Serial.println(" g");
    Serial.print("Total drank: ");
    Serial.print(totalDrank);
    Serial.println(" g");

    // Play buzzer sound for glass replacement
    tone(buzzerPin, 1500);  // 1500 Hz tone
    delay(300);             // Play for 0.3 second
    noTone(buzzerPin);      // Stop the buzzer
  }


  // --- Radar Distance & Control Logic (Only runs if radarState is true) ---
  if (radarState) {
    // 1. Read Distance
    distance = readDistance();
    cm = distance;  // Update the global variable for UI/logic

    // 2. Proximity Alert Logic (Buzzer/LED)
    if (cm > 0) {
      // Ignore 0 readings
      if (cm < 5) {
        if (!buzzerState) tone(buzzerPin, 1000);
        blinkInterval = 100;  // fast blink
      } else if (cm < 10) {
        if (!buzzerState) tone(buzzerPin, 800);
        blinkInterval = 200;
      } else if (cm < 20) {
        if (!buzzerState) tone(buzzerPin, 600);
        blinkInterval = 300;
      } else {
        if (!buzzerState) noTone(buzzerPin);
        blinkInterval = 500;  // default blink
      }
    } else {
      if (!buzzerState) noTone(buzzerPin);
      blinkInterval = 500;
    }

    // 3. LED Blinking (Runs only if LED is not manually forced ON)
    unsigned long currentMillis = millis();
    if (currentMillis - previousBlinkTime >= blinkInterval) {
      previousBlinkTime = currentMillis;
      if (!ledState) {
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));  // toggle LED
      }
    }

    // 4. Servo Sweep (If servo power is ON)
    if (servoState) {
      // Simple sweep 0 to 180 and back
      servoAngle += (5 * sweepDirection);
      if (servoAngle >= 180) {
        servoAngle = 180;
        sweepDirection = -1;
      } else if (servoAngle <= 0) {
        servoAngle = 0;
        sweepDirection = 1;
      }
      servoMotor.write(servoAngle);
    }
  } else {
    // Radar OFF: Ensure all related outputs are off
    if (!buzzerState) noTone(buzzerPin);
    if (!ledState) digitalWrite(LED_PIN, LOW);
    cm = -1;  // Indicate no distance reading for the UI
  }




  // --- Calculate session time ---
  unsigned long elapsed = millis() - sessionStart;
  unsigned long hours = elapsed / 3600000;
  unsigned long minutes = (elapsed % 3600000) / 60000;
  unsigned long seconds = (elapsed % 60000) / 1000;

  // --- OLED Display Update ---
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);


  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 55);
  display.print("Session Time: ");
  display.print(hours);
  display.print(":");
  display.print(minutes);
  display.print(":");
  display.print(seconds);

  display.setCursor(0, 45);
  display.print("Total Drank: ");
  display.print(totalDrank);
  display.print(" g");

  // ✅ Show weight
  display.setTextSize(1.5);
  display.setCursor(0, 20);
  display.print("Weight: ");
  display.setTextSize(2);
  // display.print(weight * 10.06036, 1);
  // display.print((int)weight * 11.045);
  // display.print((int)round(weight * 10.1));
  display.print(weight, 1);

  display.print(" g");



  // If Radar is ON, dedicate the top line to distance/angle data
  if (radarState) {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("Dist: ");
    display.print(cm > 0 ? String(cm) : "---");
    display.print("cm | Angle: ");
    display.print(servoAngle);
    display.print("d");
  }

  // Text display area
  if (animationState) {
    display.setTextSize(2);
    display.setCursor(10, 20);  // Adjust position based on radar header
    display.print(displayText);

    // Snowflakes animation
    for (int i = 0; i < NUMFLAKES; i++) {
      display.fillCircle(flakeX[i], flakeY[i], 1, SSD1306_WHITE);  // Smaller snowflakes for professionalism
      flakeY[i] += flakeSpeed[i];
      if (flakeY[i] > SCREEN_HEIGHT) {
        flakeY[i] = 0;
        flakeX[i] = random(0, SCREEN_WIDTH - 5);
        flakeSpeed[i] = random(1, 4);
      }
    }
  } else {
    display.setTextSize(3);
    display.setCursor(10, 20);
    display.print(displayText);
  }

  display.display();

  // --- Alerts ---
  checkAlerts(weight, totalDrank);

  delay(5);  // Small delay to prevent watchdog timer resets/allow other tasks
}

// ---------- Alerts ----------
void checkAlerts(float weight, float total) {
  // Glass empty alert
  if (weight <= 0) {
    buzzerAlertEmpty();
  }

  // Target reached alert
  if (total >= SESSION_TARGET) {
    buzzerTargetReached();
  }

  // Mini break alert
  if (millis() - lastBreakAlert >= MINI_BREAK_INTERVAL) {
    buzzerMiniBreak();
    lastBreakAlert = millis();
  }
}

// ---------- Buzzer Functions ----------
void buzzerAlertEmpty() {
  for (int i = 0; i < 3; i++) {
    tone(buzzerPin, 1000, 200);
    delay(200);
    noTone(buzzerPin);
    delay(200);
  }
}

void buzzerTargetReached() {
  tone(buzzerPin, 1000, 500);
  delay(1000);
  noTone(buzzerPin);
}

void buzzerMiniBreak() {
  for (int i = 0; i < 2; i++) {
    tone(buzzerPin, 1000, 150);
    delay(150);
    noTone(buzzerPin);
    delay(150);
  }
}
