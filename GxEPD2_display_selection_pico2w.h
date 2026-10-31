#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_BW.h>

// Waveshare Pico-ePaper-3.7, monochrome 480x280, SSD1677.
// Configuration validée sur le matériel de ce projet.
constexpr uint8_t PIN_EPD_CS = 9;
constexpr uint8_t PIN_EPD_DC = 8;
constexpr uint8_t PIN_EPD_RST = 12;
constexpr uint8_t PIN_EPD_BUSY = 13;

using PicoClockDisplay = GxEPD2_BW<GxEPD2_370_TC1, 280>;
extern PicoClockDisplay display;
