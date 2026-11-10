/*
  DuoXCardAdapter - Connect two Behringer X-Cards (X-DANTE and X-WSG) with each other
  (c) 2025 Chris Noeding
  v1.0.0 built on 25.03.2025
  https://github.com/xn--nding-jua/DuoXCardAdapter

  Ported to RP2040 (Waveshare RP2040-Zero, arduino-pico core). The FPGA of the original
  Arduino MKR Vidor 4000 version is gone. The PCB connects the TDM-lines of both cards directly
  with each other, so the RP2040 only has to
  - generate the frame-sync for both cards (BCLK comes from a 12.288 MHz oscillator),
  - emulate an X32 on the UART-lines of both cards and
  - forward ASCII-commands from USB-serial to the cards.

  Pin information (RP2040-Zero):
  +------+--------------------+-----------------------------------------------+
  | GPIO | Function           | Usage in this project                         |
  +------+--------------------+-----------------------------------------------+
  | GP2  | PIO-UART RX        | <- Uart TxD of X-CARD #2 (X-WSG)              |
  | GP3  | PIO-UART TX        | -> Uart RxD of X-CARD #2 (X-WSG)              |
  | GP4  | UART1 TX (Serial2) | -> Uart RxD of X-CARD #1 (X-DANTE)            |
  | GP5  | UART1 RX (Serial2) | <- Uart TxD of X-CARD #1 (X-DANTE)            |
  | GP6  | PIO input          | BitClock 12.288 MHz from oscillator           |
  | GP7  | PIO output         | FrameSync 48 kHz to X-CARD #1 and #2          |
  | GP10 | GPIO in            | X-CARD #2 Ready                               |
  | GP11 | GPIO in            | X-CARD #1 Ready                               |
  | GP16 | WS2812 RGB-LED     | Heartbeat                                     |
  | USB  | CDC Serial         | ASCII command interface                       |
  +------+--------------------+-----------------------------------------------+
*/

#include "SAMD.h"

//Adafruit_NeoPixel statusLed(1, STATUS_LED_PIN, NEO_GRB + NEO_KHZ800);
bool ledState = false;
uint32_t lastHeartbeat = 0;

void setup() {
  // initialize peripherals
  //statusLed.begin();
  //statusLed.show();
   
  
  // initialize communication
  // Serial for communication via USB
  Serial.begin(115200);
  Serial.setTimeout(1000); // Timeout for commands
  
  pinMode(CARD1_READY_PIN, INPUT_PULLDOWN); // CardReady-signal #1
  pinMode(CARD2_READY_PIN, INPUT_PULLDOWN); // CardReady-signal #2
  delay(9000);
  // the cards need BCLK and FrameSync as soon as they boot
  startTdmFrameSync();
  
  digitalWrite(CARD1_READY_PIN, LOW);
  digitalWrite(CARD2_READY_PIN, LOW); 
  delay(102);
  digitalWrite(CARD1_READY_PIN, HIGH);
  digitalWrite(CARD2_READY_PIN, HIGH);

  
  

  pinMode(CARD1_MIDI_RX_PIN, OUTPUT);
  digitalWrite(CARD1_MIDI_RX_PIN, HIGH);  
  pinMode(CARD2_MIDI_RX_PIN, OUTPUT);
  digitalWrite(CARD1_MIDI_RX_PIN, HIGH);
  
  // Serial for communication with Behringer X32 MainControl
  Serial.println("Dual X-CARD Controller " + String(versionstring) + " | " + String(compile_date)); // send to USB
  Serial.println(F("Check bit-clock oscillator..."));
  checkBclk();
  Serial.println(F("Init UART-ports..."));
  SerialCard1.setTX(CARD1_UART_TX_PIN);
  SerialCard1.setRX(CARD1_UART_RX_PIN);
  SerialCard1.begin(38400); // X32 uses regular 38400 8N1 for communication
  SerialCard1.setTimeout(500); // Timeout for commands
  SerialCard2.begin(38400);
  SerialCard2.setTimeout(500); // Timeout for commands

  // initialize the X32-Cards
  Serial.println(F("Init X32-Cards..."));
  x32InitCard2();
  x32InitCard1();
  Serial.println(F("Ready."));
}

void loop() {
  handleUSBCommunication(); // communication through Serial (USB) with connected computer
  handleXCard1Communication();
  handleXCard2Communication();
  handleKeepAlive(); // *8S# once a second for cards that need it

  // heartbeat: toggle LED once per second
  if (millis() - lastHeartbeat >= 1000) {
    lastHeartbeat = millis();
    ledState = !ledState;
    //checkBclk();
    //statusLed.setPixelColor(0, ledState ? statusLed.Color(32, 0, 0) : 0);
    //statusLed.show();
  }
}
