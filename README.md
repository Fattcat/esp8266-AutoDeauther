# esp8266-AutoDeauther
esp8266 - Nuke for strongest WiFi Network around

<p align="center">
  <img src="https://visitor-badge.laobi.icu/badge?page_id=Fattcat.esp8266-AutoDeauther" alt="Visitor Count">
  
- What it does ?
  - start scanning networks,
  - it Will pick strongest RSSI signal (from all networks),
  - Start sending deauth packet to that MAC Address with strongest signal,
  - after 10 seconds it will stop, and start again scanning networks around,
  - then again pick strongest ... loop

## Whitelist
- add wifi SSIDs to whitelist if you dont want to send deauth packet to well known networks
