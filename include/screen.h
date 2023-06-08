//+--------------------------------------------------------------------------
//
// File:        screen.h
//
// NightDriverStrip - (c) 2018 Plummer's Software LLC.  All Rights Reserved.  
//
// This file is part of the NightDriver software project.
//
//    NightDriver is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//   
//    NightDriver is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//   
//    You should have received a copy of the GNU General Public License
//    along with Nightdriver.  It is normally found in copying.txt
//    If not, see <https://www.gnu.org/licenses/>.
//
// Description:
//
//    Generalizes drawing to various different screens.  For example,
//    drawing a line accepts a color in some cases, but not others, and
//    it depends on what display you are compiling for.  This is a bit
//    of an abstraction layer on those various devices.
//
// History:     Dec-10-2022         Davepl      Created
//
//---------------------------------------------------------------------------

#pragma once

#include <mutex>
#include "freefonts.h"

// A project with a screen will define one of these screen types (TFT, OLED, LCD, etc) and one object of the
// correct type will be created and assigned to g_pDisplay, which will have the appropriate type.

#if USE_OLED
    extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C * g_pDisplay;
#endif

#if USE_LCD
    extern Adafruit_ILI9341 * g_pDisplay;
    #include <fonts/FreeMono18pt7b.h>
    #include <fonts/FreeMono12pt7b.h>
    #include <fonts/FreeMono9pt7b.h>
#endif

#if USE_M5DISPLAY
    extern M5Display * g_pDisplay;
#endif

#if USE_TFTSPI
    #include <TFT_eSPI.h>
    #include <SPI.h>
    extern std::unique_ptr<TFT_eSPI> g_pDisplay;
#endif

class Screen
{
  public:

    static DRAM_ATTR std::mutex _screenMutex;   

    // Define the drawable area for the spectrum to render into the status area

#if M5STICKCPLUS || M5STACKCORE2
    static const int TopMargin = 37;  
    static const int BottomMargin = 20;
#else
    static const int TopMargin = 28;  
    static const int BottomMargin = 12;
#endif


    

    static inline uint16_t to16bit(const CRGB rgb) // Convert CRGB -> 16 bit 5:6:5
    {
      return ((rgb.r / 8) << 11) | ((rgb.g / 4) << 5) | (rgb.b / 8);
    }

    // ScreenStatus
    // 
    // Display a single string of text on the TFT screen, useful during boot for status, etc.

    static inline void ScreenStatus(const String & strStatus)
    {
    #if USE_OLED 
        g_pDisplay->clear();
        g_pDisplay->clearBuffer();                   // clear the internal memory
        g_pDisplay->setFont(u8g2_font_profont15_tf); // choose a suitable font
        g_pDisplay->setCursor(0, 10);
        g_pDisplay->println(strStatus);
        g_pDisplay->sendBuffer();
    #elif USE_TFTSPI || USE_M5DISPLAY
        g_pDisplay->fillScreen(TFT_BLACK);
        g_pDisplay->setFreeFont(FF1);
        g_pDisplay->setTextColor(0xFBE0);
        auto xh = 10;
        auto yh = 0; 
        g_pDisplay->drawString(strStatus, xh, yh);
    #elif USE_LCD
        g_pDisplay->fillScreen(BLUE16);
        g_pDisplay->setFont(FM9);
        g_pDisplay->setTextColor(WHITE16);
        auto xh = 10;
        auto yh = 0; 
        g_pDisplay->setCursor(xh, yh);
        g_pDisplay->print(strStatus);
    #endif
    }

    static uint16_t screenWidth()
    {
        #if USE_OLED
            return g_pDisplay->getDisplayWidth();
        #elif USE_SCREEN
            return g_pDisplay->width();
        #else
            return 1;
        #endif
    }

    static uint16_t fontHeight()
    {
        #if USE_LCD
            int16_t x1, y1;
            uint16_t w, h;
            g_pDisplay->getTextBounds("M", 0, 0, &x1, &y1, &w, &h);         // Beats me how to do this, so I'm taking the height of M as a line height
            return w + 2;                                                   // One pixel above and below chars looks better
        #elif USE_OLED 
            return g_pDisplay->getFontAscent() + 1;
        #elif USE_TFTSPI || USE_M5DISPLAY
            return g_pDisplay->fontHeight();
        #elif USE_SCREEN
            return g_pDisplay->getFontAscent();
        #else
            return 12;                                                      // Some bogus reasonable default for those that don't support it
        #endif
    }

    static uint16_t textWidth(const String & str)
    {
        #if USE_OLED
            return g_pDisplay->getStrWidth(str.c_str());
        #elif USE_TFTSPI || USE_M5DISPLAY            
            return g_pDisplay->textWidth(str);            
        #elif USE_LCD
            int16_t x1, y1;
            uint16_t w, h;
            g_pDisplay->getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
            return w;
        #else 
            return 8 * str.length();
        #endif
    }

    static uint16_t screenHeight()
    {

        #if USE_OLED
            return g_pDisplay->getDisplayHeight();
        #elif USE_SCREEN
            return g_pDisplay->height();
        #else
            return MATRIX_HEIGHT;
        #endif
    }

    static void fillScreen(uint16_t color)
    {
        #if USE_OLED
            g_pDisplay->clear();
        #elif USE_SCREEN
            g_pDisplay->fillScreen(color);
        #endif

    }

    static void setTextColor(uint16_t foreground, uint16_t background)
    {
        #if USE_OLED
            // NOP
        #elif USE_SCREEN
            g_pDisplay->setTextColor(foreground, background);
        #endif

    }

    enum FONTSIZE { TINY, SMALL, MEDIUM, BIG };

    static void setTextSize(FONTSIZE size)
    {
        #if USE_M5DISPLAY
            switch(size)
            {
                case BIG:
                    g_pDisplay->setTextFont(1);
                    g_pDisplay->setTextSize(3);
                    break;
                case MEDIUM:
                    g_pDisplay->setTextFont(1);
                    g_pDisplay->setTextSize(2);
                    break;
                case TINY:
                    g_pDisplay->setTextFont(1);
                    g_pDisplay->setTextSize(1);
                    break;                
                default:
                    g_pDisplay->setTextFont(1);
                    g_pDisplay->setTextSize(1);
                    break;
            }
        #endif
        
        #if USE_TFTSPI 

            switch(size)
            {
                case BIG:
                    g_pDisplay->setTextFont(0);
                    g_pDisplay->setTextSize(4);
                    break;
                case MEDIUM:
                    g_pDisplay->setTextFont(0);
                    g_pDisplay->setTextSize(3);
                    break;
                case TINY:
                    g_pDisplay->setTextFont(0);
                    g_pDisplay->setTextSize(1);
                    break;                
                default:
                    g_pDisplay->setTextFont(0);
                    g_pDisplay->setTextSize(2);
                    break;
            }
        #endif

        #if USE_LCD
            switch(size)
            {
                case BIG:
                    g_pDisplay->setFont(&FreeMono18pt7b);
                    break;
                case MEDIUM:
                    g_pDisplay->setFont(&FreeMono12pt7b);
                    break;
                case TINY:
                    g_pDisplay->setFont(&FreeMono9pt7b);
                    break;                
                default:
                    g_pDisplay->setFont(&FreeMono9pt7b);
                    break;
            }
        #endif

        #if USE_OLED
               g_pDisplay->setFont(u8g2_font_profont15_tf);  // OLED uses the same little font for everything
        #endif
    }

    static void setCursor(uint16_t x, uint16_t y)
    {
        #if USE_OLED
            g_pDisplay->setCursor(x, y + fontHeight() - 1);
        #elif USE_SCREEN
            g_pDisplay->setCursor(x, y);       // M5 baselines its text at the top
        #endif
    }

    static void println(const char * strText)
    {
        #if USE_SCREEN
            g_pDisplay->println(strText);
        #endif
    }

    static void println(const String & strText)
    {
        #if USE_SCREEN
            g_pDisplay->println(strText.c_str());
        #endif
    }

    static void drawString(const String & strText, uint16_t x, uint16_t y)
    {
        #if USE_M5DISPLAY || USE_TFTSPI || USE_OLED
        setCursor(x, y);
        println(strText.c_str());
        #endif
    }

    // drawString with no x component assumed you want it centered on the display

    static void drawString(const String & strText, uint16_t y)
    {
        #if USE_M5DISPLAY || USE_TFTSPI || USE_OLED
        setCursor(screenWidth() / 2 - textWidth(strText) / 2, y);
        println(strText.c_str());
        #endif
    }

    static void drawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t color)
    {
        #if USE_OLED
            g_pDisplay->drawBox(x, y, w, h);
        #elif USE_M5DISPLAY || USE_TFTSPI 
            g_pDisplay->drawRect(x, y, w, h, color);
        #elif USE_SCREEN
            g_pDisplay->drawFrame(x, y, w, h);
        #endif
    }

    static void fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
    {
        #if USE_M5DISPLAY || USE_TFTSPI
            g_pDisplay->fillRect(x, y, w, h, color);
        #elif USE_SCREEN || USE_OLED
            g_pDisplay->drawBox(x, y, w, h);
        #endif
    }

    static void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color)
    {
        #if USE_M5DISPLAY || USE_TFTSPI
            g_pDisplay->drawLine(x0, y0, x1, y1, color);
        #elif USE_OLED 
            g_pDisplay->drawLine(x0, y0, x1, y1);
        #elif USE_SCREEN
            // BUGBUG (todo)
        #endif
   }
};
