#include <Arduino.h>
#include <lvgl.h>
#include "Display.h"
#include "Controller.h"

void startUi();
namespace {
bool initialized;
}
void setup() {
    Serial.begin(115200);
    initialized=startDisplay();
    if (!initialized) {
        Serial.println("SenseCAP D1 display or PSRAM initialization failed");
        return;
    }
    Serial.printf("SenseCAP D1 display initialized. PSRAM: %u bytes\n", ESP.getPsramSize());
    room::startClient();
    startUi();
}
void loop() {
    if (initialized) lv_timer_handler();
    delay(5);
}
