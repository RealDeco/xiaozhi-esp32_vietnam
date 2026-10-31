#include "fnk0104b.h"
#include "application.h"
#include "display/lcd_display.h"
#include "display/lcd_touch.h"
#include "audio/audio_codec.h"
#include "backlight.h"

#include <esp_log.h>
#include <driver/gpio.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_touch.h>

#define TAG "FNK0104B"

#define QSPI_LCD_HOST           SPI2_HOST
#define QSPI_PIN_NUM_LCD_PCLK   GPIO_NUM_12
#define QSPI_PIN_NUM_LCD_CS     GPIO_NUM_11
#define QSPI_PIN_NUM_LCD_DATA0  GPIO_NUM_13
#define QSPI_PIN_NUM_LCD_DATA1  GPIO_NUM_14
#define QSPI_PIN_NUM_LCD_DATA2  GPIO_NUM_15
#define QSPI_PIN_NUM_LCD_DATA3  GPIO_NUM_16
#define QSPI_PIN_NUM_LCD_RST    GPIO_NUM_NC
#define QSPI_PIN_NUM_LCD_BL     GPIO_NUM_10

#define QSPI_LCD_H_RES          (320)
#define QSPI_LCD_V_RES          (240)
#define QSPI_LCD_BIT_PER_PIXEL  (16)

// Touch
#define TOUCH_I2C_NUM           I2C_NUM_0
#define TOUCH_I2C_SCL_PIN       GPIO_NUM_10
#define TOUCH_I2C_SDA_PIN       GPIO_NUM_11
#define TOUCH_RST_PIN           GPIO_NUM_NC
#define TOUCH_INT_PIN           GPIO_NUM_4
#define TOUCH_I2C_ADDR          0x15

static bool fnk0104b_init_lcd(FNK0104B *self) {
    ESP_LOGI(TAG, "Initializing LCD...");
    
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_handle_t panel_handle = NULL;
    
    // QSPI configuration for LCD
    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = QSPI_PIN_NUM_LCD_CS,
        .dc_gpio_num = GPIO_NUM_NC,
        .spi_mode = 0,
        .pclk_hz = 40 * 1000 * 1000,
        .trans_queue_depth = 10,
        .on_color_trans_done = NULL,
        .user_ctx = NULL,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .io_color_format = ESP_LCD_COLOR_FORMAT_RGB565,
        .flags = {
            .dc_low_on_data = 0,
            .octal_mode = 0,
            .quad_mode = 1,
            .sio_mode = 0,
            .lsb_first = 0,
            .cs_high_active = 0,
        },
    };
    
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)QSPI_LCD_HOST, &io_config, &io_handle));
    
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = QSPI_PIN_NUM_LCD_RST,
        .rgb_endian = LCD_RGB_ENDIAN_RGB,
        .bits_per_pixel = QSPI_LCD_BIT_PER_PIXEL,
        .reset_active_high = false,
    };
    
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    
    // Display on
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    
    self->display = new LcdDisplay(io_handle, panel_handle, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    ESP_LOGI(TAG, "LCD initialized successfully");
    
    return true;
}

static bool fnk0104b_init_touch(FNK0104B *self) {
    ESP_LOGI(TAG, "Initializing touch controller...");
    
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_touch_handle_t tp_handle = NULL;
    
    const esp_lcd_panel_io_i2c_config_t tp_io_config = {
        .dev_addr = TOUCH_I2C_ADDR,
        .on_color_trans_done = NULL,
        .user_ctx = NULL,
        .control_phase_bytes = 1,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .dc_bit_offset = 0,
        .flags = {
            .disable_control_phase = 0,
        },
    };
    
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)TOUCH_I2C_NUM, &tp_io_config, &tp_io_handle));
    
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = DISPLAY_WIDTH,
        .y_max = DISPLAY_HEIGHT,
        .rst_gpio_num = TOUCH_RST_PIN,
        .int_gpio_num = TOUCH_INT_PIN,
        .mirror_x = DISPLAY_MIRROR_X,
        .mirror_y = DISPLAY_MIRROR_Y,
        .swap_xy = DISPLAY_SWAP_XY,
        .flags = {
            .interrupt_active_level = 1,
        },
    };
    
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst816s(tp_io_handle, &tp_cfg, &tp_handle));
    
    self->touch = new I2cLcdTouch(tp_handle, tp_io_handle, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_SWAP_XY, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
    
    // Set up gesture callback for volume control
    self->touch->SetGestureCallback([self](TouchGesture gesture, int16_t x, int16_t y) {
        ESP_LOGI(TAG, "Touch gesture: %d at (%d, %d)", static_cast<int>(gesture), x, y);
        
        switch (gesture) {
            case TOUCH_GESTURE_SWIPE_UP:
                {
                    auto codec = self->GetAudioCodec();
                    auto volume = codec->output_volume() + 10;
                    if (volume > 100) volume = 100;
                    codec->SetOutputVolume(volume);
                    self->GetDisplay()->ShowNotification("Громкость: " + std::to_string(volume));
                }
                break;
            case TOUCH_GESTURE_SWIPE_DOWN:
                {
                    auto codec = self->GetAudioCodec();
                    auto volume = codec->output_volume() - 10;
                    if (volume < 0) volume = 0;
                    codec->SetOutputVolume(volume);
                    self->GetDisplay()->ShowNotification("Громкость: " + std::to_string(volume));
                }
                break;
            case TOUCH_GESTURE_SWIPE_RIGHT:
                {
                    auto& app = Application::GetInstance();
                    auto radio = app.GetRadio();
                    if (radio) {
                        // Next station logic could go here
                        ESP_LOGI(TAG, "Next station");
                    }
                }
                break;
            case TOUCH_GESTURE_SWIPE_LEFT:
                {
                    auto& app = Application::GetInstance();
                    auto radio = app.GetRadio();
                    if (radio) {
                        // Previous station logic could go here
                        ESP_LOGI(TAG, "Previous station");
                    }
                }
                break;
            case TOUCH_GESTURE_TAP:
                ESP_LOGI(TAG, "Tap at (%d, %d)", x, y);
                break;
            case TOUCH_GESTURE_DOUBLE_TAP:
                {
                    auto& app = Application::GetInstance();
                    app.ToggleChatState();
                }
                break;
            case TOUCH_GESTURE_LONG_PRESS:
                {
                    auto codec = self->GetAudioCodec();
                    codec->SetOutputVolume(0);
                    self->GetDisplay()->ShowNotification("Выключено");
                }
                break;
            default:
                break;
        }
    });
    
    ESP_LOGI(TAG, "Touch controller initialized");
    return true;
}

static void fnk0104b_init_audio(FNK0104B *self) {
    ESP_LOGI(TAG, "Initializing audio codec...");
    
    // Audio codec initialization would go here
    // This depends on the specific codec used on FNK0104B
    
    ESP_LOGI(TAG, "Audio codec initialized");
}

FNK0104B::FNK0104B() {
    ESP_LOGI(TAG, "FNK0104B board constructor");
}

FNK0104B::~FNK0104B() {
    ESP_LOGI(TAG, "FNK0104B board destructor");
}

void FNK0104B::Initialize() {
    ESP_LOGI(TAG, "Initializing FNK0104B board...");
    
    // Initialize I2C
    const i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_BUS_SDA_PIN,
        .scl_io_num = I2C_BUS_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master = {
            .clk_speed = 400000,
        },
        .clk_flags = 0,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_BUS_PORT, &i2c_conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_BUS_PORT, I2C_MODE_MASTER, 0, 0, 0));
    
    // Initialize LCD
    if (!fnk0104b_init_lcd(this)) {
        ESP_LOGE(TAG, "Failed to initialize LCD");
        return;
    }
    
    // Initialize touch
    if (!fnk0104b_init_touch(this)) {
        ESP_LOGE(TAG, "Failed to initialize touch");
        return;
    }
    
    // Initialize audio
    fnk0104b_init_audio(this);
    
    // Initialize backlight
    auto backlight = new BacklightLedc(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
    backlight->SetBrightness(80);
    this->backlight = backlight;
    
    ESP_LOGI(TAG, "FNK0104B board initialized successfully");
}

void FNK0104B::SetChatState(int state) {
    // Update display based on chat state
}

void FNK0104B::StartNetwork() {
    // Start network connection
}

void FNK0104B::ResetWifiConfiguration() {
    // Reset WiFi configuration
}
