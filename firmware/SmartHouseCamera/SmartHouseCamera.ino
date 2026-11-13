#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_camera.h>
#include "camera_config.h"
unsigned long lastCapture=0,lastWifi=0;
void setup(){
 Serial.begin(115200);
#if !CAMERA_PINS_VERIFIED
 Serial.println("CAMERA DISABLED: Verify specific camera board pinout; then set CAMERA_PINS_VERIFIED 1.");
#else
 camera_config_t c={};
 c.ledc_channel=LEDC_CHANNEL_0;c.ledc_timer=LEDC_TIMER_0;
 c.pin_d0=CAM_Y2;c.pin_d1=CAM_Y3;c.pin_d2=CAM_Y4;c.pin_d3=CAM_Y5;
 c.pin_d4=CAM_Y6;c.pin_d5=CAM_Y7;c.pin_d6=CAM_Y8;c.pin_d7=CAM_Y9;
 c.pin_xclk=CAM_XCLK;c.pin_pclk=CAM_PCLK;c.pin_vsync=CAM_VSYNC;
 c.pin_href=CAM_HREF;c.pin_sccb_sda=CAM_SIOD;c.pin_sccb_scl=CAM_SIOC;
 c.pin_pwdn=CAM_PWDN;c.pin_reset=CAM_RESET;
 c.xclk_freq_hz=20000000;c.pixel_format=PIXFORMAT_JPEG;
 c.frame_size=FRAMESIZE_QVGA;c.jpeg_quality=18;c.fb_count=1;
 if(esp_camera_init(&c)!=ESP_OK){Serial.println("Camera init failed: check selected board and pin map");return;}
 WiFi.mode(WIFI_STA);WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
#endif
}
void loop(){
#if CAMERA_PINS_VERIFIED
 if(WiFi.status()!=WL_CONNECTED){
  if(millis()-lastWifi>10000){lastWifi=millis();WiFi.reconnect();}
  delay(100);return;
 }
 if(millis()-lastCapture<CAPTURE_INTERVAL_MS)return;
 lastCapture=millis();
 camera_fb_t* fb=esp_camera_fb_get();if(!fb)return;
 if(fb->format!=PIXFORMAT_JPEG || fb->len>300000){esp_camera_fb_return(fb);return;}
 HTTPClient h;
 if(h.begin(String(SERVER_BASE)+"/device/face")){
  h.setTimeout(45000);h.addHeader("X-Device-Token",DEVICE_TOKEN);
  h.addHeader("Content-Type","image/jpeg");
  int code=h.POST(fb->buf,fb->len);
  Serial.printf("Camera POST response %d: %.200s\n",code,h.getString().c_str());h.end();
 }
 esp_camera_fb_return(fb);
#else
 delay(5000);
#endif
}
