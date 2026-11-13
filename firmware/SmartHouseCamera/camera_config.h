#pragma once
#define WIFI_SSID "YOUR_WIFI_NAME"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define SERVER_BASE "http://192.168.1.100:8000"
#define DEVICE_TOKEN "REPLACE_WITH_LONG_RANDOM_DEVICE_TOKEN"
// CAMERA MODEL MUST MATCH YOUR CAMERA BOARD. This preset is ONLY for ESP32-S3-EYE.
// Set to 1 ONLY after verifying board model and exact camera pinout.
#define CAMERA_PINS_VERIFIED 0
#define CAM_PWDN -1
#define CAM_RESET -1
#define CAM_XCLK 15
#define CAM_SIOD 4
#define CAM_SIOC 5
#define CAM_Y9 16
#define CAM_Y8 17
#define CAM_Y7 18
#define CAM_Y6 12
#define CAM_Y5 10
#define CAM_Y4 8
#define CAM_Y3 9
#define CAM_Y2 11
#define CAM_VSYNC 6
#define CAM_HREF 7
#define CAM_PCLK 13
#define CAPTURE_INTERVAL_MS 30000UL
