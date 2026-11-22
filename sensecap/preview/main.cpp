#define NOMINMAX
#include <windows.h>
#include <lvgl.h>
#include <array>
#include <cstdio>
#include <cstring>
#include "Controller.h"

void startUi();
void previewPage(unsigned);
void previewTimeSettings();
void previewSleepClock();
void displayBrightness(unsigned) {}
void displayTimeout(unsigned) {}

namespace {
constexpr int side=480;
std::array<uint16_t,side*side> pixels{};
alignas(64) std::array<uint16_t,side*32> draw{};
HWND window;
lv_point_t pointer{};
bool pressed;
bool offline;
const char* scenario="";
bool longNames;
bool pendingFixture;
void flush(lv_display_t* display,const lv_area_t* area,uint8_t* source) {
    const auto width=lv_area_get_width(area);
    for (int y=area->y1;y<=area->y2;++y) {
        memcpy(pixels.data()+y*side+area->x1,source,width*sizeof(uint16_t));
        source+=width*sizeof(uint16_t);
    }
    lv_display_flush_ready(display);
    if(window) InvalidateRect(window,nullptr,FALSE);
}
void paint(HWND handle) {
    PAINTSTRUCT ps{};
    HDC dc=BeginPaint(handle,&ps);
    struct { BITMAPINFOHEADER header; DWORD masks[3]; } info{};
    info.header.biSize=sizeof(BITMAPINFOHEADER);
    info.header.biWidth=side;
    info.header.biHeight=-side;
    info.header.biPlanes=1;
    info.header.biBitCount=16;
    info.header.biCompression=BI_BITFIELDS;
    info.masks[0]=0xf800; info.masks[1]=0x07e0; info.masks[2]=0x001f;
    SetDIBitsToDevice(dc,0,0,side,side,0,0,0,side,pixels.data(),
                     reinterpret_cast<BITMAPINFO*>(&info),DIB_RGB_COLORS);
    EndPaint(handle,&ps);
}
LRESULT CALLBACK events(HWND handle,UINT message,WPARAM w,LPARAM l) {
    switch(message) {
        case WM_LBUTTONDOWN: pressed=true; SetCapture(handle); break;
        case WM_LBUTTONUP: pressed=false; ReleaseCapture(); break;
        case WM_CAPTURECHANGED: pressed=false; return 0;
        case WM_MOUSEMOVE: break;
        case WM_PAINT: paint(handle); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        default: return DefWindowProcW(handle,message,w,l);
    }
    pointer={static_cast<short>(LOWORD(l)),static_cast<short>(HIWORD(l))};
    return 0;
}
}
namespace room {
bool submit(Request) { return false; }
bool readConfig(bool,JsonDocument&) { return false; }
bool readState(State& result) {
    static const char snapshot[]=R"json({"type":"full_status","relays":[{"id":1,"state":true},{"id":2,"state":false},{"id":3,"state":true},{"id":4,"state":false},{"id":5,"state":false},{"id":6,"state":true}],"dimmers":[{"id":1,"state":true,"level":50,"fan_relay":true},{"id":2,"state":false,"level":0,"fan_relay":false}],"names":{"relay1":"Main light","relay2":"Reading lamp","relay3":"Spotlights","relay4":"Desk outlet","relay5":"Wall outlet","relay6":"Accent lights","fan1":"Ceiling fan","fan2":"Exhaust fan"},"environment":{"online":true,"temp":27.4,"humidity":40.0,"co2_ppm":1275},"presence":{"online":true,"state":2,"dist":2.53,"speed":0.0},"power":{"online":true,"v_rms":249.2,"i_rms":0.11,"p_active":26.3,"p_apparent":27.4,"pf":0.96,"freq":50.0,"ac_present":true,"ac_state":"present"}})json";
    JsonDocument json;
    if(deserializeJson(json,snapshot)) return false;
    result={};
    apply(result,json.as<JsonObjectConst>(),lv_tick_get());
    if(longNames) copy(result.relay[0].name,"North wall recessed lighting near the entrance");
    if(!strcmp(scenario,"vacant")) { result.occupied=result.moving=result.stationary=false; result.presence=0; result.distance=0; result.speed=0; }
    if(!strcmp(scenario,"radar-down")) result.radar=false;
    if(!strcmp(scenario,"stale")) result.liveAt=result.radarAt=result.envAt=result.powerAt=uint32_t(0)-15000;
    result.paired=true;
    result.wifi=result.socket=!offline;
    result.tempValid=result.co2Valid=result.ac=true;
    result.busy=pendingFixture;
    if(pendingFixture) { result.temperature=39.8f; result.co2=4999; result.humidity=99; }
    result.preset=0; result.master=true; result.configVersion=1;
    result.historyVersion=1; result.historyCount=24;
    for(unsigned i=0;i<24;++i) result.history[i]=12+((i*13)%19)+i/2;
    copy(result.ssid,"Room network");
    copy(result.host,"room-controller.local");
    return true;
}
}
int main(int argc,char** argv) {
    scenario=argc>2?argv[2]:"";
    offline=argc>2 && std::strstr(argv[2],"offline");
    longNames=argc>2 && std::strstr(argv[2],"long");
    pendingFixture=argc>2 && std::strstr(argv[2],"pending");
    lv_init();
    auto* display=lv_display_create(side,side);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,draw.data(),nullptr,sizeof draw,LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);
    auto* input=lv_indev_create();
    lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(input,[](lv_indev_t*,lv_indev_data_t* data) {
        data->point=pointer;
        data->state=pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
    });
    fprintf(stderr,"preview: start UI\n");
    startUi();
    if(argc>1) {
        for(unsigned screen=0;screen<8;++screen) {
            fprintf(stderr,"preview: screen %u\n",screen);
            previewPage(screen);
            if(screen==6 && !strcmp(scenario,"clock")) previewTimeSettings();
            if(screen==0 && !strcmp(scenario,"sleep-clock")) previewSleepClock();
            lv_refr_now(display);
            char path[512];
            snprintf(path,sizeof path,"%s/%u.rgb565",argv[1],screen);
            auto* file=fopen(path,"wb");
            if(!file) return 1;
            fwrite(pixels.data(),sizeof(uint16_t),pixels.size(),file);
            fclose(file);
        }
        return 0;
    }
    previewPage(0);
    WNDCLASSW cls{};
    cls.lpfnWndProc=events; cls.hInstance=GetModuleHandleW(nullptr); cls.lpszClassName=L"RoomPanelPreview";
    RegisterClassW(&cls);
    RECT rect{0,0,side,side};
    constexpr DWORD style=WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;
    AdjustWindowRect(&rect,style,FALSE);
    window=CreateWindowW(cls.lpszClassName,L"Room Panel preview (fixture data)",style|WS_VISIBLE,
                         CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,
                         nullptr,nullptr,cls.hInstance,nullptr);
    if(!window) return 1;
    auto previous=GetTickCount();
    for (;;) {
        MSG msg;
        while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {
            if(msg.message==WM_QUIT) return 0;
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        auto now=GetTickCount(); lv_tick_inc(now-previous); previous=now;
        lv_timer_handler();
        Sleep(5);
    }
}
