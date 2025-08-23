// RYLR993 Receiver
// Blue Pill + SSD1306 OLED + NeoPixel
// Address: 2, Receives from Address: 1
// Button A (PB1): Normal receive mode - LED count reflects received number (1-8)
// Button B (PB0): RSSI display mode - RSSI strength on OLED & LEDs (1-8 levels)
// With meteor animation

#include <HardwareSerial.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>

// Button pin configuration
#define BUTTON_A_PIN PB1  // Normal receive mode
#define BUTTON_B_PIN PB0  // RSSI display mode

// Button state management
volatile bool buttonAPressed = false;
volatile bool buttonBPressed = false;
unsigned long lastButtonTime = 0;
const unsigned long BUTTON_DEBOUNCE = 200; // ms

// Mode settings
enum DisplayMode {
  MODE_NORMAL = 0,  // Normal receive mode
  MODE_RSSI = 1     // RSSI display mode
};
DisplayMode currentMode = MODE_NORMAL;

// NeoPixel LED configuration
#define LED_PIN PA1
#define NUM_LEDS 8
#define LED_BRIGHTNESS 50  // Increased brightness for better visibility

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// OLED display configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// UART configuration for Blue Pill
// PA2 = TX, PA3 = RX
HardwareSerial lora(USART2);

// Display management
const int MAX_LINES = 8; // 64 pixels / 8 pixels per line
String displayLines[MAX_LINES];
int lineCount = 0;

// Device configuration
const int MY_ADDRESS = 2;      // This device address
const int SENDER_ADDRESS = 1;  // Expected sender address

// Statistics
unsigned long messageCount = 0;
unsigned long lastReceiveTime = 0;
int lastRSSI = -120;
int currentLEDCount = 0; // Current number of LEDs lit

// Meteor animation settings
unsigned long lastMeteorUpdate = 0;
const unsigned long METEOR_SPEED = 60; // ms per step (faster speed)
float meteorPosition = 0.0; // Use float for smoother movement
bool meteorDirection = true; // true = 0->7, false = 7->0
uint8_t trailBrightness[NUM_LEDS]; // Track brightness of each LED
const uint8_t METEOR_BRIGHTNESS = 255; // Meteor brightness (maximum for bright red)
const uint8_t TRAIL_FADE = 40; // Trail fading rate
const uint8_t MIN_BRIGHTNESS = 15; // Minimum brightness (brighter for visible pink/purple)

// Button interrupt handlers
void buttonAInterrupt() {
  if (millis() - lastButtonTime > BUTTON_DEBOUNCE) {
    buttonAPressed = true;
    lastButtonTime = millis();
  }
}

void buttonBInterrupt() {
  if (millis() - lastButtonTime > BUTTON_DEBOUNCE) {
    buttonBPressed = true;
    lastButtonTime = millis();
  }
}

void setup() {
  // Initialize Serial for debugging (optional)
  Serial.begin(115200);
  
  // Button pin setup
  pinMode(BUTTON_A_PIN, INPUT_PULLUP);
  pinMode(BUTTON_B_PIN, INPUT_PULLUP);
  
  // Interrupt setup
  attachInterrupt(digitalPinToInterrupt(BUTTON_A_PIN), buttonAInterrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_B_PIN), buttonBInterrupt, FALLING);
  
  // Initialize NeoPixel LEDs
  strip.begin();
  strip.setBrightness(LED_BRIGHTNESS);
  
  // Initialize trail brightness array
  for(int i = 0; i < NUM_LEDS; i++) {
    trailBrightness[i] = 0;
  }
  
  // LED startup animation
  startupAnimation();
  
  // Initialize I2C for OLED
  Wire.begin();
  
  // Initialize OLED display
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("MAX RANGE RX");
  display.println("RYLR993 Starting...");
  display.println("Long Range Mode");
  display.display();
  
  // Initialize UART2 for LoRa module
  lora.begin(9600);
  delay(1000);
  
  // Initialize the module for maximum range
  initializeLoRaMaxRange();
  
  addLine("MAX RANGE RX Ready");
  addLine("Addr:" + String(MY_ADDRESS) + " <- " + String(SENDER_ADDRESS));
  addLine("ButtonA: Normal Mode");
  addLine("ButtonB: RSSI Mode");
  addLine("SF7,125kHz,4/5,12");
  addLine("Meteor when waiting...");
  
  lastReceiveTime = millis();
}

void loop() {
  // Button handling
  handleButtons();
  
  // Check for incoming LoRa messages (highest priority)
  if (lora.available()) {
    String received = lora.readStringUntil('\n');
    received.trim();
    if (received.length() > 0) {
      processReceivedData(received);
    }
  }
  
  // Show meteor animation if no messages received for a while and in normal mode
  if (currentMode == MODE_NORMAL && millis() - lastReceiveTime > 3000) { // 3 seconds
    animateMeteor();
  }
  
  delay(1); // Minimal delay for maximum responsiveness
}

void handleButtons() {
  if (buttonAPressed) {
    buttonAPressed = false;
    if (currentMode != MODE_NORMAL) {
      currentMode = MODE_NORMAL;
      clearDisplayForMode();
      addLine("Mode: NORMAL");
      addLine("Shows received numbers");
      // Clear LEDs and reset
      strip.clear();
      strip.show();
    }
  }
  
  if (buttonBPressed) {
    buttonBPressed = false;
    if (currentMode != MODE_RSSI) {
      currentMode = MODE_RSSI;
      clearDisplayForMode();
      addLine("Mode: RSSI MONITOR");
      addLine("Shows signal strength");
      // Clear LEDs
      strip.clear();
      strip.show();
    }
  }
}

void clearDisplayForMode() {
  // Clear display lines
  for (int i = 0; i < MAX_LINES; i++) {
    displayLines[i] = "";
  }
  lineCount = 0;
}

void processReceivedData(String data) {
  lastReceiveTime = millis();
  
  // Check if it's a received message (starts with +RCV=)
  if (data.startsWith("+RCV=")) {
    // Format: +RCV=address,length,data,rssi,snr
    // Parse the received data
    int firstComma = data.indexOf(',');
    int secondComma = data.indexOf(',', firstComma + 1);
    int thirdComma = data.indexOf(',', secondComma + 1);
    int fourthComma = data.indexOf(',', thirdComma + 1);
    
    if (firstComma > 0 && secondComma > 0 && thirdComma > 0) {
      String senderAddr = data.substring(5, firstComma); // Skip "+RCV="
      String length = data.substring(firstComma + 1, secondComma);
      String message = data.substring(secondComma + 1, thirdComma);
      String rssi = "";
      String snr = "";
      
      if (fourthComma > 0) {
        rssi = data.substring(thirdComma + 1, fourthComma);
        snr = data.substring(fourthComma + 1);
      } else {
        rssi = data.substring(thirdComma + 1);
      }
      
      // Update RSSI value
      if (rssi.length() > 0) {
        lastRSSI = rssi.toInt();
      }
      
      // Processing depending on mode
      if (currentMode == MODE_NORMAL) {
        // Normal mode: display LEDs based on received number
        int receivedNumber = message.toInt();
        if (receivedNumber >= 1 && receivedNumber <= 8) {
          updateLEDsWithNumber(receivedNumber);
          messageCount++;
          
          addLine("RX[" + String(messageCount) + "] NUM: " + String(receivedNumber));
          addLine("RSSI:" + rssi + " LEDs:" + String(receivedNumber) + "/8");
        } else {
          addLine("RX: Invalid number " + message);
        }
      } else if (currentMode == MODE_RSSI) {
        // RSSI mode: display RSSI strength
        messageCount++;
        updateRSSIDisplay();
        updateLEDsWithRSSI(lastRSSI);
      }
    }
  } else {
    // Other responses from LoRa module
    if (currentMode == MODE_NORMAL) {
      if (data.length() > 18) {
        data = data.substring(0, 18) + "..";
      }
      addLine("LoRa: " + data);
    }
  }
}

void updateLEDsWithNumber(int number) {
  // Constrain number to valid range
  number = constrain(number, 0, NUM_LEDS);
  currentLEDCount = number;
  
  // Clear all LEDs first
  strip.clear();
  
  // Reset trail brightness when showing received number
  for(int i = 0; i < NUM_LEDS; i++) {
    trailBrightness[i] = 0;
  }
  
  // Light up LEDs based on received number
  for(int i = 0; i < number; i++) {
    // Color gradient from green to red
    if (i < NUM_LEDS / 2) {
      // Green to yellow
      strip.setPixelColor(i, strip.Color(i * 32, 255, 0));
    } else {
      // Yellow to red
      strip.setPixelColor(i, strip.Color(255, 255 - (i - NUM_LEDS/2) * 32, 0));
    }
  }
  
  // Show the LEDs immediately
  strip.show();
}

void updateLEDsWithRSSI(int rssi) {
  // Convert RSSI value to 1-8 levels
  // RSSI range: approx. -120 (weak) to -30 (strong)
  // More realistic range: -110 (weak) to -60 (strong)
  int rssiLevel = map(constrain(rssi, -110, -60), -110, -60, 1, 8);
  
  // Clear all LEDs first
  strip.clear();
  
  // Light up LEDs based on RSSI strength
  for(int i = 0; i < rssiLevel; i++) {
    // Color: weak (red) → strong (green)
    float ratio = (float)i / (NUM_LEDS - 1);
    uint8_t red = 255 * (1.0 - ratio);    // More red if weaker
    uint8_t green = 255 * ratio;          // More green if stronger
    strip.setPixelColor(i, strip.Color(red, green, 0));
  }
  
  // Show the LEDs immediately
  strip.show();
}

void updateRSSIDisplay() {
  display.clearDisplay();
  
  // Show RSSI in large font
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("RSSI MODE");
  
  display.setTextSize(2);
  display.setCursor(0, 20);
  display.print(lastRSSI);
  display.println(" dBm");
  
  // RSSI level bar display
  display.setTextSize(1);
  int rssiLevel = map(constrain(lastRSSI, -110, -60), -110, -60, 1, 8);
  display.setCursor(0, 50);
  display.print("Level: ");
  display.print(rssiLevel);
  display.println("/8");
  
  // Bar display (using ASCII chars)
  display.setCursor(0, 58);
  for(int i = 0; i < 8; i++) {
    if (i < rssiLevel) {
      display.print("#");  // Filled bar
    } else {
      display.print("-");  // Empty bar
    }
  }
  
  display.display();
}

void animateMeteor() {
  // Only animate if enough time has passed
  if (millis() - lastMeteorUpdate >= METEOR_SPEED) {
    
    // Clear all LEDs first
    strip.clear();
    
    // Fade all trail LEDs
    for(int i = 0; i < NUM_LEDS; i++) {
      if (trailBrightness[i] > MIN_BRIGHTNESS) {
        trailBrightness[i] = max((uint8_t)MIN_BRIGHTNESS, (uint8_t)(trailBrightness[i] - TRAIL_FADE));
      } else if (trailBrightness[i] > 0) {
        trailBrightness[i] = 0; // Immediately turn off if below minimum
      }
    }
    
    // Set current meteor position to maximum brightness
    int currentPos = (int)meteorPosition;
    trailBrightness[currentPos] = METEOR_BRIGHTNESS;
    
    // Update all LED colors based on trail brightness
    for(int i = 0; i < NUM_LEDS; i++) {
      if (trailBrightness[i] > 0) {
        uint8_t brightness = trailBrightness[i];
        if (i == currentPos) {
          // Meteor head: bright red
          strip.setPixelColor(i, strip.Color(brightness, 0, 0));
        } else {
          // Trail: bright pink/purple (red + blue = purple/pink)
          uint8_t trailRed = brightness / 2;     // Red component
          uint8_t trailBlue = brightness / 3;    // Blue component for pink/purple
          strip.setPixelColor(i, strip.Color(trailRed, 0, trailBlue));
        }
      }
    }
    
    // Move meteor faster
    if (meteorDirection) {
      meteorPosition += 0.8; // Faster movement
      if (meteorPosition >= NUM_LEDS - 1) {
        meteorPosition = NUM_LEDS - 1;
        meteorDirection = false; // Change direction to 7->0
      }
    } else {
      meteorPosition -= 0.8; // Faster movement
      if (meteorPosition <= 0) {
        meteorPosition = 0;
        meteorDirection = true; // Change direction to 0->7
      }
    }
    
    strip.show();
    lastMeteorUpdate = millis();
  }
}

void startupAnimation() {
  // Startup animation - sweep from 1 to 8 and back
  for(int count = 1; count <= NUM_LEDS; count++) {
    strip.clear();
    for(int i = 0; i < count; i++) {
      strip.setPixelColor(i, strip.Color(0, 255, 0)); // Green
    }
    strip.show();
    delay(100);
  }
  
  delay(200);
  
  for(int count = NUM_LEDS; count >= 0; count--) {
    strip.clear();
    for(int i = 0; i < count; i++) {
      strip.setPixelColor(i, strip.Color(255, 0, 0)); // Red
    }
    strip.show();
    delay(100);
  }
  
  strip.clear();
  strip.show();
}

void addLine(String text) {
  // In RSSI mode, skip normal log output
  if (currentMode == MODE_RSSI) {
    return;
  }
  
  // Add timestamp for received messages
  if (text.startsWith("RX[")) {
    unsigned long seconds = millis() / 1000;
    text = "[" + String(seconds) + "s] " + text;
  }
  
  // If we're at max lines, scroll up
  if (lineCount >= MAX_LINES) {
    // Shift all lines up
    for (int i = 0; i < MAX_LINES - 1; i++) {
      displayLines[i] = displayLines[i + 1];
    }
    // Add new line at bottom
    displayLines[MAX_LINES - 1] = text;
  } else {
    // Add line normally
    displayLines[lineCount] = text;
    lineCount++;
  }
  
  // Update display
  updateDisplay();
}

void updateDisplay() {
  // In RSSI mode, use dedicated display
  if (currentMode == MODE_RSSI) {
    return;
  }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  
  // Display all lines
  for (int i = 0; i < min(lineCount, MAX_LINES); i++) {
    display.println(displayLines[i]);
  }
  
  display.display();
}

void initializeLoRaMaxRange() {
  addLine("Init for MAX RANGE...");
  
  // Reset module
  sendATCommand("ATZ");
  delay(3000); // Longer wait
  
  // Set to proprietary mode
  sendATCommand("AT+OPMODE=1");
  delay(1000);
  // Set Freq
  sendATCommand("AT+BAND=923000000");
  delay(3000); 
  
  // Reset again to apply mode change
  sendATCommand("ATZ");
  delay(3000);
  
  // Set this device address
  sendATCommand("AT+ADDRESS=" + String(MY_ADDRESS));
  delay(500);
  
  // Set network ID (fixed for RYLR993)
  sendATCommand("AT+NETWORKID=18");
  delay(500);
  
  // Parameters for maximum range (must match sender)
  // SF=9, BW=125kHz (minimum), CR=4/5, Preamble=12
  sendATCommand("AT+PARAMETER=11,7,4,24");
  delay(1000);
  
  // Verify settings
  sendATCommand("AT+PARAMETER=?");
  delay(500);
  sendATCommand("AT+BAND=?");
  delay(3000);
  
  addLine("MAX RANGE RX Init OK!");
  addLine("Ready for long range");
}

void sendATCommand(String command) {
  addLine("CMD: " + command);
  lora.println(command);
  delay(500); // Longer wait for long-range settings
  
  // Read response with longer timeout
  String response = "";
  unsigned long timeout = millis() + 2000; // Longer timeout
  while (millis() < timeout) {
    if (lora.available()) {
      response += lora.readStringUntil('\n');
      response.trim();
      break;
    }
    delay(10);
  }
  
  if (response.length() > 0) {
    // Limit response length for display
    if (response.length() > 14) {
      response = response.substring(0, 14) + "..";
    }
    addLine("RSP: " + response);
  } else {
    addLine("RSP: (timeout)");
  }
}