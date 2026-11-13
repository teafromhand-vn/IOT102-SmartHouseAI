# IOT102-SmartHouseAI 🏠🤖

**Smart House AI 2.0** — Hệ thống nhà thông minh có trợ lý AI, là dự án môn học **IOT102 – Internet of Things** tại Trường Đại học FPT.

Hệ thống gồm 3 thành phần: **firmware** chạy trên ESP32-S3, **server** (Python/FastAPI) làm trung tâm điều khiển + lưu trữ, và **trợ lý AI** (Google Gemini) hiểu lệnh tiếng Việt để điều khiển thiết bị, kèm **giao diện web** giám sát.

> 📌 Repo nội bộ dành cho các thành viên nhóm cập nhật code. Không phải tài liệu nộp cho giảng viên.

---

## 🏗️ Kiến trúc hệ thống

```
┌──────────────────┐      HTTP (X-Device-Token)   ┌─────────────────────┐
│   Firmware        │ ◄─── Poll commands (2s) ───► │   FastAPI Server    │
│  SmartHouseAI2    │ ──── Push telemetry (5s) ───►│  ┌───────────────┐  │      ┌──────────────┐
│  SmartHouseCamera │                              │  │ SQLite DB     │  │ ◄──► │ Google       │
└──────────────────┘                               │  │ (telemetry,   │  │      │ Gemini AI    │
┌──────────────────┐                              │  │  events,      │  │      │ (tiếng Việt) │
│   Web UI          │ ◄──────────────────────────► │  │  commands)    │  │      └──────────────┘
│  (static/index)   │      HTTP                    │  └───────────────┘  │
└──────────────────┘                               └─────────────────────┘
```

**Luồng hoạt động:**
1. Firmware đẩy dữ liệu cảm biến (`telemetry`, mỗi 5s) và sự kiện (`events`) lên server.
2. Người dùng nói lệnh tiếng Việt qua Web UI → Gemini phân tích (function calling) → lệnh được **xếp vào hàng đợi** (`commands`, trạng thái `PENDING`).
3. Firmware poll hàng đợi (mỗi 2s), thực thi lệnh, xác nhận `EXECUTED`/`FAILED`.
4. AI chỉ xác nhận "đã xếp lệnh", không tự khẳng định thiết bị đã thực thi.

---

## ⚡ Tính năng chính

- 🗣️ **Trợ lý AI tiếng Việt**: ghi âm 3 giây bằng mic I2S (giữ nút BOOT để nói) → server gửi cho Gemini → điều khiển thiết bị
- 💡 **Điều khiển**: đèn chính, đèn đêm, quạt (relay) — on/off; rèm (servo) — open/close
- 📊 **Telemetry**: khí gas (ADC), lửa, mưa, trạng thái thiết bị, WiFi RSSI
- 🪪 **Access events**: quét thẻ RFID và vân tay ghi log sự kiện quan sát (không điều khiển cửa)
- 📷 **Camera module**: firmware riêng `SmartHouseCamera`
- 🔐 **Bảo mật**: token riêng thiết bị/admin, whitelist hành động, **khóa cửa bị vô hiệu hóa cố ý** (`ENABLE_DOOR_SERVO 0`), không lưu ảnh/âm thanh/sinh trắc vào log

---

## 🧩 Cấu trúc repo

```
├── firmware/
│   ├── SmartHouseAI2/
│   │   ├── SmartHouseAI2.ino   # Firmware chính
│   │   └── config.h            # WiFi, token, pin mapping, feature flags
│   └── SmartHouseCamera/
│       ├── SmartHouseCamera.ino
│       └── camera_config.h
├── server/
│   ├── main.py                 # Backend FastAPI + tích hợp Gemini
│   ├── requirements.txt
│   ├── .env.example            # Mẫu biến môi trường
│   └── static/index.html       # Giao diện web
└── README.md
```

---

## 🛠️ Phần cứng

| Linh kiện | Số lượng | Ghi chú |
|---|---|---|
| **ESP32-S3 DevKit N16R8** | 1 | Board chính (Schematic 2 — dùng GPIO numbers, không phải vị trí chân header) |
| Module camera ESP32 | 1 | Firmware riêng `SmartHouseCamera` |
| Cảm biến gas (analog) | 1 | `GPIO1` (ADC) |
| Cảm biến lửa (flame) | 1 | `GPIO12`, active-LOW |
| Cảm biến mưa (rain) | 1 | `GPIO2`, active-LOW |
| Reed switch cửa | 1 | `GPIO13` — đang tắt (`ENABLE_REED 0`, chưa wire) |
| **RC522 RFID** (SPI, 3.3V) | 1 | SS=15, RST=14, SCK=18, MISO=17, MOSI=16 |
| **AS608 vân tay** (UART) | 1 | Sensor TX → GPIO47 (RX), Sensor RX → GPIO42 (TX) |
| **INMP441 mic I2S** | 1 | BCLK=6, WS=7, SD=9, L/R→GND; press-to-talk trên nút BOOT (GPIO0) |
| Relay quạt | 1 | `GPIO21`, active-LOW |
| Servo rèm | 1 | `GPIO40` (open=120°, close=60°) |
| Servo cửa | 1 | `GPIO39` — **đang tắt** (`ENABLE_DOOR_SERVO 0`) |
| LED chính / LED đêm | 2 | `GPIO10` / `GPIO11` |
| Buzzer | 1 | `GPIO41` |

---

## 🖥️ Chạy server

**Yêu cầu:** Python 3.10+

```bash
git clone https://github.com/teafromhand-vn/IOT102-SmartHouseAI.git
cd IOT102-SmartHouseAI/server

pip install -r requirements.txt

# Tạo .env
cp .env.example .env
```

Nội dung `server/.env`:

```env
DEVICE_TOKEN=<token-thiết-bị>       # PHẢI KHÁC ADMIN_TOKEN (khớp DEVICE_TOKEN trong config.h)
ADMIN_TOKEN=<token-admin>
GEMINI_API_KEY=<key từ Google AI Studio>
GEMINI_MODEL=gemini-2.5-flash
```

```bash
uvicorn main:app --reload --host 0.0.0.0 --port 8000
```

- Web UI: `http://<IP-máy-tính>:8000`
- API docs: `http://<IP-máy-tính>:8000/docs`

> ⚠️ IP trong `config.h` là **IP LAN của máy tính chạy server** (ví dụ `192.168.x.x` / `172.x.x.x` khi dùng hotspot điện thoại), không phải `localhost`.

---

## 📟 Nạp firmware (ESP32-S3)

**Yêu cầu:** Arduino IDE + hỗ trợ ESP32 (Boards Manager: `esp32 by Espressif Systems`)

**1. Cài thư viện** (Library Manager):

| Thư viện | Dùng cho |
|---|---|
| `ArduinoJson` | Serialize/deserialize JSON |
| `ESP32Servo` | Servo rèm/cửa |
| `MFRC522` | Đọc thẻ RFID |
| `Adafruit Fingerprint` | Cảm biến vân tay AS608 |

(`WiFi`, `HTTPClient`, `SPI`, `driver/i2s` đã có sẵn trong core ESP32.)

**2. Cấu hình `firmware/SmartHouseAI2/config.h`:**

```cpp
// Điền trước khi nạp:
#define WIFI_SSID      "<tên-wifi>"
#define WIFI_PASSWORD  "<mật khẩu-wifi>"
#define SERVER_BASE    "http://<IP-máy-tính>:8000"   // IP LAN của máy chạy server
#define DEVICE_TOKEN   "<khớp DEVICE_TOKEN trong server/.env>"
```

**3. Nạp code:**
- Mở `firmware/SmartHouseAI2/SmartHouseAI2.ino`
- Chọn board: **ESP32S3 Dev Module** (Tools → Board)
- Chọn đúng port, nhấn **Upload**
- Nếu nạp lỗi, giữ nút **BOOT** khi màn hình "Connecting..."

**4. Kiểm tra:** mở Serial Monitor (115200) xem kết nối WiFi và log HTTP.

**5. Bật ngoại vi từ từ:** firmware dùng feature flags — bật từng cái sau khi đã kiểm tra chân và logic 3.3V:

```cpp
#define ENABLE_REED        0   // bật sau khi wire reed switch
#define ENABLE_DOOR_SERVO  0   // GIỮ 0 — không mở khóa cửa từ xa
#define ENABLE_RFID        1
#define ENABLE_FINGERPRINT 1
#define ENABLE_VOICE       1   // cần mic INMP441 đã wire đúng
```

> ⚠️ Đã verify trước khi cấp điện ngoài: relay/flame/rain đa số là **active-LOW**; calibrate góc servo vật lý trước khi gắn cơ khí.

---

## 🔌 API chính (tóm tắt)

| Endpoint | Mô tả | Auth |
|---|---|---|
| `POST /device/telemetry` | Firmware đẩy dữ liệu cảm biến | `X-Device-Token` |
| `POST /device/event` | Firmware ghi sự kiện (gas, flame, rain...) | `X-Device-Token` |
| `POST /device/access` | Sự kiện quét RFID / vân tay | `X-Device-Token` |
| `GET /device/commands` | Firmware poll lệnh PENDING | `X-Device-Token` |
| `POST /device/ack` | Firmware xác nhận EXECUTED/FAILED | `X-Device-Token` |
| Ask AI / home status | Lệnh giọng nói/văn bản, đọc trạng thái nhà | Admin |

Chi tiết đầy đủ tại `/docs`.

---

## 👥 Thành viên nhóm

| MSSV | Họ tên | Vai trò |
|---|---|---|
| <!-- TODO --> | <!-- TODO --> | <!-- TODO --> |
| <!-- TODO --> | <!-- TODO --> | |

---

## 📚 Tài liệu & Demo

- 📄 Báo cáo: <!-- TODO -->
- 🎬 Video demo: <!-- TODO -->
- 🖼️ Schematic mạch: <!-- TODO: ảnh/link -->

## 📄 License

<!-- TODO: chọn license (MIT/GPL-3.0...) -->
