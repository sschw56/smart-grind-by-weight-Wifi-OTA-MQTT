#include <Arduino.h>
#include <cstdio>
#include <cstring>

void log_and_mqtt_if_error(const char* format, ...) {
    char buf[192];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    Serial.print(buf);
}