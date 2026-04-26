#pragma once
#include <lvgl.h>

class UIManager;

// Shows WiFi connection status icon with color coding
// Shows diagnostic warning icon when issues detected
// Shows if weberserver is on

class StatusIndicatorController {
public:
    explicit StatusIndicatorController(UIManager* manager);

    void build();
    void update();

private:
    void update_wifi_status_icon();
    void update_warning_icon();
    void update_webserver_status_icon();

    UIManager* ui_manager_;
    lv_obj_t* wifi_status_icon_ = nullptr;
    lv_obj_t* warning_icon_ = nullptr;
    lv_obj_t* webserver_status_icon_ = nullptr;
};