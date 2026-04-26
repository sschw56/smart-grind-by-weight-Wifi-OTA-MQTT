#include "manager.h"
#include "../config/build_info.h"
#include <esp_system.h>
#include <esp_err.h>
#include <ctype.h>
#include <stdlib.h>
#include <mesh/utils.h>

// Global instance
MqttManager mqtt_manager;

// ── Constructor / Destructor ──────────────────────────────────────────────────

MqttManager::MqttManager()
    : mqtt_client_(wifi_client_) {}

MqttManager::~MqttManager() {
    if (publish_queue_) {
        vQueueDelete(publish_queue_);
        publish_queue_ = nullptr;
    }
}

// ── init() ────────────────────────────────────────────────────────────────────

void MqttManager::init(StatisticsManager* stats_mgr) {
    stats_mgr_ = stats_mgr;

    publish_queue_ = xQueueCreate(SYS_QUEUE_MQTT_SIZE, sizeof(MqttPublishRequest));
    if (!publish_queue_) {
        Serial.println("[MQTT] ERROR: Failed to create publish queue");
        return;
    }

    mqtt_client_.setBufferSize(1024); // Enough for JSON payloads
    mqtt_client_.setServer(MQTT_BROKER_HOST, MQTT_BROKER_PORT);

    mqtt_client_.setCallback([](char* topic, uint8_t* payload, unsigned int length) {
    mqtt_manager.handle_message(topic, payload, length);
    });
    
    Serial.println("[MQTT] Initialized");
}

// ── Public: enqueue helpers ───────────────────────────────────────────────────

bool MqttManager::publish_session(const GrindSession& session) {
    if (!publish_queue_) return false;
    MqttPublishRequest req;
    req.type    = MqttPayloadType::GRIND_SESSION;
    req.session = session; // value copy
    return xQueueSend(publish_queue_, &req, 0) == pdTRUE;
}

bool MqttManager::publish_statistics() {
    if (!publish_queue_) return false;
    MqttPublishRequest req;
    req.type = MqttPayloadType::STATISTICS;
    return xQueueSend(publish_queue_, &req, 0) == pdTRUE;
}

bool MqttManager::publish_system_info() {
    if (!publish_queue_) return false;
    MqttPublishRequest req;
    req.type = MqttPayloadType::SYSTEM_INFO;
    return xQueueSend(publish_queue_, &req, 0) == pdTRUE;
}

// ── task_impl() ───────────────────────────────────────────────────────────────

void MqttManager::task_impl() {
    Serial.printf("[MQTT] Task started on Core %d\n", xPortGetCoreID());

    // Initial WiFi connection attempt
    wifi_connect();

    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(SYS_TASK_MQTT_INTERVAL_MS);

    while (true) {
        // Keep WiFi alive
        wifi_reconnect_if_needed();
        if (is_wifi_connected()) {
            
            // Keep MQTT broker connection alive
            mqtt_reconnect_if_needed();

            if (is_mqtt_connected()) {
                mqtt_client_.loop(); // Handle MQTT keep-alive / incoming messages

                // Publish HA discovery once after first successful connection
                if (!discovery_published_) {
                    publish_discovery();
                    discovery_published_ = true;
                    // Immediately publish current stats after discovery
                    do_publish_statistics();
                    do_publish_system_info();
                    last_stats_publish_ms_ = millis();
                }

                // Drain the publish queue
                MqttPublishRequest req;
                while (xQueueReceive(publish_queue_, &req, 0) == pdTRUE) {
                    switch (req.type) {
                        case MqttPayloadType::GRIND_SESSION:
                            do_publish_session(req.session);
                            // Always update stats + system info after a grind
                            do_publish_statistics();
                            do_publish_system_info();
                            last_stats_publish_ms_ = millis();
                            break;
                        case MqttPayloadType::STATISTICS:
                            do_publish_statistics();
                            break;
                        case MqttPayloadType::SYSTEM_INFO:
                            do_publish_system_info();
                            break;
                    }
                }

                // Periodic stats publish (even without a new grind)
                uint32_t now = millis();
                if (now - last_stats_publish_ms_ >= MQTT_STATS_PUBLISH_INTERVAL_MS) {
                    do_publish_statistics();
                    do_publish_system_info();
                    last_stats_publish_ms_ = now;
                }
            }
        }

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

// ── WiFi ──────────────────────────────────────────────────────────────────────

void MqttManager::wifi_connect() {
    Serial.printf("[MQTT] Connecting to WiFi: %s\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[MQTT] WiFi connected — IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("[MQTT] WiFi connection timed out — will retry later");
    }
    last_wifi_attempt_ms_ = millis();
}

void MqttManager::wifi_reconnect_if_needed() {
    if (WiFi.status() == WL_CONNECTED) return;
    uint32_t now = millis();
    if (now - last_wifi_attempt_ms_ < WIFI_RECONNECT_INTERVAL_MS) return;

    Serial.println("[MQTT] WiFi lost — reconnecting...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    last_wifi_attempt_ms_ = now;
    discovery_published_  = false; // Re-publish discovery after reconnect
}

// ── MQTT connection ───────────────────────────────────────────────────────────

bool MqttManager::mqtt_connect() {
    Serial.printf("[MQTT] Connecting to broker %s:%d\n", MQTT_BROKER_HOST, MQTT_BROKER_PORT);

    // Last Will: publish "offline" if we disconnect unexpectedly
    bool ok = mqtt_client_.connect(
        MQTT_CLIENT_ID,
        MQTT_USERNAME,
        MQTT_PASSWORD,
        MQTT_TOPIC_STATUS, // LWT topic
        0,                 // LWT QoS
        true,              // LWT retain
        "offline"          // LWT message
    );

    if (ok) {
        Serial.println("[MQTT] Broker connected");

        // Announce we are online (retained so HA shows it immediately after HA restart)
        mqtt_client_.publish(MQTT_TOPIC_STATUS, "online", /*retain=*/true);

         // Subscribe to Home Assistant coffee machine timer
        bool sub_ok = mqtt_client_.subscribe(MQTT_TOPIC_COFFEE_TIMER);
        Serial.printf("[MQTT] Subscribe %s: %s\n",
                  MQTT_TOPIC_COFFEE_TIMER,
                  sub_ok ? "OK" : "FAILED");
    } else {
        Serial.printf("[MQTT] Broker connection failed, rc=%d\n", mqtt_client_.state());
    }
    return ok;
}

void MqttManager::mqtt_reconnect_if_needed() {
    if (mqtt_client_.connected()) return;
    uint32_t now = millis();
    if (now - last_mqtt_attempt_ms_ < 5000) return; // Retry every 5 s
    last_mqtt_attempt_ms_ = now;
    mqtt_connect();
}

// ── HA MQTT Discovery ─────────────────────────────────────────────────────────
// This tells Home Assistant to automatically create sensor entities.
// You don't need to add anything to configuration.yaml.

void MqttManager::publish_discovery() {
    Serial.println("[MQTT] Publishing Home Assistant discovery config...");

    // ── Grind session sensors ──────────────────────────────────────────────
    publish_discovery_sensor(
        "last_grind_target_weight", "Grinder: Target Weight",
        MQTT_TOPIC_SESSION, "{{ value_json.target_weight }}",
        "g", nullptr, "mdi:scale");

    publish_discovery_sensor(
        "last_grind_final_weight", "Grinder: Final Weight",
        MQTT_TOPIC_SESSION, "{{ value_json.final_weight }}",
        "g", nullptr, "mdi:scale-balance");

    publish_discovery_sensor(
        "last_grind_error", "Grinder: Weight Error",
        MQTT_TOPIC_SESSION, "{{ value_json.error_grams }}",
        "g", nullptr, "mdi:delta");

    publish_discovery_sensor(
        "last_grind_duration", "Grinder: Grind Duration",
        MQTT_TOPIC_SESSION, "{{ value_json.total_time_ms | int / 1000 | round(1) }}",
        "s", "duration", "mdi:timer-outline");

    publish_discovery_sensor(
        "last_grind_motor_time", "Grinder: Motor Time",
        MQTT_TOPIC_SESSION, "{{ value_json.motor_on_time_ms | int / 1000 | round(1) }}",
        "s", "duration", "mdi:engine");

    publish_discovery_sensor(
        "last_grind_pulses", "Grinder: Pulse Count",
        MQTT_TOPIC_SESSION, "{{ value_json.pulse_count }}",
        nullptr, nullptr, "mdi:pulse");

    publish_discovery_sensor(
        "last_grind_result", "Grinder: Last Result",
        MQTT_TOPIC_SESSION, "{{ value_json.result_status }}",
        nullptr, nullptr, "mdi:check-circle-outline");

    publish_discovery_sensor(
        "last_grind_profile", "Grinder: Profile",
        MQTT_TOPIC_SESSION, "{{ value_json.profile_id }}",
        nullptr, nullptr, "mdi:coffee");

    // ── Lifetime statistics sensors ────────────────────────────────────────
    publish_discovery_sensor(
        "stats_total_grinds", "Grinder: Total Grinds",
        MQTT_TOPIC_STATS, "{{ value_json.total_grinds }}",
        nullptr, nullptr, "mdi:counter");

    publish_discovery_sensor(
        "stats_total_weight_kg", "Grinder: Total Weight Ground",
        MQTT_TOPIC_STATS, "{{ value_json.total_weight_kg }}",
        "kg", "weight", "mdi:weight-kilogram");

    publish_discovery_sensor(
        "stats_motor_runtime", "Grinder: Motor Runtime",
        MQTT_TOPIC_STATS, "{{ value_json.motor_runtime_sec }}",
        "s", "duration", "mdi:engine");

    publish_discovery_sensor(
        "stats_avg_accuracy", "Grinder: Avg Accuracy",
        MQTT_TOPIC_STATS, "{{ value_json.avg_accuracy_g }}",
        "g", nullptr, "mdi:bullseye-arrow");

    publish_discovery_sensor(
        "stats_single_shots", "Grinder: Single Shots",
        MQTT_TOPIC_STATS, "{{ value_json.single_shots }}",
        nullptr, nullptr, "mdi:coffee-outline");

    publish_discovery_sensor(
        "stats_double_shots", "Grinder: Double Shots",
        MQTT_TOPIC_STATS, "{{ value_json.double_shots }}",
        nullptr, nullptr, "mdi:coffee");

    // ── System info sensors ────────────────────────────────────────────────
    publish_discovery_sensor(
        "sys_free_heap", "Grinder: Free Heap",
        MQTT_TOPIC_SYSTEM, "{{ value_json.free_heap_kb }}",
        "kB", nullptr, "mdi:memory");

    publish_discovery_sensor(
        "sys_uptime_hrs", "Grinder: Uptime",
        MQTT_TOPIC_SYSTEM, "{{ value_json.uptime_hrs }}",
        "h", "duration", "mdi:clock-outline");

    publish_discovery_sensor(
        "sys_wifi_rssi", "Grinder: WiFi Signal",
        MQTT_TOPIC_SYSTEM, "{{ value_json.wifi_rssi }}",
        "dBm", "signal_strength", "mdi:wifi");

    publish_discovery_sensor(
        "sys_build", "Grinder: Firmware Build",
        MQTT_TOPIC_SYSTEM, "{{ value_json.build_number }}",
        nullptr, nullptr, "mdi:package-up");

    Serial.println("[MQTT] Discovery config published — entities will appear in HA");
}

void MqttManager::publish_discovery_sensor(
    const char* object_id,
    const char* name,
    const char* state_topic,
    const char* value_template,
    const char* unit,
    const char* device_class,
    const char* icon)
{
    // Topic: homeassistant/sensor/smart_grinder_<object_id>/config
    char config_topic[128];
    snprintf(config_topic, sizeof(config_topic),
             "%s/sensor/%s_%s/config",
             MQTT_DISCOVERY_PREFIX, MQTT_CLIENT_ID, object_id);

    // Build JSON
    JsonDocument doc;
    doc["name"]           = name;
    doc["state_topic"]    = state_topic;
    doc["value_template"] = value_template;
    doc["unique_id"]      = String(MQTT_CLIENT_ID) + "_" + object_id;

    if (unit)         doc["unit_of_measurement"] = unit;
    if (device_class) doc["device_class"]        = device_class;
    if (icon)         doc["icon"]                = icon;

    // Device block — groups all sensors under one HA device entry
    JsonObject device = doc["device"].to<JsonObject>();
    device["identifiers"][0]  = MQTT_CLIENT_ID;
    device["name"]            = "Smart Grinder";
    device["model"]           = "smart-grind-by-weight";
    device["manufacturer"]    = "DIY";
    device["sw_version"]      = String(BUILD_NUMBER);

    // Availability — entity goes unavailable when ESP goes offline
    doc["availability_topic"] = MQTT_TOPIC_STATUS;
    doc["payload_available"]  = "online";
    doc["payload_not_available"] = "offline";

    //mqtt_client_.publish(config_topic, buf, true);
    String payload;
    serializeJson(doc, payload);    
    mqtt_client_.publish(config_topic, payload.c_str(), true);
    }

// ── Payload builders ──────────────────────────────────────────────────────────

void MqttManager::do_publish_session(const GrindSession& s) {
    JsonDocument doc;
    doc["session_id"]      = s.session_id;
    doc["target_weight"]   = serialized(String(s.target_weight, 2));
    doc["final_weight"]    = serialized(String(s.final_weight,  2));
    doc["error_grams"]     = serialized(String(s.error_grams,   2));
    doc["total_time_ms"]   = s.total_time_ms;
    doc["motor_on_time_ms"]= s.total_motor_on_time_ms;
    doc["pulse_count"]     = s.pulse_count;
    doc["profile_id"]      = s.profile_id;
    doc["grind_mode"]      = s.grind_mode;
    doc["result_status"]   = s.result_status;
    doc["termination"]     = s.termination_reason;

    char buf[384];
    serializeJson(doc, buf, sizeof(buf));
    bool ok = mqtt_client_.publish(MQTT_TOPIC_SESSION, buf, /*retain=*/true);
    Serial.printf("[MQTT] Session #%lu published: %s\n", s.session_id, ok ? "OK" : "FAILED");
}

void MqttManager::do_publish_statistics() {
    if (!stats_mgr_) return;
    JsonDocument doc;
    doc["total_grinds"]       = stats_mgr_->get_total_grinds();
    doc["single_shots"]       = stats_mgr_->get_single_shots();
    doc["double_shots"]       = stats_mgr_->get_double_shots();
    doc["custom_shots"]       = stats_mgr_->get_custom_shots();
    doc["total_weight_kg"]    = serialized(String(stats_mgr_->get_total_weight_kg(), 3));
    doc["motor_runtime_sec"]  = stats_mgr_->get_motor_runtime_sec();
    doc["weight_mode_grinds"] = stats_mgr_->get_weight_mode_grinds();
    doc["time_mode_grinds"]   = stats_mgr_->get_time_mode_grinds();
    doc["avg_accuracy_g"]     = serialized(String(stats_mgr_->get_avg_accuracy_g(), 3));
    doc["total_pulses"]       = stats_mgr_->get_total_pulses();

    char buf[256];
    serializeJson(doc, buf, sizeof(buf));
    bool ok = mqtt_client_.publish(MQTT_TOPIC_STATS, buf, /*retain=*/true);
    Serial.printf("[MQTT] Statistics published: %s\n", ok ? "OK" : "FAILED");
}

void MqttManager::do_publish_system_info() {
    JsonDocument doc;
    doc["build_number"]  = BUILD_NUMBER;
    doc["free_heap_kb"]  = ESP.getFreeHeap() / 1024;
    doc["uptime_hrs"]    = stats_mgr_ ? stats_mgr_->get_device_uptime_hrs() : 0;
    doc["wifi_rssi"]     = WiFi.RSSI();
    doc["wifi_ip"]       = WiFi.localIP().toString();
    doc["chip_model"]    = ESP.getChipModel();

    char buf[192];
    serializeJson(doc, buf, sizeof(buf));
    bool ok = mqtt_client_.publish(MQTT_TOPIC_SYSTEM, buf, /*retain=*/true);
    Serial.printf("[MQTT] System info published: %s\n", ok ? "OK" : "FAILED");
}

void MqttManager::handle_message(char* topic, uint8_t* payload, unsigned int length) {
    if (!topic || strcmp(topic, MQTT_TOPIC_COFFEE_TIMER) != 0) {
        return;
    }

    char buffer[16];
    unsigned int copy_len = length;
    if (copy_len >= sizeof(buffer)) {
        copy_len = sizeof(buffer) - 1;
    }

    memcpy(buffer, payload, copy_len);
    buffer[copy_len] = '\0';

    char* end_ptr = nullptr;
    unsigned long seconds = strtoul(buffer, &end_ptr, 10);

    if (end_ptr == buffer) {
        Serial.printf("[MQTT] Invalid coffee timer payload: %s\n", buffer);
        return;
    }

    coffee_timer_base_seconds_ = static_cast<uint32_t>(seconds);
    coffee_timer_base_millis_ = millis();
    coffee_timer_valid_ = true;

    Serial.printf("[MQTT] Coffee timer updated from HA: %lus\n", seconds);
}

bool MqttManager::get_coffee_timer_seconds(uint32_t& seconds) const {
    if (!coffee_timer_valid_) {
        return false;
    }

    uint32_t elapsed_seconds = (millis() - coffee_timer_base_millis_) / 1000;
    seconds = coffee_timer_base_seconds_ + elapsed_seconds;
    return true;
}