// RYLR993 Transmitter
// Blue Pill + SSD1306 OLED + NeoPixel LED Strip
// Address: 1, Sends to Address: 2

#include <HardwareSerial.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>

// OLED display configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// NeoPixel LED configuration
#define LED_PIN PA1
#define NUM_LEDS 8
#define LED_BRIGHTNESS 50
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// UART configuration for Blue Pill
HardwareSerial lora(USART2);

// Display management
const int MAX_LINES = 8;
String displayLines[MAX_LINES];
int lineCount = 0;

// Auto-send configuration
unsigned long lastSendTime = 0;
const unsigned long SEND_INTERVAL = 5000;
int currentNumber = 1;
unsigned long transmissionCount = 0;

// Device configuration
const int MY_ADDRESS = 1;
const int TARGET_ADDRESS = 2;

// Module ready flag
bool moduleReady = false;

// LED animation settings
unsigned long lastLedUpdate = 0;
const unsigned long LED_DISPLAY_DURATION = 2000;
bool showingNumber = false;
int displayingNumber = 0;

// LED color definitions
const uint32_t COLOR_OFF = strip.Color(0, 0, 0);
const uint32_t COLOR_NUMBER = strip.Color(0, 255, 0);
const uint32_t COLOR_TRANSMIT = strip.Color(0, 0, 255);
const uint32_t COLOR_ERROR = strip.Color(255, 0, 0);
const uint32_t COLOR_READY = strip.Color(255, 165, 0);
const uint32_t COLOR_INIT = strip.Color(255, 255, 0);

void setup() {
  Serial.begin(115200);
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
  display.println("MAX RANGE TX");
  display.println("RYLR993 + NeoPixel");
  display.println("Long Range Mode");
  display.display();
  
  // Initialize NeoPixel strip
  strip.begin();
  strip.setBrightness(LED_BRIGHTNESS);
  strip.show();
  
  initializationPattern();
  
  // Initialize UART2 for LoRa module
  lora.begin(9600);
  delay(1000);
  
  // Initialize the module for maximum range
  initializeLoRaMaxRange();
  
  addLine("MAX RANGE TX Ready");
  addLine("Addr:" + String(MY_ADDRESS) + " -> " + String(TARGET_ADDRESS));
  addLine("Long range mode");
  addLine("SF7,125kHz,4/5,12");
  
  readyPattern();
  lastSendTime = millis();
}

void loop() {
  checkLoRaResponse();
  
  if (moduleReady && (millis() - lastSendTime >= SEND_INTERVAL)) {
    sendNumber(currentNumber);
    transmissionCount++;
    
    showNumberOnLeds(currentNumber);
    
    currentNumber++;
    if (currentNumber > 8) {
      currentNumber = 1;
    }
    
    lastSendTime = millis();
    addLine("Next: " + String(currentNumber) + " (Count:" + String(transmissionCount) + ")");
  }
  
  updateLedDisplay();
  delay(1);
}

void checkLoRaResponse() {
  if (lora.available()) {
    String received = lora.readStringUntil('\n');
    received.trim();
    if (received.length() > 0) {
      if (received.length() > 18) {
        received = received.substring(0, 18) + "..";
      }
      addLine("RSP: " + received);
      
      if (received.equals("+OK")) {
        successPattern();
      } else if (received.startsWith("+ERR")) {
        errorPattern();
      }
    }
  }
}

void addLine(String text) {
  if (text.startsWith("TX->") || text.startsWith("Next:")) {
    unsigned long seconds = millis() / 1000;
    text = "[" + String(seconds) + "s] " + text;
  }
  
  if (lineCount >= MAX_LINES) {
    for (int i = 0; i < MAX_LINES - 1; i++) {
      displayLines[i] = displayLines[i + 1];
    }
    displayLines[MAX_LINES - 1] = text;
  } else {
    displayLines[lineCount] = text;
    lineCount++;
  }
  
  updateDisplay();
}

void updateDisplay() {
  display.clearDisplay();
  display.setCursor(0, 0);
  
  for (int i = 0; i < min(lineCount, MAX_LINES); i++) {
    display.println(displayLines[i]);
  }
  
  display.display();
}

void initializeLoRaMaxRange() {
  addLine("Init for MAX RANGE...");

  // Reset module
  sendATCommand("ATZ");
  delay(3000); 
  
  // Set to proprietary mode
  sendATCommand("AT+OPMODE=1");
  delay(1000);
  // Set Freq
  sendATCommand("AT+BAND=923000000");
  delay(3000); 
  
  // Reset again
  sendATCommand("ATZ");
  delay(3000);
  
  // Set this device address
  sendATCommand("AT+ADDRESS=" + String(MY_ADDRESS));
  delay(500);
  
  // Set network ID
  sendATCommand("AT+NETWORKID=18");
  delay(500);
  

  // sendATCommand("AT+PARAMETER=9,7,1,12");
  sendATCommand("AT+PARAMETER=11,7,4,24");
  delay(1000); 
  
  //
  sendATCommand("AT+CRFOP=22");
  delay(500);
  
  //
  sendATCommand("AT+PARAMETER=?");
  delay(500);
  
  sendATCommand("AT+CRFOP=?");
  delay(500);
  sendATCommand("AT+BAND=?");
  delay(3000);
  
  moduleReady = true;
  addLine("MAX RANGE Init OK!");
  addLine("Expected range: 1-5km");
}

void sendATCommand(String command) {
  addLine("CMD: " + command);
  lora.println(command);
  delay(500); 
  
  String response = "";
  unsigned long timeout = millis() + 2000; 
  while (millis() < timeout) {
    if (lora.available()) {
      response += lora.readStringUntil('\n');
      response.trim();
      break;
    }
    delay(10);
  }
  
  if (response.length() > 0) {
    if (response.length() > 14) {
      response = response.substring(0, 14) + "..";
    }
    addLine("RSP: " + response);
  } else {
    addLine("RSP: (timeout)");
  }
}

void sendNumber(int number) {
  transmissionPattern();
  
  String message = String(number);
  String command = "AT+SEND=" + String(TARGET_ADDRESS) + "," + String(message.length()) + "," + message;
  
  addLine("TX->" + String(TARGET_ADDRESS) + ": " + message);
  addLine("(Long range mode)");
  
  lora.println(command);
  lora.flush();
  
  delay(100);
}

// =============================================================================
// NeoPixel LED Functions
// =============================================================================

void showNumberOnLeds(int number) {
  clearAllLeds();
  
  for (int i = 0; i < number && i < NUM_LEDS; i++) {
    strip.setPixelColor(i, COLOR_NUMBER);
  }
  
  strip.show();
  
  showingNumber = true;
  displayingNumber = number;
  lastLedUpdate = millis();
  
  addLine("LED: Show " + String(number));
}

void updateLedDisplay() {
  if (showingNumber && (millis() - lastLedUpdate >= LED_DISPLAY_DURATION)) {
    clearAllLeds();
    showingNumber = false;
    readyPatternBrief();
  }
}

void clearAllLeds() {
  for (int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, COLOR_OFF);
  }
  strip.show();
}

void initializationPattern() {
  for (int j = 0; j < 3; j++) {
    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, COLOR_INIT);
      strip.show();
      delay(100);
    }
    clearAllLeds();
    delay(200);
  }
}

void readyPattern() {
  for (int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, COLOR_READY);
  }
  strip.show();
  delay(1000);
  clearAllLeds();
}

void readyPatternBrief() {
  for (int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, COLOR_READY);
  }
  strip.show();
  delay(100);
  clearAllLeds();
}

void transmissionPattern() {

  for (int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, COLOR_TRANSMIT);
    if (i > 0) strip.setPixelColor(i-1, COLOR_OFF);
    strip.show();
    delay(80);
  }
  clearAllLeds();
}

void successPattern() {
  for (int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, COLOR_NUMBER);
  }
  strip.show();
  delay(300);
  clearAllLeds();
}

void errorPattern() {
  for (int blink = 0; blink < 3; blink++) {
    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, COLOR_ERROR);
    }
    strip.show();
    delay(200);
    clearAllLeds();
    delay(200);
  }
}

void testAllNumbers() {
  for (int num = 1; num <= 8; num++) {
    showNumberOnLeds(num);
    delay(1500);
  }
  clearAllLeds();
}