#pragma once
// Fill in before uploading. Server IP is the COMPUTER LAN IP, not localhost.
#define WIFI_SSID "Viết Thịnh"
#define WIFI_PASSWORD "123456789"
#define SERVER_BASE "http://172.20.10.2:8000"
#define DEVICE_TOKEN "ESP32_SmartHome"

// Hardware settings: verify board labels and wiring BEFORE applying external power.
// Schematic 2: ESP32-S3 DevKit N16R8. GPIO numbers, NOT header pin positions.
#define PIN_LED_MAIN 10
#define PIN_LED_NIGHT 11
#define PIN_RELAY_FAN 21
#define PIN_SERVO_DOOR 39
#define PIN_SERVO_CURTAIN 40
#define PIN_BUZZER 41
#define PIN_GAS_ANALOG 1
#define PIN_FLAME 12
#define PIN_RAIN 2
// Reed switch pin is not clearly assigned in diagram. Disable until wired and verified.
#define ENABLE_REED 0
#define PIN_REED 13

// Many relay modules are active-LOW; change only after checking module behavior.
#define RELAY_ACTIVE_LOW 1
// Many Flame/Rain DO module outputs are active-LOW; confirm with Serial monitor.
#define FLAME_ACTIVE_LOW 1
#define RAIN_ACTIVE_LOW 1
// Set servo pulse range/angles by calibrating physically before attaching to a lock.
#define DOOR_CLOSED_ANGLE 90
#define CURTAIN_OPEN_ANGLE 120
#define CURTAIN_CLOSED_ANGLE 60
#define ENABLE_DOOR_SERVO 0 // Door actuator disabled: no remote unlocking in starter project.
#define TELEMETRY_INTERVAL_MS 5000UL
#define POLL_INTERVAL_MS 2000UL

// Peripheral switches: turn on individually after verifying pinout and 3.3V logic.
#define ENABLE_RFID 1
#define ENABLE_FINGERPRINT 1
#define ENABLE_VOICE 1
// RC522 SPI on schematic 2 (3.3V ONLY)
#define RFID_SS 15
#define RFID_RST 14
#define RFID_SCK 18
#define RFID_MISO 17
#define RFID_MOSI 16
// AS608: sensor TX -> ESP RX GPIO47, sensor RX -> ESP TX GPIO42.
#define FINGERPRINT_RX 47
#define FINGERPRINT_TX 42
// INMP441 I2S microphone (3.3V). L/R -> GND. Press-to-talk on BOOT GPIO0.
#define MIC_BCLK 6
#define MIC_WS 7
#define MIC_SD 9
#define PTT_PIN 0
#define VOICE_SAMPLE_RATE 16000
#define VOICE_SECONDS 3
// RFID/fingerprint are observation events only; physical door servo stays disabled.
