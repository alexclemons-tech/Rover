#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <FluxGarage_RoboEyes.h>
#include <VL53L0X.h> 

/* ================= OLED ================= */
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_SDA 5  
#define OLED_SCL 6  

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
RoboEyes<Adafruit_SSD1306> roboEyes(display);

VL53L0X laserSensor; 

/* ================= MOTOR PIN ================= */
#define LF   0
#define LB   1
#define RF   2
#define RB   3
#define STBY 10

/* ================= WIFI ================= */
WebServer server(80);
DNSServer dnsServer;

/* ================= STATE ================= */
volatile bool manualActive = false;

/* ================= RANDOM MODE ================= */
enum RandomMode {
  RANDOM_OFF,
  RANDOM_SOFT,
  RANDOM_NORMAL
};

volatile RandomMode randomMode = RANDOM_NORMAL;

/* ================= WIFI MOTOR ================= */
void motorWifi(byte c) {
  digitalWrite(STBY, HIGH);
  switch (c) {
    case 0:
      digitalWrite(LF,LOW); digitalWrite(LB,LOW);
      digitalWrite(RF,LOW); digitalWrite(RB,LOW);
      break;
    case 1: // FORWARD
      digitalWrite(LF,HIGH); digitalWrite(LB,LOW);
      digitalWrite(RF,LOW);  digitalWrite(RB,HIGH);
      break;
    case 2: // REVERSE
      digitalWrite(LF,LOW);  digitalWrite(LB,HIGH);
      digitalWrite(RF,HIGH); digitalWrite(RB,LOW);
      break;
    case 3: // LEFT
      digitalWrite(LF,LOW);  digitalWrite(LB,HIGH);
      digitalWrite(RF,LOW);  digitalWrite(RB,HIGH);
      break;
    case 4: // RIGHT
      digitalWrite(LF,HIGH); digitalWrite(LB,LOW);
      digitalWrite(RF,LOW);  digitalWrite(RB,HIGH);
      break;
  }
}

/* ================= RANDOM MOTOR ================= */
void MOTOR(byte c,int t1,int t2,int Time){
  for(int i=0;i<Time;i++){
    switch (c) {
      case 0: digitalWrite(LF,LOW); digitalWrite(LB,LOW); digitalWrite(RF,LOW); digitalWrite(RB,LOW); break;
      case 1: digitalWrite(LF,HIGH);digitalWrite(LB,LOW); digitalWrite(RF,HIGH);digitalWrite(RB,LOW); break; // Forward
      case 2: digitalWrite(LF,LOW); digitalWrite(LB,HIGH);digitalWrite(RF,LOW); digitalWrite(RB,HIGH);break; // Reverse
      case 3: digitalWrite(LF,LOW); digitalWrite(LB,HIGH);digitalWrite(RF,HIGH);digitalWrite(RB,LOW); break; // Left Spin
      case 4: digitalWrite(LF,HIGH);digitalWrite(LB,LOW); digitalWrite(RF,LOW); digitalWrite(RB,HIGH);break; // Right Spin
      case 5: digitalWrite(LF,LOW); digitalWrite(LB,HIGH);digitalWrite(RF,LOW); digitalWrite(RB,LOW); break;
      case 6: digitalWrite(LF,LOW); digitalWrite(LB,LOW); digitalWrite(RF,LOW); digitalWrite(RB,HIGH);break;
      case 7: digitalWrite(LF,HIGH);digitalWrite(LB,LOW); digitalWrite(RF,LOW); digitalWrite(RB,LOW); break;
      case 8: digitalWrite(LF,LOW); digitalWrite(LB,LOW); digitalWrite(RF,HIGH);digitalWrite(RB,LOW); break;
    }

    delay(t1);
    digitalWrite(LF,LOW); digitalWrite(LB,LOW);
    digitalWrite(RF,LOW); digitalWrite(RB,LOW);
    delay(t2);
  }
}

/* ================= WEB UI ================= */
void handleRoot() {
  String page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
body{margin:0;height:100vh;background:radial-gradient(circle at top,#0f2027,#000);color:#00ffe1;font-family:Arial;display:flex;align-items:center;justify-content:center;}
.panel{width:260px;padding:20px;border-radius:18px;background:rgba(0,255,225,0.05);border:1px solid rgba(0,255,225,0.4);box-shadow:0 0 25px rgba(0,255,225,0.3);}
h2{text-align:center;margin:0 0 14px;letter-spacing:2px;}
.grid{display:grid;grid-template-columns:1fr 1fr 1fr;grid-template-rows:60px 60px 60px;gap:10px;}
button{border:none;border-radius:12px;font-size:16px;font-weight:bold;background:linear-gradient(145deg,#0ff,#00b3a4);}
.stop{background:linear-gradient(145deg,#ff5555,#aa0000);color:#fff;}
.empty{background:none;}
.mode{margin-top:12px;display:flex;gap:6px;}
.mode button{flex:1;font-size:13px;opacity:0.6;}
.mode button.active{opacity:1;background:linear-gradient(145deg,#00ff9c,#00c46a);box-shadow:0 0 12px rgba(0,255,180,0.8);}
.footer{margin-top:14px;text-align:center;font-size:11px;opacity:0.6;letter-spacing:1px;}
</style>
</head>
<body>
<div class="panel">
<h2>MOCHAN ROBOT</h2>
<div class="grid">
  <div class="empty"></div>
  <button onclick="fetch('/f')">UP</button>
  <div class="empty"></div>
  <button onclick="fetch('/l')">LEFT</button>
  <button class="stop" onclick="fetch('/s')">STOP</button>
  <button onclick="fetch('/r')">RIGHT</button>
  <div class="empty"></div>
  <button onclick="fetch('/b')">DOWN</button>
  <div class="empty"></div>
</div>
<div class="mode">
  <button id="btn_sleep" onclick="setMode('off')">SLEEP</button>
  <button id="btn_wiggle" onclick="setMode('soft')">WIGGLE</button>
  <button id="btn_curious" class="active" onclick="setMode('normal')">CURIOUS</button>
</div>
<div class="footer">Youtube: Huy Vector</div>
</div>
<script>
function clearActive(){
  document.getElementById('btn_sleep').classList.remove('active');
  document.getElementById('btn_wiggle').classList.remove('active');
  document.getElementById('btn_curious').classList.remove('active');
}
function setMode(mode){
  fetch('/mode_' + mode);
  clearActive();
  if(mode === 'off') document.getElementById('btn_sleep').classList.add('active');
  if(mode === 'soft') document.getElementById('btn_wiggle').classList.add('active');
  if(mode === 'normal') document.getElementById('btn_curious').classList.add('active');
}
</script>
</body>
</html>
)rawliteral";
  server.send(200, "text/html", page);
}

/* ================= SERVER ================= */
void setupServer() {
  server.on("/", handleRoot);
  server.on("/f", [](){ manualActive=true; motorWifi(1); server.send(200); manualActive=false; });
  server.on("/b", [](){ manualActive=true; motorWifi(2); server.send(200); manualActive=false; });
  server.on("/l", [](){ manualActive=true; motorWifi(3); server.send(200); manualActive=false; });
  server.on("/r", [](){ manualActive=true; motorWifi(4); server.send(200); manualActive=false; });
  server.on("/s", [](){ manualActive=true; motorWifi(0); server.send(200); manualActive=false; });
  server.on("/mode_off",    [](){ randomMode = RANDOM_OFF;    server.send(200); });
  server.on("/mode_soft",   [](){ randomMode = RANDOM_SOFT;   server.send(200); });
  server.on("/mode_normal", [](){ randomMode = RANDOM_NORMAL; server.send(200); });
  server.onNotFound(handleRoot);
  server.begin();
}

/* ================= SETUP ================= */
void setup() {
  Serial.begin(115200); 
  unsigned long startWait = millis();
  while (!Serial && (millis() - startWait < 3000)); 
  
  Serial.println("\n=================================");
  Serial.println("--- MOCHAN ROVER SMART SYNC RUN ---");
  Serial.println("=================================");
  
  pinMode(STBY,OUTPUT); digitalWrite(STBY,LOW);
  pinMode(LF,OUTPUT); pinMode(LB,OUTPUT);
  pinMode(RF,OUTPUT); pinMode(RB,OUTPUT);

  Wire.begin(OLED_SDA, OLED_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay(); display.display();

  if (laserSensor.init()) {
    laserSensor.startContinuous();
    Serial.println("[✔] Laser Sensor Online.");
  }

  // Launch the AI UART at high speed default first
  Serial1.begin(1152000, SERIAL_8N1, 7, 4); 
  Serial.println("[✔] AI Port Initialized on Pins 7 & 4 (Speed: 1,152,000).");

  roboEyes.begin(SCREEN_WIDTH, SCREEN_HEIGHT, 100);
  roboEyes.setAutoblinker(ON, 3, 2);
  roboEyes.setIdleMode(ON, 2, 2);
  roboEyes.setMood(DEFAULT);

  randomSeed(esp_random());
  WiFi.softAP("mochan");
  dnsServer.start(53, "*", WiFi.softAPIP());
  setupServer();
  
  digitalWrite(STBY, HIGH);
  Serial.println("=== BOOT COMPLETED: Rover Is Live ===");
}

/* ================= LOOP ================= */
void loop() {
  roboEyes.update();
  server.handleClient();
  dnsServer.processNextRequest();

  // 1. AUTO-BAUD TRANSLATOR LOOP WITH NULL DROP FILTERS
  static String aiBuffer = "";
  static int nullByteStreak = 0;
  static bool runningAtHighSpeed = true;
  static unsigned long lastBaudShift = 0;

  if (Serial1.available() > 0) {
    bool validCharFound = false;

    while (Serial1.available() > 0) {
      byte b = Serial1.read();

      if (b == 0x00) {
        nullByteStreak++;
      } else {
        nullByteStreak = 0; // Break zero streak instantly
        if (!validCharFound) {
          Serial.print("🤖 [AI DETECTED]: ");
          validCharFound = true;
        }
        
        char charIn = (char)b;
        Serial.print(charIn); // Pipe the clean letters straight to monitor
        
        // Parse characters safely into lowercase buffer
        if (charIn >= 'A' && charIn <= 'Z') {
          aiBuffer += (char)(charIn + 32);
        } else if (charIn != '\n' && charIn != '\r' && charIn != '{' && charIn != '}' && charIn != '"') {
          aiBuffer += charIn;
        }
      }
    }

    if (validCharFound) {
      Serial.println();
    }

    // CRITICAL TRIGGER: If the bus accumulates a long streak of zero bits, shift speeds!
    if (nullByteStreak > 60 && millis() - lastBaudShift > 1500) {
      lastBaudShift = millis();
      nullByteStreak = 0;
      Serial1.end();

      if (runningAtHighSpeed) {
        Serial.println("\n🔄 [SYNC ERROR]: Zeros detected. Downshifting AI Port to Standard 115,200 Baud...");
        Serial1.begin(115200, SERIAL_8N1, 7, 4);
        runningAtHighSpeed = false;
      } else {
        Serial.println("\n🔄 [SYNC ERROR]: Zeros detected. Upshifting AI Port back to Fast 1,152,000 Baud...");
        Serial1.begin(1152000, SERIAL_8N1, 7, 4);
        runningAtHighSpeed = true;
      }
    }

    // PROCESS THE MATCH STRINGS IF WE CAPTURED TEXT
    aiBuffer.trim();
    if (aiBuffer.length() > 0) {
      if (aiBuffer.indexOf("forward") >= 0 || aiBuffer.indexOf("go") >= 0) {
        roboEyes.setMood(HAPPY);
        motorWifi(1); 

 }
 
 else if (aiBuffer.indexOf("dance") >= 0 || 
 aiBuffer.indexOf("party") >= 0) {
 roboEyes.setMood(HAPPY);
 MOTOR(3, 15, 10, 5);
 MOTOR(4, 15, 10, 5);
 motorWifi(0);
 }
 else if (aiBuffer.indexOf("stop") >= 0 || aiBuffer.indexOf("halt") 
 >= 0) {
roboEyes.setMood(DEFAULT);
motorWifi(0);
}
aiBuffer = "";
}
}

// 2. LASER DISTANCE SENSOR (SILENT HAZARD MANAGEMENT)

static unsigned long lastLaserCheck = 0;
int distanceMM = 9999;
if (millis() - lastLaserCheck > 300) {
  
  // Slowed logging down to 300ms to allow zero tracking clarity
  
  lastLaserCheck = millis();
  distanceMM = 
  laserSensor.readRangeContinuousMillimeters();
  if (!laserSensor.timeoutOccurred()) {
  if (distanceMM < 150 && distanceMM > 30) {
  Serial.print("⚠️ Obstacle Alert: "); 
  Serial.print(distanceMM); 
  Serial.println(" mm");
  roboEyes.setMood(ANGRY);
  if (randomMode != RANDOM_OFF || manualActive) {
    motorWifi(0);
    delay(150);
    MOTOR(3, 20, 10, 15);
    MOTOR(2, 20, 10, 20);    
    motorWifi(0);
    delay(100);
    }
    }
    else {roboEyes.setMood(DEFAULT);
    }
    }
    }
    
    // 3. RANDOM WANDERING MODE
    
    static unsigned long lastTick = 0;
    if (!manualActive && (distanceMM >= 150 || distanceMM <= 30) 
    && millis() - lastTick > 40) {
    lastTick = millis();
    if (randomMode == RANDOM_SOFT) {
      if (random(120) == 1) {
    MOTOR(random(9), random(6,18), random(40,90), 1);
    }
    }
    else if (randomMode == RANDOM_NORMAL) {
    if (random(100) == 1) {
    MOTOR(random(9), random(5,50), random(10,100), 
    random(20));
    }
    }
    }
    }