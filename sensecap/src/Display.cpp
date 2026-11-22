#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include "Display.h"
#include "PanelInit.h"

// Seeed Indicator schematic and Arduino reference: PCA9535 P04 LCD CS,
// P05 LCD reset, P06 touch interrupt, P07 touch reset. Preserve all other pins.
namespace {
constexpr int edge=480, backlight=45;
constexpr uint8_t gxTouch=0x48, dxTouch=0x38;
uint8_t expander=0x20, touch=gxTouch;
bool gx=true;
uint8_t latch;
unsigned brightness = 75, timeoutMs = 60000;
uint32_t lastTouch;
bool sleeping, consumeTouch;
bool sleepWarning;

bool writeReg(int addr, int reg, int val) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}
bool readRegs(int addr, int reg, uint8_t* bytes, size_t n) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) || Wire.requestFrom(static_cast<uint8_t>(addr), n) != n) return false;
    while (n--) *bytes++ = Wire.read();
    return true;
}
void cs(bool idle) {
    if (idle) latch |= 0x10; else latch &= ~0x10;
    writeReg(expander, 2, latch);
}

class PanelBus final : public Arduino_SWSPI {
public:
    PanelBus() : Arduino_SWSPI(GFX_NOT_DEFINED, GFX_NOT_DEFINED, 41, 48, GFX_NOT_DEFINED) {}
    void beginWrite() override { cs(false); }
    void endWrite() override { cs(true); }
};
PanelBus bus;
Arduino_ESP32RGBPanel rgb(18,17,16,21, 4,3,2,1,0, 10,9,8,7,6,5, 15,14,13,12,11,
                         1,10,8,50, 1,10,8,20);
Arduino_RGB_Display* panel;

void flush(lv_display_t* d, const lv_area_t* a, uint8_t* pixels) {
    panel->draw16bitRGBBitmap(a->x1,a->y1,reinterpret_cast<uint16_t*>(pixels),
                             a->x2-a->x1+1,a->y2-a->y1+1);
    lv_display_flush_ready(d);
}
void light(unsigned percent) { ledcWrite(backlight, percent*255/100); }
void readTouch(lv_indev_t*, lv_indev_data_t* data) {
    uint8_t r[5];
    data->state = LV_INDEV_STATE_RELEASED;
    const uint32_t idle=millis()-lastTouch;
    const bool warningDue=!sleeping && timeoutMs && idle>=timeoutMs-15000 && idle<timeoutMs;
    if (warningDue!=sleepWarning) {
        sleepWarning=warningDue;
        displaySleepOverlay(warningDue);
    }
    if (!readRegs(touch, 2, r, sizeof(r))) return;
    if (!(r[0]&0xf) || (r[1]>>6)==1) {
        consumeTouch = false;
        if (!sleeping && timeoutMs && millis()-lastTouch >= timeoutMs) {
            sleeping = true;
            sleepWarning=false;
            displaySleepOverlay(false);
            light(0);
        }
        return;
    }
    lastTouch = millis();
    if (sleepWarning) { sleepWarning=false; displaySleepOverlay(false); }
    if (sleeping) {
        sleeping = false;
        consumeTouch = true;
        light(brightness);
    }
    if (consumeTouch) return;
    const int x = ((r[1]&0xf)<<8)|r[2], y = ((r[3]&0xf)<<8)|r[4];
    if (x >= edge || y >= edge) return;
    data->point.x = gx?edge-1-x:x;
    data->point.y = gx?edge-1-y:y;
    data->state = LV_INDEV_STATE_PRESSED;
}
}

bool startDisplay() {
    Wire.begin(39,40,400000);
    Wire.setTimeOut(10);
    uint8_t config;
    if (!psramFound()) return false;
    if (!readRegs(expander,2,&latch,1)) {
        expander=0x39; // Alternate address probed by the Seeed BSP.
        if (!readRegs(expander,2,&latch,1)) return false;
    }
    if (!readRegs(expander,6,&config,1)) return false;
    latch = (latch | 0x10) & ~0xa0;
    if (!writeReg(expander,2,latch) || !writeReg(expander,6,(config|0x40)&~0xb0)) return false;
    delay(10);
    latch |= 0xa0;
    if (!writeReg(expander,2,latch)) return false;
    delay(150);
    uint8_t touchMode;
    if (!readRegs(gxTouch,0,&touchMode,1)) {
        if (readRegs(dxTouch,0,&touchMode,1)) { touch=dxTouch; gx=false; }
        else Serial.println("D1 touch did not respond. Using the GX display profile.");
    }
    // The Seeed DX panel needs RGB timings only. GX also needs ST7701 setup.
    static Arduino_RGB_Display displayPanel(edge,edge,&rgb,0,true,gx?&bus:nullptr,GFX_NOT_DEFINED,
        gx?st7701_indicator_init_operations:nullptr,gx?sizeof(st7701_indicator_init_operations):0);
    panel=&displayPanel;
    Serial.printf("SenseCAP panel profile: %s, touch 0x%02x, expander 0x%02x\n",gx?"GX":"DX",touch,expander);
    if (!panel->begin(12000000)) return false;
    panel->fillScreen(0);
    if (!ledcAttach(backlight,5000,8)) return false;
constexpr size_t bytes = edge*32*2;
    void* buffer = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer) return false;
    lv_init();
    lv_tick_set_cb(millis);
    auto* d = lv_display_create(edge,edge);
    lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d,buffer,nullptr,bytes,LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d,flush);
    auto* input = lv_indev_create();
    lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(input,readTouch);
    lastTouch = millis();
    light(brightness);
    return true;
}
void displayBrightness(unsigned percent) { brightness = constrain(percent,5u,100u); if (!sleeping) light(brightness); }
void displayTimeout(unsigned seconds) { timeoutMs=seconds*1000; lastTouch=millis(); sleepWarning=false; displaySleepOverlay(false); }
