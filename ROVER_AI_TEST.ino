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

/* ================= STATE ================= */
volatile bool manualActive = false;
int lastDistance = 9999;
bool obstacleDetected = false;

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
  Serial.println("--- MOCHAN ROVER REMOTE CONTROL ---");
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

  // Initialize Serial1 for remote control (115200 baud)
  Serial1.begin(115200, SERIAL_8N1, 7, 4); 
  Serial.println("[✔] Remote Control Port Initialized on Pins 7 & 4 (Speed: 115,200).");

  roboEyes.begin(SCREEN_WIDTH, SCREEN_HEIGHT, 100);
  roboEyes.setAutoblinker(ON, 3, 2);
  roboEyes.setIdleMode(ON, 2, 2);
  roboEyes.setMood(DEFAULT);

  digitalWrite(STBY, HIGH);
  Serial.println("=== BOOT COMPLETED: Rover Is Live ===");
}

/* ================= LOOP ================= */
void loop() {
  roboEyes.update();

  // 1. REMOTE CONTROL COMMAND PARSER
  static String commandBuffer = "";
  
  while (Serial1.available() > 0) {
    char c = Serial1.read();
    
    if (c == '\n' || c == '\r') {
      commandBuffer.trim();
      
      if (commandBuffer.length() > 0) {
        // Parse and execute command
        if (commandBuffer.indexOf("forward") >= 0) {
          manualActive = true;
          roboEyes.setMood(HAPPY);
          motorControl(1);
          Serial.println("[CMD] FORWARD");
          
        } else if (commandBuffer.indexOf("reverse") >= 0) {
          manualActive = true;
          roboEyes.setMood(DEFAULT);
          motorControl(2);
          Serial.println("[CMD] REVERSE");
          
        } else if (commandBuffer.indexOf("left") >= 0) {
          manualActive = true;
          roboEyes.setMood(DEFAULT);
          motorControl(3);
          Serial.println("[CMD] LEFT");
          
        } else if (commandBuffer.indexOf("right") >= 0) {
          manualActive = true;
          roboEyes.setMood(DEFAULT);
          motorControl(4);
          Serial.println("[CMD] RIGHT");
          
        } else if (commandBuffer.indexOf("stop") >= 0) {
          manualActive = false;
          roboEyes.setMood(DEFAULT);
          motorControl(0);
          Serial.println("[CMD] STOP");
        }
        
        commandBuffer = "";
      }
    } else if (c > 31 && c < 127) { // Printable ASCII
      commandBuffer += c;
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
          
          // Send feedback to remote
          Serial1.print("Obstacle: ");
          Serial1.print(distanceMM);
          Serial1.println("mm");
          
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

  delay(16); // ~60 FPS update
}
