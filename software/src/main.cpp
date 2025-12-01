/*
NOTE: To use this:
 - Rename "exampleWifiCredentials.h" to "wifiCredentials.h" and update the SSID and 
   password to those required for your wifi network.
 - Change POST_CODE to your post code
 - Change GMT_OFFSET_SEC and DAYLIGHT_OFFSET_SEC to values for your location
*/
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <ArduinoJson.h>
#include <limits.h>

// Wifi credentials recorded in this file
#include "wifiCredentials.h"

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

/************************
 * Variables
 ************************/

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

String getLocalTimeString(const char* format) {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    return "Failed to obtain time";
  }
  char timeString[100];
  strftime(timeString, 100, format, &timeinfo);
  return String(timeString);
}

bool willULongAdditionOverflow(unsigned long a, unsigned long b) {
    return b > ULONG_MAX - a;
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

  // Init and get the time
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  Serial.println(getLocalTimeString(API_DATE_FORMAT));

}

void loop() {

  static unsigned long prevMillis = 0;

  unsigned long currentMillis = millis();
  if (!willULongAdditionOverflow(currentMillis, REFRESH_PERIOD_MS) && currentMillis >= prevMillis + REFRESH_PERIOD_MS) {
    prevMillis = currentMillis;

    // If the WiFi connection status has changed, let us know and wait for reconnect
    if (WiFi.status() != WL_CONNECTED) {
      connectWiFi();
    }

    String nowString = getLocalTimeString(API_DATE_FORMAT);
    HTTPClient client;
    String apiURL = "https://api.carbonintensity.org.uk/regional/intensity/" + nowString + "/fw24h/postcode/" + POST_CODE;
    client.begin(apiURL);
    int httpCode = client.GET();
    if (httpCode == 200) {
      String payload = client.getString();
      JsonDocument jsonDoc;
      DeserializationError error = deserializeJson(jsonDoc, payload);

      if (error) {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.c_str());
        return;
      }

      JsonObject data = jsonDoc["data"];

      int data_regionid = data["regionid"]; // 14
      const char* data_dnoregion = data["dnoregion"]; // "UKPN South East"
      const char* data_shortname = data["shortname"]; // "South East England"

      for (JsonObject data_data_item : data["data"].as<JsonArray>()) {

        const char* data_data_item_from = data_data_item["from"]; // "2025-11-29T13:00Z", "2025-11-29T13:30Z", ...
        const char* data_data_item_to = data_data_item["to"]; // "2025-11-29T13:30Z", "2025-11-29T14:00Z", ...

        int data_data_item_intensity_forecast = data_data_item["intensity"]["forecast"]; // 65, 66, 60, 61, 76, ...
        // const char* data_data_item_intensity_index = data_data_item["intensity"]["index"]; // "low", "low", ...

        // for (JsonObject data_data_item_generationmix_item : data_data_item["generationmix"].as<JsonArray>()) {

        //   const char* data_data_item_generationmix_item_fuel = data_data_item_generationmix_item["fuel"];
        //   float data_data_item_generationmix_item_perc = data_data_item_generationmix_item["perc"]; // 0, 0, 53.4, ...

        // }

      }

      // Extract the next 12 hours of intensities into an array
      // Work out how many lights I have in the ring and how many minutes each light corresponds to
      // Use linear interpolation to map intensity values to the led array size
      // Map the intesity values to a colour scale for the LEDs from red at the top end, white in the middle, green at the bottom end
      // Assign those colour values to the LEDs
      // Paint the current time LED blue

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

