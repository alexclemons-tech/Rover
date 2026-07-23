#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <FluxGarage_RoboEyes.h>
#include <VL53L0X.h>
#include <WiFi.h>
#include <WiFiUdp.h>

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

/* ================= WiFi & UDP ================= */
const char* ssid = "mochan";
const char* password = "";
WiFiUDP udp;
const int UDP_PORT = 5005;

/* ================= STATE ================= */
volatile bool manualActive = false;
int lastDistance = 9999;
bool obstacleDetected = false;
String lastCommand = "IDLE";
unsigned long lastCommandTime = 0;

/* ================= MOTOR CONTROL ================= */
void motorControl(byte c) {
  digitalWrite(STBY, HIGH);
  switch (c) {
    case 0: // STOP
      digitalWrite(LF,LOW); digitalWrite(LB,LOW);
      digitalWrite(RF,LOW); digitalWrite(RB,LOW);
      break;
    case 1: // FORWARD
      digitalWrite(LF,HIGH); digitalWrite(LB,LOW);
      digitalWrite(RF,HIGH); digitalWrite(RB,LOW);
      break;
    case 2: // REVERSE
      digitalWrite(LF,LOW);  digitalWrite(LB,HIGH);
      digitalWrite(RF,LOW);  digitalWrite(RB,HIGH);
      break;
    case 3: // LEFT
      digitalWrite(LF,LOW);  digitalWrite(LB,HIGH);
      digitalWrite(RF,HIGH); digitalWrite(RB,LOW);
      break;
    case 4: // RIGHT
      digitalWrite(LF,HIGH); digitalWrite(LB,LOW);
      digitalWrite(RF,LOW);  digitalWrite(RB,HIGH);
      break;
  }
}

/* ================= EVASION MANEUVER ================= */
void evadeObstacle() {
  motorControl(0); // STOP
  delay(150);
  roboEyes.setMood(ANGRY);
  
  // Spin left to escape
  motorControl(3);
  delay(300);
  motorControl(0);
  delay(100);
  
  // Reverse
  motorControl(2);
  delay(400);
  motorControl(0);
}

/* ================= SETUP ================= */
void setup() {
  Serial.begin(115200); 
  unsigned long startWait = millis();
  while (!Serial && (millis() - startWait < 3000)); 
  
  Serial.println("\n=================================");
  Serial.println("--- MOCHAN ROVER WIRELESS CONTROL ---");
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

  // Initialize RoboEyes
  roboEyes.begin(SCREEN_WIDTH, SCREEN_HEIGHT, 100);
  roboEyes.setAutoblinker(ON, 3, 2);
  roboEyes.setIdleMode(ON, 2, 2);
  roboEyes.setMood(DEFAULT);

  // Setup WiFi AP
  Serial.println("[*] Starting WiFi AP: mochan (open network)");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  
  IPAddress IP = WiFi.softAPIP();
  Serial.print("[✔] WiFi AP IP: ");
  Serial.println(IP);

  // Initialize UDP listener
  udp.begin(UDP_PORT);
  Serial.print("[✔] UDP Listening on port ");
  Serial.println(UDP_PORT);

  digitalWrite(STBY, HIGH);
  Serial.println("=== BOOT COMPLETED: Rover Ready ===");
}

/* ================= LOOP ================= */
void loop() {
  roboEyes.update();

  // 1. REMOTE CONTROL UDP COMMAND PARSER
  int packetSize = udp.parsePacket();
  if (packetSize) {
    String command = "";
    while (udp.available()) {
      command += (char)udp.read();
    }
    
    command.trim();
    command.toLowerCase();
    
    Serial.print("[UDP CMD] ");
    Serial.println(command);
    lastCommand = command;
    lastCommandTime = millis();
    
    if (command == "forward") {
      manualActive = true;
      roboEyes.setMood(HAPPY);
      motorControl(1);
      Serial.println("[EXEC] FORWARD");
      
    } else if (command == "reverse") {
      manualActive = true;
      roboEyes.setMood(DEFAULT);
      motorControl(2);
      Serial.println("[EXEC] REVERSE");
      
    } else if (command == "left") {
      manualActive = true;
      roboEyes.setMood(DEFAULT);
      motorControl(3);
      Serial.println("[EXEC] LEFT");
      
    } else if (command == "right") {
      manualActive = true;
      roboEyes.setMood(DEFAULT);
      motorControl(4);
      Serial.println("[EXEC] RIGHT");
      
    } else if (command == "stop") {
      manualActive = false;
      roboEyes.setMood(DEFAULT);
      motorControl(0);
      Serial.println("[EXEC] STOP");
    }
  }

  // 2. LASER DISTANCE SENSOR (OBSTACLE DETECTION)
  static unsigned long lastLaserCheck = 0;
  
  if (millis() - lastLaserCheck > 200) {
    lastLaserCheck = millis();
    
    int distanceMM = laserSensor.readRangeContinuousMillimeters();
    
    if (!laserSensor.timeoutOccurred()) {
      lastDistance = distanceMM;
      
      // Obstacle detection: 30-150mm range
      if (distanceMM < 150 && distanceMM > 30) {
        if (!obstacleDetected) {
          Serial.print("⚠️ OBSTACLE: ");
          Serial.print(distanceMM);
          Serial.println(" mm");
          
          obstacleDetected = true;
          
          // Emergency stop and evade
          if (manualActive) {
            evadeObstacle();
            manualActive = false;
          }
        }
      } else {
        if (obstacleDetected) {
          roboEyes.setMood(DEFAULT);
          obstacleDetected = false;
          Serial.println("[✔] Path Clear");
        }
      }
    }
  }

  // 3. DISPLAY STATUS (Update every 500ms)
  static unsigned long lastDisplayUpdate = 0;
  if (millis() - lastDisplayUpdate > 500) {
    lastDisplayUpdate = millis();
    
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    
    display.setCursor(0, 0);
    display.println("=== ROVER STATUS ===");
    
    display.setCursor(0, 10);
    display.print("WiFi: ACTIVE");
    
    display.setCursor(0, 18);
    display.print("IP: ");
    display.println(WiFi.softAPIP());
    
    display.setCursor(0, 26);
    display.print("Last Cmd: ");
    display.println(lastCommand);
    
    display.setCursor(0, 34);
    display.print("Distance: ");
    if (lastDistance < 9999) {
      display.print(lastDistance);
      display.println("mm");
    } else {
      display.println("---");
    }
    
    display.setCursor(0, 42);
    display.print("Obstacle: ");
    display.println(obstacleDetected ? "YES" : "NO");
    
    display.setCursor(0, 50);
    display.print("Status: ");
    display.println(manualActive ? "ACTIVE" : "IDLE");
    
    display.display();
  }

  delay(16); // ~60 FPS update
}
