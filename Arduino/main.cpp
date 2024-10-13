#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>
#include <ESP8266WiFi.h>
#include "ESPAsyncUDP.h"
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Timezone.h> 
#include <Fonts/TomThumb.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>

//function declarations
void writeText(String msg);
void showTime();
char* uint8_to_char(uint8_t* input, size_t len);
time_t compileTime();
void getWeatherData();

//led matrix parameter
int matrixW = 64;
int matrixH = 16;
#define PIN D4 // OUTPUT PIN FROM ARDUINO TO MATRIX D-In
Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(matrixW, matrixH, PIN,
                            NEO_MATRIX_TOP     + NEO_MATRIX_LEFT +
                            NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG,
                            NEO_GRB            + NEO_KHZ800);

const uint16_t colors[] = {
  matrix.Color(255, 255, 255), matrix.Color(255, 0, 0),matrix.Color(0, 255, 0),matrix.Color(0, 0, 255)
};

#define arr_len( x )  ( sizeof( x ) / sizeof( *x ) ) // Calculation of Array Size;
int pixelPerChar = 6;
int x = matrix.width(); // Width of the Display
char udpText[255];
int selectedColor = 0;
int selectedBrightness = 5;

//wifi
#define WIFI_SSID "<YOUR WIFI SSID>"
#define WIFI_PASSWORD "<YOUR WIFI PASSWORD>"
AsyncUDP udp;
WiFiClient client;

//NTP client
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

//Timezone
TimeChangeRule myDST = {"MEZ", Last, Sun, Mar, 2, 120};    // Summer time = UTC +2 hours
TimeChangeRule mySTD = {"MESZ", Last, Sun, Oct, 3, 60};     // Standard time = UTC +1 hours
Timezone myTZ(myDST, mySTD);
TimeChangeRule *tcr;
//time_t localtime;

//Weather / OpenWeatherMap
const char* apiKey = "<YOUR OPENWEATHER API-KEY>"; 
const char* city = "Freiburg"; //<YOUR CITY>
const char* country = "DE"; //<YOUR COUNTRY>
String serverPath = "http://api.openweathermap.org/data/2.5/weather?q=" + String(city) + "," + String(country) + "&appid=" + apiKey + "&units=metric";
String weatherString = "-/-/-";
unsigned long lastWeatherDataCall = 0;


void setup() {
  Serial.begin(115200);
  Serial.println();

  //setup led matrix
  matrix.begin();
  matrix.setTextWrap(false);
  matrix.setBrightness(selectedBrightness);
  matrix.setTextColor(colors[selectedColor]);

  //wifi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED){
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();

  //UDP
  if(udp.listen(1234)) {
    Serial.print("UDP Listening on IP: ");
    Serial.println(WiFi.localIP());
    udp.onPacket([](AsyncUDPPacket packet) {
      char* receivedText = uint8_to_char(packet.data(), packet.length());
      //get length of packet
      int udpLength = strlen(receivedText);
      if(udpLength == 2) {
        char colorChar[2];
        char brightnessChar[2];
        strncpy(colorChar,receivedText,1);
        strncpy(brightnessChar,receivedText+1,1);
        colorChar[1] = '\0'; 
        brightnessChar[1] = '\0'; 
        
        selectedColor = atoi(colorChar);
        selectedBrightness = atoi(brightnessChar);

        Serial.println("Color: ");
        Serial.println(selectedColor);
        Serial.println("Brightness: ");
        Serial.println(selectedBrightness);
      }
      else if(udpLength > 2) {
        //clear udpText
        memset(udpText, '\0', sizeof(udpText)); 
        //copy new string
        strncpy(udpText,receivedText+2,udpLength-2);
        Serial.println("Text: ");
        Serial.println(udpText);
      }
      Serial.print("String:");
      Serial.print(receivedText);
    });
  }
  //NTP client
  timeClient.begin();
  setTime(myTZ.toUTC(compileTime()));
}

void loop() {
  //check for new weather data
  if((millis() - lastWeatherDataCall) > (60*1000)) { //check for new data each minute
    //need to check for weather
    Serial.write("weather check");
    getWeatherData();
  }

  //check for udp message
  int udpTextLength = strlen(udpText);
  if(udpTextLength > 0) {
    Serial.print("received Text");
    writeText(udpText);
  }
  else {
    //show time
    showTime();
  }
  delay(50);
}

String fillDecimal(int value) {
  String _value;
  if(value < 10) {
    _value.concat("0");
    _value.concat(value);
  }
  else _value.concat(value);

  return _value;
}

void showTime() {

  //get time 
  timeClient.update();
  unsigned long epochTime = timeClient.getEpochTime();
  setTime(epochTime);

  //timezone
  time_t utc = now(); //get utc time
  time_t localtime = myTZ.toLocal(utc, &tcr); //convert to local time corresponding to timezone settings
  String dateString;
  String timeString;
  
  //build date & weather
  int dateDay = day(localtime);
  int dateMonth = month(localtime);
  int dateYear = year(localtime) -2000;
  dateString.concat(dateDay);
  dateString.concat(".");
  dateString.concat(dateMonth);
  dateString.concat(".");
  dateString.concat(dateYear);
  dateString.concat(" ");
  dateString.concat(weatherString);

  //build time
  int _h = hour(localtime);
  int _m = minute(localtime);
  timeString.concat(fillDecimal(_h));
  timeString.concat(":");
  timeString.concat(fillDecimal(_m));

  matrix.setBrightness(selectedBrightness);
  matrix.setFont(&TomThumb);
  matrix.setTextColor(colors[selectedColor]);
  matrix.fillScreen(0); 
  matrix.setCursor(4, 6); //date & weather offset
  matrix.print(dateString); //print date & weather
  matrix.setFont(NULL);
  matrix.setCursor(18, 8); //time offset
  matrix.print(timeString); //print the time
  matrix.show();
  delay(1000);
}

void writeText(String msg) {
  int msgSize = (msg.length() * pixelPerChar) + (2 * pixelPerChar); // CACULATE message length;
  int scrollingMax = (msgSize) + matrix.width(); // ADJUST Displacement for message length;
  x = matrix.width(); // RESET Cursor Position and Start Text String at New Position on the Far Right;
  bool scrolling = true;

  while (scrolling) {
    matrix.setTextColor(colors[selectedColor]);
    matrix.setBrightness(selectedBrightness);
    matrix.fillScreen(0); // BLANK the Entire Screen;
    matrix.setCursor(x, 4); // Set Starting Point for Text String;
    matrix.print(msg); // Set the Message String;

    if (--x < -scrollingMax ) {
      x = matrix.width(); // After Scrolling by scrollingMax pixels, RESET Cursor Position and Start String at New Position on the Far Right;
      scrolling = false; // INCREMENT COLOR/REPEAT LOOP COUNTER AFTER MESSAGE COMPLETED;
      strcpy(udpText,"");
    }
    matrix.show(); // DISPLAY the Text/Image
    delay(40); // SPEED OF SCROLLING or FRAME RATE;
  }
}

char* uint8_to_char(uint8_t* input, size_t len) {
    // Allocate memory for the char array (including space for null terminator)
    char* output = (char*)malloc(len + 1);
    if (output == NULL) {
        // If memory allocation fails, return NULL
        return NULL;
    }

    // Copy each byte from the uint8_t array to the char array
    for (size_t i = 0; i < len; i++) {
        output[i] = (char)input[i];
    }

    // Null-terminate the string
    output[len] = '\0';

    return output;
}

time_t compileTime()
{
    const time_t FUDGE(10);     // fudge factor to allow for compile time (seconds, YMMV)
    const char *compDate = __DATE__, *compTime = __TIME__, *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char chMon[4], *m;
    tmElements_t tm;

    strncpy(chMon, compDate, 3);
    chMon[3] = '\0';
    m = strstr(months, chMon);
    tm.Month = ((m - months) / 3 + 1);

    tm.Day = atoi(compDate + 4);
    tm.Year = atoi(compDate + 7) - 1970;
    tm.Hour = atoi(compTime);
    tm.Minute = atoi(compTime + 3);
    tm.Second = atoi(compTime + 6);
    time_t t = makeTime(tm);
    return t + FUDGE;           // add fudge factor to allow for compile time
}

void getWeatherData() {
  if (WiFi.status() == WL_CONNECTED) {

    //save time of last call
    lastWeatherDataCall = millis();
    
    HTTPClient http;
    Serial.print("Requesting weather data from: ");
    Serial.println(serverPath);

    http.begin(client, serverPath);  // Connect to the weather server
    int httpResponseCode = http.GET();

    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.println(httpResponseCode);
      Serial.println(response);

      // Parse JSON data
      StaticJsonDocument<1024> doc;
      DeserializationError error = deserializeJson(doc, response);

      if (error) {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.c_str());
        return;
      }

      // Extract data from JSON (this example extracts temperature and weather description)
      float currentTemp = doc["main"]["temp"];  // Current temperature
      float minTemp = doc["main"]["temp_min"];  // Minimum temperature for today
      float maxTemp = doc["main"]["temp_max"];  // Maximum temperature for today

      weatherString = String(currentTemp,0) + "/" + String(minTemp,0) + "/" + String(maxTemp,0);
      

    } else {
      Serial.print("Error code: ");
      Serial.println(httpResponseCode);
    }
    // End the connection
    http.end();
  } else {
    Serial.println("WiFi Disconnected");
  }
}