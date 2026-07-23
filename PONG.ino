#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// NEW SAFELY ASSIGNED I2C PINS
#define SDA_PIN 5
#define SCL_PIN 6
#define TOUCH_PIN 2

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Game Physics Values
float paddleX = 52.0;
const int paddleWidth = 24;
const int paddleHeight = 4;
const int paddleY = 58;

float ballX = 64.0;
float ballY = 50.0;
float ballVX = 1.3;
float ballVY = -1.3;
const int ballSize = 3;

// Brick Grid Layout
#define BRICK_ROWS 3
#define BRICK_COLS 10
const int brickWidth = 11;
const int brickHeight = 4;
const int brickGapX = 1;
const int brickGapY = 1;
const int gridOffsetX = 4;
const int gridOffsetY = 12;
bool bricks[BRICK_ROWS][BRICK_COLS];

int score = 0;
bool ballActive = false; 
bool gameOver = false;
bool gameWon = false;

// Direct I2C BMI160 Configs
const int BMI160_ADDR = 0x68; 
bool sensorOnline = false;
int touchDirection = 1;

void initBricks() {
  for (int r = 0; r < BRICK_ROWS; r++) {
    for (int c = 0; c < BRICK_COLS; c++) bricks[r][c] = true;
  }
}

// Safely probe and wake up the BMI160
void checkAndWakeBMI160() {
  // Test communication
  Wire.beginTransmission(BMI160_ADDR);
  if (Wire.endTransmission() != 0) {
    sensorOnline = false;
    return;
  }

  // Wake Accelerometer up into Normal Mode
  Wire.beginTransmission(BMI160_ADDR);
  Wire.write(0x7E); // CMD register
  Wire.write(0x11); // Command: Accel Normal Mode
  Wire.endTransmission();
  delay(20);        

  sensorOnline = true;
}

// Read raw data from the sensor safely
int16_t readBMI160_X() {
  if (!sensorOnline) return 0;
  
  Wire.beginTransmission(BMI160_ADDR);
  Wire.write(0x14); // Accel X LSB register address
  if (Wire.endTransmission(false) != 0) {
    sensorOnline = false; // Safely disconnect if wire pulls loose
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

void setup() {
  Serial.begin(115200);
  delay(200);
  
  // 1. Start I2C matrix on your new safe pins
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setTimeOut(30); // Prevent line locking if wires shake
  
  // 2. Initialize OLED Display
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED Fail"));
    while(1); 
  }
  
  display.setRotation(2); // Keep your 180 flipped layout
  pinMode(TOUCH_PIN, INPUT);
  
  // 3. Attempt to link the sensor
  checkAndWakeBMI160();
  
  initBricks();
}

void loop() {
  display.clearDisplay();
  bool isTouched = digitalRead(TOUCH_PIN);
  float paddleSpeed = 0;

    // --- CONTROLS MODAL ENGINE ---
  if (sensorOnline) {
    // TILT CONTROL ACTIVE
    int16_t rawX = readBMI160_X();
    
    // --- DEADZONE FILTER FOR DRIFT ---
    // If the sensor reading is tiny (between -600 and 600), force it to 0
    if (abs(rawX) < 600) {
      rawX = 0;
    }
    
    float tiltFactor = rawX / 16384.0; // Scale to G-Force physics
    paddleSpeed = tiltFactor * 6.5;   // Reduced from -6.5 to -3.5 for less sensitivity
    paddleX += paddleSpeed;
  } else {

    // FAIL-SAFE/TOUCH ACTIVE: Automatic glide, tap to flip direction
    if (isTouched) {
      static unsigned long lastTouchTime = 0;
      if (millis() - lastTouchTime > 200) {
        touchDirection *= -1; 
        lastTouchTime = millis();
      }
    }
    paddleX += (touchDirection * 2.2); 
  }
  
  // Hard borders to protect screen limits
  if (paddleX < 0) { 
    paddleX = 0; 
    if(!sensorOnline) touchDirection = 1; 
  }
  if (paddleX > (128 - paddleWidth)) { 
    paddleX = 128 - paddleWidth; 
    if(!sensorOnline) touchDirection = -1; 
  }

  // --- MENU AND SYSTEM GAME LOGIC ---
  if (gameOver || gameWon) {
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(15, 15);
    display.print(gameWon ? "YOU WIN!" : "YOU SUCK!");
    
    display.setTextSize(1);
    display.setCursor(20, 42);
    display.printf("Score: %d", score);
    display.setCursor(10, 54);
    display.print("Touch to Restart");
    
    if (isTouched) {
      score = 0; ballActive = false; gameOver = false; gameWon = false;
      paddleX = 52.0; ballX = 64.0; ballY = 50.0; initBricks();
      touchDirection = 1;
      delay(400); 
    }
    display.display();
    return;
  }

   // --- BALL DYNAMICS & CENTERED MENU ---
  if (!ballActive) {
    ballX = paddleX + (paddleWidth / 2) - (ballSize / 2);
    ballY = paddleY - ballSize;
    
    // Clear the top area where the score sits to prevent overlapping pixels
    display.fillRect(0, 0, 128, 10, SSD1306_BLACK);

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    
    if (sensorOnline) {
      // "Tilt to Start" centered perfectly at X = 25
      display.setCursor(25, 2);
      display.print("Tilt to Start");
      
      if (abs(paddleSpeed) > 1.2) ballActive = true;
    } else {
      // Fallback menu text: "Tap to Start" is 12 characters long (12 * 6 = 72 wide) -> (128 - 72)/2 = 28
      display.setCursor(28, 2);
      display.print("Tap to Start");
      
      if (!isTouched) ballActive = true;
    }
  } else {
    ballX += ballVX;
    ballY += ballVY;
    
    if (ballX <= 0 || ballX >= (128 - ballSize)) ballVX = -ballVX;
    if (ballY <= 0) ballVY = -ballVY;
    if (ballY > 64) gameOver = true;
    
    if (ballY + ballSize >= paddleY && ballY <= paddleY + paddleHeight) {
      if (ballX + ballSize >= paddleX && ballX <= paddleX + paddleWidth) {
        ballVY = -abs(ballVY);
        float hitPos = (ballX + (ballSize / 2.0)) - (paddleX + (paddleWidth / 2.0));
        ballVX = (hitPos / (paddleWidth / 2.0)) * 2.0;
      }
    }
    
    bool levelCleared = true;
    for (int r = 0; r < BRICK_ROWS; r++) {
      for (int c = 0; c < BRICK_COLS; c++) {
        if (bricks[r][c]) {
          levelCleared = false;
          int bX = gridOffsetX + c * (brickWidth + brickGapX);
          int bY = gridOffsetY + r * (brickHeight + brickGapY);
          if (ballX + ballSize >= bX && ballX <= bX + brickWidth &&
              ballY + ballSize >= bY && ballY <= bY + brickHeight) {
            bricks[r][c] = false;
            ballVY = -ballVY;
            score += 10;
            break;
          }
        }
      }
    }
    if (levelCleared) gameWon = true;
  }

  // --- DRAW SYSTEM LAYERS ---
  display.fillRect((int)paddleX, paddleY, paddleWidth, paddleHeight, SSD1306_WHITE);
  display.fillRect((int)ballX, (int)ballY, ballSize, ballSize, SSD1306_WHITE);
  
  for (int r = 0; r < BRICK_ROWS; r++) {
    for (int c = 0; c < BRICK_COLS; c++) {
      if (bricks[r][c]) {
        int bX = gridOffsetX + c * (brickWidth + brickGapX);
        int bY = gridOffsetY + r * (brickHeight + brickGapY);
        display.fillRect(bX, bY, brickWidth, brickHeight, SSD1306_WHITE);
      }
    }
  }
  
  display.setTextSize(1);
  display.setCursor(4, 2);
  display.printf("%03d", score);
  if (!sensorOnline) display.print(" [AUTO]"); // Shows if sensor is sleeping
  
  display.display();
  delay(16); 
}
