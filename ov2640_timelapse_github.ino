/*************************************************************************************************************************************************
Very loosly based on this: https://www.bitsnblobs.com/time-lapse-camera-using-the-esp32-cam

Author name: Radu Privantu
License: Apache License 2.0

*************************************************************************************************************************************************/

  /********************************************************************************************************************
 *  Board Settings:
 *  Board: "ESP32 Wrover Module"
 *  Upload Speed: "921600"
 *  Flash Frequency: "80MHz"
 *  Flash Mode: "QIO"
 *  Partition Scheme: "Hue APP (3MB No OTA/1MB SPIFFS)"
 *  Core Debug Level: "None"
 *  COM Port: Depends *On Your System*
 *********************************************************************************************************************/

#include "esp_camera.h"
#include "FS.h"
#include "SPI.h"
#include "driver/i2c.h"
#include <SD.h>
#include "SD_MMC.h"
#include "driver/rtc_io.h"
#include <WiFi.h>

RTC_DATA_ATTR int cur_pic = 0;


int debug = 1;

// Select camera model
//#define CAMERA_MODEL_WROVER_KIT
//#define CAMERA_MODEL_ESP_EYE
//#define CAMERA_MODEL_M5STACK_PSRAM
//#define CAMERA_MODEL_M5STACK_WIDE
#define CAMERA_MODEL_AI_THINKER

#include "camera_pins.h"



#define TIME_TO_SLEEP  30            //time ESP32 will go to sleep (in seconds)
#define uS_TO_S_FACTOR 1000000ULL   //conversion factor for micro seconds to seconds */

uint16_t nextImageNumber = 0;
int max_retry_count=4;

static int skip_frame()
{

    int64_t st_t = esp_timer_get_time();
    while (digitalRead (VSYNC_GPIO_NUM) == 0) {
        if((esp_timer_get_time() - st_t) > 1000000LL){
            goto timeout;
        }
    }
    while (digitalRead (VSYNC_GPIO_NUM) != 0) {
        if((esp_timer_get_time() - st_t) > 1000000LL){
            goto timeout;
        }
    }
    while (digitalRead (VSYNC_GPIO_NUM) == 0) {
        if((esp_timer_get_time() - st_t) > 1000000LL){
            goto timeout;
        }
    }
    return 0;

timeout:
    ESP_LOGE(TAG, "Timeout waiting for VSYNC");
    return -1;
}


void setup() 
{
  //WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable brownout detector

  setCpuFrequencyMhz(240);
  
  Serial.begin(115200);
  Serial.println();
  Serial.println("Booting... at ");
  Serial.println(millis());

  pinMode(4, INPUT);              //GPIO for LED flash
  digitalWrite(4, LOW);
  rtc_gpio_hold_dis(GPIO_NUM_4);  //diable pin hold if it was enabled before sleeping
  
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz =   20000000;
  
  config.pixel_format = PIXFORMAT_JPEG;
  
  //init with high specs to pre-allocate larger buffers
  if(psramFound())
  {
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 3;
    config.fb_count = 1;
  } else 
  {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  //initialize camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) 
  {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }
 
camera_fb_t * fb = NULL;
sensor_t * s = esp_camera_sensor_get();
int light=0;
int day_switch_value=140;

  s->set_whitebal(s, 1);       // 0 = disable , 1 = enable
  s->set_awb_gain(s, 1);       // 0 = disable , 1 = enable
  s->set_wb_mode(s, 2);        // 0 to 4 - if awb_gain enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
//  s->set_exposure_ctrl(s, 1);  // 0 = disable , 1 = enable
 // s->set_aec2(s, 0);           // 0 = disable , 1 = enable
  //s->set_ae_level(s, 2);       // -2 to 2
  //s->set_aec_value(s, 1200);    // 0 to 1200
  s->set_gain_ctrl(s, 0);      // 0 = disable , 1 = enable
  s->set_agc_gain(s, 0);       // 0 to 30
  s->set_gainceiling(s, (gainceiling_t)6);  // 0 to 6
  s->set_bpc(s, 1);            // 0 = disable , 1 = enable
  s->set_wpc(s, 1);            // 0 = disable , 1 = enable
  s->set_raw_gma(s, 1);        // 0 = disable , 1 = enable
  s->set_lenc(s, 0);           // 0 = disable , 1 = enable
  s->set_hmirror(s, 0);        // 0 = disable , 1 = enable
  s->set_vflip(s, 0);          // 0 = disable , 1 = enable
  s->set_dcw(s, 0);            // 0 = disable , 1 = enable
  s->set_colorbar(s, 0);       // 0 = disable , 1 = enable    
 
    
   s->set_reg(s,0xff,0xff,0x01);//banksel    

   light=s->get_reg(s,0x2f,0xff);
   Serial.print("First light is ");
   Serial.println(light);
   Serial.print("Old 0x0 value is");   
   Serial.println(s->get_reg(s,0x0,0xff));

     //light=120+cur_pic*10;
     //light=0+cur_pic*5;

    if(light<day_switch_value)
    {
      //here we are in night mode
      if(light<45)s->set_reg(s,0x11,0xff,1);//frame rate (1 means longer exposure)
      s->set_reg(s,0x13,0xff,0);//manual everything
      s->set_reg(s,0x0c,0x6,0x8);//manual banding
           
      s->set_reg(s,0x45,0x3f,0x3f);//really long exposure (but it doesn't really work)
    }
    else
    {
      //here we are in daylight mode
      
      s->set_reg(s,0x2d,0xff,0x0);//extra lines
      s->set_reg(s,0x2e,0xff,0x0);//extra lines

      s->set_reg(s,0x47,0xff,0x0);//Frame Length Adjustment MSBs

    if(light<150)
    {
      s->set_reg(s,0x46,0xff,0xd0);//Frame Length Adjustment LSBs
      s->set_reg(s,0x2a,0xff,0xff);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0xff);//line adjust
      s->set_reg(s,0x45,0xff,0xff);//exposure (doesn't seem to work)
    }
    else if(light<160)
    {
      s->set_reg(s,0x46,0xff,0xc0);//Frame Length Adjustment LSBs
      s->set_reg(s,0x2a,0xff,0xb0);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0xff);//line adjust
      s->set_reg(s,0x45,0xff,0x10);//exposure (doesn't seem to work)
    }    
    else if(light<170)
    {
      s->set_reg(s,0x46,0xff,0xb0);//Frame Length Adjustment LSBs
      s->set_reg(s,0x2a,0xff,0x80);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0xff);//line adjust
      s->set_reg(s,0x45,0xff,0x10);//exposure (doesn't seem to work)
    }    
    else if(light<180)
    {
      s->set_reg(s,0x46,0xff,0xa8);//Frame Length Adjustment LSBs
      s->set_reg(s,0x2a,0xff,0x80);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0xff);//line adjust
      s->set_reg(s,0x45,0xff,0x10);//exposure (doesn't seem to work)
    } 
    else if(light<190)
    {
      s->set_reg(s,0x46,0xff,0xa6);//Frame Length Adjustment LSBs
      s->set_reg(s,0x2a,0xff,0x80);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0xff);//line adjust
      s->set_reg(s,0x45,0xff,0x90);//exposure (doesn't seem to work)
    } 
    else if(light<200)
    {
      s->set_reg(s,0x46,0xff,0xa4);//Frame Length Adjustment LSBs
      s->set_reg(s,0x2a,0xff,0x80);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0xff);//line adjust
      s->set_reg(s,0x45,0xff,0x10);//exposure (doesn't seem to work)
    } 
    else if(light<210)
    {
      s->set_reg(s,0x46,0xff,0x98);//Frame Length Adjustment LSBs
      s->set_reg(s,0x2a,0xff,0x60);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0xff);//line adjust
      s->set_reg(s,0x45,0xff,0x10);//exposure (doesn't seem to work)
    } 
    else if(light<220)
    {
      s->set_reg(s,0x46,0xff,0x80);//Frame Length Adjustment LSBs
      s->set_reg(s,0x2a,0xff,0x20);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0xff);//line adjust
      s->set_reg(s,0x45,0xff,0x10);//exposure (doesn't seem to work)
    } 
    else if(light<230)
    {
      s->set_reg(s,0x46,0xff,0x70);//Frame Length Adjustment LSBs
      s->set_reg(s,0x2a,0xff,0x20);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0xff);//line adjust
      s->set_reg(s,0x45,0xff,0x10);//exposure (doesn't seem to work)
    } 
    else if(light<240)
    {
      s->set_reg(s,0x46,0xff,0x60);//Frame Length Adjustment LSBs
      s->set_reg(s,0x2a,0xff,0x20);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0x80);//line adjust
      s->set_reg(s,0x45,0xff,0x10);//exposure (doesn't seem to work)
    } 
    else if(light<253)
    {
      s->set_reg(s,0x46,0xff,0x10);//Frame Length Adjustment LSBs
      s->set_reg(s,0x2a,0xff,0x0);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0x40);//line adjust
      s->set_reg(s,0x45,0xff,0x10);//exposure (doesn't seem to work)
    }
    else
    {
      s->set_reg(s,0x46,0xff,0x0);//Frame Length Adjustment LSBs
      s->set_reg(s,0x2a,0xff,0x0);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0x0);//line adjust
      s->set_reg(s,0x45,0xff,0x0);//exposure (doesn't seem to work)
      s->set_reg(s,0x10,0xff,0x0);//exposure (doesn't seem to work)
    }
                                        
    s->set_reg(s,0x0f,0xff,0x4b);//no idea
    s->set_reg(s,0x03,0xff,0xcf);//no idea
    s->set_reg(s,0x3d,0xff,0x34);//changes the exposure somehow, has to do with frame rate

    s->set_reg(s,0x11,0xff,0x0);//frame rate
    s->set_reg(s,0x43,0xff,0x11);//11 is the default value     
    }
    
   Serial.println("Getting first frame at");
   Serial.println(millis());    
    fb = esp_camera_fb_get();
    //skip_frame();
   Serial.println("Got first frame at");
   Serial.println(millis());    

    if(light==0)
    {
      s->set_reg(s,0x47,0xff,0x40);//Frame Length Adjustment MSBs
      s->set_reg(s,0x2a,0xf0,0xf0);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0xff);//line adjust
    }
    else if(light==1)
    {
      s->set_reg(s,0x47,0xff,0x40);//Frame Length Adjustment MSBs
      s->set_reg(s,0x2a,0xf0,0xd0);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0xff);//line adjust
    }
    else if(light==2)
    {
      s->set_reg(s,0x47,0xff,0x40);//Frame Length Adjustment MSBs
      s->set_reg(s,0x2a,0xf0,0xb0);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0xff);//line adjust  
    }
    else if(light==3)
    {
      s->set_reg(s,0x47,0xff,0x40);//Frame Length Adjustment MSBs
      s->set_reg(s,0x2a,0xf0,0x70);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0xff);//line adjust      
    }    
    else if(light==4)
    {
      s->set_reg(s,0x47,0xff,0x40);//Frame Length Adjustment MSBs
      s->set_reg(s,0x2a,0xf0,0x40);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0xff);//line adjust
    }
    else if(light==5)
    {
      s->set_reg(s,0x47,0xff,0x20);//Frame Length Adjustment MSBs
      s->set_reg(s,0x2a,0xf0,0x80);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0xff);//line adjust        
    }
    else if(light==6)
    {
      s->set_reg(s,0x47,0xff,0x20);//Frame Length Adjustment MSBs
      s->set_reg(s,0x2a,0xf0,0x40);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0xff);//line adjust        
    }         
    else if(light==7)
    {
      s->set_reg(s,0x47,0xff,0x20);//Frame Length Adjustment MSBs
      s->set_reg(s,0x2a,0xf0,0x30);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0xff);//line adjust        
    }
    else if(light==8)
    {
      s->set_reg(s,0x47,0xff,0x20);//Frame Length Adjustment MSBs
      s->set_reg(s,0x2a,0xf0,0x20);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0xff);//line adjust        
    }     
    else if(light==9)
    {
      s->set_reg(s,0x47,0xff,0x20);//Frame Length Adjustment MSBs
      s->set_reg(s,0x2a,0xf0,0x10);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0xff);//line adjust        
    }    
    else if(light==10)
    {
      s->set_reg(s,0x47,0xff,0x10);//Frame Length Adjustment MSBs
      s->set_reg(s,0x2a,0xf0,0x70);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0xff);//line adjust        
    }
    else if(light<=12)
    {
      s->set_reg(s,0x47,0xff,0x10);//Frame Length Adjustment MSBs
      s->set_reg(s,0x2a,0xf0,0x60);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0xff);//line adjust        
    }
    else if(light<=14)
    {
      s->set_reg(s,0x47,0xff,0x10);//Frame Length Adjustment MSBs
      s->set_reg(s,0x2a,0xf0,0x40);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0xff);//line adjust        
    }          
    else if(light<=18)
    {
      s->set_reg(s,0x47,0xff,0x08);//Frame Length Adjustment MSBs
      s->set_reg(s,0x2a,0xf0,0xb0);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0xff);//line adjust        
    }
    else if(light<=20)
    {
      s->set_reg(s,0x47,0xff,0x08);//Frame Length Adjustment MSBs
      s->set_reg(s,0x2a,0xf0,0x80);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0xff);//line adjust        
    }      
    else if(light<=23)
    {
      s->set_reg(s,0x47,0xff,0x08);//Frame Length Adjustment MSBs
      s->set_reg(s,0x2a,0xf0,0x60);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0xff);//line adjust        
    }           
    else if(light<=27)
    {
      s->set_reg(s,0x47,0xff,0x04);//Frame Length Adjustment MSBs
      s->set_reg(s,0x2a,0xf0,0xd0);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0xff);//line adjust        
    }
    else if(light<=31)
    {
      s->set_reg(s,0x47,0xff,0x04);//Frame Length Adjustment MSBs
      s->set_reg(s,0x2a,0xf0,0x80);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0xff);//line adjust        
    }     
    else if(light<=35)
    {
      s->set_reg(s,0x47,0xff,0x04);//Frame Length Adjustment MSBs
      s->set_reg(s,0x2a,0xf0,0x60);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0xff);//line adjust        
    }    
    else if(light<=40)
    {
      s->set_reg(s,0x47,0xff,0x02);//Frame Length Adjustment MSBs
      s->set_reg(s,0x2a,0xf0,0x70);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0xff);//line adjust        
    }
    else if(light<45)
    {
      s->set_reg(s,0x47,0xff,0x02);//Frame Length Adjustment MSBs
      s->set_reg(s,0x2a,0xf0,0x40);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0xff);//line adjust        
    }
    //after this the frame rate is higher, so we need to compensate
    else if(light<50)
    {
      s->set_reg(s,0x47,0xff,0x04);//Frame Length Adjustment MSBs
      s->set_reg(s,0x2a,0xf0,0xa0);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0xff);//line adjust        
    }    
    else if(light<55)
    {
      s->set_reg(s,0x47,0xff,0x04);//Frame Length Adjustment MSBs
      s->set_reg(s,0x2a,0xf0,0x70);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0xff);//line adjust        
    }
    else if(light<65)
    {
      s->set_reg(s,0x47,0xff,0x04);//Frame Length Adjustment MSBs
      s->set_reg(s,0x2a,0xf0,0x30);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0xff);//line adjust        
    }
    else if(light<75)
    {
      s->set_reg(s,0x47,0xff,0x02);//Frame Length Adjustment MSBs
      s->set_reg(s,0x2a,0xf0,0x80);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0xff);//line adjust        
    }                 
    else if(light<90)
    {
      s->set_reg(s,0x47,0xff,0x02);//Frame Length Adjustment MSBs
      s->set_reg(s,0x2a,0xf0,0x50);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0xbf);//line adjust        
    }
    else if(light<100)
    {
      s->set_reg(s,0x47,0xff,0x02);//Frame Length Adjustment MSBs
      s->set_reg(s,0x2a,0xf0,0x20);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0x8f);//line adjust        
    } 
    else if(light<110)
    {
      s->set_reg(s,0x47,0xff,0x02);//Frame Length Adjustment MSBs
      s->set_reg(s,0x2a,0xf0,0x10);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0x7f);//line adjust        
    }      
    else if(light<120)
    {
      s->set_reg(s,0x47,0xff,0x01);//Frame Length Adjustment MSBs
      s->set_reg(s,0x2a,0xf0,0x10);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0x5f);//line adjust        
    }     
    else if(light<130)
    {
      s->set_reg(s,0x47,0xff,0x00);//Frame Length Adjustment MSBs
      s->set_reg(s,0x2a,0xf0,0x0);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0x2f);//line adjust        
    }
    else if(light<140)
    {
      s->set_reg(s,0x47,0xff,0x00);//Frame Length Adjustment MSBs
      s->set_reg(s,0x2a,0xf0,0x0);//line adjust MSB
      s->set_reg(s,0x2b,0xff,0x0);//line adjust        
    }
                   
    if(light<day_switch_value)s->set_reg(s,0x43,0xff,0x40);//magic value to give us the frame faster (bit 6 must be 1)

    //fb = esp_camera_fb_get();
   
    s->set_reg(s,0xff,0xff,0x00);//banksel 
    s->set_reg(s,0xd3,0xff,0x8);//clock
    
    s->set_reg(s,0x42,0xff,0x2f);//image quality (lower is bad)
    s->set_reg(s,0x44,0xff,3);//quality
    
    //s->set_reg(s,0x96,0xff,0x10);//bit 4, disable saturation


    //s->set_reg(s,0xbc,0xff,0xff);//red channel adjustment, 0-0xff (the higher the brighter)
    //s->set_reg(s,0xbd,0xff,0xff);//green channel adjustment, 0-0xff (the higher the brighter)
    //s->set_reg(s,0xbe,0xff,0xff);//blue channel adjustment, 0-0xff (the higher the brighter)
    
    //s->set_reg(s,0xbf,0xff,128);//if the last bit is not set, the image is dim. All other bits don't seem to do anything but ocasionally crash the camera

    //s->set_reg(s,0xa5,0xff,0);//contrast 0 is none, 0xff is very high. Not really useful over 20 or so at most.

    //s->set_reg(s,0x8e,0xff,0x30);//bits 5 and 4, if set make the image darker, not very useful
    //s->set_reg(s,0x91,0xff,0x67);//really weird stuff in the last 4 bits, can also crash the camera           

    //no sharpening
    s->set_reg(s,0x92,0xff,0x1);
    s->set_reg(s,0x93,0xff,0x0);  

  //initialize & mount SD card
  if(!SD_MMC.begin())
  {
    Serial.println("Card Mount Failed");
    return;
  }
  
  uint8_t cardType = SD_MMC.cardType();

  if(cardType == CARD_NONE)
  {
    Serial.println("No SD card attached");
    return;
  }
  
   if(fb)esp_camera_fb_return(fb);

  fb = esp_camera_fb_get();


  int retries=0;
  if(!fb)
  while(1)
  {
    Serial.println("Not having image yet, waiting a bit");
    //delay(500);
    fb = esp_camera_fb_get();
    if(fb)break;

    retries++;
    if(retries>max_retry_count)break;
  }
/*
  Serial.print("Wifi signal is: ");
  Serial.println(WiFi.RSSI());
*/

//since we got the frame buffer, we reset the sensor and put it to sleep while saving the file
    s->set_reg(s,0xff,0xff,0x01);//banksel
    s->set_reg(s,0x12,0xff,0x80);//reset (we do this to clear the sensor registries, it seems to get more consistent images this way)
    delay(1);
    s->set_reg(s,0x09,0x10,0x10);//stand by

  char path[128];
  if(debug)sprintf(path,"/%05i_%i_%i.jpg",cur_pic,millis(),light);
  else sprintf(path,"/%05i.jpg",cur_pic);

  fs::FS &fs = SD_MMC;

  //create new file
  File file = fs.open(path, FILE_WRITE);
  if(!file)
  {
    Serial.println("Failed to create file");
    Serial.println("Exiting now"); 
    while(1);   //wait here as something is not right    
  }
  if(fb)file.write(fb->buf, fb->len);  
  file.close();
  Serial.print("Saving first file as ");
  Serial.println(path);

  cur_pic++;
   

  pinMode(4, OUTPUT);              //GPIO for LED flash
  digitalWrite(4, LOW);            //turn OFF flash LED
  rtc_gpio_hold_en(GPIO_NUM_4);    //make sure flash is held LOW in sleep  
  
  Serial.println("Entering deep sleep mode at ");
  Serial.println(millis());
  Serial.flush(); 
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  esp_deep_sleep_start();
}

void loop() 
{


}
