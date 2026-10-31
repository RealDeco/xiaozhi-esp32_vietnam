#include "wifi_board.h"
#include "codecs/es8311_audio_codec.h"
#include "display/lcd_display.h"
#include "display/lcd_touch.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "led/single_led.h"
#include "backlight.h"
#include "assets/lang_config.h"
#include "settings.h"
#include "features/music/esp32_radio.h"
#include "lvgl_theme.h"

#include <wifi_station.h>
#include <esp_log.h>
#include <esp_lcd_touch.h>
#include <esp_lcd_touch_ft5x06.h>
#include <driver/i2c_master.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_ili9341.h>
#include <lvgl.h>
#include <font_awesome.h>
#include <vector>

#define TAG "FreenoveS3LCD28"

class FreenoveEsp32S3Lcd28 : public WifiBoard {
private:
    Button boot_button_;
    LcdDisplay* display_ = nullptr;
    LcdTouch* touch_ = nullptr;
    Es8311AudioCodec* audio_codec_ = nullptr;
    i2c_master_bus_handle_t i2c_bus_handle_ = nullptr;
    lv_obj_t* wifi_button_ = nullptr;
    bool wifi_armed_ = false;
    int64_t wifi_arm_time_ = 0;

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = DISPLAY_MISO_PIN;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = DISPLAY_SPI_SCLK_HZ;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_config, &panel_io));

        ESP_LOGD(TAG, "Install ILI9341 panel driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        esp_lcd_panel_disp_on_off(panel, true);

        display_ = new SpiLcdDisplay(panel_io, panel,
                                     DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                     DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
                                     DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = (i2c_port_t)I2C_MASTER_NUM,
            .sda_io_num = I2C_MASTER_SDA_IO,
            .scl_io_num = I2C_MASTER_SCL_IO,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_handle_));
    }

    void InitializeCodec() {
        // ES8311 codec on the I2C bus (shared with touch)
        audio_codec_ = new Es8311AudioCodec(
            i2c_bus_handle_, (i2c_port_t)I2C_MASTER_NUM,
            AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK,       // MCLK
            AUDIO_I2S_GPIO_BCLK,       // BCLK
            AUDIO_I2S_GPIO_WS,         // WS
            AUDIO_I2S_GPIO_DOUT,       // DOUT (to speaker)
            AUDIO_I2S_GPIO_DIN,        // DIN  (from mic)
            AUDIO_CODEC_PA_PIN,        // PA pin
            AUDIO_CODEC_ES8311_ADDR,   // ES8311 I2C address
            true,                      // use_mclk
            true);                     // pa_inverted
        ESP_LOGI(TAG, "ES8311 codec initialized");
    }

    void InitializeTouch() {
        ESP_LOGI(TAG, "Initialize touch controller FT5x06 (I2C addr 0x%02X)", TOUCH_I2C_ADDR);

        // Optional manual reset of the touch controller
        if (TOUCH_RST_PIN != GPIO_NUM_NC) {
            const gpio_config_t rst_cfg = {
                .pin_bit_mask = (1ULL << (uint32_t)TOUCH_RST_PIN),
                .mode = GPIO_MODE_OUTPUT,
                .pull_up_en = GPIO_PULLUP_DISABLE,
                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                .intr_type = GPIO_INTR_DISABLE
            };
            gpio_config(&rst_cfg);
            gpio_set_level(TOUCH_RST_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(10));
            gpio_set_level(TOUCH_RST_PIN, 1);
            vTaskDelay(pdMS_TO_TICKS(200));
        }

        const esp_lcd_touch_config_t tp_cfg = {
            .x_max = DISPLAY_WIDTH - 1,
            .y_max = DISPLAY_HEIGHT - 1,
            .rst_gpio_num = TOUCH_RST_PIN,
            .int_gpio_num = TOUCH_INT_PIN,
            .levels = { .reset = 0, .interrupt = 0 },
            .flags = {
                .swap_xy = DISPLAY_SWAP_XY ? 1U : 0U,
                .mirror_x = DISPLAY_MIRROR_X ? 1U : 0U,
                .mirror_y = DISPLAY_MIRROR_Y ? 1U : 0U,
            },
        };

        // Create the I2C panel IO for the touch controller
        esp_lcd_panel_io_handle_t tp_io_handle = nullptr;
        esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
        tp_io_config.dev_addr = TOUCH_I2C_ADDR;
        tp_io_config.scl_speed_hz = I2C_MASTER_FREQ_HZ;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus_handle_, &tp_io_config, &tp_io_handle));

        // Create the FT5x06 touch driver
        esp_lcd_touch_handle_t tp_handle = nullptr;
        ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_ft5x06(tp_io_handle, &tp_cfg, &tp_handle));

        touch_ = new I2cLcdTouch(tp_handle, tp_io_handle,
                                 DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                 DISPLAY_SWAP_XY, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

        // Gesture-based volume control + Wi-Fi change
        touch_->SetGestureCallback([this](TouchGesture gesture, int16_t x, int16_t y) {
            ESP_LOGI(TAG, "Touch gesture: %d at (%d, %d)", static_cast<int>(gesture), x, y);
            auto codec = GetAudioCodec();
            auto display = GetDisplay();
            switch (gesture) {
                case TOUCH_GESTURE_SWIPE_UP: {
                    int volume = codec->output_volume() + 10;
                    if (volume > 100) volume = 100;
                    codec->SetOutputVolume(volume);
                    if (display) display->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
                    break;
                }
                case TOUCH_GESTURE_SWIPE_DOWN: {
                    int volume = codec->output_volume() - 10;
                    if (volume < 0) volume = 0;
                    codec->SetOutputVolume(volume);
                    if (display) display->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
                    break;
                }
                case TOUCH_GESTURE_LONG_PRESS: {
                    // Long press toggles mute: when already muted, restore the
                    // volume level that was active before muting.
                    if (codec->output_volume() == 0) {
                        codec->Unmute();
                        if (display) display->ShowNotification(Lang::Strings::VOLUME + std::to_string(codec->output_volume()));
                    } else {
                        codec->SetOutputVolume(0);
                        if (display) display->ShowNotification(Lang::Strings::MUTED);
                    }
                    break;
                }
                case TOUCH_GESTURE_SWIPE_RIGHT: {
                    // Next radio station
                    auto radio = Application::GetInstance().GetRadio();
                    if (radio) {
                        radio->NextStation();
                        if (display) display->ShowNotification("Radio: " + Esp32Radio::ToDisplayAscii(radio->GetCurrentStation()));
                    }
                    break;
                }
                case TOUCH_GESTURE_DOUBLE_TAP: {
                    auto& app = Application::GetInstance();
                    if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                        ResetWifiConfiguration();
                    } else {
                        app.ToggleChatState();
                    }
                    break;
                }
                default:
                    break;
            }
        });

        ESP_LOGI(TAG, "Touch ready (polling mode)");
    }

    void InitializeButtons() {
        // 5 rapid presses of BOOT = change Wi-Fi (reboot into config portal)
        boot_button_.OnMultipleClick([this]() {
            ESP_LOGW(TAG, "BOOT x5 -> reconfigure Wi-Fi");
            ResetWifiConfiguration();
        }, 5);

        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            } else {
                app.ToggleChatState();
            }
        });
    }

    void InitializeTools() {
        // Voice / LLM reachable "change Wi-Fi" tool
        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddTool("self.system.reconfigure_wifi",
            "Reboot the device and enter Wi-Fi configuration mode.\n"
            "**CAUTION** You must ask the user to confirm this action.",
            PropertyList(), [this](const PropertyList& properties) {
                ResetWifiConfiguration();
                return true;
            });
    }

    static void WifiButtonEventCb(lv_event_t* event) {
        auto* self = static_cast<FreenoveEsp32S3Lcd28*>(lv_event_get_user_data(event));
        if (self) self->OnWifiButtonClicked();
    }

    void OnWifiButtonClicked() {
        int64_t now = esp_timer_get_time();
        // First tap arms the action (highlight the icon + prompt),
        // a second tap within 5 seconds confirms and reboots into the Wi-Fi portal.
        if (!wifi_armed_ || (now - wifi_arm_time_) > 5 * 1000 * 1000) {
            wifi_armed_ = true;
            wifi_arm_time_ = now;
            if (wifi_button_) {
                lv_obj_set_style_bg_opa(wifi_button_, LV_OPA_COVER, 0);
                lv_obj_set_style_text_color(wifi_button_, lv_color_hex(0xFFFFFF), 0);
            }
            if (display_) display_->ShowNotification("Смена Wi-Fi: нажмите ещё раз");
            return;
        }
        // Confirmed
        wifi_armed_ = false;
        if (display_) display_->ShowNotification("Смена Wi-Fi...");
        ResetWifiConfiguration();
    }

    void InitializeWifiIcon() {
        if (!display_) return;

        DisplayLockGuard lock(display_);

        // The status bar is the first child of the container (first child of the screen)
        lv_obj_t* screen = lv_screen_active();
        lv_obj_t* container = screen ? lv_obj_get_child(screen, 0) : nullptr;
        lv_obj_t* status_bar = container ? lv_obj_get_child(container, 0) : nullptr;
        if (!status_bar) {
            ESP_LOGW(TAG, "Status bar not found, Wi-Fi icon not created");
            return;
        }

        wifi_button_ = lv_label_create(status_bar);
        lv_label_set_text(wifi_button_, FONT_AWESOME_WIFI);
        lv_obj_set_style_text_color(wifi_button_, lv_color_hex(0x3B7DD8), 0);
        lv_obj_set_style_bg_color(wifi_button_, lv_color_hex(0x3B7DD8), 0);
        lv_obj_set_style_bg_opa(wifi_button_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_radius(wifi_button_, 6, 0);
        lv_obj_set_style_pad_all(wifi_button_, 4, 0);
        lv_obj_set_style_margin_left(wifi_button_, 6, 0);
        lv_obj_add_flag(wifi_button_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(wifi_button_, WifiButtonEventCb, LV_EVENT_CLICKED, this);

        // Use the same icon font as the other status bar icons so the symbol renders
        Settings settings("display", false);
        std::string theme_name = settings.GetString("theme", "light");
        auto* theme = LvglThemeManager::GetInstance().GetTheme(theme_name);
        if (theme && theme->icon_font()) {
            lv_obj_set_style_text_font(wifi_button_, theme->icon_font()->font(), 0);
        }

        ESP_LOGI(TAG, "Wi-Fi icon added to the status bar (tap twice to reconfigure Wi-Fi)");
    }

public:
    FreenoveEsp32S3Lcd28() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeI2c();
        InitializeCodec();
        InitializeTouch();
        InitializeButtons();
        InitializeTools();
        InitializeWifiIcon();
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            GetBacklight()->RestoreBrightness();
        }
    }

    virtual std::string GetBoardType() override {
        return BOARD_NAME;
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        return audio_codec_;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual LcdTouch* GetTouch() override {
        return touch_;
    }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }
};

DECLARE_BOARD(FreenoveEsp32S3Lcd28);
