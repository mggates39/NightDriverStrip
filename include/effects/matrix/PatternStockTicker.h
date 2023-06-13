//+--------------------------------------------------------------------------
//
// File:        PatternStockTicker.h
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
//
// Description:
//
//   Gets the Stock Data for a given Ticker Code
//
// History:     Jun-04-2023         Gatesm      Adapted from Weather code
//
//---------------------------------------------------------------------------

#ifndef PatternStocks_H
#define PatternStocks_H

#include <Arduino.h>
#include <string.h>
#include <HTTPClient.h>
#include <UrlEncode.h>
#include <ledstripeffect.h>
#include <ledmatrixgfx.h>
#include <secrets.h>
#include <RemoteDebug.h>
#include <FastLED.h>
#include <ArduinoJson.h>
#include "globals.h"
#include "deviceconfig.h"
#include "jsonserializer.h"
#include <thread>
#include <map>
#include "effects.h"

#define STOCK_INTERVAL_SECONDS (10*60)
#define STOCK_CHECK_WIFI_WAIT 5000
#define DEFAULT_STOCK_TICKER "APPL"

class PatternStockTicker : public LEDStripEffect
{

private:

    String stockTicker       = DEFAULT_STOCK_TICKER;
    String strCompanyName    = "";
    String strExchangeName   = "";
    String strCurrency       = "";
    String strLogoUrl        = "";
    float  marketCap         = 0.0f;
    float  sharesOutstanding = 0.0f;

    float  currentPrice      = 0.0f;
    float  change            = 0.0f;
    float  percentChange     = 0.0f;
    float  highPrice         = 0.0f;
    float  lowPrice          = 0.0f;
    float  openPrice         = 0.0f;
    float  prevClosePrice    = 0.0f;
    long   sampleTime        = 0l;

    bool   dataReady         = false;
    bool   tickerChanged     = true;
    std::vector<SettingSpec> mySettingSpecs;
    TaskHandle_t stockTask = nullptr;
    time_t latestUpdate      = 0;


    // The stock ticker is obviously stock data, and we don't want text overlaid on top of our text

    virtual bool ShouldShowTitle() const
    {
        return false;
    }

    virtual size_t DesiredFramesPerSecond() const override
    {
        return 25;
    }

    virtual bool RequiresDoubleBuffering() const override
    {
        return false;
    }

    bool updateTickerCode()
    {
        HTTPClient http;
        String url;

        if (!tickerChanged)
            return false;

        url = "https://finnhub.io/api/v1/stock/profile2"
              "?symbol=" + urlEncode(stockTicker) + "&token=" + urlEncode(g_ptrDeviceConfig->GetStockTickerAPIKey());

        http.begin(url);
        int httpResponseCode = http.GET();

        if (httpResponseCode <= 0)
        {
            debugW("Error fetching data for company of for ticker: %s", stockTicker);
            http.end();
            return false;
        }
        /*
        {   "country":"US",
            "currency":"USD",
            "estimateCurrency":"USD",
            "exchange":"NASDAQ NMS - GLOBAL MARKET",
            "finnhubIndustry":"Retail",
            "ipo":"1997-05-15",
            "logo":"https://static2.finnhub.io/file/publicdatany/finnhubimage/stock_logo/AMZN.svg",
            "marketCapitalization":1221092.7955074026,
            "name":"Amazon.com Inc",
            "phone":"12062661000.0",
            "shareOutstanding":10260.4,
            "ticker":"AMZN",
            "weburl":"https://www.amazon.com/"}
        */

        AllocatedJsonDocument doc(2048);
        deserializeJson(doc, http.getString());
        JsonObject companyData =  doc.as<JsonObject>();

        strCompanyName    = companyData["name"].as<String>();
        strExchangeName   = companyData["exchange"].as<String>();
        strCurrency       = companyData["currency"].as<String>();
        strLogoUrl        = companyData["logo"].as<String>();
        marketCap         = companyData["marketCapitalization"].as<float>();
        sharesOutstanding = companyData["shareOutstanding"].as<float>();

        http.end();

        return true;
    }

    // getStockData
    //
    // Get the current temp and the high and low for today

    bool getStockData()
    {
        HTTPClient http;

        String url = "https://finnhub.io/api/v1/quote"
            "?symbol=" + stockTicker  + "&token=" + urlEncode(g_ptrDeviceConfig->GetStockTickerAPIKey());
        http.begin(url);
        int httpResponseCode = http.GET();
        if (httpResponseCode > 0)
        {
            /*
                {
                    "c":179.58,
                    "d":-1.37,
                    "dp":-0.7571,
                    "h":184.95,
                    "l":178.035,
                    "o":182.63,
                    "pc":180.95,
                    "t":1685995205
                }
            */
            
            AllocatedJsonDocument jsonDoc(512);
            deserializeJson(jsonDoc, http.getString());
            JsonObject stockData =  jsonDoc.as<JsonObject>();

            strCompanyName    = stockData["name"].as<float>();
 
            // Once we have a non-zero temp we can start displaying things
            if (0 < jsonDoc["c"])
                dataReady = true;


            currentPrice      = stockData["c"].as<float>();
            change            = stockData["d"].as<float>();
            percentChange     = stockData["dp"].as<float>();
            highPrice         = stockData["h"].as<float>();
            lowPrice          = stockData["l"].as<float>();
            openPrice         = stockData["o"].as<float>();
            prevClosePrice    = stockData["pc"].as<float>();
            sampleTime        = stockData["t"].as<long>();

            debugI("Got ticker: Now %f Lo %f, Hi %f, Change %f", currentPrice, lowPrice, highPrice, change);

            http.end();
            return true;
        }
        else
        {
            debugW("Error fetching Stock data for Ticker: %s", stockTicker);
            http.end();
            return false;
        }
    }

    // Thread entry point so we can update the stock data asynchronously
    static void StockTaskEntryPoint(LEDStripEffect &effect)
    {
        PatternStockTicker& stockEffect = static_cast<PatternStockTicker&>(effect);

        for(;;)
        {
            // Suspend ourself until Draw() wakes us up
            vTaskSuspend(NULL);

            stockEffect.UpdateStock();
        }
    }

    void UpdateStock()
    {
        while(!WiFi.isConnected())
        {
            debugI("Delaying Stock update, waiting for WiFi...");
            vTaskDelay(pdMS_TO_TICKS(STOCK_CHECK_WIFI_WAIT));
        }

        updateTickerCode();

        if (getStockData())
        {
            debugW("Got today's stock");
        }
        else
        {
            debugW("Failed to get today's stock");
        }
    }

public:


    virtual bool FillSettingSpecs() override
    {
        if (!LEDStripEffect::FillSettingSpecs())
            return false;

        mySettingSpecs.emplace_back(
            NAME_OF(stockTicker),
            "Stock Ticker to Show",
            "The valid Stock Ticker to show.  May be from any exchange.",
            SettingSpec::SettingType::String
        );
        _settingSpecs.insert(_settingSpecs.end(), mySettingSpecs.begin(), mySettingSpecs.end());

        return true;
    }

    PatternStockTicker() : LEDStripEffect(EFFECT_MATRIX_WEATHER, "Stock")
    {
    }

    PatternStockTicker(const JsonObjectConst&  jsonObject) : LEDStripEffect(jsonObject)
    {
        if (jsonObject.containsKey("stk"))
            stockTicker = jsonObject["stk"].as<String>();
    }

    virtual bool SerializeToJSON(JsonObject& jsonObject) override
    {
        StaticJsonDocument<256> jsonDoc;

        JsonObject root = jsonDoc.to<JsonObject>();
        LEDStripEffect::SerializeToJSON(root);

        jsonDoc["stk"] = stockTicker;

        return jsonObject.set(jsonDoc.as<JsonObjectConst>());
    }

    virtual bool Init(std::shared_ptr<GFXBase> gfx[NUM_CHANNELS]) override
    {
        if (!LEDStripEffect::Init(gfx))
            return false;

        stockTask = g_TaskManager.StartEffectThread(StockTaskEntryPoint, this, "Stock");

        return true;
    }

    virtual void Draw() override
    {
        const int fontHeight = 7;
        const int fontWidth  = 5;
        const int xHalf      = MATRIX_WIDTH / 2 - 1;

        g()->fillScreen(BLACK16);
        g()->fillRect(0, 0, MATRIX_WIDTH, 9, g()->to16bit(CRGB(0,0,128)));

        g()->setFont(&Apple5x7);

        time_t now;
        time(&now);

        auto secondsSinceLastUpdate = now - latestUpdate;

        // If location and/or country have changed, trigger an update regardless of timer, but
        // not more than once every half a minute
        if (secondsSinceLastUpdate >= STOCK_INTERVAL_SECONDS)
        {
            latestUpdate = now;

            debugW("Triggering thread to check stock now...");
            // Resume the stock task. If it's not suspended then there's an update running and this will do nothing,
            //   so we'll silently skip the update this would otherwise trigger.
            vTaskResume(stockTask);
        }


        // Print the town/city name

        int x = 0;
        int y = fontHeight + 1;
        g()->setCursor(x, y);
        g()->setTextColor(WHITE16);
        String showLocation = strCompanyName;
        showLocation.toUpperCase();
        if (g_ptrDeviceConfig->GetStockTickerAPIKey().isEmpty())
            g()->print("No API Key");
        else
            g()->print((strCompanyName.isEmpty() ? stockTicker : strCompanyName).substring(0, (MATRIX_WIDTH - 2 * fontWidth)/fontWidth));

        // Display the temperature, right-justified

        if (dataReady)
        {
            String strTemp(currentPrice);
            x = MATRIX_WIDTH - fontWidth * strTemp.length();
            g()->setCursor(x, y);
            g()->setTextColor(g()->to16bit(CRGB(192,192,192)));
            g()->print(strTemp);
        }

        // Draw the separator lines

        y+=1;

        g()->drawLine(0, y, MATRIX_WIDTH-1, y, CRGB(0,0,128));
        g()->drawLine(xHalf, y, xHalf, MATRIX_HEIGHT-1, CRGB(0,0,128));
        y+=2 + fontHeight;

        // Figure out which day of the week it is

        time_t today = time(nullptr);
        tm * todayTime = localtime(&today);
        const char * pszToday = pszDaysOfWeek[todayTime->tm_wday];
        const char * pszTomorrow = pszDaysOfWeek[ (todayTime->tm_wday + 1) % 7 ];

        // Draw the day of the week and tomorrow's day as well

        g()->setTextColor(WHITE16);
        g()->setCursor(0, MATRIX_HEIGHT);
        g()->print(pszToday);
        g()->setCursor(xHalf+2, MATRIX_HEIGHT);
        g()->print(pszTomorrow);

        // Draw the temperature in lighter white

        if (dataReady)
        {
            g()->setTextColor(g()->to16bit(CRGB(192,192,192)));
            String strHi( highPrice);
            String strLo( lowPrice);

            // Draw today's HI and LO temperatures

            x = xHalf - fontWidth * strHi.length();
            y = MATRIX_HEIGHT - fontHeight;
            g()->setCursor(x,y);
            g()->print(strHi);
            x = xHalf - fontWidth * strLo.length();
            y+= fontHeight;
            g()->setCursor(x,y);
            g()->print(strLo);

            // Draw tomorrow's HI and LO temperatures

            strHi = String((int)openPrice);
            strLo = String((int)prevClosePrice);
            x = MATRIX_WIDTH - fontWidth * strHi.length();
            y = MATRIX_HEIGHT - fontHeight;
            g()->setCursor(x,y);
            g()->print(strHi);
            x = MATRIX_WIDTH - fontWidth * strLo.length();
            y+= fontHeight;
            g()->setCursor(x,y);
            g()->print(strLo);
        }
    }

    virtual bool SerializeSettingsToJSON(JsonObject& jsonObject) override
    {
        StaticJsonDocument<256> jsonDoc;
        auto rootObject = jsonDoc.to<JsonObject>();

        LEDStripEffect::SerializeSettingsToJSON(jsonObject);

        jsonDoc[NAME_OF(stockTicker)] = stockTicker;

        return jsonObject.set(jsonDoc.as<JsonObjectConst>());
    }

    virtual bool SetSetting(const String& name, const String& value) override
    {
        RETURN_IF_SET(name, NAME_OF(stockTicker), stockTicker, value);

        return LEDStripEffect::SetSetting(name, value);
    }
};

#endif
