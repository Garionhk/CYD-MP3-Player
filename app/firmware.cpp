#include "firmware.h"
#include <esp_ota_ops.h>
#include <esp_partition.h>

static const esp_partition_t* slot(esp_partition_subtype_t sub) {
  return esp_partition_find_first(ESP_PARTITION_TYPE_APP, sub, nullptr);
}

static bool valid(const esp_partition_t* p) {
  esp_app_desc_t desc;
  return p && esp_ota_get_partition_description(p, &desc) == ESP_OK;
}

bool firmware_uploaderPresent() {
  return valid(slot(ESP_PARTITION_SUBTYPE_APP_OTA_1));
}

static bool bootInto(esp_partition_subtype_t sub, bool restart) {
  const esp_partition_t* p = slot(sub);
  if (!valid(p)) {
    Serial.printf("firmware: no valid image in %s\n", p ? p->label : "(missing partition)");
    return false;
  }
  const esp_err_t err = esp_ota_set_boot_partition(p);
  if (err != ESP_OK) {
    Serial.printf("firmware: set boot partition %s failed (%d)\n", p->label, err);
    return false;
  }
  if (restart) {
    Serial.printf("firmware: restarting into %s\n", p->label);
    delay(100);
    ESP.restart();
  }
  return true;
}

bool firmware_bootUploader()   { return bootInto(ESP_PARTITION_SUBTYPE_APP_OTA_1, true); }
bool firmware_bootPlayer()     { return bootInto(ESP_PARTITION_SUBTYPE_APP_OTA_0, true); }
void firmware_nextBootPlayer() { bootInto(ESP_PARTITION_SUBTYPE_APP_OTA_0, false); }
bool firmware_bootUploaderAgain() { return firmware_bootUploader(); }
