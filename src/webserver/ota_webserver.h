#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <esp_http_server.h>

class OtaWebServer {
public:
    enum class Status : uint8_t {
        STOPPED,
        STARTING,
        RUNNING,
        UPDATING,
        ERROR
    };

    OtaWebServer();

    bool start();
    void stop();

    bool is_running() const { return server_ != nullptr; }
    Status get_status() const { return status_; }
    const char* get_status_message() const { return status_message_.c_str(); }
    const char* get_bind_address() const { return bind_address_.c_str(); }
    uint16_t get_port() const { return port_; }

private:
    httpd_handle_t server_;
    Status status_;
    String status_message_;
    String bind_address_;
    uint16_t port_;

    static OtaWebServer* instance_;

    void set_status(Status status, const String& message);

    static void restart_task(void* param);
    static esp_err_t handle_root(httpd_req_t* req);
    static esp_err_t handle_update(httpd_req_t* req);
};

extern OtaWebServer ota_web_server;