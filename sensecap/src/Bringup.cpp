#ifdef SENSECAP_BRINGUP
#include <Arduino.h>
#include <lvgl.h>
#include "Display.h"

namespace { bool initialized; }
void setup() {
    Serial.begin(115200);
    initialized=startDisplay();
    if (!initialized) {
        Serial.println("D1 bring-up failed: check display, expander and PSRAM");
        return;
    }
    constexpr const char* corners[]={"Top left","Top right","Bottom left","Bottom right"};
    constexpr lv_align_t positions[]={LV_ALIGN_TOP_LEFT,LV_ALIGN_TOP_RIGHT,LV_ALIGN_BOTTOM_LEFT,LV_ALIGN_BOTTOM_RIGHT};
    auto* screen=lv_screen_active();
    auto* message=lv_label_create(screen);
    lv_obj_center(message);
    lv_label_set_text(message,"D1 display and touch check");
    for (unsigned i=0;i<4;i++) {
        auto* button=lv_button_create(screen);
        lv_obj_set_size(button,145,64);
        lv_obj_align(button,positions[i],(i%2?-1:1)*12,(i<2?1:-1)*12);
        auto* label=lv_label_create(button);
        lv_label_set_text(label,corners[i]); lv_obj_center(label);
        lv_obj_add_event_cb(button,[](lv_event_t* e) {
            auto* label=static_cast<lv_obj_t*>(lv_event_get_user_data(e));
            auto* source=static_cast<lv_obj_t*>(lv_event_get_target(e));
            lv_label_set_text_fmt(label,"Touched: %s",lv_label_get_text(lv_obj_get_child(source,0)));
            static unsigned brightness=100;
            brightness=brightness==100?25:100;
            displayBrightness(brightness);
        },LV_EVENT_CLICKED,message);
    }
    Serial.printf("PSRAM detected: %u bytes\n",ESP.getPsramSize());
}
void loop() { if (initialized) lv_timer_handler(); delay(5); }
#endif
