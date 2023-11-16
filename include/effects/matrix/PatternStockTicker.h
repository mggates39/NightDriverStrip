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
//   Gets the Stock Data for a given set of Ticker Symbols
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

// Default stock Ticker code Apple 
#define DEFAULT_STOCK_TICKER "APPL"

// Update stocks every 10 minutes, retry after 30 seconds on error, and check other things every 5 seconds
#define STOCK_CHECK_INTERVAL        (10 * 60000)
#define STOCK_CHECK_ERROR_INTERVAL  30000
#define STOCK_READER_INTERVAL       5000
#define STOCK_DISPLAY_INTERVAL      32000

/**
 * @brief All the data about a specific Stock Ticker
 * Stored as a circular linked list so that the draw code
 * can easily move from one to the next
 * 
 */
class StockTicker
{
public:
    char strSymbol[12];
    char strCompanyName[32];
    char strExchangeName[16];
    char strCurrency[16];

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
    StockTicker* prev        = NULL;
    StockTicker* next        = NULL;
  
};

/**
 * @brief 
 * 
 */
class PatternStockTicker : public LEDStripEffect
{

private:

    StockTicker tickers[3];
    size_t numberTickers        = 3;
    StockTicker *currentTicker  = tickers;


    bool   dataReady            = false;
    bool   stockChanged         = false;
    size_t currentOffset        = 0;
    String stockTickerList      = DEFAULT_STOCK_TICKER;
    size_t readerIndex          = std::numeric_limits<size_t>::max();
    unsigned long msLastCheck   = 0;
    bool succeededBefore        = false;
    unsigned long msLastUpdate  = 0;
    static std::vector<SettingSpec, psram_allocator<SettingSpec>> mySettingSpecs;

    /**
     * @brief The stock ticker is obviously stock data, and we don't want text overlaid on top of our text
     * 
     * @return false 
     */
    bool ShouldShowTitle() const
    {
        return false;
    }

    /**
     * @brief 
     * 
     * @return size_t 
     */
    size_t DesiredFramesPerSecond() const override
    {
        return 10;
    }

    /**
     * @brief Does this effect need double buffering>
     * 
     * @return true 
     */
    bool RequiresDoubleBuffering() const override
    {
        return true;
    }

    /**
     * @brief 
     * 
     * @param newSymbols 
     * @return int 
     */
    int ParseTickerSymbols(String newSymbols)
    {
        char *ptr;
        const int length = newSymbols.length();
        char *str = new char[length + 1];
        strcpy(str, newSymbols.c_str());
        int n = 0;
        ptr = strtok(str, ",");
        while (ptr != NULL)
        {
            n++;
            // cout << ptr  << endl;
            ptr = strtok (NULL, ",");
        }
        return n;
    }

    /**
     * @brief 
     * 
     * @return true 
     * @return false 
     */
    bool updateTickerCode(StockTicker *ticker)
    {
        HTTPClient http;
        String url;

        url = "https://finnhub.io/api/v1/stock/profile2"
              "?symbol=" + urlEncode(ticker->strSymbol) + "&token=" + urlEncode(g_ptrSystem->DeviceConfig().GetStockTickerAPIKey());

        http.begin(url);
        int httpResponseCode = http.GET();

        if (httpResponseCode <= 0)
        {
            debugW("Error fetching data for company of for ticker: %s", ticker->strSymbol);
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

        AllocatedJsonDocument doc(1024);
        String headerData = http.getString();
        debugI("Stock Heder: %s", headerData.c_str());
        if (headerData.equals("{}")) 
        {
            strcpy(ticker->strCompanyName, "Bad Symbol");
            strcpy(ticker->strExchangeName, "");
            strcpy(ticker->strCurrency, "");
            ticker->marketCap         = 0.0;
            ticker->sharesOutstanding = 0.0;

            debugW("Bad ticker symbol: '%s'", ticker->strSymbol);
            http.end();
            return false;
        }
        else
        {
            deserializeJson(doc, headerData);
            JsonObject companyData =  doc.as<JsonObject>();

            strcpy(ticker->strSymbol, companyData["ticker"]);
            strcpy(ticker->strCompanyName, companyData["name"]);
            strcpy(ticker->strExchangeName, companyData["exchange"]);
            strcpy(ticker->strCurrency, companyData["currency"]);
            ticker->marketCap         = companyData["marketCapitalization"].as<float>();
            ticker->sharesOutstanding = companyData["shareOutstanding"].as<float>();

            debugI("Got ticker header: sym %s Company %s, Exchange %s", ticker->strSymbol, ticker->strCompanyName, ticker->strExchangeName);
            http.end();
            return true;
        }
    }

    /**
     * @brief Get the Stock Data object
     * 
     * @return true 
     * @return false 
     */
    bool getStockData(StockTicker *ticker)
    {
        HTTPClient http;
        String tickerValue = ticker->strSymbol;

        String url = "https://finnhub.io/api/v1/quote"
            "?symbol=" + tickerValue  + "&token=" + urlEncode(g_ptrSystem->DeviceConfig().GetStockTickerAPIKey());

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
            
            AllocatedJsonDocument jsonDoc(256);
            String stockHeader = http.getString();
            debugI("Stock Data: %s", stockHeader.c_str());
            if (stockHeader.equals("{}")) 
            {
                ticker->currentPrice      = 0.0;
                ticker->change            = 0.0;
                ticker->percentChange     = 0.0;
                ticker->highPrice         = 0.0;
                ticker->lowPrice          = 0.0;
                ticker->openPrice         = 0.0;
                ticker->prevClosePrice    = 0.0;
                ticker->sampleTime        = 0.0;
                ticker->sharesOutstanding = 0.0;

                debugW("Bad ticker symbol: '%s'", ticker->strSymbol);
                http.end();
                return false;
            }
            else
            {
                deserializeJson(jsonDoc, stockHeader);
                JsonObject stockData =  jsonDoc.as<JsonObject>();

                // Once we have a non-zero temp we can start displaying things
                if (0 < jsonDoc["c"])
                    dataReady = true;


                ticker->currentPrice      = stockData["c"].as<float>();
                ticker->change            = stockData["d"].as<float>();
                ticker->percentChange     = stockData["dp"].as<float>();
                ticker->highPrice         = stockData["h"].as<float>();
                ticker->lowPrice          = stockData["l"].as<float>();
                ticker->openPrice         = stockData["o"].as<float>();
                ticker->prevClosePrice    = stockData["pc"].as<float>();
                ticker->sampleTime        = stockData["t"].as<long>();

                debugI("Got ticker data: Now %f Lo %f, Hi %f, Change %f", ticker->currentPrice, ticker->lowPrice, ticker->highPrice, ticker->change);
                http.end();
                return true;
            }
        }
        else
        {
            debugW("Error fetching Stock data for Ticker: %s", ticker->strSymbol);
            http.end();
            return false;
        }
    }

    void StockReader()
    {
        unsigned long msSinceLastCheck = millis() - msLastCheck;

        if (stockChanged || !msLastCheck
            || (!succeededBefore && msSinceLastCheck > STOCK_CHECK_ERROR_INTERVAL)
            || msSinceLastCheck > STOCK_CHECK_INTERVAL)
        {
            UpdateStock();
        }
    }

    /**
     * @brief Drive the checking of Stock Data
     * 
     */
    void UpdateStock()
    {
        if (!WiFi.isConnected())
        {
            debugW("Skipping Stock update, waiting for WiFi...");
            return;
        }

        // Track the check time so that we do not flood the net if we do not
        // have stocks to check or an API Key
        msLastCheck = millis();

        if (0 == numberTickers)
        {
            debugW("No Stock Tickers selected, so skipping check...");
            return;
        }

        if (g_ptrSystem->DeviceConfig().GetStockTickerAPIKey().isEmpty())
        {
            debugW("No Stock API Key, so skipping check...");
            return;
        }
            
        if (stockChanged)
            succeededBefore = false;

        stockChanged = false;

        for (int i = 0; i < numberTickers; i++) {
            if (updateTickerCode(&tickers[i])) {
                if (getStockData(&tickers[i]))
                {
                    debugW("Got Stock Data");
                    succeededBefore = true;
                }
                else
                {
                    debugW("Failed to get Stock Data");
                }
            } else {
                debugW("Failed to get Stock Ticker Info");
            }
        }
    }

protected:
    static constexpr int _jsonSize = LEDStripEffect::_jsonSize + 192;

    /**
     * @brief 
     * 
     * @return true 
     * @return false 
     */
    bool FillSettingSpecs() override
    {
        if (!LEDStripEffect::FillSettingSpecs())
            return false;

        // Lazily load this class' SettingSpec instances if they haven't been already
        if (mySettingSpecs.size() == 0)
        {
            mySettingSpecs.emplace_back(
                NAME_OF(stockTickerList),
                "Stock Symbols to Show",
                "The list of valid Stock Symbol to show, seperated by commas.  May be from any exchange.",
                SettingSpec::SettingType::String
            );
        }

        // Add our SettingSpecs reference_wrappers to the base set provided by LEDStripEffect
        _settingSpecs.insert(_settingSpecs.end(), mySettingSpecs.begin(), mySettingSpecs.end());

        return true;
    }

public:
    /**
     * @brief Construct a new Pattern Stock Ticker object
     * 
     */
    PatternStockTicker() : LEDStripEffect(EFFECT_MATRIX_STOCK_TICKER, "Stock")
    {
     
        stockTickerList  = DEFAULT_STOCK_TICKER;
        stockChanged     = true;
        setupDummyTickers();
    }

    /**
     * @brief Construct a new Pattern Stock Ticker object
     * 
     * @param jsonObject 
     */
    PatternStockTicker(const JsonObjectConst&  jsonObject) : LEDStripEffect(jsonObject)
    {
            
        stockTickerList  = DEFAULT_STOCK_TICKER;

        if (jsonObject.containsKey("stk")) {
            stockTickerList = jsonObject["stk"].as<String>();
        }
        stockChanged     = true;
        setupDummyTickers();
    }

    /**
     * @brief Destroy the Pattern Stock Ticker object
     * 
     */
    ~PatternStockTicker()
    {
        g_ptrSystem->NetworkReader().CancelReader(readerIndex);
    }

    /**
     * @brief Placeholder function until I get my string parser set up.
     * This creates three default tickert for Apple, MicroSoft and IBM.
     * Then links them in a double circular linked list.
     * 
     */
    void setupDummyTickers()
    {
        strcpy(tickers[0].strSymbol, "AAPL");
        tickers[0].next = &tickers[1];
        tickers[0].prev = &tickers[2];
        strcpy(tickers[1].strSymbol, "IBM");
        tickers[1].next = &tickers[2];
        tickers[1].prev = &tickers[0];
        strcpy(tickers[2].strSymbol, "MSFT");
        tickers[2].next = &tickers[0];
        tickers[2].prev = &tickers[1];
        currentTicker = tickers;

    }

    /**
     * @brief 
     * 
     * @param jsonObject 
     * @return true 
     * @return false 
     */
    bool SerializeToJSON(JsonObject& jsonObject) override
    {
        StaticJsonDocument<_jsonSize> jsonDoc;

        JsonObject root = jsonDoc.to<JsonObject>();
        LEDStripEffect::SerializeToJSON(root);

        jsonDoc["stk"] = stockTickerList;

        return jsonObject.set(jsonDoc.as<JsonObjectConst>());
    }

    /**
     * @brief Initialize the LED Strip Effect class and register the Network Reader task
     * 
     * @param gfx 
     * @return true 
     * @return false 
     */
    bool Init(std::vector<std::shared_ptr<GFXBase>>& gfx) override
    {
        if (!LEDStripEffect::Init(gfx))
            return false;

        readerIndex = g_ptrSystem->NetworkReader().RegisterReader([this] { StockReader(); }, STOCK_READER_INTERVAL, true);

        return true;
    }

    /**
     * @brief Perform the actual drawing of the current stock ticker data
     * 
     */
    void Draw() override
    {
        unsigned long msSinceLastCheck = millis() - msLastUpdate;

        if (msSinceLastCheck >= STOCK_DISPLAY_INTERVAL)
        {
            msLastUpdate = millis() ;
            currentTicker = currentTicker->next;
            currentOffset = 0;
        // } else {
        //     if (msSinceLastCheck % 500 == 0) {
        //         currentOffset++;
        //     }
        }

        DrawTicker(currentTicker, currentOffset);
        if (currentOffset > 0) {
            DrawTicker(currentTicker->next, (currentOffset-64));
        }
    }

    /**
     * @brief Draw the specified ticket data at the proper offset on the panel
     * 
     * @param ticker 
     * @param offset 
     */
    void DrawTicker(StockTicker *ticker, int offset) 
    {
        const int fontHeight = 7;
        const int fontWidth  = 5;
        const int xHalf      = (MATRIX_WIDTH / 2 - 1) + offset;

        g()->fillScreen(BLACK16);
        g()->fillRect(0, 0, MATRIX_WIDTH, MATRIX_HEIGHT, g()->to16bit(CRGB(0,0,128)));

        g()->setFont(&Apple5x7);

 
        // Print the Company name

        int x = offset;
        int y = fontHeight + 1;
        g()->setCursor(x, y);
        g()->setTextColor(WHITE16);

        if (NULL == ticker) {
            // Tell the user there is no stocks selected and bail
            g()->print("No Stocks");
            return;
        }

        String showCompany = strlen(ticker->strCompanyName) == 0 ? ticker->strSymbol : ticker->strCompanyName;
        showCompany.toUpperCase();
        if (g_ptrSystem->DeviceConfig().GetStockTickerAPIKey().isEmpty())
        {
            // Tell the user there is no API Key and bail
            g()->print("No API Key");
            return;
        }
        else
        {
            g()->print(showCompany.substring(0, (MATRIX_WIDTH - 2 * fontWidth)/fontWidth));
        }

        // Display the Stock Price, right-justified

        if (dataReady)
        {
            String strPrice(ticker->currentPrice);
            x = (MATRIX_WIDTH - fontWidth * strPrice.length()) + offset;
            g()->setCursor(x, y);
            if (ticker->change > 0.0) {
                g()->setTextColor(GREEN16);
            } else if (ticker->change < 0.0) {
                g()->setTextColor(RED16);
            } else {
                g()->setTextColor(WHITE16);
            }                                                                                                                                                                                                                                                                                                                                      g()->print(strPrice);
        }

        // Draw the separator lines

        y+=1;

        g()->drawLine(0, y, MATRIX_WIDTH-1, y, CRGB(0,0,128));
        g()->drawLine(xHalf + offset, y, xHalf + offset, MATRIX_HEIGHT-1, CRGB(0,0,128));
        y+=2 + fontHeight;

        // Draw the temperature in lighter white

        if (dataReady)
        {
            g()->setTextColor(g()->to16bit(CRGB(192,192,192)));
            String strHi( ticker->highPrice);
            String strLo( ticker->lowPrice);

            // Draw today's HI and LO temperatures

            x = (xHalf - fontWidth * strHi.length()) + offset;
            y = MATRIX_HEIGHT - fontHeight;
            g()->setCursor(x,y);
            g()->print(strHi);
            x = (xHalf - fontWidth * strLo.length()) + offset;
            y+= fontHeight;
            g()->setCursor(x,y);
            g()->print(strLo);

            // Draw tomorrow's HI and LO temperatures

            strHi = String(ticker->openPrice);
            strLo = String(ticker->prevClosePrice);
            x = (MATRIX_WIDTH - fontWidth * strHi.length()) + offset;
            y = MATRIX_HEIGHT - fontHeight;
            g()->setCursor(x,y);
            g()->print(strHi);
            x = (MATRIX_WIDTH - fontWidth * strLo.length()) + offset;
            y+= fontHeight;
            g()->setCursor(x,y);
            g()->print(strLo);
        }
    }

     /**
     * @brief Update the JSON Object with our current setting values
     * 
     * @param jsonObject 
     * @return true 
     * @return false 
     */
    bool SerializeSettingsToJSON(JsonObject& jsonObject) override
    {
        StaticJsonDocument<_jsonSize> jsonDoc;
        auto rootObject = jsonDoc.to<JsonObject>();

        LEDStripEffect::SerializeSettingsToJSON(jsonObject);

        jsonDoc[NAME_OF(stockTickerList)] = stockTickerList;

        assert(!jsonDoc.overflowed());
        
        return jsonObject.set(jsonDoc.as<JsonObjectConst>());
    }

    /**
     * @brief Set the Setting for this object
     * 
     * @param name Name of setting
     * @param value Value of setting
     * @return true if setting name processed
     * @return false if setting name unrecognized
     */
    bool SetSetting(const String& name, const String& value) override
    {
        if (name == NAME_OF(stockTickerList) && stockTickerList != value)
        {
            stockChanged = true;
            dataReady = false;
        }
        RETURN_IF_SET(name, NAME_OF(stockTickerList), stockTickerList, value);

        return LEDStripEffect::SetSetting(name, value);
    }
};

#endif
