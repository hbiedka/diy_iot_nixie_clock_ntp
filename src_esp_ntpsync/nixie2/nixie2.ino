#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <Time.h>
#include <TimeLib.h>
#include <Timezone.h>

// Właściwości NTP
#define NTP_OFFSET   0
#define NTP_INTERVAL 60 * 1000
#define NTP_ADDRESS  "time.coi.pw.edu.pl"  //adres serwera NTP, adresy innych serwerów można znaleźć w internecie


char ssid[] = "********";       // ssid
char pass[] = "********";       // hasło

///bufor ramki rs232
byte frame[10];
byte frCnt = 0;

//flaga czekania na odbiór ntp
boolean waitForReply = 0;
byte attempt = 0;

//obiekty UDP i protokołu NTP
WiFiUDP udp;
NTPClient timeClient(udp, NTP_ADDRESS, NTP_OFFSET, NTP_INTERVAL);

void setup()
{
  Serial.begin(9600);

  // Łączenie
  WiFi.begin(ssid, pass);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  timeClient.begin();

  //komunikat o poprawnym połączeniu
  Serial.print("ic!\n");

}

void loop()
{
  //odbierz dane
  if (Serial.available() > 0) {
    frame[frCnt] = Serial.read();
    if (frame[frCnt] == '\n') {
      //odebrano komplerną ramkę
      if(frame[2] == '?') {
        
        //odebrano zapytanie
        if (frame[0] == 't' && frame[1] == 'i') {
          //odebrano zapytanie o NTP

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
      //zeruj
      frCnt = 0;
    } else {
      frCnt++;
    }
    
  }
}
