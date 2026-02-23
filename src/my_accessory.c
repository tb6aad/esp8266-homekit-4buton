/**
 * ============================================================
 *  my_accessory.c
 *  ESP8266 + Apple HomeKit - 4 Buton Bridge
 *
 *  ESP8266 bridge olarak 4 ayrı switch aksesuarı yayınlar.
 *  Her buton bağımsız HomeKit switch'idir.
 *
 *  NOT: .c dosyası olarak kalmalıdır (HomeKit makroları C99).
 * ============================================================
 */

#include <Arduino.h>
#include <homekit/homekit.h>
#include <homekit/characteristics.h>

// ─── Bridge Adı ──────────────────────────────────────────
#define BRIDGE_NAME  "Butonlar"

homekit_characteristic_t name = HOMEKIT_CHARACTERISTIC_(NAME, BRIDGE_NAME);

// ─── 4 Switch Karakteristikleri ──────────────────────────
homekit_characteristic_t sw1_on = HOMEKIT_CHARACTERISTIC_(ON, false);
homekit_characteristic_t sw2_on = HOMEKIT_CHARACTERISTIC_(ON, false);
homekit_characteristic_t sw3_on = HOMEKIT_CHARACTERISTIC_(ON, false);
homekit_characteristic_t sw4_on = HOMEKIT_CHARACTERISTIC_(ON, false);

// ─── Prototip ────────────────────────────────────────────
void accessory_identify(homekit_value_t _value);

// ─── HomeKit Aksesuar Tanımı ─────────────────────────────
homekit_accessory_t *accessories[] = {

  // ── Bridge (ana cihaz) ──────────────────────────────────
  HOMEKIT_ACCESSORY(
    .id       = 1,
    .category = homekit_accessory_category_bridge,
    .services = (homekit_service_t*[]) {
      HOMEKIT_SERVICE(ACCESSORY_INFORMATION,
        .characteristics = (homekit_characteristic_t*[]) {
          &name,
          HOMEKIT_CHARACTERISTIC(MANUFACTURER,      "boycar"),
          HOMEKIT_CHARACTERISTIC(MODEL,             "ESP8266-4Btn"),
          HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER,     "BTN4-000"),
          HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION, "1.0"),
          HOMEKIT_CHARACTERISTIC(IDENTIFY, accessory_identify),
          NULL
        }
      ),
      NULL
    }
  ),

  // ── Switch 1 ────────────────────────────────────────────
  HOMEKIT_ACCESSORY(
    .id       = 2,
    .category = homekit_accessory_category_switch,
    .services = (homekit_service_t*[]) {
      HOMEKIT_SERVICE(ACCESSORY_INFORMATION,
        .characteristics = (homekit_characteristic_t*[]) {
          HOMEKIT_CHARACTERISTIC(NAME,              "Buton 1"),
          HOMEKIT_CHARACTERISTIC(MANUFACTURER,      "boycar"),
          HOMEKIT_CHARACTERISTIC(MODEL,             "ESP8266-Btn"),
          HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER,     "BTN4-SW1"),
          HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION, "1.0"),
          HOMEKIT_CHARACTERISTIC(IDENTIFY, accessory_identify),
          NULL
        }
      ),
      HOMEKIT_SERVICE(SWITCH, .primary = true,
        .characteristics = (homekit_characteristic_t*[]) {
          HOMEKIT_CHARACTERISTIC(NAME, "Buton 1"),
          &sw1_on,
          NULL
        }
      ),
      NULL
    }
  ),

  // ── Switch 2 ────────────────────────────────────────────
  HOMEKIT_ACCESSORY(
    .id       = 3,
    .category = homekit_accessory_category_switch,
    .services = (homekit_service_t*[]) {
      HOMEKIT_SERVICE(ACCESSORY_INFORMATION,
        .characteristics = (homekit_characteristic_t*[]) {
          HOMEKIT_CHARACTERISTIC(NAME,              "Buton 2"),
          HOMEKIT_CHARACTERISTIC(MANUFACTURER,      "boycar"),
          HOMEKIT_CHARACTERISTIC(MODEL,             "ESP8266-Btn"),
          HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER,     "BTN4-SW2"),
          HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION, "1.0"),
          HOMEKIT_CHARACTERISTIC(IDENTIFY, accessory_identify),
          NULL
        }
      ),
      HOMEKIT_SERVICE(SWITCH, .primary = true,
        .characteristics = (homekit_characteristic_t*[]) {
          HOMEKIT_CHARACTERISTIC(NAME, "Buton 2"),
          &sw2_on,
          NULL
        }
      ),
      NULL
    }
  ),

  // ── Switch 3 ────────────────────────────────────────────
  HOMEKIT_ACCESSORY(
    .id       = 4,
    .category = homekit_accessory_category_switch,
    .services = (homekit_service_t*[]) {
      HOMEKIT_SERVICE(ACCESSORY_INFORMATION,
        .characteristics = (homekit_characteristic_t*[]) {
          HOMEKIT_CHARACTERISTIC(NAME,              "Buton 3"),
          HOMEKIT_CHARACTERISTIC(MANUFACTURER,      "boycar"),
          HOMEKIT_CHARACTERISTIC(MODEL,             "ESP8266-Btn"),
          HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER,     "BTN4-SW3"),
          HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION, "1.0"),
          HOMEKIT_CHARACTERISTIC(IDENTIFY, accessory_identify),
          NULL
        }
      ),
      HOMEKIT_SERVICE(SWITCH, .primary = true,
        .characteristics = (homekit_characteristic_t*[]) {
          HOMEKIT_CHARACTERISTIC(NAME, "Buton 3"),
          &sw3_on,
          NULL
        }
      ),
      NULL
    }
  ),

  // ── Switch 4 ────────────────────────────────────────────
  HOMEKIT_ACCESSORY(
    .id       = 5,
    .category = homekit_accessory_category_switch,
    .services = (homekit_service_t*[]) {
      HOMEKIT_SERVICE(ACCESSORY_INFORMATION,
        .characteristics = (homekit_characteristic_t*[]) {
          HOMEKIT_CHARACTERISTIC(NAME,              "Buton 4"),
          HOMEKIT_CHARACTERISTIC(MANUFACTURER,      "boycar"),
          HOMEKIT_CHARACTERISTIC(MODEL,             "ESP8266-Btn"),
          HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER,     "BTN4-SW4"),
          HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION, "1.0"),
          HOMEKIT_CHARACTERISTIC(IDENTIFY, accessory_identify),
          NULL
        }
      ),
      HOMEKIT_SERVICE(SWITCH, .primary = true,
        .characteristics = (homekit_characteristic_t*[]) {
          HOMEKIT_CHARACTERISTIC(NAME, "Buton 4"),
          &sw4_on,
          NULL
        }
      ),
      NULL
    }
  ),

  NULL
};

// ─── HomeKit Sunucu Yapılandırması ───────────────────────
homekit_server_config_t config = {
  .accessories = accessories,
  .password    = "459-27-836",
  .setupId     = "BTN4"
};

// ─── Tanımlama Fonksiyonu ────────────────────────────────
void accessory_identify(homekit_value_t _value) {
  ets_printf("[HomeKit] Kimlik testi.\n");
}

// ─── GPIO Başlangıç ──────────────────────────────────────
void accessory_init() {
  ets_printf("[GPIO] 4 buton pini yapilandirildi.\n");
}

// ─── Buton Toggle Fonksiyonları ──────────────────────────
void button1_pressed() {
  bool s = !sw1_on.value.bool_value;
  sw1_on.value = HOMEKIT_BOOL(s);
  homekit_characteristic_notify(&sw1_on, sw1_on.value);
  ets_printf("[Btn1] -> %s\n", s ? "ON" : "OFF");
}

void button2_pressed() {
  bool s = !sw2_on.value.bool_value;
  sw2_on.value = HOMEKIT_BOOL(s);
  homekit_characteristic_notify(&sw2_on, sw2_on.value);
  ets_printf("[Btn2] -> %s\n", s ? "ON" : "OFF");
}

void button3_pressed() {
  bool s = !sw3_on.value.bool_value;
  sw3_on.value = HOMEKIT_BOOL(s);
  homekit_characteristic_notify(&sw3_on, sw3_on.value);
  ets_printf("[Btn3] -> %s\n", s ? "ON" : "OFF");
}

void button4_pressed() {
  bool s = !sw4_on.value.bool_value;
  sw4_on.value = HOMEKIT_BOOL(s);
  homekit_characteristic_notify(&sw4_on, sw4_on.value);
  ets_printf("[Btn4] -> %s\n", s ? "ON" : "OFF");
}
