
//control LED // dioda kontrolna
#define PLED PORTD
#define LED 0b00100000

//latch pulse time (in ms) // czas impulsu otwieraj�cego przerzutniki w ms
#define LPT 1

//aktualna godzina, minuta i sekunda
signed char h;
signed char m;
signed char s;
//aktualny dzie� i miesi�c
signed char day;
signed char month;
signed char year;

/*
flaga synchronizacji/ustawienia zegara
0 - nieustawiony, brak po��czenie z ntp
1 - ustawiony r�cznie
2 - nieustawiony, ale trwa ��czenie z ntp (domy�lnie)
3 - ustawiony z ntp
*/
unsigned char sync = 2;

//MODY: zegar mo�e wy�wietla� r�ne warto�ci pobierane przez esp z neta
//domy�lnie 0 to ustawianie zegara, 1 to zegar a 2 to data, tryby od 3 do 9 mo�na programowa� dowolnie.
//przy uruchomieniu w��cza si� tryb 1
unsigned char mod = 1;

//submod: dla modu 0 (ustawie�) dodatkowa zmienna: 0-miesi�c, 1-dzie�, 2-godzina, 3-minuta, 4-zapisywanie 
unsigned char submod = 0;

//bufor pami�ci wy�wietlanych tekst�w - 7 x 10 bajt�w
char text[7][10] = {"0","0","0","0","0","0","0"};

//czas wy�wietlania (0 - nie wy�wietlaj, 255 - wy�wietlaj a� do wci�ni�cia przycisku)
unsigned char swapTime[10] = {255,255,5,0,0,0,0,0,0,0};
unsigned char modCnt = 0;

//czas od�wierzania (dotyczy tylko tryb�w 2-9, dla kom�rek 0 i 1 nie maj� znaczenia)
unsigned char refreshTime[10] = {0,0,0,0,0,0,0,0,0,0};
unsigned char modRefCnt = 0;

//ramka danych odbioru rs232, licznik ramki oraz licznik czasu opuszczenia ramki
char fr[32];
unsigned char frCnt = 0;
unsigned char dropCnt = 0;

//dodatkowy licznik dla timera0 oraz maksymalny czas liczenia
unsigned char blcnt = 0;
unsigned char blmax = 10;

//licznik dla przycisk�w
unsigned char butCnt = 0;
unsigned char butMax = 0;

/*
void showClock(void);
void showDate(void);
void showYear(void);
void showText(char *t);

void clock(char jumpDays);
unsigned char computeDayNum(unsigned char y,unsigned char m);
void rsSend(char *command);

void sendDigits(unsigned char data, unsigned char latch);
void send(unsigned char data, unsigned char latch);
*/

unsigned long int milBuf = 0;
volatile boolean selfTest = 0;

void setup() {
    
  //timer1: liczb� cykli przerwania obliczamy na podstawie pr�dko�ci zegara
  //przesuwamy cz�totliwo�� zegara o 10 bit�w, bo preskaler jest ustawiony na f/1024 (7812 cykli)
  
  TCCR1A = 0;// set entire TCCR1A register to 0
  TCCR1B = 0;// same for TCCR1B
  TCNT1  = 0;//initialize counter value to 0
  // set compare match register for 1hz increments
  OCR1A = 15624;// = (16*10^6) / (1*1024) - 1 (must be <65536)
  // turn on CTC mode
  TCCR1B |= (1 << WGM12);
  // Set CS12 and CS10 bits for 1024 prescaler
  TCCR1B |= (1 << CS12) | (1 << CS10);  
  // enable timer compare interrupt
  TIMSK1 |= (1 << OCIE1A);
  sei(); // w��cz ca�y system przerwa�

  //rs232
  Serial.begin(9600);

  
  DDRB = 0b111111;
  DDRC = 0b111111;
  DDRD = 0b11100010;
  
  PORTB = 0;
  PORTC = 0;
  PORTD = 0;
  
  h = 0;
  m = 0;
  s = 0;
  year = 17;
  day = 1;
  month = 1;

  delay(500);

  digitalWrite(3,HIGH);
  if (!digitalRead(3)) {
    //OK pressed - run selftest
    doSelfTest();
  }

}

void loop() {
  //obługa transmisji danych
  uartRead();

  //co 10 ms
  if (milBuf+10 < millis()) {
    milBuf = millis();
    routine10ms();
  }
}

void doSelfTest() {
  selfTest = true;

  for (uint8_t digit = 0; digit < 100; digit+=11) {
    sendDigits(digit,1);  
    sendDigits(digit,2);  
    sendDigits(digit,3);

    delay(700);
      
  } 

  Serial.print("ap+\n");

  selfTest = false;
}

//funkcja wywo�ywana przez przerwanie co 1 sekund�
ISR (TIMER1_COMPA_vect)
{
  //ignore when selftest
  if (selfTest) return;

  //sekundnik
    s++;
  clock(1);
  if (mod == 1 || (mod == 0 && (submod == 3 || submod == 4))) showClock();
  
  //prze��czanie tryb�w
  if (swapTime[mod] != 255 && mod != 0) {
    modCnt++;
    if (modCnt > swapTime[mod]) {
      modCnt = 0;
    
      //przejd� do nast�pnego trybu z pomini�ciem tych dla kt�rych swap time r�wny 0
      do {
        mod++;
        if(mod > 9) mod = 1;
      } while (swapTime[mod] == 0);
      
      if(mod == 1) showClock();
      else if(mod == 2) showDate();
      else {
        //wy�lij wcze�niej zbuforowane dane na ekran
        showText(text[mod-3]);
        //od�wie� szybko dane
        modRefCnt = refreshTime[mod];
        
      }
    }
  }
  
  //od�wie�anie
  if (refreshTime[mod] != 0 && mod > 2) {
    modRefCnt++;
    if (modRefCnt > refreshTime[mod]) {
      modRefCnt = 0;
      
      //formatuj ramk� zapytania
      char f[4] = {'a',mod+'0','?','\0'};
      
      //od�wie� dane
      Serial.print(f);
      Serial.print("\n");
    
    }
  }
  
  //liczenie czasu dropu ramki
  if (dropCnt > 0) {
    dropCnt++;
    if (dropCnt > 5 ) {
      //opu�� ramk�
      frCnt = 0;
      dropCnt = 0;
      Serial.print("FDe\n");
      
      PLED &= !LED;
    }
  }
  
  //synchronizacja kropek
    blcnt = 0;
}


//funkcja wywo�ywana przez przerwanie timera0 co ok 10ms
void routine10ms() {    
    //sprawdzanie przycisk�w
    PORTD |= 0B00011100;
    
    //czy kt�ry� przycisk wci�ni�ty?
    if ((PIND | 0b11100011) == 255) {
        //nie?
        //resetuj licznik
        butCnt = 0;
        butMax = 10;
        
    } else {
        //tak
        
        //dolicz 1 do licznika, chyba �e przepe�niony
        if (butCnt != 255) butCnt++;
        
        //je�li przeliczono odpowiedni czas (lub przycisk zosta� �wie�o wci�ni�ty)
        if (butCnt == butMax) {
            
            //sprawdzaj przyciski
            if (mod == 0) {
                //tryb ustawie�
                if(!(PIND & 0b00001000)) {
                    //wci�ni�to OK - nast�pny submod
                    submod++;
          
          if (submod == 0) showYear();
          if (submod == 1 || submod == 2) showDate();
          else if (submod == 3 || submod == 4) showClock();
          else if (submod > 4) {
            sync = 1;
            submod = 0;
            mod = 1;
          }
                } else {
                    //wci�ni�to przyciski + lub -
                    if (!(PIND & 0b00010000)) {
            if (submod == 0) year++;
            if (submod == 1) month++;
            if (submod == 2) day++;
            if (submod == 3) h++;
            if (submod == 4) m++;
          } else if (!(PIND & 0b00000100)) {
            if (submod == 0) year--;
            if (submod == 1) month--;
            if (submod == 2) day--;
            if (submod == 3) h--;
            if (submod == 4) m--;
          }
                    
          //zeruj sekundnik, wykonaj obliczenia zegarowe
                    s = 0;
          clock(0);
          
          //od�wie� ekran
          if(submod == 0) showYear();
          if(submod == 1 || submod == 2) showDate();
          if(submod == 3 || submod == 4) showClock();
                    
                    //rozp�dzanie liczb
                    if (butMax <= 10) {
                        butMax = 70;    //pocz�tkowy odst�p 700ms
                    } else {
                        if (butMax > 15) butMax -= 5;    //minimalnie 150ms, skok 50ms
                    }
                    
                    //cofnij licznik
                    butCnt = 0;
                    
                }
                
                
            } else {
                //pozosta�e tryby
                if(!(PIND & 0b00001000)) {
                    //wci�ni�to ok d�u�ej ni� sekund�
                    //wej�cie do ustawie�
          submod = 0;
          mod = 0;
          showYear();
                }
                else if (!(PIND & 0b00010000)) {
                    //wci�ni�ty przycisk +  (zmie� tryb)  
          modCnt = 0;
          
          //przejd� do nast�pnego trybu z pomini�ciem tych dla kt�rych swap time r�wny 0
          do {
            mod++;
            if(mod > 9) mod = 1;
          } while (swapTime[mod] == 0);
          
          if(mod == 1) showClock();
          else if(mod == 2) showDate();
          else {
            //wy�lij wcze�niej zbuforowane dane na ekran
            showText(text[mod-3]);
            //od�wie� szybko dane
            modRefCnt = refreshTime[mod];
                        
          }
                }
                else if (!(PIND & 0b00000100)) {
                    //wci�ni�ty przycisk - (przytrzymanie synchronizuje z ntp)
                    //ustaw flag� na synchronizacje
                    sync = 2;
                    //TODO wy�lij komunikat
                    Serial.print("ti?\n");
                    
                }
            }
        
        } else {
            //dla przycisk�w kt�re musz� by� trzymane:
            if (mod != 0) {
                if (!(PIND & 0b00001000)) butMax = 100;
                if (!(PIND & 0b00000100)) butMax = 100;
            }
        }   
    }
    
    //obs�uga kropek
  blcnt++;
  
  // dzielimy okres na p�
  if (blcnt == (blmax >> 1)) {
    if(mod == 1) {
      //tryb wy�wietlania
      if (sync == 0) {
        //niezsynchronizowany - wolne miganie f=1Hz
        send(0b01010000,4);
        blmax = 100;
      }
      
      if (sync == 2) {
        //synchronizacja w toku - szybkie miganie f= 2Hz
        send(0b01010000,4);
        blmax = 50;
      }
      
    }
    
    if(mod == 0) {
      //tryb ustawie�
         if (submod == 0) send(0b11111000,4);
      else if (submod == 1) send(0b01111000,4);
      else if (submod == 2) send(0b11001000,4);
      else if (submod == 3) send(0b00011000,4);
      else if (submod == 4) send(0b01100000,4);
      
      blmax = 50;
    }
  }
  
  if(blcnt == blmax) {
    //cofamy podlicznik
    blcnt = 0;
    
    //sterowanie miganiem
    if (mod == 1 && (sync == 0 || sync == 2)) send(0b00000000,4);
    
    if (mod == 0) {
      if (submod == 0) send(0b00011000,4);
      else if (submod == 1 || submod == 2) send(0b01001000,4);
      else if (submod == 3 || submod == 4) send(0b00000000,4);
    }
  }
}


//funkcja odczytu z RS232
void uartRead()
{ 
  
  //sprawdź czy są nowe dane. Jeśli nie to zakoncz
  if (Serial.available() == 0) return;

  //jeśli są

  //dodaj bit do ramki
  fr[frCnt] = Serial.read();
  
  unsigned char recv;

  //uruchom/cofnij licznik dropu
  dropCnt = 1;
  
  //czy odebrano znak nowej linii
  if(fr[frCnt] == 10) { 
    //TODO analiza i interpretacja
    
    //je�li trzeci znak to = znaczy �e przysz�a jaka� nowa wie��
    if (fr[2] == '=') {
      
      if (fr[0] == 't' && fr[1] == 'i') {
        //aktualizacja z ntp
        //                                             0      7  10 13 16 19
        //sprawdzenie poprawno�ci formatu i d�ugo�ci. powinno by� ti=2017-09-30-15-20-03<lf>
        if (fr[7]=='-' && fr[10]=='-' && fr[13]=='-' && fr[16]=='-' && fr[19]=='-' && frCnt>=22) {
          //konwersja danych
          year  = (fr[5] - '0') * 10;
          year  += fr[6] - '0';
          
          month = (fr[8] - '0') * 10;
          month += fr[9] - '0';
          
          day   = (fr[11] - '0') * 10;
          day   += fr[12] - '0';
          
          h = (fr[14] - '0') * 10;
          h += fr[15] - '0';
          
          m = (fr[17] - '0') * 10;
          m += fr[18] - '0';
          
          s = (fr[20] - '0') * 10;
          s += fr[21] - '0';
          
          //odpowiedz ok
                    Serial.print("ok\n");
          
          //ustaw flag� sync
          sync = 3;
        } else {
          //b��d sk�adni
          Serial.print("SYe\n");
        }
      
      }
      
      else if (fr[0] == 'T') {
        //ustawienie czasu wy�wietlania dowolnego moda
        
        recv = 0;
        for(unsigned char i = 3 ; i < frCnt;i++) {
          recv *= 10;
          recv += (fr[i]-'0');
        }
        
        swapTime[fr[1]-'0'] = recv;
        
        Serial.print("ok\n");
      } 
      else if (fr[0] == 'R') {
        //ustawienie czasu od�wie�ania dowolnego moda
        
        recv = 0;
        for(unsigned char i = 3 ; i < frCnt;i++) {
          recv *= 10;
          recv += (fr[i]-'0');
        }
        
        refreshTime[fr[1]-'0'] = recv;
        
        Serial.print("ok\n");
      } 
      
      else if (fr[0] == 'A') {
        //ustawienie tekstu
        unsigned char md = fr[1] - '0';
        
        for (unsigned char i = 3; i < frCnt;i++) {
          text[md-3][i-3] = fr[i];
        }
        text[md-3][frCnt-3] = '\0';
        
        //od�wie�, je�li mod jest aktywny
        if (mod == md) showText(text[md]);
        
        //TODO
        Serial.print("ok\n");
      }
      
      else {
                //nieznane polecenie
                Serial.print("UCe\n");
            }
      
      
    }
    else if (fr[2] == '!') {
      //komunikaty, np ic! - uda�o si� po��czy�
      if (fr[0] == 'i' && fr[1] == 'c') {
        //ask for a time when connected
        Serial.print("ti?\n");
      }      
    } else {
            //nieznane polecenie
            Serial.print("ECe\n");
        }
    
    //zeruj
    frCnt = 0;  
    
  } else {
        //nast�pny znak do ramki
    frCnt++;
    if (frCnt == 32) frCnt = 0;
  }
}


//przetwarzanie zegara i kalendarza
//jumpDays okre�la czy ma dochodzi� do przeskakiwania dnia przy przepe�nienu godzin
void clock(char jumpDays) {
    
  if (s >= 60) {
    s = 0;
    m++;
  }

  if (m >= 60) {
    m = 0;
    h++;
  }

  if (h >= 24) { 
    h = 0;
    if (jumpDays) day++;
  }
  
  if (day > computeDayNum(year,month)) {
    day = 1;
    month++;
  }
  
  if (month > 12) {
    month = 1;
    if(jumpDays) year++;
  }
  
  if(year > 99) year = 0;

  if (s < 0) {
    s = 59;
    m--;
  }

  if (m < 0) {
    m = 59;
    h--;
  }
  
  if (h < 0) h = 23;
  
  if (day < 1) {
    month--;
    day = computeDayNum(year,month);
  }
  
  if (month < 1) month = 12;
  if (year < 0) year = 99;
}

//oblicza d�ugo�� dni w��cznie z latami przest�pnymi
unsigned char computeDayNum(unsigned char y,unsigned char m) {
  if (m==4 || m==6 || m==9 || m==11) return 30;
  else if (m==2) {
    if (y % 4) {
      return 28;
    } else {
      return 29;
    }
  } else return 31;
}

//pokazuje aktualny czas
void showClock(void) {
  sendDigits(h,1);
  sendDigits(m,2);
  sendDigits(s,3);
  if (mod == 1 && (sync == 1 || sync == 3)) send(0b01010000,4);
}

//pokazuje aktualn� dat�
void showDate(void) {
  //miesi�c dzielimy na dziesi�tki i jedno�ci
  char jmies, dmies;
  
  if(month > 9) {
    dmies = 1;
    jmies = month-10;
  } else {
    dmies = 0;
    jmies = month;
  }
  
  dmies |= 0b11110000;
  jmies = jmies << 4;
  jmies |= 0b00001111;
  
  send(dmies,1);
  send(jmies,2);
  sendDigits(day,3);
  
  if (mod == 2) send(0b01001000,4);
}

//pokazuje rok
void showYear(void) {
  send(0xFF,1);
  sendDigits(20,2);
  sendDigits(year,3);
}

//wy�wietla liczb� z plaintextu
void showText(char *t) {
  //przesuwamy pointer do ko�ca (czyli znacznika \0)
  char *end = t;
  while(*end != '\0') end++;
  
  char digits[6] = {0xF,0xF,0xF,0xF,0xF,0xF};
  unsigned char digindex = 6;
  char dots = 0;
  
  //teraz od ko�ca analizujemy liczb�
  for( ; end >= t; end--) {
    //je�li cyfra
    if (*end >= '0' && *end <= '9') digits[--digindex] = *end - '0';
    //je�li znak - dostaw znak 0 np -3,2 -> 03,2
    if (*end == '-') digits[--digindex] = '0'; 
    //je�li kropka lub przecinek
    if (*end == '.' || *end == ',') dots |= (0b00000100 << digindex); 
    if (digindex == 0) break; 
  }
  
  //dogaszanie zer nieznacz�cych
  dots |= (0b11111000 >> (5-digindex));
  dots &= 0b11111000;
  
  
  //zbijanie 6 p�bajt�w w 3 bajty :)
  digits[1] |= (digits[0] << 4);
  digits[3] |= (digits[2] << 4);
  digits[5] |= (digits[4] << 4);
  
  //wysy�anie
  send(digits[1],1);
  send(digits[3],2);
  send(digits[5],3);
  send(dots,4);
  
}

//wysy�a sygna� do przerzutnik�w i lamp, konwertuj�c na bcd
void sendDigits(unsigned char data, unsigned char latch)
{
  //dziel liczb� na dziesi�tki i jedno�ci
  unsigned char jedn = data % 10;
  data -= jedn;
  unsigned char dzies = data / 10;

  //je�li liczba wi�ksza ni� 100
  if (dzies > 9) dzies -= 10;
  
  //teraz z powrotem po��cz liczby
  dzies = dzies << 4;
  dzies |= jedn;
  
  //wy�lij
  send(dzies,latch);
  
}

//wysy�a dane na porty
void send(unsigned char data, unsigned char latch)
{
    //dziel liczb� na dziesi�tki i jedno�ci
  unsigned char jedn = data & 0b1111;
  unsigned char dzies = data >> 4;

  //wystawiamy zmienne na porty
  PORTB &= 0b110000;
  PORTC &= 0b110000;
  PORTB |= jedn;
  PORTC |= dzies;

  //aktywujemy bramk�
  if(latch == 1) PORTD |= 0b01000000;
  if(latch == 2) PORTC |= 0b100000;
  if(latch == 3) PORTC |= 0b010000;
  if(latch == 4) PORTD |= 0b10000000;
  
  delay(LPT);
  
  if(latch == 1) PORTD &= 0b10111111;
  if(latch == 2) PORTC &= 0b011111;
  if(latch == 3) PORTC &= 0b101111;
  if(latch == 4) PORTD &= 0b01111111;
  
  delay(LPT); 
}
