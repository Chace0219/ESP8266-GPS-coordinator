#include <Arduino.h>
#include <TinyGPS++.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <QueueArray.h>
#include "MyFunc.h"

#define POST_MSG_TXID_LEN (8)
#define POST_MSG_LAT_LEN (8)
#define POST_MSG_LNG_LEN (8)
#define POST_MSG_ALTITUDE_LEN (4)
#define POST_MSG_SPEED_LEN (4)
#define POST_MSG_COURSE_LEN (4)
#define POST_MSG_TIME_LEN (4)
#define POST_MSG_DATE_LEN (4)
#define INVALID (0xFFFFFFFF)

#define POST_PAYLOAD_ARRAY_LEN_8   (POST_MSG_TXID_LEN + \
                  POST_MSG_LAT_LEN + \
                  POST_MSG_LNG_LEN + \
                  POST_MSG_ALTITUDE_LEN + \
                  POST_MSG_SPEED_LEN + \
                  POST_MSG_COURSE_LEN + \
                  POST_MSG_TIME_LEN + \
                  POST_MSG_DATE_LEN)

typedef struct postMsg_t
{
  uint8_t msg8[POST_PAYLOAD_ARRAY_LEN_8];
}postMsg_t;

//int gpsRxPin = 8;  //! GPS TX Pin has to connected D2
//int gpsTxPin = 7;  //! GPS RX Pin has to connected D3
const int gpsBaud = 9600;

// Wifi SSID and Password
const char* ssid = "arx001-ap";
const char* password = "!@atmoscan";

/**/
TinyGPSPlus gps;
//SoftwareSerial gpsSerial(gpsRxPin, gpsTxPin);

String serverAdd = "http://192.168.42.1:8080/gpsData";

String TXID = "abc123"; // keep it exactly (POST_MSG_TXID_LEN - 1) number of characters excluding '\0'

            // Queue Count is 360, 120/min 
#define MAXQUEUECNT 360 // 3minutes

QueueArray<postMsg_t> PostDataQueue;

/* */
void sendPostToServer(String msg)
{
  //Check WiFi connection status

  HTTPClient http;    //Declare object of class HTTPClient

  http.begin(serverAdd); //Specify request destination
  http.addHeader("Content-Type", "text/plain"); //Specify content-type header

  int httpCode = http.POST(msg);     //Send the request
  String payload = http.getString();    //Get the response payload

  http.end();  //Close connection
}

void sendPostToServer(uint8_t* msg, size_t size)
{
  HTTPClient http;    //Declare object of class HTTPClient

  http.begin(serverAdd); //Specify request destination
  http.addHeader("Content-Type", "text/plain"); //Specify content-type header

  int httpCode = http.POST(msg, size);     //Send the request
  String payload = http.getString();    //Get the response payload

  http.end();  //Close connection
}

void setup()
{
  delay(1000);
  Serial.begin(gpsBaud);

  InitVars();

  WifiConnect();
  while (WiFi.status() != WL_CONNECTED)
    delay(2000);
}

// Current GPS Buffer
static uint32_t nGPSTimems = 0;
static uint32_t nHttpTimems = 0;
static uint32_t nCheckQueuems = 0;

struct postMsg_t GetGPSInfo()
{
  postMsg_t postMsg;
  uint8_t posBytes = 0;
  memset(postMsg.msg8, 0xFF, POST_PAYLOAD_ARRAY_LEN_8);
  TXID.getBytes(&(postMsg.msg8[posBytes]), POST_MSG_TXID_LEN);
  posBytes += POST_MSG_TXID_LEN;
  int gpsInfoValidity = -6;

  if (gps.time.isValid())
  {
    gpsInfoValidity++;
    uint32_t value = gps.time.value();
    memcpy(&(postMsg.msg8[posBytes]), &value, POST_MSG_TIME_LEN);
  }
  posBytes += POST_MSG_TIME_LEN;

  if (gps.location.isValid())
  {
    gpsInfoValidity++;
    double value = gps.location.lat();
    memcpy(&(postMsg.msg8[posBytes]), &value, POST_MSG_LAT_LEN);
    posBytes += POST_MSG_LNG_LEN;

    value = gps.location.lng();
    memcpy(&(postMsg.msg8[posBytes]), &value, POST_MSG_LNG_LEN);
    posBytes += POST_MSG_LNG_LEN;
  }
  else
  {
    posBytes += POST_MSG_LAT_LEN;
    posBytes += POST_MSG_LNG_LEN;
  }

  if (gps.altitude.isValid())
  {
    gpsInfoValidity++;
    uint32_t value = gps.altitude.value();
    memcpy(&(postMsg.msg8[posBytes]), &value, POST_MSG_ALTITUDE_LEN);
  }
  posBytes += POST_MSG_ALTITUDE_LEN;

  if (gps.speed.isValid())
  {
    gpsInfoValidity++;
    uint32_t value = gps.speed.value();
    memcpy(&(postMsg.msg8[posBytes]), &value, POST_MSG_SPEED_LEN);
  }
  posBytes += POST_MSG_SPEED_LEN;

  if (gps.course.isValid())
  {
    gpsInfoValidity++;
    uint32_t value = gps.course.value();
    memcpy(&(postMsg.msg8[posBytes]), &value, POST_MSG_COURSE_LEN);
  }
  posBytes += POST_MSG_COURSE_LEN;

  if (gps.date.isValid())
  {
    gpsInfoValidity++;
    uint32_t value = gps.date.value();
    memcpy(&(postMsg.msg8[posBytes]), &value, POST_MSG_DATE_LEN);
  }
  posBytes += POST_MSG_DATE_LEN;
  return postMsg;
}

#define GPSINTERVAL 500 // ms unit
#define HTTPINTERVAL 2000 // ms unit
#define CHECKTINTERVAL 200 // ms unit


#define CONNECTINGTIME 7000 // ms unit
#define DISCONNECTTIME 3000 // ms unit

static TON DisconnectTON;
static TP ConnectingTON;

void InitVars()
{
  ConnectingTON.IN = 0;
  ConnectingTON.PRE = 0;
  ConnectingTON.ET = 0;
  ConnectingTON.PT = CONNECTINGTIME;
  ConnectingTON.Q = 0;

  DisconnectTON.IN = 0;
  DisconnectTON.PRE = 0;
  DisconnectTON.ET = 0;
  DisconnectTON.PT = DISCONNECTTIME;
  DisconnectTON.Q = 0;
}

postMsg_t postMsg;

void loop()
{
  // This sketch displays information every time a new sentence is correctly encoded.
  while (Serial.available() > 0)
  {
    if (gps.encode(Serial.read()))
    {
      postMsg = GetGPSInfo();
    }
  }

  if ((nGPSTimems + GPSINTERVAL) <= millis())
  {
    nGPSTimems = millis();
    if (PostDataQueue.count() >= MAXQUEUECNT)
      PostDataQueue.pop();
    PostDataQueue.push(postMsg);

  }

  /*
  if(nCheckQueuems + CHECKTINTERVAL <= millis())
  {
  nCheckQueuems = millis();
  if(PostDataQueue.count() > 8 && WiFi.status() == WL_CONNECTED)
  {
  postMsg_t postMsg;
  postMsg = PostDataQueue.pop();
  sendPostToServer(postMsg.msg8, POST_PAYLOAD_ARRAY_LEN_8);
  nHttpTimems = millis();
  }
  }
  */

  if ((nHttpTimems + HTTPINTERVAL) <= millis())
  {
    nHttpTimems = millis();
    postMsg_t postMsg;
    // 
    for (uint8_t idx = 0; idx < 8; idx++)
    {
      if (WiFi.status() != WL_CONNECTED)
        break;
      if (PostDataQueue.isEmpty())
        break;
      postMsg = PostDataQueue.pop();
      sendPostToServer(postMsg.msg8, POST_PAYLOAD_ARRAY_LEN_8);
      delay(50);
    }

  }

  // Connection Again Part
  DisconnectTON.IN = (WiFi.status() != WL_CONNECTED) && (ConnectingTON.Q == 0);
  TONFunc(&DisconnectTON);
  if (DisconnectTON.Q == 1)
    WifiConnect();
  TPFunc(&ConnectingTON);
  ConnectingTON.IN = 0;

  // If 5000 milliseconds pass and there are no characters coming in
  // over the software serial port, show a "No GPS detected" error
  if (millis() > 5000 && gps.charsProcessed() < 10)
  {
    //Serial.println(F("No GPS detected"));
    //while (true)
    delay(500);
  }
}

void WifiConnect()
{
  WiFi.setOutputPower(25.0);    // Maximum Output Power
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
  delay(200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  ConnectingTON.IN = 1;
}
