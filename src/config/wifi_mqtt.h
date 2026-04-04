#pragma once

//==============================================================================
// WIFI & MQTT CONFIGURATION
//==============================================================================
// Configure your home network and Home Assistant MQTT broker here.
// These settings are compiled into the firmware - rebuild after changing.

//------------------------------------------------------------------------------
// WIFI CREDENTIALS
//------------------------------------------------------------------------------
#define WIFI_SSID           "Mein WLAN"
#define WIFI_PASSWORD       "Dim2.sPmPuE!"

// Connection timeout before giving up and continuing without WiFi
#define WIFI_CONNECT_TIMEOUT_MS     15000   // 15 seconds
// How often to retry a lost connection (milliseconds)
#define WIFI_RECONNECT_INTERVAL_MS  30000   // 30 seconds

//------------------------------------------------------------------------------
// MQTT BROKER (Home Assistant)
//------------------------------------------------------------------------------
#define MQTT_BROKER_HOST    "192.168.178.159"  // IP of your Home Assistant server
#define MQTT_BROKER_PORT    1883
#define MQTT_USERNAME       "homeassistant"      // MQTT username in HA
#define MQTT_PASSWORD       "yAjG2wq3f!KiAoNzDW35"  // MQTT password in HA
#define MQTT_CLIENT_ID      "grinder"  // Unique ID for this device

// How often to publish statistics & system info even without a new grind
#define MQTT_STATS_PUBLISH_INTERVAL_MS   600000  // Every 10 minutes

//------------------------------------------------------------------------------
// MQTT TOPICS
//------------------------------------------------------------------------------
// Device base topic - all messages live under this prefix
#define MQTT_BASE_TOPIC             "grinder"

// Published topics (ESP32 → Home Assistant)
#define MQTT_TOPIC_SESSION          MQTT_BASE_TOPIC "/session"      // After each grind
#define MQTT_TOPIC_STATS            MQTT_BASE_TOPIC "/statistics"   // Lifetime stats
#define MQTT_TOPIC_SYSTEM           MQTT_BASE_TOPIC "/system"       // System info
#define MQTT_TOPIC_STATUS           MQTT_BASE_TOPIC "/status"       // Online/offline
#define MQTT_TOPIC_ERRORS           MQTT_BASE_TOPIC "/errors"       // System errors

// Home Assistant MQTT Discovery prefix (leave as-is unless you changed it in HA)
#define MQTT_DISCOVERY_PREFIX       "homeassistant"

//------------------------------------------------------------------------------
// FREERTOS TASK CONFIGURATION FOR MQTT
//------------------------------------------------------------------------------
#define SYS_TASK_MQTT_STACK_SIZE    8192    // 8KB stack (ArduinoJson + PubSubClient)
#define SYS_TASK_MQTT_PRIORITY      1       // Low priority (same as FileIO)
#define SYS_TASK_MQTT_INTERVAL_MS   500     // Check queue / reconnect every 500ms
#define SYS_QUEUE_MQTT_SIZE         5       // Max pending MQTT publish requests
