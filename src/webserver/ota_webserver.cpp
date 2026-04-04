#include "ota_webserver.h"

#include <esp_ota_ops.h>
#include <esp_err.h>

OtaWebServer ota_web_server;
OtaWebServer* OtaWebServer::instance_ = nullptr;

static const char OTA_WEB_PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Smart Grinder OTA</title>
<style>
body{font-family:sans-serif;background:#111;color:#eee;max-width:480px;margin:40px auto;padding:16px}
h1{color:#FF3D00}
.card{background:#1b1b1b;border:1px solid #333;border-radius:8px;padding:16px}
input[type=file],button{width:100%%;box-sizing:border-box;margin:10px 0}
button{background:#FF3D00;color:#fff;border:none;padding:12px;border-radius:6px;font-size:1em}
progress{width:100%%;margin-top:12px}
#status{margin-top:12px;white-space:pre-wrap;color:#ccc}
</style>
</head>
<body>
<h1>Smart Grinder OTA</h1>
<div class="card">
  <p>Wähle eine <code>firmware.bin</code> und starte das Update.</p>
  <form id="uploadForm">
    <input type="file" id="firmware" accept=".bin" required>
    <button type="submit">Firmware flashen</button>
  </form>
  <progress id="progress" value="0" max="100" style="display:none"></progress>
  <div id="status">Bereit.</div>
</div>

<script>
(function() {
  const form = document.getElementById('uploadForm');
  const fileInput = document.getElementById('firmware');
  const progress = document.getElementById('progress');
  const status = document.getElementById('status');

  form.addEventListener('submit', function(ev) {
    ev.preventDefault();

    const file = fileInput.files[0];
    if (!file) {
      status.textContent = 'Bitte zuerst eine firmware.bin auswählen.';
      return;
    }

    status.textContent = 'Upload im Gang...';
    progress.style.display = 'block';
    progress.value = 0;

    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/update', true);
    xhr.setRequestHeader('Content-Type', 'application/octet-stream');
    xhr.setRequestHeader('X-Filename', file.name);

    xhr.upload.onprogress = function(e) {
      if (e.lengthComputable) {
        progress.value = Math.round((e.loaded / e.total) * 100);
      }
    };

    xhr.onload = function() {
      status.textContent = xhr.responseText || ('HTTP ' + xhr.status);
    };

    xhr.onerror = function() {
      status.textContent = 'Netzwerkfehler beim Upload.';
    };

    xhr.send(file);
  });
})();
</script>
</body>
</html>
)HTML";

OtaWebServer::OtaWebServer()
    : server_(nullptr),
      status_(Status::STOPPED),
      status_message_("Stopped"),
      bind_address_(""),
      port_(8080) {
    instance_ = this;
}

void OtaWebServer::set_status(Status status, const String& message) {
    status_ = status;
    status_message_ = message;
}

void OtaWebServer::restart_task(void* param) {
    (void)param;
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

bool OtaWebServer::start() {
    if (server_) {
        set_status(Status::RUNNING, "Started");
        return true;
    }

    if (WiFi.status() != WL_CONNECTED) {
        bind_address_ = "";
        set_status(Status::ERROR, "WiFi disconnected");
        Serial.println("[WEB] Start fehlgeschlagen: WiFi disconnected");
        return false;
    }

    set_status(Status::STARTING, "Starting");
    bind_address_ = "";

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port_;
    config.stack_size = 4096;
    config.max_uri_handlers = 2;
    config.max_open_sockets = 2;
    config.lru_purge_enable = true;

    esp_err_t err = httpd_start(&server_, &config);
    if (err != ESP_OK) {
        set_status(Status::ERROR, String("Start failed: ") + esp_err_to_name(err));
        server_ = nullptr;
        Serial.printf("[WEB] Server-Start fehlgeschlagen: %s (%d)\n", esp_err_to_name(err), (int)err);
        return false;
    }

    httpd_uri_t root_uri = {};
    root_uri.uri = "/";
    root_uri.method = HTTP_GET;
    root_uri.handler = OtaWebServer::handle_root;
    root_uri.user_ctx = nullptr;

    httpd_uri_t update_uri = {};
    update_uri.uri = "/update";
    update_uri.method = HTTP_POST;
    update_uri.handler = OtaWebServer::handle_update;
    update_uri.user_ctx = nullptr;

    err = httpd_register_uri_handler(server_, &root_uri);
    if (err != ESP_OK) {
        httpd_stop(server_);
        server_ = nullptr;
        set_status(Status::ERROR, String("GET handler failed: ") + esp_err_to_name(err));
        Serial.printf("[WEB] Registrierung GET / fehlgeschlagen: %s (%d)\n", esp_err_to_name(err), (int)err);
        return false;
    }

    err = httpd_register_uri_handler(server_, &update_uri);
    if (err != ESP_OK) {
        httpd_stop(server_);
        server_ = nullptr;
        set_status(Status::ERROR, String("POST handler failed: ") + esp_err_to_name(err));
        Serial.printf("[WEB] Registrierung POST /update fehlgeschlagen: %s (%d)\n", esp_err_to_name(err), (int)err);
        return false;
    }

    bind_address_ = WiFi.localIP().toString() + ":" + String(port_);
    set_status(Status::RUNNING, "Started");
    Serial.printf("[WEB] Server bereit: http://%s\n", bind_address_.c_str());
    return true;
}

void OtaWebServer::stop() {
    if (server_) {
        httpd_stop(server_);
        server_ = nullptr;
        Serial.println("[WEB] Server gestoppt");
    }

    bind_address_ = "";
    set_status(Status::STOPPED, "Stopped");
}

esp_err_t OtaWebServer::handle_root(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, OTA_WEB_PAGE, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t OtaWebServer::handle_update(httpd_req_t* req) {
    if (!instance_) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No server instance");
        return ESP_FAIL;
    }

    if (req->content_len <= 0) {
        instance_->set_status(Status::ERROR, "Upload failed: empty body");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty upload");
        return ESP_FAIL;
    }

    const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
    if (!part) {
        instance_->set_status(Status::ERROR, "Upload failed: no OTA partition");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        return ESP_FAIL;
    }

    instance_->set_status(Status::UPDATING, "Updating");
    Serial.printf("[WEB] OTA Upload gestartet, %d Bytes\n", req->content_len);

    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        instance_->set_status(Status::ERROR, String("OTA begin failed: ") + esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }

    char buffer[1024];
    int remaining = req->content_len;
    bool first_chunk = true;
    int total_written = 0;

    while (remaining > 0) {
        int to_read = remaining > (int)sizeof(buffer) ? (int)sizeof(buffer) : remaining;
        int received = httpd_req_recv(req, buffer, to_read);

        if (received == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }

        if (received <= 0) {
            esp_ota_abort(ota_handle);
            instance_->set_status(Status::ERROR, "Upload failed: receive error");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive failed");
            return ESP_FAIL;
        }

        if (first_chunk) {
            first_chunk = false;
            if ((uint8_t)buffer[0] != 0xE9) {
                esp_ota_abort(ota_handle);
                instance_->set_status(Status::ERROR, "Upload failed: invalid firmware");
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid firmware image");
                return ESP_FAIL;
            }
        }

        err = esp_ota_write(ota_handle, buffer, received);
        if (err != ESP_OK) {
            esp_ota_abort(ota_handle);
            instance_->set_status(Status::ERROR, String("OTA write failed: ") + esp_err_to_name(err));
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write failed");
            return ESP_FAIL;
        }

        total_written += received;
        remaining -= received;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        instance_->set_status(Status::ERROR, String("OTA finalize failed: ") + esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Finalize failed");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        instance_->set_status(Status::ERROR, String("Boot switch failed: ") + esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Boot switch failed");
        return ESP_FAIL;
    }

    instance_->set_status(Status::UPDATING, "Update successful - restarting");
    Serial.printf("[WEB] OTA erfolgreich, %d Bytes geschrieben. Neustart...\n", total_written);

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "Update erfolgreich. Neustart...");

    BaseType_t task_created = xTaskCreate(
        OtaWebServer::restart_task,
        "ota_restart",
        2048,
        nullptr,
        1,
        nullptr
    );

    if (task_created != pdPASS) {
        Serial.println("[WEB] Restart-Task konnte nicht erstellt werden, starte direkt neu");
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
    }

    return ESP_OK;
}