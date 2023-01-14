#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>                

//control LED // dioda kontrolna
#define PLED PORTD
#define LED 0b00100000

//latch pulse time (in ms) // czas impulsu otwierającego przerzutniki w ms
#define LPT 1

//aktualna godzina, minuta i sekunda
signed char h;
signed char m;
signed char s;
//aktualny dzień i miesiąc
signed char day;
signed char month;
signed char year;

/*
flaga synchronizacji/ustawienia zegara
0 - nieustawiony, brak połączenie z ntp
1 - ustawiony ręcznie
2 - nieustawiony, ale trwa łączenie z ntp (domyślnie)
3 - ustawiony z ntp
*/
unsigned char sync = 2;

//MODY: zegar może wyświetlać różne wartości pobierane przez esp z neta
//domyślnie 0 to ustawianie zegara, 1 to zegar a 2 to data, tryby od 3 do 9 można programować dowolnie.
//przy uruchomieniu włącza się tryb 1
unsigned char mod = 1;

//submod: dla modu 0 (ustawień) dodatkowa zmienna: 0-miesiąc, 1-dzień, 2-godzina, 3-minuta, 4-zapisywanie 
unsigned char submod = 0;

//bufor pamięci wyświetlanych tekstów - 7 x 10 bajtów
char text[7][10] = {"0","0","0","0","0","0","0"};

//czas wyświetlania (0 - nie wyświetlaj, 255 - wyświetlaj aż do wciśnięcia przycisku)
unsigned char swapTime[10] = {255,255,5,0,0,0,0,0,0,0};
unsigned char modCnt = 0;

//czas odświerzania (dotyczy tylko trybów 2-9, dla komórek 0 i 1 nie mają znaczenia)
unsigned char refreshTime[10] = {0,0,0,0,0,0,0,0,0,0};
unsigned char modRefCnt = 0;

//ramka danych odbioru rs232, licznik ramki oraz licznik czasu opuszczenia ramki
char fr[32];
unsigned char frCnt = 0;
unsigned char dropCnt = 0;

//ramka danych nadawania rs232;
volatile char tfr[10];
volatile unsigned char tfrCnt = 0;

//dodatkowy licznik dla timera0 oraz maksymalny czas liczenia
unsigned char blcnt = 0;
unsigned char blmax = 10;

//licznik dla przycisków
unsigned char butCnt = 0;
unsigned char butMax = 0;

void showClock(void);
void showDate(void);
void showYear(void);
void showText(char *t);

void clock(char jumpDays);
unsigned char computeDayNum(unsigned char y,unsigned char m);
void rsSend(char *command);

void sendDigits(unsigned char data, unsigned char latch);
void send(unsigned char data, unsigned char latch);

int main(void) {
	
	//włącz timer0
	TIMSK |= (1 << TOIE0);

	//ustaw preskaler na f/1024
	TCCR0 |= (1 << CS02) | (1 << CS00);
	
	//timer1: liczbę cykli przerwania obliczamy na podstawie prędkości zegara
	//przesuwamy częśtotliwość zegara o 10 bitów, bo preskaler jest ustawiony na f/1024 (7812 cykli)
	OCR1A = F_CPU >> 10;
	
	//włącz timer1, ustaw tryb 4 (ctc)
    TCCR1B |= (1 << WGM12);
    TIMSK |= (1 << OCIE1A);
	
	//ustaw preskaler na f/1024
    TCCR1B |= (1 << CS12) | (1 << CS10);

	//rs232
	UCSRC=(1<<URSEL) | (1<<UCSZ1) | (1<<UCSZ0);
	UBRRL = 51;
	UCSRB = (1<<RXEN) | (1<<TXEN) | (1<<RXCIE);
	
    sei();
    // włącz cały system przerwań
  
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

	//procedury startowe
    _delay_ms(500);
    rsSend("ti?");

	while(1) {
        
        //obsługa transmisji danych
        
        if (tfrCnt > 0) {
        
            for (unsigned char i = 0; tfr[i] != '\0' && i <=tfrCnt; i++) {
                while ( !( UCSRA & (1<<UDRE)) ) ;
                UDR = tfr[i];
            }
            
            tfrCnt = 0;
            PLED &= !LED;            
        }
    }
}

//funkcja wywoływana przez przerwanie co 1 sekundę
ISR (TIMER1_COMPA_vect)
{
	//sekundnik
    s++;
	clock(1);
	if (mod == 1 || (mod == 0 && (submod == 3 || submod == 4))) showClock();
	
	//przełączanie trybów
	if (swapTime[mod] != 255 && mod != 0) {
		modCnt++;
		if (modCnt > swapTime[mod]) {
			modCnt = 0;
		
			//przejdź do następnego trybu z pominięciem tych dla których swap time równy 0
			do {
				mod++;
				if(mod > 9) mod = 1;
			} while (swapTime[mod] == 0);
			
			if(mod == 1) showClock();
			else if(mod == 2) showDate();
			else {
				//wyślij wcześniej zbuforowane dane na ekran
				showText(text[mod-3]);
				//odśwież szybko dane
				modRefCnt = refreshTime[mod];
				
			}
		}
	}
	
	//odświeżanie
	if (refreshTime[mod] != 0 && mod > 2) {
		modRefCnt++;
		if (modRefCnt > refreshTime[mod]) {
			modRefCnt = 0;
			
			//formatuj ramkę zapytania
			char f[4] = {'a',mod+'0','?','\0'};
			
			//odśwież dane
			rsSend(f);
		
		}
	}
	
	//liczenie czasu dropu ramki
	if (dropCnt > 0) {
		dropCnt++;
		if (dropCnt > 5 ) {
			//opuść ramkę
			frCnt = 0;
			dropCnt = 0;
			rsSend("FDe");
			
			PLED &= !LED;
		}
	}
	
	//synchronizacja kropek
    blcnt = 0;
}


//funkcja wywoływana przez przerwanie timera0 co ok 10ms

ISR (TIMER0_OVF_vect)
{
	//przesuń timer tak by impuls był co 10ms
	TCNT0 = 180;
    
    //sprawdzanie przycisków
    PORTD |= 0B00011100;
    
    //czy któryś przycisk wciśnięty?
    if ((PIND | 0b11100011) == 255) {
        //nie?
        //resetuj licznik
        butCnt = 0;
        butMax = 10;
        
    } else {
        //tak
        
        //dolicz 1 do licznika, chyba że przepełniony
        if (butCnt != 255) butCnt++;
        
        //jeśli przeliczono odpowiedni czas (lub przycisk został świeżo wciśnięty)
        if (butCnt == butMax) {
            
            //sprawdzaj przyciski
            if (mod == 0) {
                //tryb ustawień
                if(!(PIND & 0b00001000)) {
                    //wciśnięto OK - następny submod
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
                    //wciśnięto przyciski + lub -
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
					
					//odśwież ekran
					if(submod == 0) showYear();
					if(submod == 1 || submod == 2) showDate();
					if(submod == 3 || submod == 4) showClock();
                    
                    //rozpędzanie liczb
                    if (butMax <= 10) {
                        butMax = 70;    //początkowy odstęp 700ms
                    } else {
                        if (butMax > 15) butMax -= 5;    //minimalnie 150ms, skok 50ms
                    }
                    
                    //cofnij licznik
                    butCnt = 0;
                    
                }
                
                
            } else {
                //pozostałe tryby
                if(!(PIND & 0b00001000)) {
                    //wciśnięto ok dłużej niż sekundę
                    //wejście do ustawień
					submod = 0;
					mod = 0;
					showYear();
                }
                else if (!(PIND & 0b00010000)) {
                    //wciśnięty przycisk +  (zmień tryb)	
					modCnt = 0;
					
					//przejdź do następnego trybu z pominięciem tych dla których swap time równy 0
					do {
						mod++;
						if(mod > 9) mod = 1;
					} while (swapTime[mod] == 0);
					
					if(mod == 1) showClock();
					else if(mod == 2) showDate();
					else {
						//wyślij wcześniej zbuforowane dane na ekran
						showText(text[mod-3]);
						//odśwież szybko dane
						modRefCnt = refreshTime[mod];
                        
					}
                }
                else if (!(PIND & 0b00000100)) {
                    //wciśnięty przycisk - (przytrzymanie synchronizuje z ntp)
                    //ustaw flagę na synchronizacje
                    sync = 2;
                    //TODO wyślij komunikat
                    rsSend("ti?");
                    
                }
            }
        
        } else {
            //dla przycisków które muszą być trzymane:
            if (mod != 0) {
                if (!(PIND & 0b00001000)) butMax = 100;
                if (!(PIND & 0b00000100)) butMax = 100;
            }
        }   
    }
    
    //obsługa kropek
	blcnt++;
	
	// dzielimy okres na pół
	if (blcnt == (blmax >> 1)) {
		if(mod == 1) {
			//tryb wyświetlania
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
			//tryb ustawień
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


//funkcja wywoływana po odebraniu znaku
SIGNAL (SIG_UART_RECV)
{	
    unsigned char recv;

	//dodaj bit do ramki
	fr[frCnt] = UDR;
	
	//uruchom/cofnij licznik dropu
	dropCnt = 1;
	
	//czy odebrano znak nowej linii
	if(fr[frCnt] == 10) {	
		//TODO analiza i interpretacja
		
		//jeśli trzeci znak to = znaczy że przyszła jakaś nowa wieść
		if (fr[2] == '=') {
			
			if (fr[0] == 't' && fr[1] == 'i') {
				//aktualizacja z ntp
				//                                             0      7  10 13 16 19
				//sprawdzenie poprawności formatu i długości. powinno być ti=2017-09-30-15-20-03<lf>
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
                    rsSend("ok");
					
					//ustaw flagę sync
					sync = 3;
				} else {
					//błąd składni
					rsSend("SYe");
				}
			
			}
			
			else if (fr[0] == 'T') {
				//ustawienie czasu wyświetlania dowolnego moda
				
				recv = 0;
				for(unsigned char i = 3 ; i < frCnt;i++) {
					recv *= 10;
					recv += (fr[i]-'0');
				}
				
				swapTime[fr[1]-'0'] = recv;
				
				rsSend("ok");
			} 
			else if (fr[0] == 'R') {
				//ustawienie czasu odświeżania dowolnego moda
				
				recv = 0;
				for(unsigned char i = 3 ; i < frCnt;i++) {
					recv *= 10;
					recv += (fr[i]-'0');
				}
				
				refreshTime[fr[1]-'0'] = recv;
				
				rsSend("ok");
			} 
			
			else if (fr[0] == 'A') {
				//ustawienie tekstu
				unsigned char md = fr[1] - '0';
				
				for (unsigned char i = 3; i < frCnt;i++) {
					text[md-3][i-3] = fr[i];
				}
				text[md-3][frCnt-3] = '\0';
				
				//odśwież, jeśli mod jest aktywny
				if (mod == md) showText(text[md]);
				
				//TODO
				rsSend("ok");
			}
			
			else {
                //nieznane polecenie
                rsSend("UCe");
            }
			
			
		}
		else if (fr[2] == '!') {
			//komunikaty, np ic! - udało się połączyć
		} else {
            //nieznane polecenie
            rsSend("ECe");
        }
		
		//zeruj
		frCnt = 0;	
		
	} else {
        //następny znak do ramki
		frCnt++;
		if (frCnt == 32) frCnt = 0;
	}
}

//wysyła komendę przez rs232 (do ESP)

void rsSend(char *command) {
    PLED |= LED;

    //zeruj licznik znaków
    tfrCnt = 0;

    //przepisz bit po bicie informację do bufora
    while (*command != '\0' && tfrCnt < 9) {
        tfr[tfrCnt] = *command;
        tfrCnt++;
        command++;
    }
    
    //dodaj znak nowej linii
    tfr[tfrCnt++] = '\n';
    tfr[tfrCnt] = '\0';
    //teraz w tfpCnt znajduje się długość ramki (bez \0)
    //resztą zajmie się pętla główna
}



//przetwarzanie zegara i kalendarza
//jumpDays określa czy ma dochodzić do przeskakiwania dnia przy przepełnienu godzin
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

//oblicza długość dni włącznie z latami przestępnymi
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

//pokazuje aktualną datę
void showDate(void) {
	//miesiąc dzielimy na dziesiątki i jedności
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

//wyświetla liczbę z plaintextu
void showText(char *t) {
	//przesuwamy pointer do końca (czyli znacznika \0)
	char *end = t;
	while(*end != '\0') end++;
	
	char digits[6] = {0xF,0xF,0xF,0xF,0xF,0xF};
	unsigned char digindex = 6;
	char dots = 0;
	
	//teraz od końca analizujemy liczbę
	for( ; end >= t; end--) {
		//jeśli cyfra
		if (*end >= '0' && *end <= '9') digits[--digindex] = *end - '0';
		//jeśli znak - dostaw znak 0 np -3,2 -> 03,2
		if (*end == '-') digits[--digindex] = '0'; 
		//jeśli kropka lub przecinek
		if (*end == '.' || *end == ',') dots |= (0b00000100 << digindex);	
		if (digindex == 0) break;	
	}
	
	//dogaszanie zer nieznaczących
	dots |= (0b11111000 >> (5-digindex));
	dots &= 0b11111000;
	
	
	//zbijanie 6 półbajtów w 3 bajty :)
	digits[1] |= (digits[0] << 4);
	digits[3] |= (digits[2] << 4);
	digits[5] |= (digits[4] << 4);
	
	//wysyłanie
	send(digits[1],1);
	send(digits[3],2);
	send(digits[5],3);
	send(dots,4);
	
}

//wysyła sygnał do przerzutników i lamp, konwertując na bcd
void sendDigits(unsigned char data, unsigned char latch)
{
	//dziel liczbę na dziesiątki i jedności
	unsigned char jedn = data % 10;
	data -= jedn;
	unsigned char dzies = data / 10;

	//jeśli liczba większa niż 100
	if (dzies > 9) dzies -= 10;
	
	//teraz z powrotem połącz liczby
	dzies = dzies << 4;
	dzies |= jedn;
	
	//wyślij
	send(dzies,latch);
  
}

//wysyła dane na porty
void send(unsigned char data, unsigned char latch)
{
    //dziel liczbę na dziesiątki i jedności
	unsigned char jedn = data & 0b1111;
	unsigned char dzies = data >> 4;

	//wystawiamy zmienne na porty
	PORTB &= 0b110000;
	PORTC &= 0b110000;
	PORTB |= jedn;
	PORTC |= dzies;

	//aktywujemy bramkę
	if(latch == 1) PORTD |= 0b01000000;
	if(latch == 2) PORTC |= 0b100000;
	if(latch == 3) PORTC |= 0b010000;
	if(latch == 4) PORTD |= 0b10000000;
	
	_delay_ms(LPT);
	
	if(latch == 1) PORTD &= 0b10111111;
	if(latch == 2) PORTC &= 0b011111;
	if(latch == 3) PORTC &= 0b101111;
	if(latch == 4) PORTD &= 0b01111111;
	
	_delay_ms(LPT);	
}
