/*
NOTE: To use this:
 - Rename "exampleWifiCredentials.h" to "wifiCredentials.h" and update the SSID and 
   password to those required for your wifi network.
 - Change POST_CODE to your post code
 - Change GMT_OFFSET_SEC and DAYLIGHT_OFFSET_SEC to values for your location
 - Update LED_PIN to the pin you have connected the LED ring to 
*/
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <ArduinoJson.h>
#include <limits.h>
#include <map>
#include <Adafruit_NeoPixel.h>

// Wifi credentials recorded in this file
#include "wifiCredentials.h"

/************************
 * Pins
 ************************/
#define LED_PIN     6 // Pin where the LED ring is connected

/************************
 * Constants
 ************************/
// The characters used for the "waiting" spinner on the display
static const char WAITING_CHARS[] = "-\\|/";
// Post code used for API check
static const String POST_CODE = "KT6";
// The NTP server to sync time from
static const char* NTP_SERVER = "pool.ntp.org";
// The local offset from GMT in seconds
static const long GMT_OFFSET_SEC = 0;
// The local offset for daylight savings time in seconds
static const int DAYLIGHT_OFFSET_SEC = 0;

// Datetime in ISO8601 format YYYY-MM-DDThh:mmZ e.g. 2017-08-25T12:35Z
static const char* API_DATE_FORMAT = "%FT%RZ";
// Period between data refreshes - 30 minutes in milliseconds
static const unsigned long REFRESH_PERIOD_MS = 30 * 60 * 1000;

// Number of LEDs in the ring
static const int LED_COUNT = 100;
// Time per LED in seconds
static const int TIME_PER_LED_SEC = (12 * 60 * 60) / LED_COUNT;

/************************
 * Variables
 ************************/
// The LED Strip
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB);
// The Colour scale array
uint32_t colourScale[256];

/************************
 * Utility functions
 ************************/
// Format Wi-Fi status to something human readable
String formatWiFiStatus(wl_status_t status) {
  switch (status) {
    case WL_NO_SHIELD:
      return "WL_NO_SHIELD";
    case WL_IDLE_STATUS:
      return "WL_IDLE_STATUS";
    case WL_NO_SSID_AVAIL:
      return "WL_NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED:
      return "WL_SCAN_COMPLETED";
    case WL_CONNECTED:
      return "WL_CONNECTED";
    case WL_CONNECT_FAILED:
      return "WL_CONNECT_FAILED";
    case WL_CONNECTION_LOST:
      return "WL_CONNECTION_LOST";
    case WL_DISCONNECTED:
      return "WL_DISCONNECTED";
    default:
      return "UNKNOWN STATUS: " + status;
  }
}

void connectWiFi() {
  // Connect and let us know how that goes
  wl_status_t status;
  unsigned long prevMillis = millis();
  while (status != WL_CONNECTED) {
    // If the status has changed, let us know
    if (WiFi.status() != status) {
      status = WiFi.status();
      Serial.println();
      Serial.println("WiFi status change to: " + formatWiFiStatus(status));
      Serial.print("Connecting to wiFi");
    }
    // If it's been 500 ms since last time we checked, let us know we're still alive
    if (millis() - prevMillis >= 500) {
      Serial.print(".");
      prevMillis = millis();
    }
  }
  Serial.println();
  Serial.println("Connected. IP: " + WiFi.localIP().toString());
}

// Format a time string from a tm struct
String createFormattedTimeString(const tm* time, const char* format) {
  char timeString[100];
  strftime(timeString, 100, format, time);
  return String(timeString);
}

// Populates the colour scale array from green (low) to white (mid) to red (high)
void populateColourScale() {
  for (int i = 0; i < 256; i++) {
    if (i < 128) {
      // Green to White
      uint8_t green = 255;
      uint8_t red = map(i, 0, 127, 0, 255);
      uint8_t blue = map(i, 0, 127, 0, 255);
      colourScale[i] = strip.Color(red, green, blue);
    } else {
      // White to Red
      uint8_t red = 255;
      uint8_t green = map(i, 128, 255, 255, 0);
      uint8_t blue = map(i, 128, 255, 255, 0);
      colourScale[i] = strip.Color(red, green, blue);
    }
  }
}

/************************
 * Entry point methods
 ************************/
void setup() {

  // Set up serial interface comms
  Serial.begin(115200);
  while(!Serial); // Wait for initialisation of the serial interface

  // Set up the Wifi connection - credentials pulled from included wifiCredentials.h (git ignored)
  Serial.println("Starting up WiFi interface");
  WiFi.begin(ssid, password);

  // Establish WiFi connection
  connectWiFi();

  // Initialise the time library
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  struct tm currentTime;
  if(!getLocalTime(&currentTime)) {
    Serial.println("Failed to obtain time");
  } else {
    Serial.println("Current time: " + createFormattedTimeString(&currentTime, API_DATE_FORMAT));
  }

  // Set up the LED arrays and strip
  populateColourScale();
  strip.begin();
  strip.show(); // Initialize all pixels to 'off'

}

void loop() {

  static unsigned long prevMillis = millis() - REFRESH_PERIOD_MS;

  unsigned long currentMillis = millis();
  if (currentMillis - prevMillis >= REFRESH_PERIOD_MS) {
    prevMillis += REFRESH_PERIOD_MS;

    // If the WiFi connection status has changed, let us know and wait for reconnect
    if (WiFi.status() != WL_CONNECTED) {
      connectWiFi();
    }

    // Get the current time
    struct tm currentTime;
    if(!getLocalTime(&currentTime)) {
      Serial.println("Failed to obtain time");
      return;
    }
    time_t timein12Hours = mktime(&currentTime) + 12 * 60 * 60;

    // Format the current time for the API request
    String nowString = createFormattedTimeString(&currentTime, API_DATE_FORMAT); 
    
    HTTPClient client;
    String apiURL = "https://api.carbonintensity.org.uk/regional/intensity/" + nowString + "/fw24h/postcode/" + POST_CODE;
    client.begin(apiURL);
    int httpCode = client.GET();
    if (httpCode == 200) {

      // Extract the next 12 hours of intensities into a map

      String payload = client.getString();
      JsonDocument jsonDoc;
      DeserializationError error = deserializeJson(jsonDoc, payload);

      if (error) {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.c_str());
        return;
      }

      JsonObject data = jsonDoc["data"];

      struct intensityDatum {
        time_t to;
        int intensity;
      };
      std::map<unsigned long, intensityDatum> intensityMap;
      int minIntensity = INT_MAX;
      int maxIntensity = INT_MIN;
      for (JsonObject data_data_item : data["data"].as<JsonArray>()) {

        const char* data_data_item_from = data_data_item["from"]; // "2025-11-29T13:00Z", "2025-11-29T13:30Z", ...
        time_t fromTime = 0;
        struct tm tm_from = {};
        strptime(data_data_item_from, API_DATE_FORMAT, &tm_from);

        // The returned array is ordered, so if we're over the 12 hour window we can stop processing
        fromTime = mktime(&tm_from);
        if (fromTime > timein12Hours) {
          break;
        }

        const char* data_data_item_to = data_data_item["to"]; // "2025-11-29T13:30Z", "2025-11-29T14:00Z", ...
        int data_data_item_intensity_forecast = data_data_item["intensity"]["forecast"]; // 65, 66, 60, 61, 76, ...
        
        // Translate the times to time_t timestamps and store in map
        time_t toTime = 0;
        struct tm tm_to = {};
        strptime(data_data_item_to, API_DATE_FORMAT, &tm_to);
        toTime = mktime(&tm_to);
        intensityMap[fromTime] = {toTime, data_data_item_intensity_forecast};
        
        // Keep track of min and max intensity values so we can define our colour scale later
        if (data_data_item_intensity_forecast < minIntensity) {
          minIntensity = data_data_item_intensity_forecast;
        }
        if (data_data_item_intensity_forecast > maxIntensity) {
          maxIntensity = data_data_item_intensity_forecast;
        }
        
      }

      // Extract the current 12 hour time from the current time object in seconds
      unsigned long currentTime12HourSecs = currentTime.tm_hour % 12 * 3600 + currentTime.tm_min * 60 + currentTime.tm_sec;
      
      // Given the lED ring represents a 12 hour clock, work out which LED index represents the current time
      int currentTimeLEDIndex = map(currentTime12HourSecs, 0, 12 * 3600, 0, LED_COUNT - 1);

      // Paint the current time LED blue
      strip.setPixelColor(currentTimeLEDIndex, strip.Color(0, 0, 255));

      // Use linear interpolation to map intensity values from the half-hourly data to per-led data for the 12 hour period
      time_t currentTimeT = mktime(&currentTime);
      for (int ledIndex = 1; ledIndex < LED_COUNT; ledIndex++) {
        time_t ledTime = currentTimeT + ledIndex * TIME_PER_LED_SEC;
        int intensity = 0;
        // Find the two intensity data points that bracket this time
        auto upper = intensityMap.lower_bound(ledTime);
        if (upper == intensityMap.end()) {
          // Out of range - use the last known value
          intensity = std::prev(upper)->second.intensity;
        } else if (upper == intensityMap.begin()) {
          // Out of range - use the first known value
          intensity = upper->second.intensity;
        } else {
          auto lower = std::prev(upper);
          // Do linear interpolation
          time_t t0 = lower->first;
          time_t t1 = upper->first;
          int i0 = lower->second.intensity;
          int i1 = upper->second.intensity;
          intensity = i0 + (i1 - i0) * (ledTime - t0) / (t1 - t0);
        }
        // Map the intensity to a colour index
        int colourIndex = map(intensity, minIntensity, maxIntensity, 0, 255);
        colourIndex = constrain(colourIndex, 0, 255);
        
        // Set the colour on the LED - starting from the current time LED index
        strip.setPixelColor((currentTimeLEDIndex + ledIndex) % LED_COUNT, colourScale[colourIndex]);
      }
      
      // Show the updated LED ring
      strip.show();
      // Clear off the JSON document to free memory
      jsonDoc.clear();

    } else if (httpCode == 400) {

      // Bad request
      Serial.println("Bad request (400)");
      Serial.println("API time string: " + nowString);
      Serial.println("Post code: " + POST_CODE);
      Serial.println("API request URL: " + apiURL);

    } else if (httpCode == 500) {

      // Internal Server Error

    } else {

      // Failed for unexpected reason
      Serial.println("Error making API request: " + httpCode);
      
    }
  }
}

