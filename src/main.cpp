/**
 * ============================================================
 *  main.cpp
 *  ESP8266 + Apple HomeKit - 4 Buton Bridge
 *  Kütüphane: arduino-homekit-esp8266 v1.2.0
 *
 *  GPIO0  (D3) — fabrika sıfırlama (boot veya 5 sn basılı tut)
 *  GPIO5  (D1) — Buton 1
 *  GPIO4  (D2) — Buton 2
 *  GPIO14 (D5) — Buton 3
 *  GPIO12 (D6) — Buton 4
 * ============================================================
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <LittleFS.h>
#include <arduino_homekit_server.h>
#include "config.h"

// ─── HomeKit Dışa Aktarımları (my_accessory.c) ───────────
extern "C" homekit_server_config_t config;
extern "C" homekit_characteristic_t name;
extern "C" homekit_characteristic_t sw1_on;
extern "C" homekit_characteristic_t sw2_on;
extern "C" homekit_characteristic_t sw3_on;
extern "C" homekit_characteristic_t sw4_on;
extern "C" void accessory_init();
extern "C" void button1_pressed();
extern "C" void button2_pressed();
extern "C" void button3_pressed();
extern "C" void button4_pressed();

// ─── Sabitler ────────────────────────────────────────────
#define NUM_BUTTONS         4
#define STATE_FILE          "/states.dat"
#define HAP_PORT            5556

static const uint32_t DEBOUNCE_MS         = 50;
static const uint32_t FACTORY_HOLD_MS     = 5000;
static const uint32_t WIFI_RETRY_MS       = 5000;
static const uint32_t HEALTH_CHECK_MS     = 60000;
static const uint8_t  HEALTH_MAX_FAILURES = 3;

// ─── Pin Dizileri ─────────────────────────────────────────
static const uint8_t BTN_PINS[NUM_BUTTONS] = {
  PIN_BTN_1, PIN_BTN_2, PIN_BTN_3, PIN_BTN_4
};

typedef void (*btn_handler_t)(void);
static const btn_handler_t BTN_HANDLERS[NUM_BUTTONS] = {
  button1_pressed, button2_pressed, button3_pressed, button4_pressed
};

// Switch karakteristik pointer dizisi (load/save kolaylığı için)
static homekit_characteristic_t* sw_chars[NUM_BUTTONS];

// ─── Buton Durum Değişkenleri ─────────────────────────────
static bool     btn_last[NUM_BUTTONS]     = {HIGH, HIGH, HIGH, HIGH};
static uint32_t btn_debounce[NUM_BUTTONS] = {0, 0, 0, 0};
static bool     btn_stable[NUM_BUTTONS]   = {HIGH, HIGH, HIGH, HIGH};
static bool     btn_active[NUM_BUTTONS]   = {false, false, false, false};

// ─── Reset Butonu Değişkenleri ────────────────────────────
static uint32_t factory_press_start = 0;
static bool     factory_active      = false;
static bool     factory_last        = HIGH;

// ─── WiFi Değişkenleri ────────────────────────────────────
static uint8_t  s_bssid[6]           = {0};
static int32_t  s_channel             = 0;
static bool     s_bssid_valid         = false;
static bool     wifi_was_disconnected = false;
static uint32_t last_wifi_check       = 0;

// ─── Sağlık Kontrolü Değişkenleri ────────────────────────
static uint32_t last_health_check    = 0;
static uint32_t last_hk_active_ms   = 0;
static uint8_t  health_fail_count   = 0;


// ─── LittleFS'ten 4 switch state'ini yükler ──────────────
void loadStates() {
  if (!LittleFS.begin()) {
    printf("[State] LittleFS baslatilamadi.\n");
    return;
  }
  File f = LittleFS.open(STATE_FILE, "r");
  if (!f) {
    printf("[State] Kayitli state yok, varsayilan kullaniliyor.\n");
    LittleFS.end();
    return;
  }
  uint8_t buf[NUM_BUTTONS];
  if (f.read(buf, NUM_BUTTONS) == NUM_BUTTONS) {
    for (int i = 0; i < NUM_BUTTONS; i++) {
      sw_chars[i]->value.bool_value = (buf[i] != 0);
    }
    printf("[State] Yuklendi: %d %d %d %d\n", buf[0], buf[1], buf[2], buf[3]);
  }
  f.close();
  LittleFS.end();
}

// ─── 4 switch state'ini LittleFS'e kaydeder ──────────────
void saveStates() {
  if (!LittleFS.begin()) return;
  File f = LittleFS.open(STATE_FILE, "w");
  if (!f) { LittleFS.end(); return; }
  uint8_t buf[NUM_BUTTONS] = {
    (uint8_t)sw_chars[0]->value.bool_value,
    (uint8_t)sw_chars[1]->value.bool_value,
    (uint8_t)sw_chars[2]->value.bool_value,
    (uint8_t)sw_chars[3]->value.bool_value
  };
  f.write(buf, NUM_BUTTONS);
  f.close();
  LittleFS.end();
  printf("[State] Kaydedildi: %d %d %d %d\n", buf[0], buf[1], buf[2], buf[3]);
}

// ─── HomeKit + WiFi verilerini silerek fabrika sıfırlaması yapar ──
void factory_reset() {
  printf("[Reset] Fabrika sifirlamasi basliyor...\n");
  homekit_storage_reset();
  WiFi.disconnect(true);
  delay(300);
  ESP.restart();
}

// ─── 2.4GHz AP'yi tarar ve BSSID/kanal bilgisini saklar ──
bool wifi_find_24ghz() {
  printf("[WiFi] 2.4GHz taranıyor: %s\n", WIFI_SSID);
  int n = WiFi.scanNetworks(false, false);
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == WIFI_SSID && WiFi.channel(i) <= 13) {
      memcpy(s_bssid, WiFi.BSSID(i), 6);
      s_channel = WiFi.channel(i);
      s_bssid_valid = true;
      printf("[WiFi] 2.4GHz bulundu — Kanal: %d\n", s_channel);
      WiFi.scanDelete();
      return true;
    }
  }
  WiFi.scanDelete();
  printf("[WiFi] 2.4GHz bulunamadi.\n");
  return false;
}

// ─── WiFi bağlantısını kurar; BSSID biliniyorsa hedefli bağlanır ──
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
    printf(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    printf("\n[WiFi] Baglandi. IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    printf("\n[WiFi] Baglanamadi!\n");
  }
}

// ─── HomeKit sunucusunu başlatır; adı MAC ile benzersiz kılar ──
void setupHomeKit() {
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
  printf("[HomeKit] Sunucu baslatildi.\n");
}

// ─── Buton debounce + toggle mantığını non-blocking yürütür ──
void handleButtons() {
  uint32_t now = millis();
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
        } else if (btn_active[i]) {
          BTN_HANDLERS[i]();   // toggle + homekit_characteristic_notify
          saveStates();        // yalnızca değişince yazar
          btn_active[i] = false;
        }
      }
    }
    btn_last[i] = reading;
  }
}

// ─── GPIO0'a 5 sn basılı tutulunca fabrika sıfırlaması yapar ──
void handleResetButton() {
  uint32_t now = millis();
  bool reading = digitalRead(PIN_FACTORY);
  if (reading == LOW && factory_last == HIGH) {
    factory_press_start = now;
    factory_active = true;
  } else if (reading == HIGH) {
    factory_active = false;
  }
  if (factory_active && (now - factory_press_start) >= FACTORY_HOLD_MS) {
    printf("[Reset] 5 sn basili tutuldu! Fabrika sifirlamasi...\n");
    factory_reset();
  }
  factory_last = reading;
}

// ─── WiFi koparsa 5sn'de bir yeniden bağlanmayı dener ────
void handleWiFiResilience() {
  uint32_t now = millis();
  if (now - last_wifi_check < WIFI_RETRY_MS) return;
  last_wifi_check = now;

  if (WiFi.status() != WL_CONNECTED) {
    wifi_was_disconnected = true;
    if (s_bssid_valid) {
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD, s_channel, s_bssid);
    } else {
      WiFi.reconnect();
    }
  } else if (wifi_was_disconnected) {
    // WiFi yeniden bağlandı → mDNS kaydı eskidi → temiz restart
    wifi_was_disconnected = false;
    printf("[WiFi] Yeniden baglandi. HomeKit icin restart...\n");
    delay(500);
    ESP.restart();
  }
}

// ─── 60sn'de bir HomeKit sağlığını kontrol eder; önce mDNS yeniler, sonra restart ──
void handleHealthCheck() {
  uint32_t now = millis();
  if (now - last_health_check < HEALTH_CHECK_MS) return;
  last_health_check = now;

  if (WiFi.status() != WL_CONNECTED) return;

  // Bağlı istemci varsa sağlıklı say
  if (arduino_homekit_connected_clients_count() > 0) {
    last_hk_active_ms = now;
    health_fail_count = 0;
    return;
  }

  // Son 60sn içinde bağlantı gördüysek henüz sorun yok
  if ((now - last_hk_active_ms) < HEALTH_CHECK_MS) return;

  health_fail_count++;
  printf("[Health] HomeKit erisim yok. (%d/%d)\n", health_fail_count, HEALTH_MAX_FAILURES);

  if (health_fail_count < HEALTH_MAX_FAILURES) {
    // Adım 1: mDNS/Bonjour servisini yenile
    String hostname = "esp8266btn";
    MDNS.begin(hostname.c_str());
    MDNS.addService("_hap", "_tcp", HAP_PORT);
    MDNS.update();
    printf("[Health] mDNS yenilendi.\n");
  } else {
    // Adım 2: 3 ardışık başarısızlık → restart
    printf("[Health] Esik asildi. Yeniden baslatiliyor...\n");
    delay(500);
    ESP.restart();
  }
}


void setup() {
  Serial.begin(115200);
  delay(100);
  printf("\n\n[Sistem] Baslatiliyor...\n");

  // Fabrika sıfırlama pinini kontrol et (boot sırasında basılıysa)
  pinMode(PIN_FACTORY, INPUT_PULLUP);
  factory_last = digitalRead(PIN_FACTORY);
  if (factory_last == LOW) {
    printf("[Reset] Boot'ta basili! 5 sn tut...\n");
    uint32_t t = millis();
    while (digitalRead(PIN_FACTORY) == LOW) {
      if (millis() - t > FACTORY_HOLD_MS) factory_reset();
    }
  }

  // 4 buton pinini yapılandır
  for (int i = 0; i < NUM_BUTTONS; i++) {
    pinMode(BTN_PINS[i], INPUT_PULLUP);
    btn_stable[i] = digitalRead(BTN_PINS[i]);
    btn_last[i]   = btn_stable[i];
  }

  // Switch karakteristik pointer dizisini doldur
  sw_chars[0] = &sw1_on;
  sw_chars[1] = &sw2_on;
  sw_chars[2] = &sw3_on;
  sw_chars[3] = &sw4_on;

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.disconnect(false);

  wifi_connect();
  setupHomeKit();

  // Kaydedilmiş state'leri yükle (HomeKit server başladıktan sonra)
  loadStates();

  last_hk_active_ms = millis();  // Başlangıçta sağlıklı kabul et
  printf("[Sistem] Hazir. Butona bas!\n");
}


void loop() {
  arduino_homekit_loop();
  yield();  // ESP8266 WiFi stack'i için — delay(5) yerine

  handleButtons();
  handleResetButton();
  handleWiFiResilience();
  handleHealthCheck();
}
