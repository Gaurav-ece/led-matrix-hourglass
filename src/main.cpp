#include <Arduino.h>
#define DATA 9
#define CLOCK 10
#define LATCH 11
#define BUZZER_PIN 4

// Inputs
#define POT_PIN A6
#define BUTTON_PIN 7
#define TILT_PIN 5  

// The active display array
byte led[12] = {0,0,0,0,0,0,0,0,0,0,0,0};

// ==========================================
// PIXEL PHYSICS MAPPING
// ==========================================
byte hourglassMask[12] = {
  0b11111111, 0b11111111, 0b11111110, 0b01111100, 
  0b00111000, 0b00010000, 0b00010000, 0b00111000, 
  0b01111100, 0b11111110, 0b11111111, 0b11111111
};

byte bottomFill[32] = {
  0xB4, 0xB3, 0xB5, 0xA4, 0xB2, 0xB6, 0xA3, 0xA5, 0x94, 0xB1, 0xB7, 0xA2, 0xA6, 0x93, 0x95, 0x84,
  0xB0, 0xA1, 0xA7, 0x92, 0x96, 0x83, 0x85, 0x74, 0xA0, 0x91, 0x97, 0x82, 0x86, 0x73, 0x75, 0x64
};

byte topFill[32] = {
  0x54, 0x45, 0x43, 0x36, 0x32, 0x27, 0x21, 0x10, 0x44, 0x35, 0x33, 0x26, 0x22, 0x17, 0x11, 0x00,
  0x34, 0x25, 0x23, 0x16, 0x12, 0x07, 0x01, 0x24, 0x15, 0x13, 0x06, 0x02, 0x14, 0x05, 0x03, 0x04
};

// ==========================================
// ANIMATION & STATE VARIABLES
// ==========================================
int topSand = 32;    
int bottomSand = 0;  
int fallingX = 4;    
int fallingY = -1;   

unsigned long lastFallTime = 0;
int dropSpeed = 40;     

unsigned long lastSpawnTime = 0;
unsigned long spawnRate = 1875;   

bool lastButtonState = HIGH; 

// NON-BLOCKING GRAVITY TRACKING
bool isFlipped = false; 
bool lastTiltReading = LOW; 
unsigned long lastTiltChange = 0;

// The Alarm Lock
bool alarmPlayed = false; 

void setup() {
  pinMode(DATA, OUTPUT);
  pinMode(CLOCK, OUTPUT);
  pinMode(LATCH, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(TILT_PIN, INPUT_PULLUP); 
  
  Serial.begin(9600);
  
  lastTiltReading = digitalRead(TILT_PIN);
  isFlipped = (lastTiltReading == LOW); 
  
  Serial.println("Hourglass Powered On. Gravity Calibrated.");
}

void drawStaticSand() {
  for(int i = 0; i < 12; i++) led[i] = 0;

  for (int i = 0; i < topSand; i++) {
    byte pos = topFill[i];
    bitSet(led[pos >> 4], pos & 0x0F);
  }

  for (int i = 0; i < bottomSand; i++) {
    byte pos = bottomFill[i];
    bitSet(led[pos >> 4], pos & 0x0F);
  }
}

// ==========================================
// NEW: THE DISPLAY ENGINE FUNCTION
// ==========================================
void renderMatrix() {
  for(int r = 0; r < 12; r++) {
    
    digitalWrite(LATCH, LOW);
    shiftOut(DATA, CLOCK, MSBFIRST, 0b00000000); 
    shiftOut(DATA, CLOCK, MSBFIRST, 0b00000000); 
    shiftOut(DATA, CLOCK, MSBFIRST, 0b00000000); 
    digitalWrite(LATCH, HIGH);

    byte displayRow = r;
    if (isFlipped) {
      displayRow = 11 - r; 
    }

    byte columnData = led[displayRow]; 
    byte rowLow = 0; 
    byte rowHigh = 0; 

    if(r < 8) {
      bitSet(rowLow, r);  
    } else {
      if (r == 8)  bitSet(rowHigh, 4); 
      if (r == 9)  bitSet(rowHigh, 3); 
      if (r == 10) bitSet(rowHigh, 2); 
      if (r == 11) bitSet(rowHigh, 1); 
    }

    digitalWrite(LATCH, LOW);
    shiftOut(DATA, CLOCK, LSBFIRST, columnData); 
    shiftOut(DATA, CLOCK, MSBFIRST, rowLow);     
    shiftOut(DATA, CLOCK, MSBFIRST, rowHigh);    
    digitalWrite(LATCH, HIGH);

    delayMicroseconds(100); 
  }
}

void loop() {
  
  // ==========================================
  // PART 0: GRAVITY DETECTION (Non-Blocking)
  // ==========================================
  bool currentReading = digitalRead(TILT_PIN);

  if (currentReading != lastTiltReading) {
    lastTiltChange = millis();
  }
  lastTiltReading = currentReading;

  if ((millis() - lastTiltChange) > 50) {
    bool newFlippedState = (currentReading == LOW); 

    if (newFlippedState != isFlipped) {
      isFlipped = newFlippedState;

      int temp = topSand;
      topSand = bottomSand;
      bottomSand = temp;

      alarmPlayed = false; 

      fallingY = -1;
      lastSpawnTime = millis();
      drawStaticSand(); 
      
      Serial.println("Hourglass Flipped! Reversing Gravity.");
      
      // Multitasking chirp for the flip!
      tone(BUZZER_PIN, 1000); 
      unsigned long chirpTime = millis();
      while(millis() - chirpTime < 50) { renderMatrix(); }
      noTone(BUZZER_PIN);
    }
  }

  // ==========================================
  // PART 1: THE USER INPUT (7-Zone Timer)
  // ==========================================
  bool currentButtonState = digitalRead(BUTTON_PIN);
  
  if (lastButtonState == HIGH && currentButtonState == LOW) {
    int potValue = analogRead(POT_PIN); 
    String timeString = ""; 
    
    if (potValue < 146) {
      spawnRate = 937;       
      timeString = "30 Seconds";
    } else if (potValue < 292) {
      spawnRate = 1875;      
      timeString = "1 Minute";
    } else if (potValue < 438) {
      spawnRate = 3750;      
      timeString = "2 Minutes";
    } else if (potValue < 585) {
      spawnRate = 28125;     
      timeString = "15 Minutes";
    } else if (potValue < 731) {
      spawnRate = 56250;     
      timeString = "30 Minutes";
    } else if (potValue < 877) {
      spawnRate = 84375;     
      timeString = "45 Minutes";
    } else {
      spawnRate = 112500;    
      timeString = "60 Minutes";
    }
    
    Serial.print("Timer Set To: ");
    Serial.println(timeString);
    
    tone(BUZZER_PIN, 1500);
    unsigned long chirpTime = millis();
    while(millis() - chirpTime < 100) { renderMatrix(); }
    noTone(BUZZER_PIN);
    
    topSand = 32;
    bottomSand = 0;
    fallingY = -1;
    alarmPlayed = false; 
    lastSpawnTime = millis();
    drawStaticSand(); 
  }
  lastButtonState = currentButtonState;

  // ==========================================
  // PART 2: THE PARTICLE ENGINE
  // ==========================================
  if (fallingY == -1) {
    if (millis() - lastSpawnTime > spawnRate) {
      lastSpawnTime += spawnRate; 
      
      if (topSand > 0) {
        topSand--;      
        fallingY = 6;   
      } 
      // THE FIX: Multitasking the Alarm!
      else if (bottomSand == 32 && !alarmPlayed) {
        drawStaticSand();
        
        for(int b = 0; b < 3; b++) {
          tone(BUZZER_PIN, 2000); 
          unsigned long beepStart = millis();
          // Keep drawing the matrix while the buzzer rings!
          while(millis() - beepStart < 150) { renderMatrix(); }
          
          noTone(BUZZER_PIN);     
          unsigned long silenceStart = millis();
          // Keep drawing the matrix while the buzzer is silent!
          while(millis() - silenceStart < 100) { renderMatrix(); }
        }
        
        alarmPlayed = true; 
      }
      
      drawStaticSand();
      if (fallingY != -1) bitSet(led[fallingY], fallingX);
    }
  } else {
    if (millis() - lastFallTime > dropSpeed) {
      lastFallTime += dropSpeed;
      fallingY++;

      drawStaticSand(); 
      bool landed = false;

      if (fallingY == 11) {
        landed = true; 
      } else if (bitRead(led[fallingY + 1], fallingX) == 1) {
        landed = true; 
      }

      if (landed) {
        bottomSand++;  
        fallingY = -1; 
      }
      
      drawStaticSand();
      if (fallingY != -1) bitSet(led[fallingY], fallingX);
    }
  }

  // ==========================================
  // PART 3: CONTINUOUS RENDER
  // ==========================================
  renderMatrix();
}