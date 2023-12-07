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
//   Gets the Stock Data for a given set of comman seperated 
//   Stock Ticker Symbols
//
// History: Jun-04-2023     Gatesm      Adapted from Weather code
//          Aug-15-2030     Gatesm      Modified to support more than one stock symbol
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

// Default stock Ticker symbols for Apple, IBM, and Microsoft 
#define DEFAULT_STOCK_TICKERS       "AAPL,IBM,MSFT"
#define MAX_STOCK_TICKER            10

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
    char _strSymbol[12];
    char _strCompanyName[32];
    char _strExchangeName[32];
    char _strCurrency[16];

    bool _isValid               = false;

    float  _marketCap           = 0.0f;
    float  _sharesOutstanding   = 0.0f;

    float  _currentPrice        = 0.0f;
    float  _change              = 0.0f;
    float  _percentChange       = 0.0f;
    float  _highPrice           = 0.0f;
    float  _lowPrice            = 0.0f;
    float  _openPrice           = 0.0f;
    float  _prevClosePrice      = 0.0f;
    long   _sampleTime          = 0l;
    StockTicker* _prevTicker    = NULL;
    StockTicker* _nextTicker    = NULL;
  
};

/**
 * @brief This class implements the stock ticker effect
 * it will show a repeating list of stock symbols,
 * their high and low values
 * 
 */
class PatternStockTicker : public LEDStripEffect
{

private:

    // @todo rework this for dynamic stock list
    std::vector<StockTicker, psram_allocator<StockTicker>> _tickers = {};
    StockTicker *_currentTicker    = NULL;

    bool   _stockChanged            = false;
    size_t _currentOffset           = 0;
    String _stockTickerList         = DEFAULT_STOCK_TICKERS;
    size_t _readerIndex             = std::numeric_limits<size_t>::max();
    unsigned long _msLastCheck      = 0;
    bool _succeededBefore           = false;
    unsigned long _msLastDrawTime   = 0;

    static std::vector<SettingSpec, psram_allocator<SettingSpec>> mySettingSpecs;
    StockTicker _emptyTicker;

    /**
     * @brief The stock ticker is obviously stock data, 
     * and we don't want text overlaid on top of our text
     * 
     * @return bool - false 
     */
    bool ShouldShowTitle() const
    {
        return false;
    }

    /**
     * @brief How many frames per second do we need?
     * 
     * @return size_t - 10 FPS
     */
    size_t DesiredFramesPerSecond() const override
    {
        return 10;
    }

    /**
     * @brief Does this effect need double buffering?
     * 
     * @return bool - true 
     */
    bool RequiresDoubleBuffering() const override
    {
        return true;
    }

    /**
     * @brief Process the list of stock symbols 
     * and build the data structures in PSRAM to hold the data
     * 
     * @todo This method stills need propper implementation
     * 
     * @param newSymbols String, comma seperated
     * @return int - number of symbls found
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
     * @brief Retrieve the 'static' information about the supplied
     * stock symbol
     * 
     * @param ticker Pointer to a StockTicker object
     * @return bool - true if the symbol is found by the API
     */
    bool updateTickerCode(StockTicker *ticker)
    {
        HTTPClient http;
        String url;
        bool dataFound = false;

        url = "https://finnhub.io/api/v1/stock/profile2"
              "?symbol=" + urlEncode(ticker->_strSymbol) + "&token=" + urlEncode(g_ptrSystem->DeviceConfig().GetStockTickerAPIKey());

        http.begin(url);
        int httpResponseCode = http.GET();

        if (httpResponseCode > 0)
        {
            /* Sample returned data
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
                strcpy(ticker->_strCompanyName, "Bad Symbol");
                strcpy(ticker->_strExchangeName, "");
                strcpy(ticker->_strCurrency, "");
                ticker->_isValid           = false;
                ticker->_marketCap         = 0.0;
                ticker->_sharesOutstanding = 0.0;

                debugW("Bad ticker symbol: '%s'", ticker->_strSymbol);
            }
            else
            {
                deserializeJson(doc, headerData);
                JsonObject companyData =  doc.as<JsonObject>();
                dataFound = true;

                strcpy(ticker->_strSymbol, companyData["ticker"]);
                strcpy(ticker->_strCompanyName, companyData["name"]);
                strcpy(ticker->_strExchangeName, companyData["exchange"]);
                strcpy(ticker->_strCurrency, companyData["currency"]);
                ticker->_marketCap         = companyData["marketCapitalization"].as<float>();
                ticker->_sharesOutstanding = companyData["shareOutstanding"].as<float>();

                debugI("Got ticker header: sym %s Company %s, Exchange %s", ticker->_strSymbol, ticker->_strCompanyName, ticker->_strExchangeName);
            }
        }
        else
        {
            debugE("Error (%d) fetching company data for ticker: %s", httpResponseCode, ticker->_strSymbol);
        }


        http.end();
        return dataFound;
    }

    /**
     * @brief Get the price data for the supplied stock symbol
     * 
     * @param ticker Pointer to a StockTicker object
     * @return bool - true if we successfully pulled data for the symbol
     */
    bool getStockData(StockTicker *ticker)
    {
        HTTPClient http;
        String tickerValue = ticker->_strSymbol;
        bool dataFound = false;

        String url = "https://finnhub.io/api/v1/quote"
            "?symbol=" + tickerValue  + "&token=" + urlEncode(g_ptrSystem->DeviceConfig().GetStockTickerAPIKey());

        http.begin(url);
        int httpResponseCode = http.GET();
        
        if (httpResponseCode > 0)
        {
            /* Sample returned data
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
            String apiData = http.getString();
            debugI("Stock Data: %s", apiData.c_str());
            if (apiData.equals("{}")) 
            {
                ticker->_isValid           = false;
                ticker->_currentPrice      = 0.0;
                ticker->_change            = 0.0;
                ticker->_percentChange     = 0.0;
                ticker->_highPrice         = 0.0;
                ticker->_lowPrice          = 0.0;
                ticker->_openPrice         = 0.0;
                ticker->_prevClosePrice    = 0.0;
                ticker->_sampleTime        = 0.0;
                ticker->_sharesOutstanding = 0.0;

                debugW("Bad ticker symbol: '%s'", ticker->_strSymbol);
            }
            else
            {
                deserializeJson(jsonDoc, apiData);
                JsonObject stockData =  jsonDoc.as<JsonObject>();

                // Once we have a non-zero current price the data is valid
                if (0 < stockData["c"])
                {
                    dataFound = true;

                    ticker->_isValid           = true;
                    ticker->_currentPrice      = stockData["c"].as<float>();
                    ticker->_change            = stockData["d"].as<float>();
                    ticker->_percentChange     = stockData["dp"].as<float>();
                    ticker->_highPrice         = stockData["h"].as<float>();
                    ticker->_lowPrice          = stockData["l"].as<float>();
                    ticker->_openPrice         = stockData["o"].as<float>();
                    ticker->_prevClosePrice    = stockData["pc"].as<float>();
                    ticker->_sampleTime        = stockData["t"].as<long>();

                    debugI("Got ticker data: Now %f Lo %f, Hi %f, Change %f", ticker->_currentPrice, ticker->_lowPrice, ticker->_highPrice, ticker->_change);
                }
            }
        }
        else
        {
            debugE("Error (%d) fetching Stock data for Ticker: %s", httpResponseCode, ticker->_strSymbol);
        }

        http.end();
        return dataFound;
    }

    /**
     * @brief The hook called from the network thread to
     * Update the stock data 
     * 
     */
    void StockReader()
    {
        unsigned long msSinceLastCheck = millis() - _msLastCheck;

        /*
         * if the symbols have changed
         * or last check time is zero (first run)
         * or we have not had a succesfull data pull and last check interval is greater than the error interval
         * or last check interval is greater than the check interval
         */
        if (_stockChanged || !_msLastCheck
            || (!_succeededBefore && msSinceLastCheck > STOCK_CHECK_ERROR_INTERVAL)
            || msSinceLastCheck > STOCK_CHECK_INTERVAL)
        {
            // Track the check time so that we do not flood the net if we do not
            // have stocks to check or an API Key
            _msLastCheck = millis();
            UpdateStock();
        }
    }

    /**
     * @brief Drive the actual checking of Stock Data
     * 
     */
    void UpdateStock()
    {
        if (!WiFi.isConnected())
        {
            debugW("Skipping Stock update, waiting for WiFi...");
            return;
        }

        if (_tickers.empty())
        {
            debugW("No Stock Tickers selected, so skipping check...");
            return;
        }

        if (g_ptrSystem->DeviceConfig().GetStockTickerAPIKey().isEmpty())
        {
            debugW("No Stock API Key, so skipping check...");
            return;
        }
            
        if (_stockChanged)
            _succeededBefore = false;

        for (size_t i = 0; i < _tickers.size(); i++) {
            bool doUpdateStock = true;
            if (_stockChanged)
                doUpdateStock = updateTickerCode(&_tickers[i]);
            if (doUpdateStock)
                if (getStockData(&_tickers[i]))
                    _succeededBefore = true;
        }
        _stockChanged = false;
    }

protected:
    static constexpr int _jsonSize = LEDStripEffect::_jsonSize + 192;

    /**
     * @brief 
     * 
     * @return bool - true if the settings were saved
     * @return false 
     */
    bool FillSettingSpecs() override
    {
        bool settingsSaved = false;

        // Save the parent class settings
        if (LEDStripEffect::FillSettingSpecs()) 
        {
            // Lazily load this class' SettingSpec instances if they haven't been already
            if (mySettingSpecs.size() == 0)
            {
                mySettingSpecs.emplace_back(
                    NAME_OF(_stockTickerList),
                    "Stock Symbols to Show",
                    "The list of valid Stock Symbol to show, seperated by commas.  May be from any exchange.",
                    SettingSpec::SettingType::String
                );
            }

            // Add our SettingSpecs reference_wrappers to the base set provided by LEDStripEffect
            _settingSpecs.insert(_settingSpecs.end(), mySettingSpecs.begin(), mySettingSpecs.end());

            settingsSaved = true;
        }
        return settingsSaved;
    }

public:
    /**
     * @brief Construct a new Pattern Stock Ticker object
     * 
     */
    PatternStockTicker() : LEDStripEffect(EFFECT_MATRIX_STOCK_TICKER, "Stock")
    {
     
        _stockTickerList  = DEFAULT_STOCK_TICKERS;
        _stockChanged     = true;
        setupDummyTickers();
    }

    /**
     * @brief Construct a new Pattern Stock Ticker object
     * 
     * @param jsonObject 
     */
    PatternStockTicker(const JsonObjectConst&  jsonObject) : LEDStripEffect(jsonObject)
    {
            
        _stockTickerList  = DEFAULT_STOCK_TICKERS;

        if (jsonObject.containsKey(PTY_STOCK_TICKERS)) {
            _stockTickerList = jsonObject[PTY_STOCK_TICKERS].as<String>();
        }
        _stockChanged     = true;
        setupDummyTickers();
    }

    /**
     * @brief Destroy the Pattern Stock Ticker object
     * 
     */
    ~PatternStockTicker()
    {
        g_ptrSystem->NetworkReader().CancelReader(_readerIndex);
        cleanUpTickerData();
    }

    /**
     * @brief Placeholder function until I get my string parser set up.
     * This creates three default tickert for Apple, MicroSoft and IBM.
     * Then links them in a double circular linked list.
     * 
     */
    void setupDummyTickers()
    {
        cleanUpTickerData();
        _tickers.reserve(3);

        strcpy(_emptyTicker._strSymbol, "AAPL");
        _tickers.push_back(_emptyTicker);
        strcpy(_emptyTicker._strSymbol, "IBM");
        _tickers.push_back(_emptyTicker);
        strcpy(_emptyTicker._strSymbol, "MSFT");
        _tickers.push_back(_emptyTicker);

        // fake double link list for now
        _tickers[0]._nextTicker = &_tickers[1];
        _tickers[0]._prevTicker = &_tickers[2];
        _tickers[1]._nextTicker = &_tickers[2];
        _tickers[1]._prevTicker = &_tickers[0];
        _tickers[2]._nextTicker = &_tickers[0];
        _tickers[2]._prevTicker = &_tickers[1];
        _currentTicker = &_tickers[0];

    }

    /**
     * @brief Free up the Ticket objects in PSRAM
     * 
     */
    void cleanUpTickerData()
    {
        _tickers.clear();
        _currentTicker = NULL;
    }

    /**
     * @brief populate the jsonObject with our settings
     * 
     * @param jsonObject 
     * @return bool - true if the json object is 
     * @return false 
     */
    bool SerializeToJSON(JsonObject& jsonObject) override
    {
        StaticJsonDocument<_jsonSize> jsonDoc;

        JsonObject root = jsonDoc.to<JsonObject>();
        LEDStripEffect::SerializeToJSON(root);

        jsonDoc[PTY_STOCK_TICKERS] = _stockTickerList;

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

        _readerIndex = g_ptrSystem->NetworkReader().RegisterReader([this] { StockReader(); }, STOCK_READER_INTERVAL, true);

        return true;
    }

    /**
     * @brief Perform the actual drawing of the current stock ticker data
     * 
     */
    void Draw() override
    {
        unsigned long msSinceLastCheck = millis() - _msLastDrawTime;

        if (msSinceLastCheck >= STOCK_DISPLAY_INTERVAL)
        {
            _msLastDrawTime = millis() ;
            if (NULL != _currentTicker)
                _currentTicker = _currentTicker->_nextTicker;
            _currentOffset = 0;
        }

        DrawTicker(_currentTicker, _currentOffset);
    }

    /**
     * @brief Draw the specified ticket data at the proper offset on the panel
     * 
     * @param ticker Pointer to the StockTicker object to draw
     * @param offset For scrolling. Not used at this time
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
        // debugI("company: %d, %d", x, y);

        if (NULL == ticker) {
            // Tell the user there is no stocks selected and bail
            g()->setTextColor(YELLOW16);
            g()->print("No Stocks");
            return;
        }

        if (g_ptrSystem->DeviceConfig().GetStockTickerAPIKey().isEmpty())
        {
            // Tell the user there is no API Key and bail
            g()->setTextColor(RED16);
            g()->print("No API Key");
            return;
        }

        // Display the company name if set otherwise the symbol


        String showCompany = strlen(ticker->_strCompanyName) == 0 ? ticker->_strSymbol : ticker->_strCompanyName;
        showCompany.toUpperCase();
        g()->print(showCompany.substring(0, (MATRIX_WIDTH - fontWidth)/fontWidth));

        // Display the Stock Price, right-justified 
        // set the color based on the direction of the last change

        if (ticker->_isValid)
        {
            String strPrice(ticker->_currentPrice, 2);
            x = (MATRIX_WIDTH - fontWidth * strPrice.length()) + offset;
            y +=1 + fontHeight;
            // debugI("price: %d, %d", x, y);
            g()->setCursor(x, y);
            if (ticker->_change > 0.0) {
                g()->setTextColor(GREEN16);
            } else if (ticker->_change < 0.0) {
                g()->setTextColor(RED16);
            } else {
                g()->setTextColor(WHITE16);
            }
            g()->print(strPrice);                                                                                                                                                                                                                                                                                                                                   g()->print(strPrice);
        }

        // Draw the separator lines

        y+=1;

        g()->drawLine(0, y, MATRIX_WIDTH-1, y, CRGB(0,0,128));
        g()->drawLine(xHalf + offset, y, xHalf + offset, MATRIX_HEIGHT-1, CRGB(0,0,128));
        y+=2 + fontHeight;

        // Draw the price data in lighter white

        if (ticker->_isValid)
        {
            g()->setTextColor(g()->to16bit(CRGB(192,192,192)));
            String strHi( ticker->_highPrice, 2);
            String strLo( ticker->_lowPrice, 2);

            // Draw current high and low price

            x = (xHalf - fontWidth * strHi.length()) + offset;
            y = MATRIX_HEIGHT - fontHeight;
            // debugI("high: %d, %d", x, y);
            g()->setCursor(x,y);
            g()->print(strHi);

            x = (xHalf - fontWidth * strLo.length()) + offset;
            y+= fontHeight;
            // debugI("low: %d, %d", x, y);
            g()->setCursor(x,y);
            g()->print(strLo);

            // Draw Open and Close price on the other side
            
            String strOpen = String(ticker->_openPrice, 2);
            String strClose = String(ticker->_prevClosePrice, 2);

            x = (MATRIX_WIDTH - fontWidth * strOpen.length()) + offset;
            y = MATRIX_HEIGHT - fontHeight;
            // debugI("open: %d, %d", x, y);
            g()->setCursor(x,y);
            g()->print(strOpen);

            x = (MATRIX_WIDTH - fontWidth * strClose.length()) + offset;
            y+= fontHeight;
            // debugI("close: %d, %d", x, y);
            g()->setCursor(x,y);
            g()->print(strClose);
        }
    }

    /**
     * @brief Update the JSON Object with our current setting values
     * 
     * @param jsonObject Reference to the settings JSON object
     * @return bool - true if json object is populated
     */
    bool SerializeSettingsToJSON(JsonObject& jsonObject) override
    {
        StaticJsonDocument<_jsonSize> jsonDoc;
        auto rootObject = jsonDoc.to<JsonObject>();

        LEDStripEffect::SerializeSettingsToJSON(jsonObject);

        jsonDoc[NAME_OF(_stockTickerList)] = _stockTickerList;

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
        if (name == NAME_OF(_stockTickerList) && _stockTickerList != value)
            _stockChanged = true;

        RETURN_IF_SET(name, NAME_OF(_stockTickerList), _stockTickerList, value);

        return LEDStripEffect::SetSetting(name, value);
    }
};

#endif
