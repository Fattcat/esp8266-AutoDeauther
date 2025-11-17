# esp8266-AutoDeauther
esp8266 - Nuke for strongest WiFi Network around

- What it does ?
  - start scanning networks,
  - it Will pick strongest RSSI signal (from all networks),
  - Start sending deauth packet to that MAC Address with strongest signal,
  - after 10 seconds it will stop, and start again scanning networks around,
  - then again pick strongest ... loop
