#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "../config/wifi_mqtt.h"
#include "../logging/grind_logging.h"
#include "../system/statistics_manager.h"

// ── Payload types that can be queued for publishing ──────────────────────────

enum class MqttPayloadType : uint8_t {
    GRIND_SESSION,
    STATISTICS,
    SYSTEM_INFO,
};

// Compact queue item: just the type + a copy of the relevant data.
// Grind session data is self-contained so we copy it directly.
struct MqttPublishRequest {
    MqttPayloadType type;

    // Only one of these is populated per request:
    GrindSession    session;        // for GRIND_SESSION
    // STATISTICS and SYSTEM_INFO read live data at publish time, no copy needed
};

// ── MqttManager ──────────────────────────────────────────────────────────────

class MqttManager {
public:
    MqttManager();
    ~MqttManager();

    // Call from main.cpp setup(), after WiFi password / broker are configured
    void init(StatisticsManager* stats_mgr);

    // FreeRTOS task entry point (called by TaskManager)
    void task_impl();

    // Thread-safe: enqueue a completed grind session for publishing.
    // Safe to call from any FreeRTOS task (Core 0 grind task → Core 1 MQTT task).
    bool publish_session(const GrindSession& session);

    // Thread-safe: request a statistics snapshot to be published immediately.
    bool publish_statistics();

    // Thread-safe: request a system-info snapshot to be published immediately.
    bool publish_system_info();
    
    // Accessors
    bool is_wifi_connected()  const { return WiFi.status() == WL_CONNECTED; }
    bool is_mqtt_connected()  { return mqtt_client_.connected(); }
    QueueHandle_t get_queue() const { return publish_queue_; }

    // Home Assistant coffee machine timer
    void handle_message(char* topic, uint8_t* payload, unsigned int length);
    bool get_coffee_timer_seconds(uint32_t& seconds) const;

private:
    // ── WiFi helpers ─────────────────────────────────────────────────────────
    void wifi_connect();
    void wifi_reconnect_if_needed();

    // ── MQTT helpers ─────────────────────────────────────────────────────────
    bool mqtt_connect();
    void mqtt_reconnect_if_needed();

    // ── HA MQTT Discovery (auto-creates entities in HA) ──────────────────────
    void publish_discovery();
    void publish_discovery_sensor(const char* object_id,
                                  const char* name,
                                  const char* state_topic,
                                  const char* value_template,
                                  const char* unit          = nullptr,
                                  const char* device_class  = nullptr,
                                  const char* icon          = nullptr);

    // ── Payload builders ─────────────────────────────────────────────────────
    void do_publish_session(const GrindSession& session);
    void do_publish_statistics();
    void do_publish_system_info();

    // ── Members ──────────────────────────────────────────────────────────────
    WiFiClient          wifi_client_;
    PubSubClient        mqtt_client_;
    StatisticsManager*  stats_mgr_           = nullptr;
    QueueHandle_t       publish_queue_       = nullptr;

    uint32_t last_wifi_attempt_ms_           = 0;
    uint32_t last_mqtt_attempt_ms_           = 0;
    uint32_t last_stats_publish_ms_          = 0;
    bool     discovery_published_            = false;

    // Coffee machine timer received from Home Assistant
    volatile bool coffee_timer_valid_         = false;
    uint32_t coffee_timer_base_seconds_       = 0;
    uint32_t coffee_timer_base_millis_        = 0;
};

// Global instance (mirrors the pattern used by BluetoothManager etc.)
extern MqttManager mqtt_manager;
