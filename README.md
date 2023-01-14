# diy_iot_nixie_clock_ntp
NTP synchronized nixie clock

This is a dump of my 6-digit nixie clock project, which
* Shows time in HH:MM:SS
* Shows date in MM-DD
* Synchronizes with NTP server via Wi-Fi.

Link to original description (in Polish): https://majsterkowo.pl/zegar-nixie-synchronizowany-z-ntp-cz-1/

## Hardware
There are 2 MCUs inside. ATMega8 controls nixie valves and switches. ESP8266 provides connection to Wi-Fi and provides syncing via NTP server (it is optional).
To drive nixie valves, there are 6x 74LS377 open-collector amplifiers and 3x 74LS377 latches inside. 

I used Z573M valves, removed from obsolete measuring equimpement. Anode voltage (~120 volts) is generated from 12V voltage by simple step-up converter.

The clock is based on ATMega8 MCU. Source code is written in "plain" C in WinAVR IDE. 
The code should be able compile using avr-gcc. 
