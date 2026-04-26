#include "status_indicator_controller.h"
#include "../../config/constants.h"
#include "../../system/diagnostics_controller.h"
#include "../../mqtt/manager.h"
#include "../../webserver/ota_webserver.h"
#include "../ui_manager.h"

StatusIndicatorController::StatusIndicatorController(UIManager* manager)
    : ui_manager_(manager) {}

void StatusIndicatorController::build() {
    if (!ui_manager_ || wifi_status_icon_) return;

    // WiFi-Icon an der Position des bisherigen BLE-Icons (rechts oben)
    wifi_status_icon_ = lv_label_create(lv_scr_act());
    lv_label_set_text(wifi_status_icon_, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(wifi_status_icon_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(wifi_status_icon_, lv_color_hex(THEME_COLOR_WARNING), 0);
    lv_obj_align(wifi_status_icon_, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_clear_flag(wifi_status_icon_, LV_OBJ_FLAG_CLICKABLE);
    // Immer sichtbar — zeigt immer den aktuellen WiFi-Zustand

    // Warning-Icon links davon
    warning_icon_ = lv_label_create(lv_scr_act());
    lv_label_set_text(warning_icon_, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_font(warning_icon_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(warning_icon_, lv_color_hex(THEME_COLOR_WARNING), 0);
    lv_obj_align(warning_icon_, LV_ALIGN_TOP_RIGHT, -45, 10);
    lv_obj_add_flag(warning_icon_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(warning_icon_, LV_OBJ_FLAG_CLICKABLE);

    // Webserver links davon
    webserver_status_icon_ = lv_label_create(lv_scr_act());
    lv_label_set_text(webserver_status_icon_, LV_SYMBOL_PASTE);
    lv_obj_set_style_text_font(webserver_status_icon_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(webserver_status_icon_, lv_color_hex(THEME_COLOR_WARNING), 0);
    lv_obj_align(webserver_status_icon_, LV_ALIGN_TOP_RIGHT, -80, 10);
    lv_obj_add_flag(webserver_status_icon_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(webserver_status_icon_, LV_OBJ_FLAG_CLICKABLE);

    update_wifi_status_icon();
    update_warning_icon();
    update_webserver_status_icon();
}

void StatusIndicatorController::update() {
    update_wifi_status_icon();
    update_warning_icon();
    update_webserver_status_icon();
}

void StatusIndicatorController::update_wifi_status_icon() {
    if (!wifi_status_icon_) return;

    if (mqtt_manager.is_mqtt_connected()) {
        // Vollständig verbunden (WiFi + MQTT) → grün
        lv_obj_set_style_text_color(wifi_status_icon_,
            lv_color_hex(THEME_COLOR_SUCCESS), 0);
    } else if (mqtt_manager.is_wifi_connected()) {
        // WiFi ok, MQTT noch nicht verbunden → gelb
        lv_obj_set_style_text_color(wifi_status_icon_,
            lv_color_hex(THEME_COLOR_WARNING), 0);
    } else {
        // Kein WiFi → rot
        lv_obj_set_style_text_color(wifi_status_icon_,
            lv_color_hex(THEME_COLOR_ERROR), 0);
    }
}

void StatusIndicatorController::update_warning_icon() {
    if (!ui_manager_ || !warning_icon_) return;

    if (ui_manager_->diagnostics_controller_) {
        DiagnosticCode d = ui_manager_->diagnostics_controller_
                               ->get_highest_priority_warning();
        if (d != DiagnosticCode::NONE) {
            lv_obj_clear_flag(warning_icon_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(warning_icon_, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        lv_obj_add_flag(warning_icon_, LV_OBJ_FLAG_HIDDEN);
    }
}

void StatusIndicatorController::update_webserver_status_icon() {
    if (!webserver_status_icon_) return;

    const auto status = ota_web_server.get_status();

    if (status == OtaWebServer::Status::RUNNING) {
        lv_obj_clear_flag(webserver_status_icon_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(
            webserver_status_icon_,
            lv_color_hex(THEME_COLOR_SUCCESS),
            0
        );
    } else if (status == OtaWebServer::Status::UPDATING) {
        lv_obj_clear_flag(webserver_status_icon_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(
            webserver_status_icon_,
            lv_color_hex(THEME_COLOR_ACCENT),
            0
        );
    } else {
        lv_obj_add_flag(webserver_status_icon_, LV_OBJ_FLAG_HIDDEN);
    }
}