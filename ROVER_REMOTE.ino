#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// I2C PINS
#define SDA_PIN 5
#define SCL_PIN 6
#define TOUCH_PIN 2

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// BMI160 Accelerometer
const int BMI160_ADDR = 0x68;
bool sensorOnline = false;

// Motor Command States
enum MotorCommand {
  CMD_STOP = 0,
  CMD_FORWARD = 1,
  CMD_REVERSE = 2,
  CMD_LEFT = 3,
  CMD_RIGHT = 4
};

volatile MotorCommand currentCommand = CMD_STOP;
volatile MotorCommand lastCommand = CMD_STOP;

// Status variables for display
int laserDistance = 9999;
String aiResponse = "";
unsigned long lastCommandTime = 0;
unsigned long commandDuration = 0;

// Safely probe and wake up the BMI160
void checkAndWakeBMI160() {
  Wire.beginTransmission(BMI160_ADDR);
  if (Wire.endTransmission() != 0) {
    sensorOnline = false;
    return;
  }

  Wire.beginTransmission(BMI160_ADDR);
  Wire.write(0x7E); // CMD register
  Wire.write(0x11); // Command: Accel Normal Mode
  Wire.endTransmission();
  delay(20);

  sensorOnline = true;
}

// Read raw X-axis data from accelerometer
int16_t readBMI160_X() {
  if (!sensorOnline) return 0;

  Wire.beginTransmission(BMI160_ADDR);
  Wire.write(0x14); // Accel X LSB register address
  if (Wire.endTransmission(false) != 0) {
    sensorOnline = false;
    return 0;
  }

  Wire.requestFrom(BMI160_ADDR, 2);
  if (Wire.available() == 2) {
    uint8_t lsb = Wire.read();
    uint8_t msb = Wire.read();
    return (int16_t)((msb << 8) | lsb);
  }
  return 0;
}

// Send motor command via Serial1 to rover
void sendMotorCommand(MotorCommand cmd) {
  if (cmd == lastCommand) return; // Don't repeat same command

  lastCommand = cmd;
  lastCommandTime = millis();

  String cmdStr = "";
  switch (cmd) {
    case CMD_STOP:
      cmdStr = "stop";
      break;
    case CMD_FORWARD:
      cmdStr = "forward";
      break;
    case CMD_REVERSE:
      cmdStr = "reverse";
      break;
    case CMD_LEFT:
      cmdStr = "left";
      break;
    case CMD_RIGHT:
      cmdStr = "right";
      break;
  }

  Serial1.println(cmdStr);
  Serial.print("[REMOTE CMD] ");
  Serial.println(cmdStr);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  // Initialize I2C
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setTimeOut(30);

  // Initialize OLED Display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED Fail"));
    while (1);
  }

  display.setRotation(2);
  pinMode(TOUCH_PIN, INPUT);

  // Initialize accelerometer
  checkAndWakeBMI160();

  // Initialize Serial1 for rover communication (115200 baud)
  Serial1.begin(115200, SERIAL_8N1, 7, 4);

  Serial.println("=== ROVER REMOTE CONTROL INITIALIZED ===");
}

void loop() {
  display.clearDisplay();

  // --- READ ACCELEROMETER FOR TILT CONTROL ---
  float tiltFactor = 0;
  if (sensorOnline) {
    int16_t rawX = readBMI160_X();

    // Deadzone filter (-600 to 600)
    if (abs(rawX) < 600) {
      rawX = 0;
    }

    tiltFactor = rawX / 16384.0;
  }

  // --- DETERMINE MOTOR COMMAND BASED ON TILT ---
  if (tiltFactor > 0.3) {
    currentCommand = CMD_RIGHT;
  } else if (tiltFactor < -0.3) {
    currentCommand = CMD_LEFT;
  } else if (abs(tiltFactor) > 0.1) {
    currentCommand = CMD_FORWARD;
  } else {
    currentCommand = CMD_STOP;
  }

  // Send command to rover
  sendMotorCommand(currentCommand);

  // --- CHECK FOR ROVER FEEDBACK ---
  static String roverBuffer = "";
  while (Serial1.available() > 0) {
    char c = Serial1.read();
    if (c == '\n' || c == '\r') {
      if (roverBuffer.length() > 0) {
        // Parse rover feedback
        if (roverBuffer.indexOf("Obstacle") >= 0) {
          // Extract distance value
          int idx = roverBuffer.indexOf(":");
          if (idx >= 0) {
            laserDistance = roverBuffer.substring(idx + 1).toInt();
          }
        }
        aiResponse = roverBuffer;
        Serial.print("[ROVER RESPONSE] ");
        Serial.println(roverBuffer);
        roverBuffer = "";
      }
    } else if (c > 31 && c < 127) { // Printable ASCII
      roverBuffer += c;
    }
  }

  // --- DRAW STATUS DISPLAY ---
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Header
  display.setCursor(0, 0);
  display.println("=== ROVER REMOTE ===");

  // Connection Status
  display.setCursor(0, 10);
  display.print("Accel: ");
  display.println(sensorOnline ? "OK" : "FAIL");

  // Current Command
  display.setCursor(0, 18);
  display.print("Command: ");
  switch (currentCommand) {
    case CMD_STOP:
      display.println("STOP");
      break;
    case CMD_FORWARD:
      display.println("FORWARD");
      break;
    case CMD_REVERSE:
      display.println("REVERSE");
      break;
    case CMD_LEFT:
      display.println("LEFT");
      break;
    case CMD_RIGHT:
      display.println("RIGHT");
      break;
  }

  // Tilt indicator
  display.setCursor(0, 26);
  display.print("Tilt: ");
  display.println(tiltFactor, 2);

  // Laser Distance
  display.setCursor(0, 34);
  display.print("Distance: ");
  if (laserDistance < 9999) {
    display.print(laserDistance);
    display.println("mm");
  } else {
    display.println("---");
  }

  // Last response (scrolling/truncated)
  display.setCursor(0, 42);
  display.print("Status: ");
  String statusMsg = aiResponse;
  if (statusMsg.length() > 18) {
    statusMsg = statusMsg.substring(0, 18);
  }
  display.println(statusMsg);

  // Command duration
  commandDuration = millis() - lastCommandTime;
  display.setCursor(0, 54);
  display.print("Uptime: ");
  display.print(millis() / 1000);
  display.println("s");

  display.display();
  delay(50); // 20 FPS update rate
}
