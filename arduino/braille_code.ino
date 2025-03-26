/*
  Braille Printer Controller
  For Arduino MEGA with RAMPS 1.4, HC-05 Bluetooth
  
  Hardware:
  - Arduino MEGA with RAMPS 1.4
  - HC-05 Bluetooth module connected to AUX-1 (D1, D2)
  - X-axis stepper motor (for horizontal movement)
  - Paper roller stepper motor (Z-axis)
  - Solenoid for dot impression
  - 16x2 LCD display
  
  Functionality:
  - Receives text via Bluetooth
  - Translates text to standard Braille (letters and numbers)
  - Prints in mirrored format for proper reading when flipped
  - Prints row-by-row across all characters in a line
  - Uses proper Braille dimensions (2mm gaps, 6mm char spacing, 12mm word spacing)
  - Optimized for standard Braille dimensions
  - Displays status information on LCD
  - Adjustable X-axis speed via Bluetooth app
*/

#include <SoftwareSerial.h>
#include <AccelStepper.h>
#include <LiquidCrystal.h>

// Pins for RAMPS 1.4 X-axis (horizontal movement)
#define X_STEP_PIN         54
#define X_DIR_PIN          55
#define X_ENABLE_PIN       38
#define X_MIN_PIN           3
#define X_MAX_PIN           2  // Right side stopper (end of line detection)

// Pins for RAMPS 1.4 Z-axis (used for paper roller)
#define Z_STEP_PIN         46
#define Z_DIR_PIN          48
#define Z_ENABLE_PIN       62
#define Z_MIN_PIN          18
#define Z_MAX_PIN          19

// Solenoid pin (using E0 output for solenoid)
#define SOLENOID_PIN       10

// Bluetooth module connected to AUX-1
#define BT_RX_PIN           1  // Digital pin D1
#define BT_TX_PIN           2  // Digital pin D2

// LCD pins for 16x2 display
#define LCD_RS_PIN         16
#define LCD_ENABLE_PIN     17
#define LCD_D4_PIN         23
#define LCD_D5_PIN         25
#define LCD_D6_PIN         27
#define LCD_D7_PIN         29

// Constants
#define MAX_LINE_LENGTH    18  // Maximum characters per line for Letter paper
#define DOT_PRESS_TIME    300  // Time in ms to keep solenoid active

// Braille dimensions in stepper motor steps (calibrated for 2mm gaps)
// Assuming 40 steps per mm for the stepper motor configuration
#define STEP_PER_MM        70
#define DOT_DIAMETER       STEP_PER_MM * 1.4   // 1.4mm dot diameter 
#define DOT_GAP            STEP_PER_MM * 2   // 2mm gap between dots 
#define CHAR_SPACING       STEP_PER_MM * 6  // 6mm between characters 
#define WORD_SPACING       STEP_PER_MM * 12  // 12mm between words 
#define LINE_SPACING       STEP_PER_MM * 15  // 12mm between lines 

// Motor settings
#define X_ACCEL_MIN       1000  // Minimum acceleration for X-axis (slowest setting)
#define X_MAX_SPEED_MIN   2000  // Minimum max speed for X-axis (slowest setting)
#define X_ACCEL_MAX      12000  // Maximum acceleration for X-axis (fastest setting)
#define X_MAX_SPEED_MAX  24000  // Maximum max speed for X-axis (fastest setting)
#define Z_ACCEL           200   // Acceleration for paper roller stepper
#define Z_MAX_SPEED       400   // Max speed for paper roller stepper

// Paper dimensions (Letter size: 8.5" x 11")
// Converted to stepper motor steps based on assumed 40 steps per mm
#define PAPER_WIDTH       8500  // 8.5" in stepper steps (40 * 8.5 * 25.4)
#define PAPER_HEIGHT     11000  // 11" in stepper steps (40 * 11 * 25.4)
#define MARGIN_LEFT       500   // Left margin in stepper steps
#define MARGIN_RIGHT      500   // Right margin in stepper steps
#define MARGIN_TOP        500   // Top margin in stepper steps

// Setup stepper motors
AccelStepper xStepper(AccelStepper::DRIVER, X_STEP_PIN, X_DIR_PIN);
AccelStepper zStepper(AccelStepper::DRIVER, Z_STEP_PIN, Z_DIR_PIN);

// Setup Bluetooth serial
SoftwareSerial btSerial(BT_RX_PIN, BT_TX_PIN);

// Setup LCD
LiquidCrystal lcd(LCD_RS_PIN, LCD_ENABLE_PIN, LCD_D4_PIN, LCD_D5_PIN, LCD_D6_PIN, LCD_D7_PIN);

// Variables
String inputText = "";
bool newData = false;
int printPositionX = 0;
int printPositionY = 0;
int printableWidth = PAPER_WIDTH - MARGIN_LEFT - MARGIN_RIGHT;
char currentLine[MAX_LINE_LENGTH + 1];
int lineLength = 0;
bool numberMode = false;    // Flag for number mode in Braille

// Speed control variable
int xSpeedSetting = 70; // Default speed setting (40-100 range, 70 is middle)

// LCD message variables
String lcdStatus = "Ready";
String lcdInfo = "";

// Test mode variables
String testText = "";       // Text for testing without Bluetooth input
bool testMode = false;      // Flag to indicate if test mode is active

// Store line data for row-by-row printing
struct BrailleChar {
  byte pattern;
  bool isSpace;
};

BrailleChar lineBuffer[MAX_LINE_LENGTH];

// Braille patterns for each letter and number (6 dots in 3x2 matrix)
// Bit positions represent dots in standard Braille order:
// 0 3
// 1 4
// 2 5
const byte braillePatterns[] = {
  // Letters (standard Braille patterns)
  0b00000000,  // Space
  0b00001000,  // a/1 - dot 0 only
  0b00000110,  // b/2 - dots 0, 1
  0b00100100,  // c/3 - dots 0, 3
  0b00110100,  // d/4 - dots 0, 3, 4
  0b00000110,  // e/5 - dots 0, 4
  0b00001011,  // f/6 - dots 0, 1, 3 - // 0b00 001011
  0b00110110,  // g/7 - dots 0, 1, 3, 4
  0b00010110,  // h/8 - dots 0, 1, 4
  0b00100010,  // i/9 - dots 1, 3
  0b00001101,  // j/0 - dots 1, 3, 4
  0b00000101,  // k - dots 0, 2
  0b00000111,  // l - dots 0, 1, 2
  0b00100011,  // m - dots 0, 2, 3
  0b00110101,  // n - dots 0, 2, 3, 4
  0b00100110,  // o - dots 0, 2, 4
  0b00100111,  // p - dots 0, 1, 2, 3
  0b00110111,  // q - dots 0, 1, 2, 3, 4
  0b00101110,  // r - dots 0, 1, 2, 4
  0b00100011,  // s - dots 1, 2, 3
  0b00110011,  // t - dots 1, 2, 3, 4
  0b00001101,  // u - dots 0, 2, 5
  0b00001111,  // v - dots 0, 1, 2, 5
  0b00111010,  // w - dots 1, 3, 4, 5
  0b00101101,  // x - dots 0, 2, 3, 5
  0b00111101,  // y - dots 0, 2, 3, 4, 5
  0b00011101,  // z - dots 0, 2, 4, 5
};

// Number sign (placed before numbers)
const byte numberSign = 0b00111110;  // Number sign pattern

// Numbers in Braille are the same as the first 10 letters, but with the number sign before them
// 1=a, 2=b, 3=c, 4=d, 5=e, 6=f, 7=g, 8=h, 9=i, 0=j

// Add these variables after other global variables
long totalStepsFromHome = 0;  // Tracks total steps moved from home position
long maxStepsFromHome = 0;    // Stores maximum steps moved in one line

#define BT_BUFFER_SIZE 256
char btBuffer[BT_BUFFER_SIZE];
int btBufferIndex = 0;
unsigned long lastBtRead = 0;
const int BT_READ_TIMEOUT = 100; // ms

bool btConnected = false;
unsigned long lastActivity = 0;
#define ACTIVITY_TIMEOUT 1000  // 1 second timeout

void setup() {
  // Initialize serial communications
  Serial.begin(9600);
  btSerial.begin(9600);

  // Initialize LCD
  lcd.begin(16, 2);
  updateLCD("Braille Printer", "Initializing...");
  
  // Initialize stepper motors
  updateMotorSpeeds(xSpeedSetting); // Set initial motor speeds
  xStepper.setEnablePin(X_ENABLE_PIN);
  xStepper.setPinsInverted(false, false, true);
  xStepper.enableOutputs();

  zStepper.setMaxSpeed(Z_MAX_SPEED);
  zStepper.setAcceleration(Z_ACCEL);
  zStepper.setEnablePin(Z_ENABLE_PIN);
  zStepper.setPinsInverted(false, false, true);
  zStepper.enableOutputs();

  // Initialize solenoid pin
  pinMode(SOLENOID_PIN, OUTPUT);
  digitalWrite(SOLENOID_PIN, LOW);

  // Initialize endstop pins
  pinMode(X_MIN_PIN, INPUT_PULLUP);
  pinMode(X_MAX_PIN, INPUT_PULLUP);

  // Home the X-axis to right side (X_MAX)
  updateLCD("Homing X-Axis", "Please wait...");
  homeXAxisToRight();

  // Set test text - default to empty string
  // Change this string to test printing without Bluetooth input
  testText = "";
  
  // If test text is provided, set test mode to true
  if (testText.length() > 0) {
    testMode = true;
    Serial.print("Test mode active. Will print: ");
    Serial.println(testText);
  }

  updateLCD("Braille Printer", "Ready");
  Serial.println("Braille Printer Ready");
  btSerial.println("Braille Printer Ready");

  // Clear any residual data
  clearBuffers();
}

// Function to update LCD with status information
void updateLCD(String line1, String line2) {
  lcdStatus = line1;
  lcdInfo = line2;
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

// Function to update motor speeds based on speed setting (40-100)
void updateMotorSpeeds(int speedSetting) {
  // Ensure speed setting is in valid range
  speedSetting = constrain(speedSetting, 40, 100);
  xSpeedSetting = speedSetting;
  
  // Map speed setting to actual motor values
  long maxSpeed = map(speedSetting, 40, 100, X_MAX_SPEED_MIN, X_MAX_SPEED_MAX);
  long accel = map(speedSetting, 40, 100, X_ACCEL_MIN, X_ACCEL_MAX);
  
  // Update stepper motor settings
  xStepper.setMaxSpeed(maxSpeed);
  xStepper.setAcceleration(accel);
  
  // Update LCD with new speed setting
  String speedInfo = "Speed: " + String(speedSetting) + "%";
  updateLCD(lcdStatus, speedInfo);
  
  Serial.print("X-axis speed set to: ");
  Serial.print(speedSetting);
  Serial.print("% (MaxSpeed: ");
  Serial.print(maxSpeed);
  Serial.print(", Accel: ");
  Serial.print(accel);
  Serial.println(")");
  
  // Wait a moment to display speed, then restore previous info
  delay(1000);
  updateLCD(lcdStatus, lcdInfo);
}

// Add new function to clear all buffers
void clearBuffers() {
  // Clear Bluetooth buffer
  while(btSerial.available()) {
    btSerial.read();
  }
  
  // Clear hardware serial buffer
  while(Serial.available()) {
    Serial.read();
  }
  
  // Reset all buffer variables
  btBufferIndex = 0;
  inputText = "";
  newData = false;
  memset(btBuffer, 0, BT_BUFFER_SIZE);
}

void loop() {
  // If in test mode and we haven't printed the test text yet
  if (testMode) {
    Serial.println("Printing test text...");
    printBrailleText(testText);
    testMode = false; // Only print the test text once
    Serial.println("Test print completed.");
  }
  
  // Check for inactivity timeout
  if ((millis() - lastActivity) > ACTIVITY_TIMEOUT) {
    clearBuffers();  // Clear buffers if no activity
    lastActivity = millis();
  }

  // Serial Monitor handling
  if (Serial.available()) {
    String serialInput = Serial.readStringUntil('\n');
    serialInput.trim();  // Remove any whitespace/carriage return
    
    if (serialInput.length() > 0) {
      Serial.print("Serial received: ");
      Serial.println(serialInput);
      
      if (serialInput.startsWith("#")) {
        processSerialCommand();
      } else {
        inputText = serialInput;
        newData = true;
      }
    }
  }

  // Bluetooth handling with improved buffer management
  while (btSerial.available()) {
    lastActivity = millis();  // Reset activity timer
    
    char inChar = btSerial.read();
    Serial.print("BT Raw: 0x");
    Serial.println(inChar, HEX);
    
    // Handle connection state changes
    if (inChar == '\0' || inChar == 0xFF) {
      clearBuffers();  // Clear buffers on potential disconnection
      updateLCD("BT Disconnected", "Waiting...");
      btConnected = false;
      continue;
    } else if (!btConnected) {
      btConnected = true;
      updateLCD("BT Connected", "Ready");
    }
    
    if (inChar == '#') {
      clearBuffers();
      processCommand();
    } else if (inChar == '$') {
      // New command prefix for speed control
      processSpeedCommand();
    } else if (inChar == '\n' || inChar == '\r') {
      if (btBufferIndex > 0) {
        btBuffer[btBufferIndex] = '\0';
        inputText = String(btBuffer);
        Serial.print("BT Message complete: ");
        Serial.println(inputText);
        updateLCD("Received text", String(btBufferIndex) + " chars");
        newData = true;
        btBufferIndex = 0;  // Reset buffer
      }
    } else {
      if (btBufferIndex < BT_BUFFER_SIZE - 1) {
        btBuffer[btBufferIndex++] = inChar;
      } else {
        clearBuffers();  // Buffer overflow protection
      }
    }
    
    delay(2);  // Small delay for stable reading
  }

  // Process new data if available
  if (newData) {
    if (inputText.length() > 0) {
      Serial.print("Processing: ");
      Serial.println(inputText);
      updateLCD("Printing", String(inputText.length()) + " chars");
      printBrailleText(inputText);
      updateLCD("Print Complete", "Ready");
      btSerial.println("OK");  // Acknowledge receipt
    }
    clearBuffers();  // Clear after processing
  }

  // Check for incoming Serial data (for computer-based testing)
  while (Serial.available()) {
    char inChar = Serial.read();
    
    delay(10); // Small delay to allow buffer to fill
    
    // Check for command prefixes
    if (inChar == '#') {
      // Command mode
      processSerialCommand();
    } else if (inChar == '\n' || inChar == '\r') {
      // End of text input
      if (inputText.length() > 0) {
        newData = true;
      }
    } else {
      // Add character to input buffer
      inputText += inChar;
    }
  }

  // Process new data if available
  if (newData) {
    Serial.print("Received text: ");
    Serial.println(inputText);
    
    printBrailleText(inputText);
    
    // Clear for next input
    inputText = "";
    newData = false;
  }

  // Update steppers
  xStepper.run();
  zStepper.run();
}

// Process speed command from Bluetooth ($SPEED:XX where XX is 40-100)
void processSpeedCommand() {
  Serial.println("Processing speed command...");
  btBufferIndex = 0; // Reset buffer
  
  // Wait for complete command with timeout
  unsigned long commandStart = millis();
  while ((millis() - commandStart) < 1000) { // 1 second timeout
    while (btSerial.available()) {
      char c = btSerial.read();
      
      if (c == '\n' || c == '\r') {
        btBuffer[btBufferIndex] = '\0';
        String command = String(btBuffer);
        btBufferIndex = 0;
        
        Serial.print("Processing speed command: ");
        Serial.println(command);
        
        if (command.startsWith("SPEED:")) {
          String speedValueStr = command.substring(6);
          int speedValue = speedValueStr.toInt();
          
          if (speedValue >= 40 && speedValue <= 100) {
            updateMotorSpeeds(speedValue);
            btSerial.println("OK:SPEED=" + String(speedValue));
          } else {
            btSerial.println("ERROR:Invalid speed value");
          }
        }
        return;
      } else if (btBufferIndex < BT_BUFFER_SIZE - 1) {
        btBuffer[btBufferIndex++] = c;
      }
      delay(10); // Small delay between reads
    }
  }
  
  Serial.println("Speed command timeout!");
  btBufferIndex = 0;
  clearBuffers();
}

// Process commands from the Bluetooth app
void processCommand() {
  Serial.println("Processing BT command...");
  btBufferIndex = 0; // Reset buffer
  
  // Wait for complete command with timeout
  unsigned long commandStart = millis();
  while ((millis() - commandStart) < 1000) { // 1 second timeout
    while (btSerial.available()) {
      char c = btSerial.read();
      Serial.print("Command char: ");
      Serial.println(c);
      
      if (c == '\n' || c == '\r') {
        btBuffer[btBufferIndex] = '\0';
        String command = String(btBuffer);
        btBufferIndex = 0;
        
        Serial.print("Processing command: ");
        Serial.println(command);
        
        // Process the command
        if (command.startsWith("HOME")) {
          updateLCD("Homing X-Axis", "Please wait...");
          homeXAxisToRight();
          printPositionY = 0;
          btSerial.println("OK:HOME");
          updateLCD("Braille Printer", "Ready");
        } else if (command.startsWith("RESET")) {
          updateLCD("Resetting", "Please wait...");
          homeXAxisToRight();
          resetPaper();
          btSerial.println("OK:RESET");
          updateLCD("Braille Printer", "Ready");
        } else if (command.startsWith("TEST=")) {
          String testText = command.substring(5);
          updateLCD("Test Printing", testText.substring(0, 16));
          printBrailleText(testText);
          btSerial.println("OK:TEST");
          updateLCD("Braille Printer", "Ready");
        }
        return;
      } else if (btBufferIndex < BT_BUFFER_SIZE - 1) {
        btBuffer[btBufferIndex++] = c;
      }
      delay(10); // Small delay between reads
    }
  }
  
  Serial.println("Command timeout!");
  btBufferIndex = 0;

  clearBuffers();
}

// Process commands from the Serial monitor
void processSerialCommand() {
  delay(100); // Give time for complete command to arrive
  String command = "";
  
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') break;
    command += c;
  }

  if (command.startsWith("HOME")) {
    homeXAxisToRight();
    printPositionY = 0;
    Serial.println("Printer homed");
  } else if (command.startsWith("RESET")) {
    homeXAxisToRight();
    resetPaper();
    Serial.println("Printer reset");
  } else if (command.startsWith("TEST=")) {
    String testText = command.substring(5);
    Serial.print("Printing test text: ");
    Serial.println(testText);
    printBrailleText(testText);
    Serial.println("Test print complete");
  } else if (command.startsWith("SPEED=")) {
    int speedValue = command.substring(6).toInt();
    if (speedValue >= 40 && speedValue <= 100) {
      updateMotorSpeeds(speedValue);
      Serial.println("Speed updated to: " + String(speedValue));
    } else {
      Serial.println("Invalid speed value. Use range 40-100.");
    }
  }
  
  // Additional commands can be added here

  clearBuffers();
}

// Home the X-axis to right side (X_MAX) - this is the starting point for printing
void homeXAxisToRight() {
  Serial.println("X-AXIS: Homing to right margin...");
  
  // Move towards right end switch until triggered
  xStepper.moveTo(30000); // Move a large distance towards right end
  
  while (!digitalRead(X_MAX_PIN) && xStepper.distanceToGo() != 0) {
    xStepper.run();
  }
  
  xStepper.stop();
  
  // Set the current position to be at the right margin
  xStepper.setCurrentPosition(PAPER_WIDTH - MARGIN_RIGHT);
  printPositionX = PAPER_WIDTH - MARGIN_RIGHT;
  
  // Move slightly away from end switch
  xStepper.moveTo(printPositionX - 50);
  while (xStepper.distanceToGo() != 0) {
    xStepper.run();
  }
  
  // Reset position to right margin
  xStepper.setCurrentPosition(PAPER_WIDTH - MARGIN_RIGHT);
  printPositionX = PAPER_WIDTH - MARGIN_RIGHT;
  
  totalStepsFromHome = 0;  // Reset step counter when at home
  maxStepsFromHome = 0;    // Reset maximum step counter
  
  Serial.print("X-AXIS: Homed to position ");
  Serial.println(printPositionX);
  updateLCD("X-Axis Homed", String(printPositionX));
}

// Reset paper position
void resetPaper() {
  updateLCD("Resetting Paper", "Please wait...");
  // Move paper to beginning
  zStepper.moveTo(-10000);
  while (zStepper.distanceToGo() != 0) {
    zStepper.run();
  }
  
  zStepper.setCurrentPosition(MARGIN_TOP);
  printPositionY = MARGIN_TOP;
  updateLCD("Paper Reset", "Ready");
}

// Get Braille pattern for a character
byte getBraillePattern(char c) {
  // Get Braille pattern for the character
  if (c == ' ') {
    return braillePatterns[0]; // Space
  } else if (c >= 'a' && c <= 'z') {
    return braillePatterns[c - 'a' + 1]; // a-z
  } else if (c >= 'A' && c <= 'Z') {
    return braillePatterns[c - 'A' + 1]; // A-Z (same as lowercase)
  } else {
    return braillePatterns[0]; // Default to space for unsupported characters
  }
}

// Print Braille text
void printBrailleText(String text) {
  int textLength = text.length();
  updateLCD("Printing Text", String(text.length()) + " chars");
  
  // Process text in lines
  for (int i = 0; i < textLength; i++) {
    currentLine[lineLength] = text.charAt(i);
    lineLength++;
    
    // Check if line is full or we reached end of text
    if (lineLength >= MAX_LINE_LENGTH || i == textLength - 1) {
      // Prepare and print current line
      prepareBrailleLine(currentLine, lineLength);
      printPreparedLine();
      
      // Reset line buffer
      lineLength = 0;
    }
    
    // Update LCD with progress periodically
    if (i % 5 == 0) {
      updateLCD("Printing", String(i+1) + "/" + String(textLength));
    }
  }
  
  // Send confirmation
  updateLCD("Print Complete", "Ready");
  Serial.println("Printing complete");
  btSerial.println("Printing complete");
}

// Prepare a line of Braille text for row-by-row printing
void prepareBrailleLine(char* line, int length) {
  // Reset number mode flag
  numberMode = false;
  
  // Process each character in the line
  int bufferIndex = 0;
  
  for (int i = 0; i < length; i++) {
    char c = line[i];
    
    // Check for numbers
    if (c >= '0' && c <= '9') {
      // If first number in sequence, add number sign
      if (!numberMode) {
        lineBuffer[bufferIndex].pattern = numberSign;
        lineBuffer[bufferIndex].isSpace = false;
        bufferIndex++;
        numberMode = true;
      }
      
      // Convert number to equivalent letter pattern (1=a, 2=b, etc.)
      if (c == '0') {
        lineBuffer[bufferIndex].pattern = getBraillePattern('j');
      } else {
        // Convert '1'-'9' to 'a'-'i'
        lineBuffer[bufferIndex].pattern = getBraillePattern('a' + (c - '1'));
      }
      lineBuffer[bufferIndex].isSpace = false;
      bufferIndex++;
      
    } else {
      // Reset number mode when non-number encountered
      numberMode = false;
      
      lineBuffer[bufferIndex].pattern = getBraillePattern(c);
      lineBuffer[bufferIndex].isSpace = (c == ' ');
      bufferIndex++;
    }
  }
  
  // Set actual line length (may be greater than original if number signs were added)
  lineLength = bufferIndex;
}

// Print the prepared line row by row
void printPreparedLine() {
  homeXAxisToRight();
  totalStepsFromHome = 0;  // Reset step counter at start of line
  
  for (int row = 0; row < 3; row++) {
    Serial.println("\n----------------------------------------");
    Serial.print("ROW ");
    Serial.print(row + 1);
    Serial.println(" OF 3");
    Serial.println("----------------------------------------");
    
    homeXAxisToRight();
    
    if (row > 0) {
      // Return to home before starting new row
      returnToHomePosition();
    }
    
    for (int i = 0; i < lineLength; i++) {
      byte pattern = lineBuffer[i].pattern;
      bool isSpace = lineBuffer[i].isSpace;
      
      if (!isSpace) {
        // Log current character being printed
        Serial.println("\n-> Processing character:");
        Serial.print("   Position: ");
        Serial.print(i + 1);
        Serial.print(" of ");
        Serial.println(lineLength);
        Serial.print("   Character: ");
        if (pattern == numberSign) {
          Serial.println("#"); // Number sign
        } else {
          Serial.write(currentLine[i]);
          Serial.println();
        }
        
        // Right dot in this row
        Serial.println("Pattern: ");
        Serial.print(pattern);
        Serial.print(" - ");
        Serial.print((1 << (row * 2 + 1)));
        if (pattern & (1 << (row * 2 + 1))) {
          Serial.println("   * Printing right dot");
          activateSolenoid();
        }
        
        Serial.println("   * Moving to left dot position");
        moveXBy(-(DOT_GAP * 1.5 ));
        
        // Left dot in this row
        if (pattern & (1 << (row * 2))) {
          Serial.println("   * Printing left dot");
          activateSolenoid();
        }
        
        if (i < lineLength - 1) {
          if (isSpace) {
            moveXBy(-WORD_SPACING);
          } else {
            moveXBy(-(CHAR_SPACING + (DOT_GAP * 1.5)));
          }
        }
      } else {
        Serial.println("Character: [SPACE]");
        moveXBy(-WORD_SPACING);
      }
    }
    
    // After printing each row, return to home
    returnToHomePosition();
    
    if (row < 2) {
      advancePaperForRow();
    }
  }
  
  advancePaperForNewLine();
  
  Serial.print("Maximum steps from home in this line: ");
  Serial.println(maxStepsFromHome);
  maxStepsFromHome = 0;  // Reset for next line
}

// Activate solenoid to make a dot
void activateSolenoid() {
  digitalWrite(SOLENOID_PIN, HIGH);
  delay(DOT_PRESS_TIME);
  digitalWrite(SOLENOID_PIN, LOW);
}

// Move X-axis to absolute position
void moveXTo(int position) {
  Serial.print("X-AXIS: Moving to position ");
  Serial.print(position);
  Serial.print(" from ");
  Serial.println(printPositionX);
  
  xStepper.moveTo(position);
  while (xStepper.distanceToGo() != 0) {
    xStepper.run();
  }
  printPositionX = position;
}

// Move X-axis by relative amount
void moveXBy(int distance) {
  Serial.print("X-AXIS: Moving by ");
  Serial.print(distance);
  Serial.print(" steps from position ");
  Serial.println(printPositionX);
  
  xStepper.move(distance);
  while (xStepper.distanceToGo() != 0) {
    xStepper.run();
  }
  printPositionX += distance;
  totalStepsFromHome += (-distance);  // Negative because moving left increases steps from home
  
  // Update maximum steps if this is the furthest we've moved
  if (totalStepsFromHome > maxStepsFromHome) {
    maxStepsFromHome = totalStepsFromHome;
  }
  
  Serial.print("Steps from home: ");
  Serial.println(totalStepsFromHome);
}

// Add new function to return to home using counted steps
void returnToHomePosition() {
  Serial.println("X-AXIS: Returning to home using step count");
  Serial.print("Moving right by ");
  Serial.print(totalStepsFromHome);
  Serial.println(" steps");
  
  xStepper.move(totalStepsFromHome);  // Move right by total steps moved left
  while (xStepper.distanceToGo() != 0) {
    xStepper.run();
  }
  
  printPositionX = PAPER_WIDTH - MARGIN_RIGHT;
  totalStepsFromHome = 0;  // Reset step counter
  
  Serial.println("X-AXIS: Returned to home position");
}

// Advance paper slightly for next row within the same Braille cell
void advancePaperForRow() {
  Serial.print("PAPER: Moving up by ");
  Serial.print(DOT_GAP);
  Serial.println(" steps for next dot row");
  
  zStepper.move(DOT_GAP);
  while (zStepper.distanceToGo() != 0) {
    zStepper.run();
  }
  printPositionY += DOT_GAP;
  Serial.print("PAPER: New Y position: ");
  Serial.println(printPositionY);
  updateLCD("Moving Paper", "Next Row");
}

// Advance paper to the next line
void advancePaperForNewLine() {
  int remainingDistance = LINE_SPACING - (DOT_GAP * 2);
  
  Serial.print("PAPER: Advancing to next line by ");
  Serial.print(remainingDistance);
  Serial.println(" steps");
  
  zStepper.move(remainingDistance);
  while (zStepper.distanceToGo() != 0) {
    zStepper.run();
  }
  printPositionY += remainingDistance;
  
  Serial.print("PAPER: New line Y position: ");
  Serial.println(printPositionY);
  
  homeXAxisToRight();
  
  if (printPositionY >= PAPER_HEIGHT - MARGIN_TOP) {
    updateLCD("Warning!", "End of page");
    btSerial.println("Warning: Approaching end of page");
    Serial.println("Warning: Approaching end of page");
    delay(2000); // Show warning for 2 seconds
    updateLCD("Braille Printer", "Ready");
  }
}