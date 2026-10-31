#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

// ---------------------------------------------------------------------------
// Freenove ESP32-S3 Display 2.8 inch LCD (FNK0104B)
//
// Based on official Freenove repository:
// https://github.com/Freenove/Freenove_ESP32_S3_Display
// Pinout from TFT_eSPI_Setups/FNK0104AB_2.8_240x320_ILI9341.h
//
// Display : ILI9341 240x320 over 4-line SPI (HSPI/SPI2)
// Touch   : FT6336U (FocalTech) capacitive, I2C address 0x38
// Audio   : ES8311 I2S codec (speaker + microphone)
// ---------------------------------------------------------------------------

#define BOARD_NAME "freenove-esp32-s3-lcd-2.8"

#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

// I2S audio (ES8311 codec handles both ADC and DAC via I2S)
// Pins verified against the working FNK0104B board definition
#define AUDIO_I2S_GPIO_MCLK      GPIO_NUM_4
#define AUDIO_I2S_GPIO_BCLK      GPIO_NUM_5
#define AUDIO_I2S_GPIO_DIN       GPIO_NUM_6
#define AUDIO_I2S_GPIO_WS        GPIO_NUM_7
#define AUDIO_I2S_GPIO_DOUT      GPIO_NUM_8
#define AUDIO_CODEC_PA_PIN       GPIO_NUM_1
#define AUDIO_CODEC_ES8311_ADDR  ES8311_CODEC_DEFAULT_ADDR

// Buttons / LED
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define BUILTIN_LED_GPIO        GPIO_NUM_48
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_NC
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_NC

// Display (ILI9341, SPI) — official Freenove FNK0104B pins (verified)
#define LCD_SPI_HOST            SPI3_HOST
#define DISPLAY_MOSI_PIN        GPIO_NUM_11
#define DISPLAY_MISO_PIN        GPIO_NUM_13
#define DISPLAY_CLK_PIN         GPIO_NUM_12
#define DISPLAY_CS_PIN          GPIO_NUM_10
#define DISPLAY_DC_PIN          GPIO_NUM_46
#define DISPLAY_RST_PIN         GPIO_NUM_NC   // TFT_RST = -1, reset via board RST
#define DISPLAY_BACKLIGHT_PIN   GPIO_NUM_45   // HIGH = ON
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false

// Display resolution: 240x320
#define DISPLAY_WIDTH           240
#define DISPLAY_HEIGHT          320
#define DISPLAY_MIRROR_X        true
#define DISPLAY_MIRROR_Y        false
#define DISPLAY_SWAP_XY         false
#define DISPLAY_OFFSET_X        0
#define DISPLAY_OFFSET_Y        0
#define DISPLAY_SPI_MODE        0
#define DISPLAY_SPI_SCLK_HZ     (20 * 1000 * 1000)
#define DISPLAY_RGB_ORDER       LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_INVERT_COLOR    true

// Touch (FT5x06, I2C) - per meshcore-remoteterm-display FNK0104
#define TOUCH_I2C_SDA_PIN       GPIO_NUM_16
#define TOUCH_I2C_SCL_PIN       GPIO_NUM_15
#define TOUCH_I2C_ADDR          0x38
#define TOUCH_INT_PIN           GPIO_NUM_17
#define TOUCH_RST_PIN           GPIO_NUM_18

#define I2C_MASTER_NUM          I2C_NUM_0
#define I2C_MASTER_SDA_IO       TOUCH_I2C_SDA_PIN
#define I2C_MASTER_SCL_IO       TOUCH_I2C_SCL_PIN
#define I2C_MASTER_FREQ_HZ      400000

#endif // _BOARD_CONFIG_H_
