/**
 * Stub settings_manager.h a SchoolLive snap_app komponenshez.
 *
 * Az eredeti CarlosDerSeher settings_manager komponenst kihagytuk (a saját
 * PersistStore/NVS rétegünket használjuk), de a snap_app.c és
 * connection_handler.c néhány függvényt még közvetlenül hív belőle.
 * Ezeket itt deklaráljuk, és a snap_app.c-ben a snap_app_config_t-ből
 * adunk vissza értékeket.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A snap eszköz hostneve. snap_app_config_t.device_id-ből szolgáljuk. */
esp_err_t settings_get_hostname(char *buf, size_t buflen);

/** mDNS engedélyezés - nálunk mindig false (fix host/port). */
esp_err_t settings_get_mdns_enabled(bool *enabled);

/** Snap szerver hosztneve - snap_app_config_t.host-ból. */
esp_err_t settings_get_server_host(char *buf, size_t buflen);

/** Snap szerver portja - snap_app_config_t.port-ból.
 *  A hívóoldal int32_t-t használ, ezért int32_t*. */
esp_err_t settings_get_server_port(int32_t *port);

#ifdef __cplusplus
}
#endif
