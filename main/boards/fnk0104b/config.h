#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <driver/spi_master.h>

// FNK0104B Board Configuration
// ESP32-S3 with Touch LCD Display

#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

#define BOOT_BUTTON_GPIO        GPIO_NUM_0

// I2S Audio pins
#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_2
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_15
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_39
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_47
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_48
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_38

// I2C Bus for touch controller
#define I2C_BUS_PORT            I2C_NUM_0
#define I2C_BUS_SCL_PIN         GPIO_NUM_10
#define I2C_BUS_SDA_PIN         GPIO_NUM_11

// Display configuration
#define DISPLAY_WIDTH           320
#define DISPLAY_HEIGHT          240
#define DISPLAY_MIRROR_X        false
#define DISPLAY_MIRROR_Y        false
#define DISPLAY_SWAP_XY         false

// QSPI LCD pins
#define QSPI_LCD_H_RES          (320)
#define QSPI_LCD_V_RES          (240)
#define QSPI_LCD_BIT_PER_PIXEL  (16)

#define QSPI_LCD_HOST           SPI2_HOST
#define QSPI_PIN_NUM_LCD_PCLK   GPIO_NUM_12
#define QSPI_PIN_NUM_LCD_CS     GPIO_NUM_11
#define QSPI_PIN_NUM_LCD_DATA0  GPIO_NUM_13
#define QSPI_PIN_NUM_LCD_DATA1  GPIO_NUM_14
#define QSPI_PIN_NUM_LCD_DATA2  GPIO_NUM_15
#define QSPI_PIN_NUM_LCD_DATA3  GPIO_NUM_16
#define QSPI_PIN_NUM_LCD_RST    GPIO_NUM_NC
#define QSPI_PIN_NUM_LCD_BL     GPIO_NUM_10

#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0

// Touchscreen configuration (CST816S or similar)
#define TOUCH_I2C_NUM           I2C_BUS_PORT
#define TOUCH_I2C_SCL_PIN       I2C_BUS_SCL_PIN
#define TOUCH_I2C_SDA_PIN       I2C_BUS_SDA_PIN
#define TOUCH_RST_PIN           GPIO_NUM_NC
#define TOUCH_INT_PIN           GPIO_NUM_4
#define TOUCH_I2C_ADDR          0x15

#define DISPLAY_BACKLIGHT_PIN   QSPI_PIN_NUM_LCD_BL
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false

// SD Card (optional)
#define CARD_SDMMC_CLK_GPIO GPIO_NUM_39
#define CARD_SDMMC_CMD_GPIO GPIO_NUM_40
#define CARD_SDMMC_D0_GPIO  GPIO_NUM_38

#endif // _BOARD_CONFIG_H_
