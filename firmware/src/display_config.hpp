#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// ESP32-2432S028 "Cheap Yellow Display" config
class LGFX : public lgfx::LGFX_Device {
public:
    lgfx::Panel_ILI9341 _panel_instance;
    lgfx::Bus_SPI       _bus_instance;
    lgfx::Light_PWM     _light_instance;
    lgfx::Touch_XPT2046 _touch_instance;

    LGFX(void) {
        // SPI bus config
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host = VSPI_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = 40000000;
            cfg.freq_read  = 16000000;
            cfg.spi_3wire  = false;
            cfg.use_lock   = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = 14;  // SCK
            cfg.pin_mosi = 13;  // MOSI
            cfg.pin_miso = 12;  // MISO
            cfg.pin_dc   = 2;   // DC
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        // Panel config
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs   = 15;  // CS
            cfg.pin_rst  = -1;  // RST (directly to 3.3V on CYD)
            cfg.pin_busy = -1;
            cfg.memory_width  = 240;
            cfg.memory_height = 320;
            cfg.panel_width   = 240;
            cfg.panel_height  = 320;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits  = 1;
            cfg.readable = true;
            cfg.invert = false;
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = true;
            _panel_instance.config(cfg);
        }

        // Backlight config
        {
            auto cfg = _light_instance.config();
            cfg.pin_bl = 21;      // Backlight pin
            cfg.invert = false;
            cfg.freq   = 44100;
            cfg.pwm_channel = 7;
            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }

        // Touch config (XPT2046)
        {
            auto cfg = _touch_instance.config();
            cfg.x_min      = 300;
            cfg.x_max      = 3900;
            cfg.y_min      = 200;
            cfg.y_max      = 3700;
            cfg.pin_int    = 36;   // Touch IRQ
            cfg.bus_shared = false;
            cfg.offset_rotation = 0;  // Try 0 for correct touch
            cfg.spi_host = HSPI_HOST;
            cfg.freq = 1000000;
            cfg.pin_sclk = 25;    // Touch CLK
            cfg.pin_mosi = 32;    // Touch DIN
            cfg.pin_miso = 39;    // Touch DOUT
            cfg.pin_cs   = 33;    // Touch CS
            _touch_instance.config(cfg);
            _panel_instance.setTouch(&_touch_instance);
        }

        setPanel(&_panel_instance);
    }
};

static LGFX display;
