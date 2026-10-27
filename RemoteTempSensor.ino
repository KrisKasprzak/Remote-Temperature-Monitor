/*

  Remote Temp Sensor -- log the temperature in a refrigerator with an ESP32 and watch it on a phone

  how it works
  1. the ESP32 joins your home WiFi (put your SSID and password below) and prints its IP address
     to the serial monitor
  2. open a browser on your phone and go to http://<that IP address>
  3. the ESP sends the web page stored in TempPage.h
  4. the web page asks the ESP for all the readings it has saved (/all) and plots them
  5. after that the page asks for just the latest reading (/latest) every few seconds and adds
     a new point to the plot whenever there is one

  measuring
  a DS18B20 is read every READ_INTERVAL ms (3 minutes) and saved as a whole number in an array
  the array holds MAX_READINGS values, when it is full the oldest reading is dropped
  each reading has a number (1, 2, 3...) that the page uses for the x axis

  required libraries
  OneWire            -- https://github.com/PaulStoffregen/OneWire
  DallasTemperature  -- https://github.com/milesburton/Arduino-Temperature-Control-Library

  compile options
  1. esp32 dev module
  2. upload speed 921600
  3. cpu speed 240 mhz
  partition scheme default

  wiring
  DS18B20 GND  -> ESP32 GND
  DS18B20 VDD  -> ESP32 3.3 V
  DS18B20 DATA -> ESP32 GPIO 4, plus a 4.7k resistor from DATA to 3.3 V
  the waterproof probe version works great, the thin cable fits under the refrigerator door seal

  The MIT License (MIT)

*/

#include <WiFi.h>               // standard library
#include <WebServer.h>          // standard library
#include <OneWire.h>            // https://github.com/PaulStoffregen/OneWire
#include <DallasTemperature.h>  // https://github.com/milesburton/Arduino-Temperature-Control-Library
#include "TempPage.h"           // .h file that stores the html page code

// #define DEBUG

// your home WiFi, fill these in
#define WIFI_SSID ""
#define WIFI_PASS ""

// sensor data pin
#define PIN_SENSOR 4

// true = Fahrenheit, false = Celsius
#define USE_FAHRENHEIT true

// time between readings, 3 minutes
#define READ_INTERVAL 180000UL

// readings kept on the ESP, 960 x 3 minutes = 48 hours
// each reading is 2 bytes so this can go much higher if you want a longer history
#define MAX_READINGS 960

// how often to check that the WiFi is still connected
#define WIFI_CHECK_INTERVAL 30000UL

OneWire oneWire(PIN_SENSOR);
DallasTemperature sensors(&oneWire);

// saved readings, used as a ring buffer
// Temps[Head] is the oldest reading, there are Count readings in total
int16_t Temps[MAX_READINGS];
int Head = 0;
int Count = 0;

// number of the latest reading, 1 for the first one, keeps counting up after the array is full
// the page uses it for the x axis and to know when it has missed readings
uint32_t ReadingNumber = 0;

uint32_t LastRead = 0;
uint32_t LastWiFiCheck = 0;

// the JSON text buffer for /latest, roughly 50 characters
char JSONTxt[96];

// gotta create a server
WebServer server(80);

void setup() {

  Serial.begin(115200);

  sensors.begin();
  if (sensors.getDeviceCount() == 0) {
    Serial.println("No DS18B20 found, check the wiring and the 4.7k pullup resistor");
  }
  // whole numbers are all we need, but 12 bit is the most accurate and the 750 ms wait
  // every 3 minutes doesn't hurt anything
  sensors.setResolution(12);

  ConnectWiFi();

  // page request, upon ESP getting / the web page will be sent
  server.on("/", SendWebsite);

  // the page asks for every saved reading when it first loads
  server.on("/all", SendAll);

  // then it asks for just the latest reading every few seconds
  server.on("/latest", SendLatest);

  server.onNotFound(SendWebsite);

  server.begin();

  // take the first reading right away instead of waiting 3 minutes
  TakeReading();
  LastRead = millis();
}

void loop() {

  // no matter what you must call this handleClient repeatedly--otherwise the web page
  // will not get any data
  server.handleClient();

  if ((millis() - LastRead) >= READ_INTERVAL) {
    LastRead += READ_INTERVAL;
    TakeReading();
  }

  // the router may drop us now and then, keep trying to get back on
  // readings keep getting saved while the WiFi is down
  if ((millis() - LastWiFiCheck) >= WIFI_CHECK_INTERVAL) {
    LastWiFiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi lost, reconnecting");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
  }
}

void ConnectWiFi() {

  if (strlen(WIFI_SSID) == 0) {
    Serial.println("WIFI_SSID is blank, add your network name and password at the top of the sketch");
  }

  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  // wait up to 20 seconds, if it doesn't connect keep going and let loop() retry
  // so the readings still get saved
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 20000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected, open http://");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Could not connect to WiFi, will keep trying");
  }
}

// reads the sensor and saves the temperature as a whole number
void TakeReading() {

  sensors.requestTemperatures();
  float tempC = sensors.getTempCByIndex(0);

  if (tempC == DEVICE_DISCONNECTED_C) {
    Serial.println("Sensor read failed, reading skipped");
    return;
  }

  float temp = USE_FAHRENHEIT ? tempC * 9.0f / 5.0f + 32.0f : tempC;
  int16_t value = (int16_t)lroundf(temp);

  if (Count < MAX_READINGS) {
    Temps[(Head + Count) % MAX_READINGS] = value;
    Count++;
  } else {
    // full, write over the oldest
    Temps[Head] = value;
    Head = (Head + 1) % MAX_READINGS;
  }
  ReadingNumber++;

  Serial.printf("Reading %lu: %d %s", (unsigned long)ReadingNumber, value, USE_FAHRENHEIT ? "F" : "C");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("   http://");
    Serial.print(WiFi.localIP());
  }
  Serial.println();
}

// code to send the main web page
// PAGE_MAIN is a large char defined in TempPage.h
void SendWebsite() {

  Serial.println("sending web page");

  server.send(200, "text/html", PAGE_MAIN);
}

// seconds until the next reading, lets the page show a countdown
uint32_t SecondsToNext() {
  uint32_t elapsed = millis() - LastRead;
  return elapsed >= READ_INTERVAL ? 0 : (READ_INTERVAL - elapsed) / 1000;
}

// sends every saved reading, oldest first
// {"u":"F","i":180,"s":95,"n":1234,"t":[38,37,38,...]}
// u = units, i = seconds between readings, s = seconds to the next reading
// n = number of the last reading in t, so the first one in t is number n - length + 1
// with 960 readings this is around 4 KB, so it is sent in pieces instead of one big String
void SendAll() {

  Serial.println("sending all readings");

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");

  char buf[512];
  int len = snprintf(buf, sizeof(buf), "{\"u\":\"%s\",\"i\":%lu,\"s\":%lu,\"n\":%lu,\"t\":[",
                     USE_FAHRENHEIT ? "F" : "C", READ_INTERVAL / 1000,
                     (unsigned long)SecondsToNext(), (unsigned long)ReadingNumber);

  for (int i = 0; i < Count; i++) {
    len += snprintf(buf + len, sizeof(buf) - len, i == 0 ? "%d" : ",%d", Temps[(Head + i) % MAX_READINGS]);
    // flush the buffer before it can run out of room
    if (len > (int)sizeof(buf) - 16) {
      server.sendContent(buf, len);
      len = 0;
    }
  }

  len += snprintf(buf + len, sizeof(buf) - len, "]}");
  server.sendContent(buf, len);

  // an empty piece tells the browser the response is finished
  server.sendContent("");
}

// sends only the latest reading
// {"u":"F","i":180,"s":95,"n":1234,"t":38}
// the page checks n, if it skipped a number (the phone was asleep) it asks for /all again
// before the first good reading n is 0 and t is null
void SendLatest() {

  if (Count > 0) {
    snprintf(JSONTxt, sizeof(JSONTxt), "{\"u\":\"%s\",\"i\":%lu,\"s\":%lu,\"n\":%lu,\"t\":%d}",
             USE_FAHRENHEIT ? "F" : "C", READ_INTERVAL / 1000, (unsigned long)SecondsToNext(),
             (unsigned long)ReadingNumber, Temps[(Head + Count - 1) % MAX_READINGS]);
  } else {
    snprintf(JSONTxt, sizeof(JSONTxt), "{\"u\":\"%s\",\"i\":%lu,\"s\":%lu,\"n\":0,\"t\":null}",
             USE_FAHRENHEIT ? "F" : "C", READ_INTERVAL / 1000, (unsigned long)SecondsToNext());
  }

#ifdef DEBUG
  Serial.println(JSONTxt);
#endif

  server.send(200, "application/json", JSONTxt);
}

// end of code
