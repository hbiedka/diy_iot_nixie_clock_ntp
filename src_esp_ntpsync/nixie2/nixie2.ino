#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <Time.h>
#include <TimeLib.h>
#include <Timezone.h>

#include "configServer.h"

// Właściwości NTP
#define NTP_OFFSET   0
#define NTP_INTERVAL 60 * 1000
//todo get it from EEPROM
#define NTP_ADDRESS  "time.coi.pw.edu.pl"  //adres serwera NTP, adresy innych serwerów można znaleźć w internecie

enum state {
  INIT,  
  CONNECTING,
  START_AP_RQ,
  RUNNING_AP,
  CONNECTED,
  ERROR
} st;

unsigned long long ts = 0;
unsigned long long stateTs = 0;

char ssid[128], password[128], url[256];

///bufor ramki rs232
byte frame[10];
byte frCnt = 0;

//obiekty UDP i protokołu NTP
WiFiUDP udp;
NTPClient timeClient(udp, NTP_ADDRESS, NTP_OFFSET, NTP_INTERVAL);

void setup()
{
  Serial.begin(9600);
  configInit();

  st = INIT;

}

void loop()
{
  //poll HTTP server
  if (st == CONNECTED || st == RUNNING_AP) pollServer();

  //run through state machine
  switch(st) {
    case INIT:
      //keep disconnected from 10 secs from startup
      if (millis() > 10000) {
        //get data from EEPROM
        if (getCredentials(ssid,password,url)) {

          //connect do wifi
          WiFi.mode(WIFI_STA);
          WiFi.begin(ssid, password);
          
          stateTs = millis();
          st = CONNECTING;
        } else {
          //if data corrupted
          //start access point
          st = START_AP_RQ;
        }
      }
      break;
    case CONNECTING:

      //check every 500 ms
      if (ts < millis()) {
        ts == millis() + 500;

        if (WiFi.status() == WL_CONNECTED) {
          Serial.println("[debug] connected");

          timeClient.begin();
          configServer();
          st = CONNECTED;
        } 

        if ( millis() - stateTs > 120000 ) {
          Serial.println("[debug] timeout");
          st = START_AP_RQ;          
        }
      }
      break;

    case START_AP_RQ:
      //run AP
      Serial.println("[debug] entering AP mode...");

      IPAddress local_IP(192,168,127,2);
      IPAddress gateway(192,168,127,1);
      IPAddress subnet(255,255,255,0);

      if (!WiFi.softAPConfig(local_IP, gateway, subnet)) {
        Serial.println("[debug] error config");
        st = ERROR;
        break;
      }

      Serial.println("[debug] Setting soft-AP ... ");
      if(WiFi.softAP("nixie_clock")) {
        Serial.println("[debug] OK");

        configServer();
        st = RUNNING_AP;
      } else {
        Serial.println("[debug] ERROR");
        st = ERROR;

      }
      break;
  }

  //odbierz dane
  if (Serial.available() > 0) {
    frame[frCnt] = Serial.read();
    if (frame[frCnt] == '\n') {
      //odebrano komplerną ramkę
      if(frame[2] == '?') {
        
        //odebrano zapytanie
        if (frame[0] == 't' && frame[1] == 'i') {
          //odebrano zapytanie o NTP

          //ask only when fully connected
          if (st != CONNECTED) return;

          //aktualizuj dane
          timeClient.update();

          //pobierz dane w formacie unix (liczba sekund od 1.01.1970, UTC)
          unsigned long unix =  timeClient.getEpochTime();
      
          // biblitoeka time.h wymaga używania formatu zmiennej time_t, dlatego tworzymy dwie zmienne (czas UTC i lokalny)...
          time_t utc, ti;
          //..i rzutujemy czas z serwera do zmiennej
          utc = unix;

          //Dane strefy czasowej wraz z regułami zmiany czasu. W naszym przypadku będzie to CET (zimowy) oraz CEST (letni).
          TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120};     //Central European Summer Time
          TimeChangeRule CET = {"CET ", Last, Sun, Oct, 3, 60};       //Central European Standard Time
          Timezone LocalTime(CEST, CET);
          //konwersja czasu
          ti = LocalTime.toLocal(utc);

          //wysyłamy dane
          Serial.print("ti=");
          Serial.print(year(ti));
          Serial.print("-");
          if (month(ti) < 10) Serial.print("0");
          Serial.print(month(ti));
          Serial.print("-");
          if (day(ti) < 10) Serial.print("0");
          Serial.print(day(ti));
          Serial.print("-");
          
          if (hour(ti) < 10) Serial.print("0");
          Serial.print(hour(ti));
          Serial.print("-");
          if (minute(ti) < 10) Serial.print("0");
          Serial.print(minute(ti));
          Serial.print("-");
          if (second(ti) < 10) Serial.print("0");
          Serial.print(second(ti)); 
          Serial.print("\n");       
        }
      } 
      else if( frame[2] == '+') {
        //command received
        if (frame[0] == 'a' && frame[1] == 'p') {
          //run AP mode
          if (st == INIT || st == CONNECTED) st = START_AP_RQ;
          Serial.print("ok\n");
        }
        else if (frame[0] == 'c' && frame[1] == 'r') {
          //run AP mode
          if (st == INIT || st == CONNECTED || st == RUNNING_AP) 
            Serial.print( clearROM() ? "ok\n" : "CRe\n");
        }

      }
      //zeruj
      frCnt = 0;
    } else {
      frCnt++;
    }
    
  }
}
