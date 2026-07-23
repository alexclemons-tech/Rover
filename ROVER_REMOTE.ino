#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// I2C PINS
#define SDA_PIN 5
#define SCL_PIN 6

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// BMI160 Accelerometer
const int BMI160_ADDR = 0x68;
bool sensorOnline = false;

// WiFi & UDP
#include <WiFi.h>
#include <WiFiUdp.h>

const char* ssid = "mochan";
const char* password = "";
WiFiUDP udp;
const int UDP_PORT = 5005;
IPAddress roverIP(192, 168, 4, 1); // Broadcast to AP IP

// Motor Command States
enum MotorCommand {
  CMD_STOP = 0,
  CMD_FORWARD = 1,
  CMD_REVERSE = 2,
  CMD_LEFT = 3,
  CMD_RIGHT = 4,
  CMD_IDLE = 5
};

volatile MotorCommand currentCommand = CMD_IDLE;
volatile MotorCommand lastCommand = CMD_IDLE;

// Status variables
unsigned long lastCommandTime = 0;
String connectionStatus = "Connecting...";
unsigned long lastUDPSend = 0;
bool wifiConnected = false;
unsigned long lastWifiCheck = 0;

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

// Send motor command via UDP to rover
void sendMotorCommand(MotorCommand cmd) {
  if (cmd == lastCommand) return; // Don't repeat same command
  if (!wifiConnected) return;

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
    case CMD_IDLE:
      return; // Don't send idle
  }

  udp.beginPacket(roverIP, UDP_PORT);
  udp.print(cmdStr);
  udp.endPacket();

  Serial.print("[REMOTE UDP] ");
  Serial.println(cmdStr);
  lastUDPSend = millis();
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

  // Initialize accelerometer
  checkAndWakeBMI160();

  Serial.println("\n=== ROVER WIRELESS REMOTE CONTROL ===");
  
  // Connect to WiFi AP
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[✔] WiFi Connected!");
    Serial.print("Local IP: ");
    Serial.println(WiFi.localIP());
    connectionStatus = "WiFi OK";
    wifiConnected = true;
  } else {
    Serial.println("\n[✗] WiFi Failed - Check SSID and Power");
    connectionStatus = "WiFi FAIL";
    wifiConnected = false;
  }

  // Initialize UDP
  udp.begin(UDP_PORT);
  Serial.println("[✔] UDP Initialized on port 5005");
}

void loop() {
  // Check WiFi every 2 seconds (to avoid constant polling)
  if (millis() - lastWifiCheck > 2000) {
    lastWifiCheck = millis();
    if (WiFi.status() == WL_CONNECTED) {
      if (!wifiConnected) {
        wifiConnected = true;
        connectionStatus = "WiFi OK";
        Serial.println("[✔] WiFi Reconnected!");
      }
    } else {
      if (wifiConnected) {
        wifiConnected = false;
        connectionStatus = "WiFi LOST";
        Serial.println("[✗] WiFi Lost");
      }
    }
  }

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
  if (tiltFactor > 0.4) {
    currentCommand = CMD_RIGHT;
  } else if (tiltFactor < -0.4) {
    currentCommand = CMD_LEFT;
  } else if (tiltFactor > 0.15) {
    currentCommand = CMD_FORWARD;
  } else if (tiltFactor < -0.15) {
    currentCommand = CMD_FORWARD;
  } else {
    currentCommand = CMD_STOP;
  }

  // Send command to rover
  sendMotorCommand(currentCommand);

  // --- DRAW STATUS DISPLAY ---
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Header
  display.setCursor(0, 0);
  display.println("=== ROVER REMOTE ===");

  // Connection Status
  display.setCursor(0, 10);
  display.print("WiFi: ");
  display.println(connectionStatus);

  // Local IP
  display.setCursor(0, 18);
  display.print("IP: ");
  if (wifiConnected) {
    display.println(WiFi.localIP());
  } else {
    display.println("---");
  }

  // Current Command
  display.setCursor(0, 26);
  display.print("Cmd: ");
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
    case CMD_IDLE:
      display.println("IDLE");
      break;
  }

  // Tilt indicator
  display.setCursor(0, 34);
  display.print("Tilt: ");
  display.println(tiltFactor, 2);

  // Accelerometer status
  display.setCursor(0, 42);
  display.print("Accel: ");
  display.println(sensorOnline ? "OK" : "FAIL");

  // Last send time
  display.setCursor(0, 50);
  display.print("Last: ");
  unsigned long timeSinceLastSend = millis() - lastUDPSend;
  if (timeSinceLastSend < 60000) {
    display.print(timeSinceLastSend / 1000);
    display.println("s ago");
  } else {
    display.println("---");
  }

  display.display();
  delay(50); // 20 FPS update rate
}
