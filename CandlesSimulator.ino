#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include "FS.h"
#include "SPIFFS.h"
#include <ArduinoJson.h>

#define FASTLED_INTERNAL
#include <FastLED.h>

const char* ssid     = "CandleAP";
const char* password = "bougiedanslevent";
WebServer server(80);
WebSocketsServer webSocket(81);

struct Parameter4JSON {
  char* id;
  char* hmtlType;
  uint8_t* value;
  uint8_t min;
  uint8_t max;

  Parameter4JSON(char* _id, char*_hmtlType, uint8_t* _value, uint8_t _min = 1, uint8_t _max = 255) :
    id(_id), hmtlType(_hmtlType), value(_value), min(_min), max(_max) {}

  void serialPrint() {
    Serial.printf("{\n\tid:%s\n\thtmlTypes:%s\n\tvalue:%i\n\tmin:%i\n\tmax:%i\n}\n", id, hmtlType, value, min, max);
  }
};

#define LED_PIN     4
#define COLOR_ORDER GRB
#define CHIPSET     WS2812B
#define NUM_LEDS    116           //// SET PARAMETER !

CRGB leds[NUM_LEDS];
uint8_t heat[NUM_LEDS];
// parameters editable from websocket
#define NB_PARAMS 5
uint8_t brightness = 120; 
uint8_t fps = 50;
uint8_t cooling = 16;
uint8_t sparking = 12;
uint8_t saveFlag = 0;
Parameter4JSON forJSON[] = { Parameter4JSON("brightness", "range", &brightness), 
                             Parameter4JSON("fps", "range", &fps, 1, 200), 
                             Parameter4JSON("cooling", "range", &cooling, 1, 128), 
                             Parameter4JSON("sparking", "range", &sparking, 1, 180),
                             Parameter4JSON("save", "button", &saveFlag, 0, 1)
                           };

CRGBPalette16 gPal;

const TProgmemPalette16 CandlePalette_p PROGMEM =
{
    CRGB::DarkOrange,
    CRGB::DarkOrange,
    CRGB::DarkOrange,
    CRGB::OrangeRed,
    
    CRGB::Orange,
    CRGB::Orange,
    CRGB::Orange,
    CRGB::Gold,
    
    CRGB::Gold,
    CRGB::Gold,
    CRGB::Gold,
    CRGB::Gold,
    
    CRGB::Gold,
    CRGB::Yellow,
    CRGB::Yellow,
    CRGB::Grey
};



/**************************************************************************/
/*                 load and save parameters                               */
/**************************************************************************/
void saveParameters() {
  File file = SPIFFS.open("/config.json", "w");
  if(!file){
    // File not opened
    Serial.println("Failed to open config file");
    return;
  }
  
  StaticJsonDocument<512> doc;
  doc["brightness"] = brightness;
  doc["fps"] = fps;
  doc["cooling"] = cooling;
  doc["sparking"] = sparking;
  serializeJsonPretty(doc, file);
  file.close();
}



void loadParameters() {
  File file = SPIFFS.open("/config.json", "r");
  if(!file){
    // File not opened
    Serial.println("Failed to open config file");
    return;
  }
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, file);
  if(err) {
    Serial.printf("deserializeJson failed with code %i\n", err.c_str());
  }
  else {
    brightness = doc["brightness"];
    fps = doc["fps"];
    cooling = doc["cooling"];
    sparking = doc["sparking"];
  }
  file.close();
}



/**************************************************************************/
/*  server funtions (needed here, it depends on global variables)         */
/**************************************************************************/
String getContentType(String filename){
  if(server.hasArg("download")) return "application/octet-stream";
  else if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}


bool handleFileRead(String path){
  if(path == "/") path += "config.html";
  Serial.println("handleFileRead: " + path);
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)){
    if(SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    size_t sent = server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}



/**************************************************************************/
/*  WebSocket callbacks                                                   */
/**************************************************************************/
/*String crgbToHtmlString(CRGB color) {
  char buf[8];
  sprintf(buf, "#%02x%02x%02x", color.r, color.g, color.b);
  return String(buf);
}*/

String getJSONCurrentConfig(bool pretty = false) {
  StaticJsonDocument<1024> doc;
  JsonArray params = doc.createNestedArray("parameters");
  for(int i = 0; i < NB_PARAMS; i++) {
    JsonObject obj = params.createNestedObject();
    obj["id"] = forJSON[i].id;
    obj["htmlType"] = forJSON[i].hmtlType;
    obj["value"] = *forJSON[i].value;
    obj["min"] = forJSON[i].min;
    obj["max"] = forJSON[i].max;
  }
  String jsonStr;
  if(pretty) 
    serializeJsonPretty(doc, jsonStr);
  else 
    serializeJson(doc, jsonStr);
  return jsonStr;
}


void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            Serial.printf("[%u] Disconnected!\n", num);
            break;
        case WStype_CONNECTED:
            {
                IPAddress ip = webSocket.remoteIP(num);
                Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
                // send message to client
                String str = getJSONCurrentConfig();
                //Serial.println(str);
                webSocket.sendTXT(num, str);
            }
            break;
        case WStype_TEXT:
            {
              //Serial.printf("[%u] get Text: %s\n", num, payload);   
              StaticJsonDocument<256> doc;
              DeserializationError err = deserializeJson(doc, payload);
              if(err) {
                Serial.printf("deserializeJson failed with code %i\n", err.c_str());
              }
              for(int i = 0; i < NB_PARAMS; i++) {
                int v = doc[forJSON[i].id];
                if(v) {
                  *forJSON[i].value = v;
                  if(forJSON[i].value == &brightness) {
                    FastLED.setBrightness(brightness);
                  }
                }
              }
            }
            break;
      default:  
        break;
    }
}



/**************************************************************************/
/*  Arduino main funtions                                                 */
/**************************************************************************/
void setup() {
  Serial.begin(115200);
  delay(100);
  
  if(!SPIFFS.begin()){
        Serial.println("An Error has occurred while mounting SPIFFS");
        return;
  }

  loadParameters();
  
  FastLED.addLeds<CHIPSET, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection( TypicalLEDStrip );
  FastLED.setBrightness( brightness );
  fill_solid(leds, NUM_LEDS, CRGB::Black); 
  gPal = CandlePalette_p;

  WiFi.softAP(ssid, password);
  Serial.printf("WiFi network : %s, password : %s, server IP address : %s\n", ssid, password, WiFi.softAPIP().toString().c_str());
  
  server.onNotFound([](){
    if(!handleFileRead(server.uri()))
      server.send(404, "text/plain", "FileNotFound");
  });
  server.begin();

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}



void loop()
{ 
  webSocket.loop();
  server.handleClient();

  if(saveFlag > 0) {
    saveParameters();
    saveFlag = 0;
    Serial.printf("brightness = %i, fps = %i, cooling = %i, sparking = %i\n", brightness, fps, cooling, sparking);
    Serial.println("Parameters saved !");
  }
  
  for(int i = 0; i < NUM_LEDS; i+=2) {
    heat[i] = qsub8(heat[i],  random8(0, cooling));
    if ( random8() < sparking ) {
      heat[i] = qadd8(heat[i], random8(64, 196) );
    }
    leds[i] = ColorFromPalette(gPal, heat[i]);
  }
  FastLED.show(); // display this frame
  FastLED.delay(1000 / fps);
}
