
#ifndef CONFIGSERVER_H
#define CONFIGSERVER_H

#include <Arduino.h>

void configInit();
bool getCredentials(char* ssid, char* password, char* apiEndpoint);
void configServer();
void setWebDefaults(char* newSsid, char* newApiEndpoint);
void pollServer();
void handleRoot();
void handleConfig();
void loadString(char* str, uint16_t address, uint8_t maxLen);
bool saveAll(char* buffer);
bool clearROM();
uint8_t parseHttpOutput(String msg);
bool isStringValid(char* text, uint8_t maxLen);
String convertChars(String input);

#endif //CONFIGSERVER_H
