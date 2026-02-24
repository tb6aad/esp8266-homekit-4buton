/**
 * ============================================================
 *  main.cpp
 *  ESP8266 + Apple HomeKit - 4 Buton Bridge
 *
 *  GPIO0  (D3) — fabrika sıfırlama (boot'ta veya 5 sn basılı tut)
 *  GPIO5  (D1) — Buton 1
 *  GPIO4  (D2) — Buton 2
 *  GPIO14 (D5) — Buton 3
 *  GPIO12 (D6) — Buton 4
 *
 *  Fabrika sıfırlamak için GPIO0 (D3) butonuna 5 sn basılı tutun.
 * ============================================================
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <arduino_homekit_server.h>
#include "config.h"

// ─── HomeKit Dışa Aktarımları ────────────────────────────
extern "C" homekit_server_config_t config;
extern "C" homekit_characteristic_t name;
extern "C" void accessory_init();
extern "C" void button1_pressed();
extern "C" void button2_pressed();
extern "C" void button3_pressed();
extern "C" void button4_pressed();

// ─── 4 Buton Tanımları ───────────────────────────────────
#define NUM_BUTTONS 4

static const uint8_t BTN_PINS[NUM_BUTTONS] = {
  PIN_BTN_1, PIN_BTN_2, PIN_BTN_3, PIN_BTN_4
};

typedef void (*btn_handler_t)(void);
static const btn_handler_t BTN_HANDLERS[NUM_BUTTONS] = {
  button1_pressed, button2_pressed, button3_pressed, button4_pressed
};

static bool     btn_last[NUM_BUTTONS]    = {HIGH, HIGH, HIGH, HIGH};
static uint32_t btn_debounce[NUM_BUTTONS]= {0, 0, 0, 0};
static bool     btn_stable[NUM_BUTTONS]  = {HIGH, HIGH, HIGH, HIGH};
static bool     btn_active[NUM_BUTTONS]  = {false, false, false, false};

// ─── Sabite Tanımları ────────────────────────────────────
static const uint32_t DEBOUNCE_MS = 50;
static const uint32_t FACTORY_MS  = 5000;

// ─── WiFi Band Kilitleme ─────────────────────────────────
static uint8_t  s_bssid[6]   = {0};
static int32_t  s_channel     = 0;
static bool     s_bssid_valid = false;

// ─── Zamanlayıcılar ──────────────────────────────────────
static const uint32_t WIFI_RETRY_INTERVAL_MS    = 10000;
static const uint32_t HOMEKIT_CHECK_INTERVAL_MS = 30000;
static const uint8_t  HOMEKIT_MAX_DISCONNECTED  = 3;   // 90sn (önceki 180sn)

static uint32_t last_wifi_check       = 0;
static uint32_t last_hk_check         = 0;
static uint8_t  hk_disconnected_count = 0;
static bool     wifi_was_disconnected = false;  // WiFi kopma takibi

// ─── Fabrika Sıfırlama (loop) ─────────────────────────────
static uint32_t factory_press_start = 0;
static bool     factory_active      = false;
static bool     factory_last        = HIGH;

// ─── HomeKit Storage Sıfırla ─────────────────────────────
// Kütüphanenin kendi API'si kullanılır — ham EEPROM silme yok.
// Ham EEPROM silme güç kesiminde storage bozulmasına yol açıyordu.
void factory_reset() {
  Serial.println("[Reset] HomeKit eslestirme siliniyor...");
  homekit_storage_reset();
  Serial.println("[Reset] Silindi! Yeniden baslatiliyor...");
  delay(500);
  ESP.restart();
}

bool wifi_find_24ghz() {
  Serial.printf("[WiFi] 2.4GHz taranıyor: %s\n", WIFI_SSID);
  int n = WiFi.scanNetworks(false, false);
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == WIFI_SSID && WiFi.channel(i) <= 13) {
      memcpy(s_bssid, WiFi.BSSID(i), 6);
      s_channel = WiFi.channel(i);
      s_bssid_valid = true;
      Serial.printf("[WiFi] 2.4GHz bulundu — Kanal: %d\n", s_channel);
      WiFi.scanDelete();
      return true;
    }
  }
  WiFi.scanDelete();
  Serial.println("[WiFi] 2.4GHz bulunamadı, standart bağlantı deneniyor.");
  return false;
}

void wifi_connect() {
  if (!s_bssid_valid) wifi_find_24ghz();
  if (s_bssid_valid) {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD, s_channel, s_bssid);
  } else {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
  uint8_t attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] Bağlandı. IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n[WiFi] Bağlantı kurulamadı!");
  }
}

void homekit_setup() {
  accessory_init();
  uint8_t mac[WL_MAC_ADDR_LENGTH];
  WiFi.macAddress(mac);
  int name_len = snprintf(NULL, 0, "%s_%02X%02X%02X",
                          name.value.string_value, mac[3], mac[4], mac[5]);
  char *name_value = (char *)malloc(name_len + 1);
  if (name_value) {
    snprintf(name_value, name_len + 1, "%s_%02X%02X%02X",
             name.value.string_value, mac[3], mac[4], mac[5]);
    name.value.string_value = name_value;
  }
  arduino_homekit_setup(&config);
  Serial.println("[HomeKit] Sunucu başlatıldı.");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n\n[Sistem] Başlatılıyor...");

  // Fabrika sıfırlama pini (GPIO0)
  pinMode(PIN_FACTORY, INPUT_PULLUP);
  factory_last = digitalRead(PIN_FACTORY);
  if (factory_last == LOW) {
    Serial.println("[Reset] Buton basili! 5 sn tutarsan fabrika sifirlamasi yapilir...");
    uint32_t t = millis();
    while (digitalRead(PIN_FACTORY) == LOW) {
      if (millis() - t > FACTORY_MS) {
        factory_reset();
      }
    }
  }

  // 4 buton pini
  for (int i = 0; i < NUM_BUTTONS; i++) {
    pinMode(BTN_PINS[i], INPUT_PULLUP);
    btn_stable[i] = digitalRead(BTN_PINS[i]);
    btn_last[i]   = btn_stable[i];
  }

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.disconnect(false);

  wifi_connect();
  homekit_setup();

  Serial.println("[Sistem] Hazır. Butona bas!");
}

void loop() {
  arduino_homekit_loop();
  delay(5);

  uint32_t now = millis();

  // ── 4 Buton Debounce ─────────────────────────────────────
  for (int i = 0; i < NUM_BUTTONS; i++) {
    bool reading = digitalRead(BTN_PINS[i]);

    if (reading != btn_last[i]) {
      btn_debounce[i] = now;
    }

    if ((now - btn_debounce[i]) > DEBOUNCE_MS) {
      if (reading != btn_stable[i]) {
        btn_stable[i] = reading;

        if (btn_stable[i] == LOW) {
          btn_active[i] = true;
        } else {
          if (btn_active[i]) {
            BTN_HANDLERS[i]();  // Toggle
            btn_active[i] = false;
          }
        }
      }
    }

    btn_last[i] = reading;
  }

  // ── Fabrika Sıfırlama (GPIO0 uzun basış) ─────────────────
  bool factory_reading = digitalRead(PIN_FACTORY);
  if (factory_reading == LOW && factory_last == HIGH) {
    factory_press_start = now;
    factory_active = true;
  } else if (factory_reading == HIGH) {
    factory_active = false;
  }
  if (factory_active && (now - factory_press_start) >= FACTORY_MS) {
    Serial.println("[Reset] Uzun basin tespit edildi! Fabrika sifirlamasi...");
    factory_reset();
  }
  factory_last = factory_reading;

  // ── WiFi Kontrol ─────────────────────────────────────────
  if (now - last_wifi_check > WIFI_RETRY_INTERVAL_MS) {
    last_wifi_check = now;
    if (WiFi.status() != WL_CONNECTED) {
      wifi_was_disconnected = true;
      if (s_bssid_valid) {
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD, s_channel, s_bssid);
      } else {
        WiFi.reconnect();
      }
    } else if (wifi_was_disconnected) {
      // WiFi yeniden bağlandı → HomeKit mDNS kaydını bilemez → temiz restart
      wifi_was_disconnected = false;
      Serial.println("[WiFi] Yeniden baglandi. HomeKit icin yeniden baslatiliyor...");
      delay(1000);
      ESP.restart();
    }
  }

  // ── HomeKit Watchdog ──────────────────────────────────────
  if (now - last_hk_check > HOMEKIT_CHECK_INTERVAL_MS) {
    last_hk_check = now;
    if (arduino_homekit_connected_clients_count() == 0) {
      hk_disconnected_count++;
      Serial.printf("[Watchdog] HomeKit bağlantısı yok. (%d/%d)\n",
                    hk_disconnected_count, HOMEKIT_MAX_DISCONNECTED);
      if (hk_disconnected_count >= HOMEKIT_MAX_DISCONNECTED) {
        Serial.println("[Watchdog] Yeniden başlatılıyor...");
        delay(500);
        ESP.restart();
      }
    } else {
      hk_disconnected_count = 0;
    }
  }
}
