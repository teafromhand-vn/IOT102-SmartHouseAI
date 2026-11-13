
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include "config.h"

#if ENABLE_RFID
#include <SPI.h>
#include <MFRC522.h>
MFRC522 rfid(RFID_SS, RFID_RST);
#endif

#if ENABLE_FINGERPRINT
#include <Adafruit_Fingerprint.h>
HardwareSerial fingerUart(1);
Adafruit_Fingerprint finger(&fingerUart);
bool fingerReady = false;
#endif

#if ENABLE_VOICE
#include <driver/i2s.h>
bool voiceReady = false;
uint8_t* waveData = nullptr;
const size_t WAV_DATA_BYTES =
    VOICE_SAMPLE_RATE * VOICE_SECONDS * 2;
#endif

Servo curtainServo;

#if ENABLE_DOOR_SERVO
Servo doorServo;
#endif

bool mainLight = false;
bool nightLight = false;
bool fanOn = false;
bool curtainOpen = false;

int gasAdc = 0;
bool flameDetected = false;
bool rainDetected = false;

#if ENABLE_REED
bool doorClosed = false;
#endif

unsigned long lastTelemetry = 0;
unsigned long lastPoll = 0;
unsigned long lastWifiAttempt = 0;

// ===============================
// HTTP COMMUNICATION
// ===============================

bool httpJson(
    const String& path,
    const String& method,
    const String& payload,
    String& out
) {
    if (WiFi.status() != WL_CONNECTED) return false;

    HTTPClient http;
    String url = String(SERVER_BASE) + path;

    if (!http.begin(url)) return false;

    http.setTimeout(10000);
    http.addHeader("X-Device-Token", DEVICE_TOKEN);
    http.addHeader("Content-Type", "application/json");

    int code = (method == "GET")
        ? http.GET()
        : http.POST(payload);

    if (code > 0) {
        out = http.getString();
    } else {
        Serial.printf("HTTP failure: %d\n", code);
    }

    http.end();

    if (code < 200 || code >= 300) {
        Serial.printf(
            "Server status: %d path=%s\n",
            code,
            path.c_str()
        );
        return false;
    }

    return true;
}

// ===============================
// DEVICE CONTROL
// ===============================

void setFan(bool on) {
    fanOn = on;
    digitalWrite(
        PIN_RELAY_FAN,
        (on != bool(RELAY_ACTIVE_LOW)) ? HIGH : LOW
    );
}

void applyOutputs() {
    digitalWrite(
        PIN_LED_MAIN,
        mainLight ? HIGH : LOW
    );

    digitalWrite(
        PIN_LED_NIGHT,
        nightLight ? HIGH : LOW
    );
}

void readSensors() {
    gasAdc = analogRead(PIN_GAS_ANALOG);

    flameDetected = (
        digitalRead(PIN_FLAME) ==
        (FLAME_ACTIVE_LOW ? LOW : HIGH)
    );

    rainDetected = (
        digitalRead(PIN_RAIN) ==
        (RAIN_ACTIVE_LOW ? LOW : HIGH)
    );

#if ENABLE_REED
    doorClosed = (digitalRead(PIN_REED) == LOW);
#endif
}

// ===============================
// EVENTS AND TELEMETRY
// ===============================

void postEvent(const char* type) {
    JsonDocument d;

    d["type"] = type;
    d["detail"]["gas_raw_adc"] = gasAdc;
    d["detail"]["flame_detected"] = flameDetected;
    d["detail"]["rain_detected"] = rainDetected;

    String s, r;
    serializeJson(d, s);

    httpJson("/device/event", "POST", s, r);
}

void telemetry() {
    JsonDocument d;

    d["gas_raw_adc"] = gasAdc;
    d["flame_detected"] = flameDetected;
    d["rain_detected"] = rainDetected;

#if ENABLE_REED
    d["door_closed"] = doorClosed;
#endif

    d["light"] = mainLight;
    d["night_light"] = nightLight;
    d["fan"] = fanOn;
    d["curtain"] = curtainOpen ? "open" : "close";
    d["wifi_rssi_dbm"] = WiFi.RSSI();

    String s, r;
    serializeJson(d, s);

    httpJson("/device/telemetry", "POST", s, r);
}

bool execute(
    const String& device,
    const String& action
) {
    if (
        device == "light" &&
        (action == "on" || action == "off")
    ) {
        mainLight = (action == "on");
        applyOutputs();
        return true;
    }

    if (
        device == "night_light" &&
        (action == "on" || action == "off")
    ) {
        nightLight = (action == "on");
        applyOutputs();
        return true;
    }

    if (
        device == "fan" &&
        (action == "on" || action == "off")
    ) {
        setFan(action == "on");
        return true;
    }

    if (
        device == "curtain" &&
        (action == "open" || action == "close")
    ) {
        curtainOpen = (action == "open");

        curtainServo.write(
            curtainOpen
                ? CURTAIN_OPEN_ANGLE
                : CURTAIN_CLOSED_ANGLE
        );

        return true;
    }

    return false;
}

void pollCommands() {
    String raw;

    if (!httpJson(
        "/device/commands",
        "GET",
        "",
        raw
    )) return;

    JsonDocument doc;

    if (deserializeJson(doc, raw)) {
        Serial.println("JSON parse failed");
        return;
    }

    JsonArray cmds = doc["commands"].as<JsonArray>();

    for (JsonObject item : cmds) {
        long id = item["id"] | 0L;
        String device = item["device"] | "";
        String action = item["action"] | "";

        if (id <= 0) continue;

        bool success = execute(device, action);

        JsonDocument ack;

        ack["id"] = id;
        ack["status"] = success
            ? "EXECUTED"
            : "FAILED";

        String body, reply;
        serializeJson(ack, body);

        if (!httpJson(
            "/device/ack",
            "POST",
            body,
            reply
        )) {
            Serial.println(
                "ACK failed; command may be repeated"
            );
        }
    }
}

// ===============================
// RFID AND FINGERPRINT
// ===============================

void sendAccess(
    const char* type,
    bool matched,
    const char* reader
) {
    JsonDocument d;

    d["type"] = type;
    d["detail"]["matched"] = matched;
    d["detail"]["reader"] = reader;

    String body, out;
    serializeJson(d, body);

    httpJson(
        "/device/access",
        "POST",
        body,
        out
    );
}

#if ENABLE_RFID
void pollRfid() {
    if (
        !rfid.PICC_IsNewCardPresent() ||
        !rfid.PICC_ReadCardSerial()
    ) return;

    Serial.println(
        "RFID tag detected (UID intentionally not logged)"
    );

    sendAccess("rfid_seen", false, "rc522");

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
}
#endif

#if ENABLE_FINGERPRINT
void pollFingerprint() {
    if (!fingerReady) return;

    uint8_t result = finger.getImage();

    if (result == FINGERPRINT_NOFINGER) return;
    if (result != FINGERPRINT_OK) return;

    result = finger.image2Tz();

    if (result != FINGERPRINT_OK) return;

    result = finger.fingerFastSearch();

    if (result == FINGERPRINT_OK) {
        Serial.println(
            "Fingerprint recognized; no door movement"
        );

        sendAccess(
            "fingerprint_seen",
            true,
            "as608"
        );

    } else if (result == FINGERPRINT_NOTFOUND) {
        sendAccess(
            "fingerprint_unknown",
            false,
            "as608"
        );
    }

    delay(900);
}
#endif

// ===============================
// VOICE - INMP441
// ===============================

#if ENABLE_VOICE

static void writeU16(uint8_t* p, uint16_t v) {
    p[0] = v & 255;
    p[1] = v >> 8;
}

static void writeU32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; i++) {
        p[i] = (v >> (8 * i)) & 255;
    }
}

void initVoice() {
    Serial.printf(
        "PSRAM size: %u bytes\n",
        ESP.getPsramSize()
    );

    Serial.printf(
        "Free PSRAM: %u bytes\n",
        ESP.getFreePsram()
    );

    pinMode(PTT_PIN, INPUT_PULLUP);

    i2s_config_t cfg = {};

    cfg.mode = (i2s_mode_t)(
        I2S_MODE_MASTER | I2S_MODE_RX
    );

    cfg.sample_rate = VOICE_SAMPLE_RATE;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format =
        I2S_COMM_FORMAT_STAND_I2S;

    cfg.intr_alloc_flags = 0;
    cfg.dma_buf_count = 4;
    cfg.dma_buf_len = 256;

    i2s_pin_config_t pin = {};

    pin.bck_io_num = MIC_BCLK;
    pin.ws_io_num = MIC_WS;
    pin.data_out_num = I2S_PIN_NO_CHANGE;
    pin.data_in_num = MIC_SD;

    esp_err_t err = i2s_driver_install(
        I2S_NUM_0,
        &cfg,
        0,
        nullptr
    );

    Serial.printf(
        "I2S driver install: %d\n",
        err
    );

    if (err != ESP_OK) return;

    err = i2s_set_pin(I2S_NUM_0, &pin);

    Serial.printf(
        "I2S pin setup: %d\n",
        err
    );

    if (err != ESP_OK) {
        i2s_driver_uninstall(I2S_NUM_0);
        return;
    }

    waveData = (uint8_t*)ps_malloc(
        WAV_DATA_BYTES + 44
    );

    if (!waveData) {
        Serial.println(
            "Voice buffer allocation failed; enable PSRAM"
        );
        return;
    }

    voiceReady = true;
}

// ===============================
// RECORD AND SEND VOICE
// ===============================

void recordAndSendVoice() {
    if (!voiceReady) {
        Serial.println(
            "Voice not ready"
        );
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(
            "Voice failed: WiFi disconnected"
        );
        return;
    }

    Serial.println(
        "Recording voice for 3 seconds..."
    );

    uint8_t* b = waveData;

    memcpy(b, "RIFF", 4);
    writeU32(b + 4, WAV_DATA_BYTES + 36);
    memcpy(b + 8, "WAVEfmt ", 8);

    writeU32(b + 16, 16);
    writeU16(b + 20, 1);
    writeU16(b + 22, 1);

    writeU32(b + 24, VOICE_SAMPLE_RATE);
    writeU32(b + 28, VOICE_SAMPLE_RATE * 2);

    writeU16(b + 32, 2);
    writeU16(b + 34, 16);

    memcpy(b + 36, "data", 4);
    writeU32(b + 40, WAV_DATA_BYTES);

    int32_t samples[256];
    size_t collected = 0;

    while (collected < WAV_DATA_BYTES) {
        size_t n = 0;

        esp_err_t result = i2s_read(
            I2S_NUM_0,
            samples,
            sizeof(samples),
            &n,
            pdMS_TO_TICKS(250)
        );

        if (result != ESP_OK) {
            Serial.printf(
                "I2S read error: %d\n",
                result
            );
            break;
        }

        size_t count = n / 4;

        for (
            size_t k = 0;
            k < count && collected < WAV_DATA_BYTES;
            k++
        ) {
            int32_t val = samples[k] >> 14;

            if (val > 32767) val = 32767;
            if (val < -32768) val = -32768;

            b[44 + collected++] =
                (uint8_t)(val & 255);

            b[44 + collected++] =
                (uint8_t)((val >> 8) & 255);
        }

        yield();
    }

    if (collected == 0) {
        Serial.println(
            "Voice failed: no audio collected"
        );
        return;
    }

    Serial.printf(
        "Voice bytes collected: %u\n",
        (unsigned int)collected
    );

    writeU32(
        b + 4,
        (uint32_t)collected + 36
    );

    writeU32(
        b + 40,
        (uint32_t)collected
    );

    HTTPClient http;

    if (!http.begin(
        String(SERVER_BASE) + "/device/voice"
    )) {
        Serial.println(
            "Voice HTTP begin failed"
        );
        return;
    }

    http.setTimeout(45000);

    http.addHeader(
        "X-Device-Token",
        DEVICE_TOKEN
    );

    http.addHeader(
        "Content-Type",
        "audio/wav"
    );

    int code = http.POST(
        b,
        44 + collected
    );

    String response = http.getString();

    http.end();

    Serial.printf(
        "Voice HTTP %d response: %.240s\n",
        code,
        response.c_str()
    );
}
#endif

// ===============================
// SETUP
// ===============================

void setup() {
    Serial.begin(115200);

    pinMode(PIN_LED_MAIN, OUTPUT);
    pinMode(PIN_LED_NIGHT, OUTPUT);

    digitalWrite(
        PIN_RELAY_FAN,
        RELAY_ACTIVE_LOW ? HIGH : LOW
    );

    pinMode(PIN_RELAY_FAN, OUTPUT);

    pinMode(PIN_FLAME, INPUT);
    pinMode(PIN_RAIN, INPUT);

#if ENABLE_REED
    pinMode(PIN_REED, INPUT_PULLUP);
#endif

    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);

    applyOutputs();
    setFan(false);

    analogReadResolution(12);

    curtainServo.setPeriodHertz(50);
    curtainServo.attach(
        PIN_SERVO_CURTAIN,
        500,
        2400
    );

    curtainServo.write(
        CURTAIN_CLOSED_ANGLE
    );

#if ENABLE_DOOR_SERVO
    doorServo.setPeriodHertz(50);

    doorServo.attach(
        PIN_SERVO_DOOR,
        500,
        2400
    );

    doorServo.write(
        DOOR_CLOSED_ANGLE
    );
#endif

#if ENABLE_RFID
    SPI.begin(
        RFID_SCK,
        RFID_MISO,
        RFID_MOSI,
        RFID_SS
    );

    rfid.PCD_Init();
    Serial.println("RC522 initialized");
#endif

#if ENABLE_FINGERPRINT
    fingerUart.begin(
        57600,
        SERIAL_8N1,
        FINGERPRINT_RX,
        FINGERPRINT_TX
    );

    finger.begin(57600);

    fingerReady = finger.verifyPassword();

    Serial.printf(
        "AS608 ready=%d\n",
        fingerReady
    );
#endif

#if ENABLE_VOICE
    initVoice();

    Serial.printf(
        "I2S microphone ready=%d\n",
        voiceReady
    );
#endif

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.println(
        "Smart House started. Waiting for WiFi..."
    );
}

// ===============================
// MAIN LOOP
// ===============================

void loop() {
    unsigned long ms = millis();

    // WiFi reconnect
    if (
        WiFi.status() != WL_CONNECTED &&
        ms - lastWifiAttempt > 10000
    ) {
        lastWifiAttempt = ms;

        WiFi.disconnect();
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

        Serial.println("WiFi retry");
    }

#if ENABLE_RFID
    pollRfid();
#endif

#if ENABLE_FINGERPRINT
    static unsigned long lastFinger = 0;

    if (ms - lastFinger > 500) {
        lastFinger = ms;
        pollFingerprint();
    }
#endif

    // ===========================
    // BOOT BUTTON - VOICE CONTROL
    // ===========================

#if ENABLE_VOICE
    static bool wasPressed = false;

    bool pressed = (
        digitalRead(PTT_PIN) == LOW
    );

    if (pressed != wasPressed) {
        Serial.printf(
            "BOOT GPIO%d = %d, pressed = %d\n",
            PTT_PIN,
            digitalRead(PTT_PIN),
            pressed
        );
    }

    if (pressed && !wasPressed) {
        Serial.println("BOOT detected!");

        Serial.printf(
            "voiceReady=%d WiFi=%d\n",
            voiceReady,
            WiFi.status()
        );

        recordAndSendVoice();
    }

    wasPressed = pressed;
#endif

    // ===========================
    // SENSORS
    // ===========================

    readSensors();

    static bool priorFlame = false;
    static bool priorRain = false;

    if (flameDetected && !priorFlame) {
        postEvent("flame_detected");
        digitalWrite(PIN_BUZZER, HIGH);
    }

    if (!flameDetected && priorFlame) {
        digitalWrite(PIN_BUZZER, LOW);
    }

    if (rainDetected && !priorRain) {
        postEvent("rain_detected");
    }

    priorFlame = flameDetected;
    priorRain = rainDetected;

    // ===========================
    // TELEMETRY
    // ===========================

    if (
        ms - lastTelemetry >=
        TELEMETRY_INTERVAL_MS
    ) {
        lastTelemetry = ms;

        telemetry();

        Serial.printf(
            "ADC gas=%d flame=%d rain=%d WiFi=%d\n",
            gasAdc,
            flameDetected,
            rainDetected,
            WiFi.status()
        );
    }

    // ===========================
    // POLL COMMANDS
    // ===========================

    if (
        ms - lastPoll >=
        POLL_INTERVAL_MS
    ) {
        lastPoll = ms;
        pollCommands();
    }

    delay(60);
}
