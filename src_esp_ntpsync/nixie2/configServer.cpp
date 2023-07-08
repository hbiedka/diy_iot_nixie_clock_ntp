#include <Arduino.h>

#include "configServer.h"

#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <StreamString.h>


const char *header = "\
<html>\
  <head>\
    <title>ESP8266 Demo</title>\
    <style>\
      body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
    </style>\
  </head>\
  <body>\
    <h1>Hello from ESP8266!</h1>";

const char *footer = "\
  </body>\
</html>";

//char buffers to show by HTTP page
char httpSsid[128], httpApiEndpoint[256];

ESP8266WebServer server(80);

void configInit() {
  EEPROM.begin(512);


} 

bool getCredentials(char* ssid, char* password, char* apiEndpoint) {
  //load SSID and password from EEPROM
  loadString(ssid,0,128);
  loadString(password,128,128);
  loadString(apiEndpoint,256,255);

  //validate data got from EEPROM
  return (isStringValid(ssid,128) && isStringValid(password,128));
}

void configServer() {

  //first, load current data for HTTP server from EEPROM 
  loadString(httpSsid,0,128);
  loadString(httpApiEndpoint,256,255);

  //if not valid (corrupted or not saved yet), clear string
  if (!isStringValid(httpSsid,128)) httpSsid[0] = '\0';
  if (!isStringValid(httpApiEndpoint,255)) httpApiEndpoint[0] = '\0';
  
  server.on("/", handleRoot);
  server.on("/config", handleConfig);
  server.begin();
}

void pollServer() {
  server.handleClient();
}

void handleRoot() {
  StreamString temp;
  temp.reserve(500);  // Preallocate a large chunk to avoid memory fragmentation
  temp.printf(header);
  temp.printf("\
  <form method='POST' action='/config'>\
    <p>API endpoint (IR code will be a suffix):</p>\
    <input type='text' name='url' value='%s'>\
    <p>SSID:</p>\
    <input type='text' name='ssid' value='%s'>\
    <p>Password:</p>\
    <input type='password' name='pass'>\
    <p>\
      <input type='submit' value=' Update '>\
    </p>\
  </form>",httpApiEndpoint,httpSsid);
  temp.printf(footer);
  server.send(200, "text/html", temp.c_str());
}

void handleConfig() { //Handler for the body path

  StreamString temp;
  uint8_t saveResult = 3; // will be set to 0 by successful ParseHttpOutput 

  temp.reserve(500);  // Preallocate a large chunk to avoid memory fragmentation
  temp.printf(header);

  if (server.hasArg("plain")){ //Check if body received
    saveResult = parseHttpOutput(server.arg("plain"));
  } 
  else {
        temp.printf("<p>Body not received</p>");
  } 

  if ( saveResult == 1 ) {
    temp.printf("</p>Invalid data entried</p>");
  } 
  else if ( saveResult == 2 ) {
    temp.printf("<p>Error writing to ROM</p>");
  } 
  else {
    temp.printf("<p>Data saved successfully</p>");
  }

  temp.printf(footer);

  server.send(200, "text/html", temp.c_str());

  //todo reset after successful save

}

void loadString(char* str, uint16_t address, uint8_t maxLen) {

  for (uint8_t i = 0; i < maxLen; i++) {
    str[i] = EEPROM.read(address+i);
    delay(1);
    
    if (str[i] == '\0' ) break;
  }
}

bool saveAll(char* buffer) {
  for (int i = 0; i < 512; i++) {
    EEPROM.write(i,buffer[i]);
  }

  return EEPROM.commit();
}

bool clearROM() {
  //for debug purposes
  for (int i = 0; i < 512; i++) {
    EEPROM.write(i,0);
  }

  return EEPROM.commit();
}

uint8_t parseHttpOutput(String msg) {

  char buf[512];
  char* ptrSsid = &buf[0];
  char* ptrPass = &buf[128];
  char* ptrApiUrl = &buf[256];

  String head;
  String tail = String(msg);

  String key;
  String val;
  
  int pos = -1;
  do {
    //find next & sign
    pos = tail.indexOf('&');


    if (pos > 0) 
    {
      head = tail.substring(0,pos);
      tail = tail.substring(pos+1);
    }
    else
    {
      head = String(tail);
      tail = String("");
    }      


    pos = head.indexOf('=');

    key = head.substring(0,pos);
    val = head.substring(pos+1);

    if (key.equals("url")) {
      convertChars(val).toCharArray(ptrApiUrl,256);
    }
    else if (key.equals("ssid")) {
      val.toCharArray(ptrSsid,128);
    } 
    else if(key.equals("pass")) {
      val.toCharArray(ptrPass,128);
    }

   } while (tail.length() > 0);

   //validate than all args received
  if(!isStringValid(ptrApiUrl,256)) return 1;
  if(!isStringValid(ptrSsid,128)) return 1;
  if(!isStringValid(ptrPass,128)) return 1;

  //save and return value according to result

#if DEBUG
  return 0;
#else
  return saveAll(buf) ? 0 : 2;
#endif
}

//check that string contains only ASCII chars
bool isStringValid(char* text, uint8_t maxLen) {
  for (uint8_t i = 0; i < maxLen; i++) {

    //if string end occured return true if there is not a first char (means empty string)
    if (text[i] == '\0') return (i > 0);
    
    //return false if contains special ASCII chars
    if (text[i] < ' ' || text[i] > '~') return false;
  }
  return true;
}

String convertChars(String input) {
  String output = String(input);
  int pos = -1;
  char c = 0;

  //replace all + chars into spaces
  output.replace('+',' ');

  do {
    pos = output.indexOf('%');

    if (pos >= 0) {
      c = 0;
      for (uint8_t i = 1; i <= 2; i++) {
        char t = output.charAt(pos+i);
        if (t >='A' && t <= 'F') {
          c = c << 4;
          c += t-'A'+10;
        }
        else if (t >='a' && t <= 'f') {
          c = c << 4;
          c += t-'a'+10;
        }
        else if (t >='0' && t <= '9') {
          c = c << 4;
          c += t-'0';
        }
      }

      output = output.substring(0,pos) + String(c) + output.substring(pos+3);      
    }

  } while (pos >= 0);

  return output;
 }
