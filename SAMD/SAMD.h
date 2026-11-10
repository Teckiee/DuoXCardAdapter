const char* versionstring = "v1.0.0-rp2040";
const char compile_date[] = __DATE__ " " __TIME__;

//#include <Adafruit_NeoPixel.h>

// Pin assignment of the custom RP2040-Zero PCB
#define CARD2_UART_RX_PIN    2   // PIO-UART RX <- TxD of X-CARD #2 (X-WSG)
#define CARD2_UART_TX_PIN    3   // PIO-UART TX -> RxD of X-CARD #2 (X-WSG)
#define CARD1_UART_TX_PIN    4   // UART1 TX -> RxD of X-CARD #1 (X-DANTE)
#define CARD1_UART_RX_PIN    5   // UART1 RX <- TxD of X-CARD #1 (X-DANTE)
#define CARD1_MIDI_RX_PIN    14
#define CARD1_MIDI_TX_PIN    15
#define CARD2_MIDI_RX_PIN    26
#define CARD2_MIDI_TX_PIN    27
#define TDM_BCLK_PIN         6   // 12.288 MHz bit-clock from oscillator (input)
#define TDM_FSYNC_PIN        7   // 48 kHz frame-sync (output)
#define CARD2_READY_PIN      10  // CardReady-signal of X-CARD #2
#define CARD1_READY_PIN      11  // CardReady-signal of X-CARD #1
#define STATUS_LED_PIN       16  // WS2812 RGB-LED on RP2040-Zero

// GP2/GP3 are no hardware-UART TX/RX pins, so card #2 uses a PIO-based UART
#define SerialCard1 Serial2 // hardware UART1
SerialPIO SerialCard2(CARD2_UART_TX_PIN, CARD2_UART_RX_PIN);
