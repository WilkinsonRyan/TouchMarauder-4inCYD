#pragma once
//
// LovyanGFX panel config — PORTED for the 4" ST7796 CYD with XPT2046 resistive
// touch (original TouchBoard targeted the 2.4" ILI9341 + CST820 capacitive).
// Changes: Panel_ILI9341 -> Panel_ST7796, 240x320 -> 320x480, and the touch is
// now an XPT2046 sharing the display's HSPI bus (CS on GPIO 33), configured here
// so touch.cpp can just call tft.getTouch().
//
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "config.h"

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7796  _panel;
  lgfx::Bus_SPI       _bus;
  lgfx::Light_PWM     _light;
  lgfx::Touch_XPT2046 _touch;

public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host    = HSPI_HOST;      // pins 12/13/14 are the HSPI set
      cfg.spi_mode    = 0;
      cfg.freq_write  = 40000000;
      cfg.freq_read   = 16000000;
      cfg.spi_3wire   = false;
      cfg.use_lock    = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk    = PIN_LCD_SCK;
      cfg.pin_mosi    = PIN_LCD_MOSI;
      cfg.pin_miso    = PIN_LCD_MISO;   // needed for XPT2046 reads on the shared bus
      cfg.pin_dc      = PIN_LCD_DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs           = PIN_LCD_CS;
      cfg.pin_rst          = PIN_LCD_RST;
      cfg.pin_busy         = -1;
      cfg.panel_width      = 320;       // ST7796 native portrait
      cfg.panel_height     = 480;
      cfg.offset_x         = 0;
      cfg.offset_y         = 0;
      cfg.offset_rotation  = 0;
      cfg.readable         = false;
      cfg.invert           = false;     // colors look negative? -> set true
      cfg.rgb_order        = false;     // red/blue swapped?    -> set true
      cfg.dlen_16bit       = false;
      cfg.bus_shared       = true;      // SPI bus is shared with the touch chip
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl      = PIN_LCD_BL;
      cfg.invert      = false;
      cfg.freq        = 12000;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    {
      // XPT2046 resistive touch, sharing the display's HSPI bus.
      auto cfg = _touch.config();
      cfg.x_min      = 300;             // raw ADC range for this panel
      cfg.x_max      = 3900;
      cfg.y_min      = 3900;            // Y swapped (3900<->300) to un-flip the
      cfg.y_max      = 300;             // vertical axis — taps were mirrored top/bottom
      cfg.pin_int    = -1;              // INT not wired for reads; we poll
      cfg.bus_shared = true;
      cfg.offset_rotation = 0;          // flip if taps land mirrored/rotated
      cfg.spi_host   = HSPI_HOST;
      cfg.freq       = 1000000;         // XPT2046 wants a slow clock
      cfg.pin_sclk   = PIN_LCD_SCK;
      cfg.pin_mosi   = PIN_LCD_MOSI;
      cfg.pin_miso   = PIN_LCD_MISO;
      cfg.pin_cs     = PIN_TOUCH_CS;    // GPIO 33 on this board
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};

extern LGFX tft;
