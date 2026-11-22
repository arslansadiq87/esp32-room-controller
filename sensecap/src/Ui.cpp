#include <Arduino.h>
#include <Preferences.h>
#include <lvgl.h>
#include <sys/time.h>
#include "Display.h"
#include "Controller.h"
#include "TimeService.h"

namespace {
using namespace room;
constexpr uint32_t background=0x070e19, surface=0x142239, raised=0x182536,
                   white=0xf4f7fb, muted=0x98a9bf, cyan=0x35c7f4, green=0x25c69a,
                   amber=0xf5b64c, red=0xef7474;
enum Page { Home, Lights, Fans, Climate, Energy, AutomationPage, Settings, Setup, Advanced, MorePage, Devices };
constexpr const char* nav[]={LV_SYMBOL_HOME "\nHome",LV_SYMBOL_LIST "\nDevices",LV_SYMBOL_EYE_OPEN "\nInsights",LV_SYMBOL_SETTINGS "\nMore"};
constexpr const char* titles[]={"Good morning","Lights","Fans","Climate & presence","Energy","Automation","Settings","Connect your panel","Advanced automation","More","Devices"};
constexpr const char* presets[]={"Normal","Movie","Cleaning","Away"};
State state;
Page page=Setup;
lv_obj_t *body,*header,*status,*notice,*navigation,*modal,*keyboard,*pageTitle;
lv_obj_t* sleepClockOverlay;
lv_obj_t *sleepHour,*sleepMinute,*sleepSeparator;
lv_obj_t *metrics[16],*device[8],*deviceText[8],*deviceMeta[8],*gauge[2],*chart,*historyLabel;
lv_obj_t *fields[3],*networkList;
lv_obj_t* masterControl;
lv_obj_t* presetCards[4];
lv_obj_t* fanPresets[5];
lv_obj_t* relayIndicators[6];
int paintedSpeed=-2;
int8_t paintedDevice[8];
int8_t paintedPreset{-2};
lv_chart_series_t* line;
lv_obj_t* interactive[160];
unsigned interactiveCount;
bool online, rendered, pairedBefore, reduced, meterVerified;
bool lastVisibleBusy=~false;
int brightness=75, screenSeconds=60;
uint32_t chartVersion=UINT32_MAX, shownConfig=UINT32_MAX, nextHistory, nextAdvanced, nextConfigTry;
unsigned displayedNetworks=UINT32_MAX;
uint32_t fanDragSession;
int fanPreviewLevel=-1;
bool fanCommandPending;
Page requestedPage=Setup;
bool pageRequested;
Request followup{};
uint32_t fanDue;
bool fanLongPressHandled;
struct Edit {
    bool safety{};
    const char* section{};
    int channel{};
    const char* key{};
    double min{},max{};
    bool decimal{}, boolean{}, name{};
};
Edit selected;
lv_obj_t* editor;
lv_obj_t* dialogMessage;
char localNotice[100]{};
char lastWorkerMessage[120]{};
uint32_t localNoticeAt{};
bool choiceEditor;
Request confirmation;
uint32_t shownPairingRevision;

void show(Page);
void refresh();
lv_color_t color(uint32_t c) { return lv_color_hex(c); }
void style(lv_obj_t* o,uint32_t c,int radius=14) {
    lv_obj_set_style_bg_color(o,color(c),0);
    lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(o,0,0);
    lv_obj_set_style_radius(o,radius,0);
}
void size(lv_obj_t* o,int w,int h) { lv_obj_set_size(o,w,h); }
void text(lv_obj_t* o,const char* value) {
    if (o && strcmp(lv_label_get_text(o),value)) lv_label_set_text(o,value);
}
lv_obj_t* label(lv_obj_t* parent,const char* caption="",uint32_t ink=white,const lv_font_t* font=&lv_font_montserrat_16) {
    auto* o=lv_label_create(parent);
    lv_label_set_text(o,caption);
    lv_obj_set_style_text_color(o,color(ink),0);
    lv_obj_set_style_text_font(o,font,0);
    return o;
}
template<typename... T> void print(lv_obj_t* o,const char* fmt,T... values) {
    char buffer[420]; snprintf(buffer,sizeof buffer,fmt,values...); text(o,buffer);
}
void gridRow(lv_obj_t* o,int gap=8) {
    lv_obj_set_flex_flow(o,LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(o,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(o,gap,0);
}
lv_obj_t* row(lv_obj_t* parent,int h) {
    auto* o=lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    size(o,LV_PCT(100),h);
    lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE);
    gridRow(o);
    return o;
}
lv_obj_t* card(lv_obj_t* parent,int h,int w=LV_PCT(100)) {
    auto* o=lv_obj_create(parent);
    size(o,w,h); style(o,surface);
    lv_obj_set_style_bg_grad_color(o,color(0x0d1828),0);
    lv_obj_set_style_bg_grad_dir(o,LV_GRAD_DIR_VER,0);
    lv_obj_set_style_border_width(o,1,0);
    lv_obj_set_style_border_color(o,color(0x253951),0);
    lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(o,12,0);
    return o;
}
lv_obj_t* button(lv_obj_t* parent,const char* title,lv_event_cb_t cb,uintptr_t id=0,int w=LV_PCT(100),int h=44,bool gated=false) {
    auto* o=lv_button_create(parent);
    size(o,w,h); style(o,raised,10);
    lv_obj_set_style_border_width(o,1,0);
    lv_obj_set_style_border_color(o,color(0x314054),0);
    lv_obj_set_style_bg_color(o,color(0x293c49),LV_STATE_PRESSED);
    lv_obj_set_style_opa(o,LV_OPA_50,LV_STATE_DISABLED);
    lv_obj_center(label(o,title));
    if (cb) lv_obj_add_event_cb(o,cb,LV_EVENT_CLICKED,reinterpret_cast<void*>(id));
    if (gated && interactiveCount<160) interactive[interactiveCount++]=o;
    return o;
}
int eventId(lv_event_t* e) { return static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e))); }
void go(Page p) { requestedPage=p; pageRequested=true; }
void gotoPage(lv_event_t* e) { go(static_cast<Page>(eventId(e))); }
void warn(const char* detail) {
    copy(localNotice,detail); localNoticeAt=millis();
    if (dialogMessage) text(dialogMessage,detail);
    else text(notice,detail);
}
bool dispatch(Request r) {
    const bool local=r.action==Action::Pair || r.action==Action::Scan || r.action==Action::Reset;
    if ((!local && (!state.ready(millis()) || state.busy)) || !submit(r)) {
        warn("Action unavailable. Wait for synchronization.");
        return false;
    }
    if (r.action==Action::Relay || r.action==Action::Fan) {
        state.optimisticActive=true;
        state.optimisticFan=r.action==Action::Fan;
        state.optimisticId=r.id;
        state.optimisticOn=r.value>0;
        state.optimisticLevel=r.value;
    }
    if (r.action==Action::Relay || r.action==Action::Fan || r.action==Action::Group)
        text(notice,"Pending");
    return true;
}
void remember() {
    Preferences prefs;
    if (!prefs.begin("panel-display",false)) return;
    prefs.putUChar("bright",brightness);
    prefs.putUShort("timeout",screenSeconds);
    prefs.putBool("reduced",reduced);
    prefs.putBool("verified",meterVerified);
}
void closeModal() {
    if (modal) lv_obj_delete(modal);
    modal=nullptr; keyboard=nullptr; editor=nullptr; dialogMessage=nullptr; choiceEditor=false;
}
void cancel(lv_event_t*) { closeModal(); }
lv_obj_t* dialog(const char* title,int height) {
    closeModal();
    modal=lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(modal);
    size(modal,480,480); lv_obj_center(modal);
    lv_obj_set_style_bg_opa(modal,LV_OPA_70,0);
    lv_obj_set_style_bg_color(modal,color(0),0);
    lv_obj_clear_flag(modal,LV_OBJ_FLAG_SCROLLABLE);
    auto* panel=card(modal,height,440);
    lv_obj_align(panel,LV_ALIGN_TOP_MID,0,20);
    lv_obj_set_flex_flow(panel,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel,10,0);
    label(panel,title,cyan,&lv_font_montserrat_20);
    return panel;
}
void confirm(const char* title,const char* description,Request r) {
    confirmation=r;
    auto* panel=dialog(title,195);
    auto* note=label(panel,description,muted);
    lv_obj_set_width(note,400);
    auto* buttons=row(panel,48);
    button(buttons,"Cancel",cancel,0,193);
    button(buttons,"Confirm",[](lv_event_t*) {
        dispatch(confirmation);
        closeModal();
    },0,193);
}
void relayTap(lv_event_t* e) {
    int i=eventId(e);
    dispatch({Action::Relay,i+1,!state.relay[i].on});
}
void groupTap(lv_event_t* e) {
    bool on=eventId(e);
    confirm(on?"Turn all six outputs on?":"Turn all six outputs off?",
            "Each output is checked before the next one switches.",{Action::Group,0,on});
}
int chosenFan=0;
void fanPower(lv_event_t* e) {
    int i=eventId(e);
    const auto& f=state.fan[i];
    fanDue=0;
    fanPreviewLevel=f.on?0:(f.level?f.level:50);
    fanCommandPending=true;
    dispatch({Action::Fan,i+1,fanPreviewLevel});
}
void fanHold(lv_event_t* e) {
    chosenFan=eventId(e);
    fanLongPressHandled=true;
    go(Fans);
}
void fanPreset(lv_event_t* e) {
    fanDue=0;
    fanPreviewLevel=eventId(e);
    fanCommandPending=true;
    dispatch({Action::Fan,chosenFan+1,fanPreviewLevel});
}
void dragFan(lv_event_t* e) {
    auto* arc=static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (lv_event_get_code(e)==LV_EVENT_PRESSED) {
        lv_anim_delete(arc,nullptr);
        fanDragSession=state.session;
        fanPreviewLevel=lv_arc_get_value(arc);
        fanCommandPending=false;
        fanDue=0;
    } else if (lv_event_get_code(e)==LV_EVENT_RELEASED && online && !state.busy && fanDragSession) {
        const int level=lv_arc_get_value(arc);
        const auto& f=state.fan[chosenFan];
        if (level!=(f.on?f.level:0)) {
            fanPreviewLevel=level;
            fanCommandPending=true;
            followup={Action::Fan,chosenFan+1,level};
            followup.session=fanDragSession;
            fanDue=millis()+160;
        }
        fanDragSession=0;
    } else if (lv_event_get_code(e)==LV_EVENT_PRESS_LOST) fanDragSession=0;
}
void layoutHome() {
    auto* climate=card(body,116);
    label(climate,"ROOM CLIMATE",cyan);
    metrics[0]=label(climate,"",white,&lv_font_montserrat_36);
    lv_obj_align(metrics[0],LV_ALIGN_LEFT_MID,0,7);
    metrics[6]=label(climate,"",green);
    lv_obj_align(metrics[6],LV_ALIGN_TOP_RIGHT,-6,4);
    metrics[1]=label(climate,"",muted);
    lv_obj_align(metrics[1],LV_ALIGN_TOP_RIGHT,-6,28);
    metrics[7]=label(climate,"",muted);
    lv_obj_align(metrics[7],LV_ALIGN_BOTTOM_LEFT,0,-2);
    metrics[8]=label(climate,"",muted);
    lv_obj_align(metrics[8],LV_ALIGN_BOTTOM_RIGHT,-6,-2);
    label(body,"Quick controls",muted);
    auto* quick=row(body,92);
    const int slots[]={0,2};
    for (int index:slots) {
        device[index]=button(quick,"",relayTap,index,103,88,true);
        deviceText[index]=lv_obj_get_child(device[index],0);
        size(deviceText[index],97,62);
        lv_obj_set_style_text_align(deviceText[index],LV_TEXT_ALIGN_CENTER,0);
        lv_label_set_long_mode(deviceText[index],LV_LABEL_LONG_DOT);
    }
    device[6]=button(quick,"",fanPower,0,103,88,true);
    device[7]=button(quick,"",fanPower,1,103,88,true);
    for (int index=6;index<8;++index) {
        deviceText[index]=lv_obj_get_child(device[index],0);
        size(deviceText[index],97,62);
        lv_obj_set_style_text_align(deviceText[index],LV_TEXT_ALIGN_CENTER,0);
        lv_label_set_long_mode(deviceText[index],LV_LABEL_LONG_DOT);
        lv_obj_add_event_cb(device[index],fanHold,LV_EVENT_LONG_PRESSED,reinterpret_cast<void*>(static_cast<intptr_t>(index-6)));
    }
    auto* lights=row(body,78);
    const int lightSlots[]={1,3,4,5};
    for (int index:lightSlots) {
        device[index]=button(lights,"",relayTap,index,103,72,true);
        deviceText[index]=lv_obj_get_child(device[index],0);
        size(deviceText[index],97,54);
        lv_obj_set_style_text_align(deviceText[index],LV_TEXT_ALIGN_CENTER,0);
        lv_label_set_long_mode(deviceText[index],LV_LABEL_LONG_DOT);
    }
    auto* summary=row(body,34);
    metrics[5]=label(summary,"",muted);
    lv_obj_set_style_bg_color(summary,color(surface),0);
    lv_obj_set_style_radius(summary,18,0);
}
void deviceCategory(const char* title, Page destination, unsigned metric) {
    auto* tile=button(body,"",gotoPage,destination,LV_PCT(100),96);
    auto* heading=lv_obj_get_child(tile,0);
    text(heading,title);
    lv_obj_set_style_text_font(heading,&lv_font_montserrat_20,0);
    lv_obj_align(heading,LV_ALIGN_TOP_LEFT,6,3);
    metrics[metric]=label(tile,"",muted);
    lv_obj_align(metrics[metric],LV_ALIGN_BOTTOM_LEFT,6,-3);
    lv_obj_align(label(tile,LV_SYMBOL_RIGHT,cyan),LV_ALIGN_RIGHT_MID,-4,0);
}
void layoutDevices() {
    deviceCategory("Lights",Lights,0);
    deviceCategory("Fans",Fans,1);
    label(body,"Quick controls",muted);
    auto* actions=row(body,48);
    button(actions,"Ceiling fan",[](lv_event_t*) { chosenFan=0; go(Fans); },0,211,46);
    button(actions,"Exhaust fan",[](lv_event_t*) { chosenFan=1; go(Fans); },0,211,46);
}
void layoutLights() {
    metrics[5]=label(body,"",muted);
    auto* pages=row(body,40);
    button(pages,"Lights",gotoPage,Lights,205,38);
    button(pages,"Fans",gotoPage,Fans,205,38);
    for (int i=0;i<6;i+=2) {
        auto* tiles=row(body,66);
        for (int k=i;k<i+2;k++) {
        device[k]=button(tiles,"",relayTap,k,211,64,true);
        style(device[k],surface,14);
        lv_obj_set_style_border_width(device[k],1,0);
        lv_obj_set_style_border_color(device[k],color(0x315069),0);
        deviceText[k]=lv_obj_get_child(device[k],0);
        lv_obj_set_width(deviceText[k],138);
        auto* indicator=relayIndicators[k]=lv_switch_create(device[k]);
        size(indicator,36,18);
        lv_obj_align(indicator,LV_ALIGN_TOP_RIGHT,0,1);
        lv_obj_clear_flag(indicator,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(indicator,color(green),LV_PART_INDICATOR|LV_STATE_CHECKED);
        lv_obj_set_height(deviceText[k],20);
        lv_obj_align(deviceText[k],LV_ALIGN_TOP_LEFT,3,0);
        lv_label_set_long_mode(deviceText[k],LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(deviceText[k],LV_TEXT_ALIGN_LEFT,0);
        deviceMeta[k]=label(device[k],"",muted);
        lv_obj_align(deviceMeta[k],LV_ALIGN_BOTTOM_LEFT,3,0);
        lv_obj_set_width(deviceMeta[k],185);
        lv_obj_set_height(deviceMeta[k],20);
        lv_label_set_long_mode(deviceMeta[k],LV_LABEL_LONG_DOT);
        }
    }
    auto* r=row(body,46);
    button(r,"All ON",groupTap,1,211,44,true);
    button(r,"All OFF",groupTap,0,211,44,true);
}
void layoutFans() {
    auto* pages=row(body,40);
    button(pages,"Lights",gotoPage,Lights,205,38);
    button(pages,"Fans",gotoPage,Fans,205,38);
    auto* r=row(body,44);
    for (int i=0;i<2;i++) {
        deviceText[i+6]=lv_obj_get_child(button(r,"",[](lv_event_t* e){
            chosenFan=eventId(e); go(Fans);
        },i,216),0);
    }
    auto* c=card(body,180);
    auto* arc=gauge[chosenFan]=lv_arc_create(c);
    size(arc,152,152); lv_obj_align(arc,LV_ALIGN_LEFT_MID,3,0);
    lv_arc_set_rotation(arc,135); lv_arc_set_bg_angles(arc,0,270);
    lv_arc_set_range(arc,0,100);
    lv_obj_set_style_arc_color(arc,color(cyan),LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(arc,color(cyan),LV_PART_KNOB);
    interactive[interactiveCount++]=arc;
    lv_obj_add_event_cb(arc,dragFan,LV_EVENT_PRESSED,nullptr);
    lv_obj_add_event_cb(arc,dragFan,LV_EVENT_RELEASED,nullptr);
    lv_obj_add_event_cb(arc,dragFan,LV_EVENT_PRESS_LOST,nullptr);
    metrics[0]=label(c,"",white,&lv_font_montserrat_28); lv_obj_align(metrics[0],LV_ALIGN_LEFT_MID,58,-2);
    metrics[1]=label(c,"",muted); lv_obj_align(metrics[1],LV_ALIGN_TOP_RIGHT,0,10); lv_obj_set_width(metrics[1],225);
    auto* power=button(c,"Power",fanPower,chosenFan,160,46,true); lv_obj_align(power,LV_ALIGN_BOTTOM_RIGHT,0,0);
    metrics[2]=lv_obj_get_child(power,0);
    r=row(body,50);
    constexpr const char* names[]={"OFF","25%","50%","75%","100%"};
    for (int i=0;i<5;i++) fanPresets[i]=button(r,names[i],fanPreset,i*25,81,46,true);
}
void layoutClimate() {
    auto* pages=row(body,40);
    button(pages,"Climate",gotoPage,Climate,205,38);
    button(pages,"Energy",gotoPage,Energy,205,38);
    auto* metricsRow=row(body,92);
    auto* c=card(metricsRow,88,136); label(c,"TEMP",cyan);
    metrics[0]=label(c,"",white,&lv_font_montserrat_20); lv_obj_align(metrics[0],LV_ALIGN_BOTTOM_LEFT,0,0);
    c=card(metricsRow,88,136); label(c,"HUMIDITY",cyan);
    metrics[1]=label(c,"",white,&lv_font_montserrat_20); lv_obj_align(metrics[1],LV_ALIGN_BOTTOM_LEFT,0,0);
    c=card(metricsRow,88,136); label(c,"CO2",cyan);
    metrics[3]=label(c,"",white,&lv_font_montserrat_20); lv_obj_align(metrics[3],LV_ALIGN_BOTTOM_LEFT,0,0);
    c=card(body,86); label(c,"AIR QUALITY  /  CO2",cyan);
    metrics[4]=label(c,"",white,&lv_font_montserrat_20); lv_obj_align(metrics[4],LV_ALIGN_TOP_LEFT,0,23);
    gauge[0]=lv_bar_create(c); size(gauge[0],408,10); lv_obj_align(gauge[0],LV_ALIGN_BOTTOM_LEFT,0,0);
    lv_bar_set_range(gauge[0],0,3000);
    c=card(body,105); label(c,"PRESENCE  /  C4002",cyan);
    metrics[2]=label(c,"",muted); lv_obj_align(metrics[2],LV_ALIGN_TOP_LEFT,0,26);
}
void layoutEnergy() {
    auto* pages=row(body,40);
    button(pages,"Climate",gotoPage,Climate,205,38);
    button(pages,"Energy",gotoPage,Energy,205,38);
    auto* c=card(body,96);
    lv_obj_set_style_pad_ver(c,6,0);
    label(c,LV_SYMBOL_CHARGE "  LIVE ELECTRICAL",cyan);
    metrics[0]=label(c,"",white,&lv_font_montserrat_28); lv_obj_align(metrics[0],LV_ALIGN_LEFT_MID,0,8);
    metrics[1]=label(c,"",muted); lv_obj_align(metrics[1],LV_ALIGN_RIGHT_MID,-4,4);
    c=card(body,62);
    lv_obj_set_style_pad_ver(c,6,0);
    metrics[2]=label(c,"",muted);
    label(body,meterVerified?"Meter verification marked complete":"Calibration pending  /  readings indicative",meterVerified?muted:amber);
    c=card(body,100);
    historyLabel=label(c,"24 h  /  Waiting for history",muted);
    chart=lv_chart_create(c); size(chart,414,54); lv_obj_align(chart,LV_ALIGN_BOTTOM_LEFT,0,0);
    style(chart,background,8); lv_chart_set_type(chart,LV_CHART_TYPE_LINE);
    lv_chart_set_div_line_count(chart,3,5);
    lv_obj_set_style_size(chart,0,0,LV_PART_INDICATOR);
    line=lv_chart_add_series(chart,color(cyan),LV_CHART_AXIS_PRIMARY_Y);
    button(body,"Refresh history",[](lv_event_t*) { dispatch({Action::History}); },0,LV_PCT(100),44,true);
    nextHistory=0;
}

lv_obj_t* input(lv_obj_t* parent,const char* placeholder,const char* value,int max,bool secret=false) {
    auto* o=lv_textarea_create(parent); size(o,LV_PCT(100),46);
    style(o,raised,10); lv_obj_set_style_text_color(o,color(white),0);
    lv_textarea_set_one_line(o,true); lv_textarea_set_max_length(o,max);
    lv_textarea_set_placeholder_text(o,placeholder); lv_textarea_set_text(o,value);
    lv_textarea_set_password_mode(o,secret);
    return o;
}
void focus(lv_event_t* e) {
    auto* o=static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (keyboard) {
        lv_obj_remove_flag(keyboard,LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(keyboard,o);
        if (page==Setup && !modal) {
            lv_obj_set_height(body,190);
            lv_obj_scroll_to_view(o,LV_ANIM_OFF);
        }
    }
}
void hideKeyboard(lv_event_t*) {
    lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);
    if (page==Setup && !modal) lv_obj_set_height(body,333);
}
void createKeyboard(lv_obj_t* parent,bool numeric=false) {
    keyboard=lv_keyboard_create(parent); size(keyboard,480,194);
    lv_obj_align(keyboard,LV_ALIGN_BOTTOM_MID,0,0);
    if (numeric) lv_keyboard_set_mode(keyboard,LV_KEYBOARD_MODE_NUMBER);
    lv_obj_add_event_cb(keyboard,hideKeyboard,LV_EVENT_READY,nullptr);
    lv_obj_add_event_cb(keyboard,hideKeyboard,LV_EVENT_CANCEL,nullptr);
    lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);
}
void setupPage() {
    label(body,"1  Choose Wi-Fi",cyan,&lv_font_montserrat_20);
    auto* r=row(body,44);
    button(r,"Scan networks",[](lv_event_t*) { dispatch({Action::Scan}); },0,216);
    button(r,"Controller AP",[](lv_event_t*) {
        lv_textarea_set_text(fields[0],"ESP32-RoomController");
        lv_textarea_set_text(fields[2],"192.168.4.1");
    },0,216);
    networkList=lv_dropdown_create(body); size(networkList,LV_PCT(100),44);
    lv_dropdown_set_options(networkList,"Select scanned network");
    lv_obj_add_event_cb(networkList,[](lv_event_t*) {
        unsigned i=lv_dropdown_get_selected(networkList);
        if (i && i<=state.networkCount) lv_textarea_set_text(fields[0],state.networks[i-1]);
    },LV_EVENT_VALUE_CHANGED,nullptr);
    fields[0]=input(body,"Wi-Fi name",state.savedSsid[0]?state.savedSsid:state.ssid,32);
    fields[1]=input(body,state.paired?"Password (blank keeps saved)":"Wi-Fi password","",64,true);
    label(body,"2  Pair the room controller",cyan,&lv_font_montserrat_20);
    fields[2]=input(body,"Hostname or IP",state.host[0]?state.host:"room-controller.local",64);
    button(body,"Test and save connection",[](lv_event_t*) {
        if (state.scanning || state.busy) { warn("Wait for the current Wi-Fi operation"); return; }
        Request r{Action::Pair};
        copy(r.first,lv_textarea_get_text(fields[0]));
        copy(r.second,lv_textarea_get_text(fields[1]));
        copy(r.host,lv_textarea_get_text(fields[2]));
        hideKeyboard(nullptr);
        dispatch(r);
    });
    createKeyboard(lv_screen_active());
    for (auto* f:fields) lv_obj_add_event_cb(f,focus,LV_EVENT_CLICKED,nullptr);
}
void saveEdit(lv_event_t*) {
    const char* raw=choiceEditor?nullptr:lv_textarea_get_text(editor);
    JsonDocument payload;
    Request r{selected.name?Action::Names:Action::Patch,selected.safety};
    if (selected.name) {
        if (!raw[0]) return;
        payload[selected.key]=raw;
    } else {
        char* end=nullptr;
        double value=choiceEditor?lv_dropdown_get_selected(editor):strtod(raw,&end);
        if ((!choiceEditor && (end==raw || *end)) || !isfinite(value) ||
            value<selected.min || value>selected.max ||
            (!selected.decimal && trunc(value)!=value)) {
            warn("Value outside the allowed range");
            return;
        }
        JsonObject dest=payload.to<JsonObject>();
        if (selected.channel) {
            dest=dest["relays"].to<JsonArray>().add<JsonObject>();
            dest["id"]=selected.channel;
        } else if (selected.section) dest=dest[selected.section].to<JsonObject>();
        if (selected.boolean) dest[selected.key]=value!=0;
        else if (selected.decimal) dest[selected.key]=value;
        else dest[selected.key]=static_cast<int>(value);
    }
    serializeJson(payload,r.payload,sizeof r.payload);
    if (dispatch(r)) closeModal();
}
void edit(const char* title,const char* value,Edit spec) {
    selected=spec;
    auto* p=dialog(title,222);
    const bool relayMode=spec.channel && !strcmp(spec.key,"mode");
    const bool exhaustMode=spec.section && !strcmp(spec.section,"fan2") && !strcmp(spec.key,"mode");
    choiceEditor=spec.boolean || relayMode || exhaustMode;
    if (choiceEditor) {
        editor=lv_dropdown_create(p);
        size(editor,LV_PCT(100),46);
        lv_dropdown_set_options(editor,spec.boolean?"Off\nOn":
            relayMode?"Manual\nPresence\nPresence with time window\nScheduled":
            "Manual\nCO2\nTemperature\nCombined");
        lv_dropdown_set_selected(editor,atoi(value));
    } else editor=input(p,"Value",value,spec.name?48:10);
    if (!spec.name && !choiceEditor) {
        lv_textarea_set_accepted_chars(editor,"0123456789.-");
        label(p,spec.boolean?"0 = off, 1 = on":"Enter a value in the displayed units",muted);
    }
    auto* r=row(p,45);
    button(r,"Cancel",cancel,0,193); button(r,"Save",saveEdit,0,193);
    dialogMessage=label(p,"",amber);
    if (!choiceEditor) {
        createKeyboard(modal,!spec.name);
        lv_obj_add_event_cb(editor,focus,LV_EVENT_CLICKED,nullptr);
    }
}
struct Setting {
    const char* label;
    Edit edit;
};
constexpr Setting autoFields[]={
    {"Automation master",{false,nullptr,0,"master",0,1,false,true}},
    {"Global inactivity (s)",{false,nullptr,0,"off_delay",0,86400}},
    {"Temperature automation",{false,"fan1",0,"enabled",0,1,false,true}},
    {"Target temperature (C)",{false,"fan1",0,"target_temp",10,45,true}},
    {"Low threshold (C)",{false,"fan1",0,"temp_low",10,45,true}},
    {"Medium threshold (C)",{false,"fan1",0,"temp_med",10,45,true}},
    {"High threshold (C)",{false,"fan1",0,"temp_high",10,45,true}},
    {"Low fan speed (%)",{false,"fan1",0,"speed_low",0,100}},
    {"Medium fan speed (%)",{false,"fan1",0,"speed_med",0,100}},
    {"High fan speed (%)",{false,"fan1",0,"speed_high",0,100}},
    {"Exhaust mode",{false,"fan2",0,"mode",0,3}},
    {"Low CO2 (ppm)",{false,"fan2",0,"co2_low",400,10000}},
    {"Medium CO2 (ppm)",{false,"fan2",0,"co2_med",400,10000}},
    {"High CO2 (ppm)",{false,"fan2",0,"co2_high",400,10000}},
    {"CO2 low speed (%)",{false,"fan2",0,"co2_speed_low",0,100}},
    {"CO2 medium speed (%)",{false,"fan2",0,"co2_speed_med",0,100}},
    {"CO2 high speed (%)",{false,"fan2",0,"co2_speed_high",0,100}},
    {"Low temperature (C)",{false,"fan2",0,"temp_low",10,45,true}},
    {"High temperature (C)",{false,"fan2",0,"temp_high",10,45,true}},
    {"Temp medium speed (%)",{false,"fan2",0,"temp_speed_med",0,100}},
    {"Temp high speed (%)",{false,"fan2",0,"temp_speed_high",0,100}},
    {"Night Quiet",{false,"scenarios",0,"night_quiet",0,1,false,true}},
    {"Night starts (hour)",{false,"scenarios",0,"night_start",0,23}},
    {"Night ends (hour)",{false,"scenarios",0,"night_end",0,23}},
    {"Night maximum speed (%)",{false,"scenarios",0,"night_max_speed",0,100}},
    {"Air Boost",{false,"scenarios",0,"air_boost",0,1,false,true}},
    {"Boost trigger (ppm)",{false,"scenarios",0,"air_boost_trigger",400,10000}},
    {"Boost clear (ppm)",{false,"scenarios",0,"air_boost_clear",400,10000}},
    {"Boost exhaust speed (%)",{false,"scenarios",0,"air_boost_exhaust",0,100}},
    {"Boost ceiling speed (%)",{false,"scenarios",0,"air_boost_ceiling",0,100}},
};
constexpr Setting relayFields[]={
    {"Enable automation",{false,nullptr,1,"auto",0,1,false,true}},
    {"Lighting mode",{false,nullptr,1,"mode",0,3}},
    {"Allow at night",{false,nullptr,1,"night_allowed",0,1,false,true}},
    {"Inactivity (s, 0 inherits)",{false,nullptr,1,"timeout",0,86400}},
    {"Window start (hour)",{false,nullptr,1,"start_h",0,23}},
    {"Window start (minute)",{false,nullptr,1,"start_m",0,59}},
    {"Window end (hour)",{false,nullptr,1,"end_h",0,23}},
    {"Window end (minute)",{false,nullptr,1,"end_m",0,59}},
    {"Quiet level (%)",{false,nullptr,1,"quiet_lvl",0,100}},
};
constexpr Setting safetyFields[]={
    {"Temperature hysteresis (C)",{true,nullptr,0,"t_hyst",0,10,true}},
    {"CO2 hysteresis (ppm)",{true,nullptr,0,"c_hyst",0,2000}},
    {"Minimum light on (s)",{true,nullptr,0,"l_min_on",0,3600}},
    {"Minimum light off (s)",{true,nullptr,0,"l_min_off",0,3600}},
    {"Minimum fan run (s)",{true,nullptr,0,"f_min_on",0,3600}},
    {"Minimum fan off (s)",{true,nullptr,0,"f_min_off",0,3600}},
    {"Relay anti-chatter (s)",{true,nullptr,0,"r_min_int",0,3600}},
    {"Fan ramp step (%)",{true,nullptr,0,"f_ramp_step",1,100}},
    {"Fan ramp interval (s)",{true,nullptr,0,"f_ramp_int",0,300}},
    {"Light decision delay (s)",{true,nullptr,0,"d_del_light",0,3600}},
    {"Fan decision delay (s)",{true,nullptr,0,"d_del_fan",0,3600}},
    {"Manual override (s)",{true,nullptr,0,"man_dur",0,86400}},
    {"Power recovery delay (s)",{true,nullptr,0,"pwr_delay",0,3600}},
};
unsigned automationGroup=0;
unsigned relayChoice=0;
void settingRow(const Setting& setting,JsonVariantConst value,uintptr_t selector) {
    if (value.isNull()) return;
    char caption[118];
    if (value.is<bool>()) snprintf(caption,sizeof caption,"%s  |  %s",setting.label,value.as<bool>()?"On":"Off");
    else if (!strcmp(setting.edit.key,"mode") && value.as<int>()>=0 && value.as<int>()<=3) {
        constexpr const char* lightModes[]={"Manual","Presence","Presence with window","Scheduled"};
        constexpr const char* exhaustModes[]={"Manual","CO2","Temperature","Combined"};
        snprintf(caption,sizeof caption,"%s  |  %s",setting.label,
                 (setting.edit.channel?lightModes:exhaustModes)[value.as<int>()]);
    } else snprintf(caption,sizeof caption,"%s  |  %.5g",setting.label,value.as<double>());
    auto* b=button(body,caption,[](lv_event_t* e) {
        const int index=eventId(e);
        Setting s=index<100?autoFields[index]:index<200?relayFields[index-100]:safetyFields[index-200];
        if (s.edit.channel) s.edit.channel=relayChoice+1;
        JsonDocument config;
        if (!readConfig(s.edit.safety,config)) return;
        JsonVariantConst value=config.as<JsonVariantConst>();
        if (s.edit.channel) {
            for (auto r:config["relays"].as<JsonArrayConst>()) {
                if ((r["id"]|0)==s.edit.channel) { value=r; break; }
            }
        } else if (s.edit.section) value=value[s.edit.section];
        value=value[s.edit.key];
        if (value.isNull()) return;
        char str[18];
        snprintf(str,sizeof str,"%.6g",value.is<bool>()?static_cast<double>(value.as<bool>()):value.as<double>());
        edit(s.label,str,s.edit);
    },selector,LV_PCT(100),54,true);
    auto* captionLabel=lv_obj_get_child(b,0);
    lv_obj_set_width(captionLabel,410);
    lv_obj_set_style_text_align(captionLabel,LV_TEXT_ALIGN_CENTER,0);
}
void automationPage(bool advanced) {
    if (!advanced) {
        label(body,"Choose a room scene",muted);
        auto* tabs=row(body,40);
        button(tabs,"Presets",gotoPage,AutomationPage,205,38);
        button(tabs,"Advanced",gotoPage,Advanced,205,38);
        for (int i=0;i<4;i+=2) {
            auto* r=row(body,80);
            for (int k=i;k<i+2;k++) {
                auto* b=button(r,presets[k],[](lv_event_t* e) {
                    const int selected=eventId(e);
                    confirm(presets[selected],"Apply this room scene on the controller?",{Action::Preset,0,selected});
                },k,211,76,true);
                presetCards[k]=b;
                constexpr const char* icons[]={LV_SYMBOL_HOME,LV_SYMBOL_VIDEO,LV_SYMBOL_REFRESH,LV_SYMBOL_POWER};
                lv_obj_align(label(b,icons[k],cyan),LV_ALIGN_TOP_LEFT,0,0);

                lv_obj_set_style_text_font(lv_obj_get_child(b,0),&lv_font_montserrat_20,0);
                if (state.preset==k) {
                    lv_obj_set_style_bg_color(b,color(0x16483f),0);
                    lv_obj_set_style_border_width(b,1,0);
                    lv_obj_set_style_border_color(b,color(green),0);
                }
            }
        }
        masterControl=button(body,"",[](lv_event_t*) {
            JsonDocument cfg;
            if (!readConfig(false,cfg)) return;
            JsonDocument p; p["master"]=!state.master;
            Request r{Action::Patch,0}; serializeJson(p,r.payload,sizeof r.payload); dispatch(r);
        },0,LV_PCT(100),50,true);
        lv_obj_set_style_text_align(lv_obj_get_child(masterControl,0),LV_TEXT_ALIGN_LEFT,0);
        button(body,"Automation rules & thresholds",gotoPage,Advanced,LV_PCT(100),46,true);
        auto* groups=lv_dropdown_create(body); size(groups,LV_PCT(100),44);
        lv_dropdown_set_options(groups,"General settings\nLighting channels\nCeiling fan\nExhaust fan\nSmart scenarios");
        lv_dropdown_set_selected(groups,automationGroup);
        lv_obj_add_event_cb(groups,[](lv_event_t* e) {
            automationGroup=lv_dropdown_get_selected(static_cast<lv_obj_t*>(lv_event_get_target(e))); go(AutomationPage);
        },LV_EVENT_VALUE_CHANGED,nullptr);
    } else {
        button(body,"Back to Automation",gotoPage,AutomationPage);
        metrics[0]=label(body,"",muted);
        metrics[1]=label(body,"",muted);
        metrics[2]=label(body,"",muted);
        button(body,"Clear all overrides",[](lv_event_t*) {
            confirm("Clear manual overrides?","Automation will regain control of affected devices.",{Action::Clear});
        },0,LV_PCT(100),44,true);
    }
    JsonDocument cfg;
    if (!readConfig(advanced,cfg)) {
        label(body,"Waiting for controller settings",amber);
        return;
    }
    if (advanced) {
        for (unsigned i=0;i<sizeof safetyFields/sizeof *safetyFields;i++)
            settingRow(safetyFields[i],cfg[safetyFields[i].edit.key],200+i);
        return;
    }
    if (automationGroup==1) {
        auto* dropdown=lv_dropdown_create(body); size(dropdown,LV_PCT(100),44);
        String options;
        for (int i=0;i<6;i++) { if (i) options+='\n'; options+=state.relay[i].name[0]?state.relay[i].name:"Relay"; }
        lv_dropdown_set_options(dropdown,options.c_str()); lv_dropdown_set_selected(dropdown,relayChoice);
        lv_obj_add_event_cb(dropdown,[](lv_event_t* e) {
            relayChoice=lv_dropdown_get_selected(static_cast<lv_obj_t*>(lv_event_get_target(e))); go(AutomationPage);
        },LV_EVENT_VALUE_CHANGED,nullptr);
        for (auto r:cfg["relays"].as<JsonArrayConst>())
            if ((r["id"]|0)==static_cast<int>(relayChoice+1))
                for (unsigned i=0;i<sizeof relayFields/sizeof *relayFields;i++)
                    settingRow(relayFields[i],r[relayFields[i].edit.key],100+i);
        return;
    }
    const char* section=automationGroup==2?"fan1":automationGroup==3?"fan2":automationGroup==4?"scenarios":nullptr;
    for (unsigned i=0;i<sizeof autoFields/sizeof *autoFields;i++) {
        const auto& s=autoFields[i];
        if (!section && !s.edit.section) settingRow(s,cfg[s.edit.key],i);
        else if (section && s.edit.section && !strcmp(section,s.edit.section)) settingRow(s,cfg[section][s.edit.key],i);
    }
}
constexpr const char* nameKeys[]={"relay1","relay2","relay3","relay4","relay5","relay6","fan1","fan2"};
void settingsPage() {
    auto section=[](const char* title,const char* icon) {
        auto* heading=button(body,"",nullptr,0,LV_PCT(100),54);
        lv_obj_align(label(heading,icon,cyan),LV_ALIGN_LEFT_MID,0,0);
        lv_obj_align(label(heading,title),LV_ALIGN_LEFT_MID,34,0);
        lv_obj_align(label(heading,LV_SYMBOL_DOWN,muted),LV_ALIGN_RIGHT_MID,0,0);
        auto* panel=lv_obj_create(body);
        lv_obj_remove_style_all(panel);
        size(panel,LV_PCT(100),LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(panel,LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(panel,8,0);
        lv_obj_add_flag(panel,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(heading,[](lv_event_t* event) {
            auto* detail=static_cast<lv_obj_t*>(lv_event_get_user_data(event));
            if (lv_obj_has_flag(detail,LV_OBJ_FLAG_HIDDEN)) {
                lv_obj_remove_flag(detail,LV_OBJ_FLAG_HIDDEN);
                lv_obj_scroll_to_view(static_cast<lv_obj_t*>(lv_event_get_target(event)),LV_ANIM_OFF);
            } else lv_obj_add_flag(detail,LV_OBJ_FLAG_HIDDEN);
        },LV_EVENT_CLICKED,panel);
        return panel;
    };
    auto* detail=section("Display",LV_SYMBOL_IMAGE);
    auto* r=row(detail,44);
    metrics[0]=label(r,"",muted); lv_obj_set_width(metrics[0],160);
    auto* slider=lv_slider_create(r); size(slider,255,18);
    lv_slider_set_range(slider,5,100); lv_slider_set_value(slider,brightness,LV_ANIM_OFF);
    auto sliderChange=[](lv_event_t* e) {
        brightness=lv_slider_get_value(static_cast<lv_obj_t*>(lv_event_get_target(e)));
        displayBrightness(brightness);
        if (lv_event_get_code(e)==LV_EVENT_RELEASED) remember();
    };
    lv_obj_add_event_cb(slider,sliderChange,LV_EVENT_VALUE_CHANGED,nullptr);
    lv_obj_add_event_cb(slider,sliderChange,LV_EVENT_RELEASED,nullptr);
    auto* time=lv_dropdown_create(detail); size(time,LV_PCT(100),44);
    lv_dropdown_set_options(time,"Always on\nWake on touch after 30 s\nWake on touch after 60 s\nWake on touch after 120 s");
    lv_dropdown_set_selected(time,screenSeconds==0?0:screenSeconds==30?1:screenSeconds==120?3:2);
    lv_obj_add_event_cb(time,[](lv_event_t* e) {
        const int values[]={0,30,60,120};
        screenSeconds=values[lv_dropdown_get_selected(static_cast<lv_obj_t*>(lv_event_get_target(e)))];
        displayTimeout(screenSeconds); remember();
    },LV_EVENT_VALUE_CHANGED,nullptr);
    button(detail,reduced?"Reduced motion: on":"Reduced motion: off",[](lv_event_t*) {
        reduced=!reduced; remember(); go(Settings);
    });
    button(detail,meterVerified?"Restore meter verification notice":"Mark meter verified",[](lv_event_t*) {
        if (meterVerified) { meterVerified=false; remember(); go(Settings); return; }
        auto* p=dialog("Mark meter verified?",166);
        label(p,"Confirm only after comparing with a reference.",muted);
        auto* r=row(p,48);
        button(r,"Cancel",cancel,0,193);
        button(r,"Verified",[](lv_event_t*) { meterVerified=true; remember(); closeModal(); go(Settings); },0,193);
    });
    detail=section("Time & Date",LV_SYMBOL_SETTINGS);
    auto choice=[detail](const char* options,unsigned selected,lv_event_cb_t handler) {
        auto* item=lv_dropdown_create(detail); size(item,LV_PCT(100),44);
        lv_dropdown_set_options(item,options); lv_dropdown_set_selected(item,selected);
        lv_obj_add_event_cb(item,handler,LV_EVENT_VALUE_CHANGED,nullptr);
    };
    choice("Automatic sync off\nAutomatic sync on",panelClock::automatic,[](lv_event_t* e) {
        panelClock::automatic=lv_dropdown_get_selected(static_cast<lv_obj_t*>(lv_event_get_target(e))); panelClock::save();
    });
    choice("Karachi (UTC+5)\nUTC\nLondon\nNew York",panelClock::zone,[](lv_event_t* e) {
        panelClock::zone=lv_dropdown_get_selected(static_cast<lv_obj_t*>(lv_event_get_target(e))); panelClock::save();
    });
    choice("24-hour clock\n12-hour clock",panelClock::twelve,[](lv_event_t* e) {
        panelClock::twelve=lv_dropdown_get_selected(static_cast<lv_obj_t*>(lv_event_get_target(e))); panelClock::save();
    });
    choice("Hide date\nShow date",panelClock::date,[](lv_event_t* e) {
        panelClock::date=lv_dropdown_get_selected(static_cast<lv_obj_t*>(lv_event_get_target(e))); panelClock::save();
    });
    metrics[7]=label(detail,"",muted);
    button(detail,"Retry automatic sync",[](lv_event_t*) { panelClock::dirty=true; });
    button(detail,"Set NTP server",[](lv_event_t*) {
        auto* p=dialog("NTP server",200);
        editor=input(p,"Hostname or IP",panelClock::server,64);
        lv_textarea_set_accepted_chars(editor,"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-");
        auto* actions=row(p,46); button(actions,"Cancel",cancel,0,193);
        button(actions,"Save",[](lv_event_t*) {
            const char* host=lv_textarea_get_text(editor);
            if (!*host) return;
            copy(panelClock::server,host); panelClock::save(); closeModal();
        },0,193);
        createKeyboard(modal); lv_obj_add_event_cb(editor,focus,LV_EVENT_CLICKED,nullptr);
    });
    button(detail,"Set local date and time",[](lv_event_t*) {
        auto* p=dialog("Set local date and time",190);
        editor=input(p,"YYYY-MM-DD HH:MM","",16);
        lv_textarea_set_accepted_chars(editor,"0123456789- :");
        auto* buttons=row(p,46);
        button(buttons,"Cancel",cancel,0,193);
        button(buttons,"Set time",[](lv_event_t*) {
            const char* value=lv_textarea_get_text(editor);
            int year,month,day,hour,minute; char extra;
            if (sscanf(value,"%d-%d-%d %d:%d%c",&year,&month,&day,&hour,&minute,&extra)!=5 ||
                year<2024 || year>2099 || month<1 || month>12 || day<1 || day>31 ||
                hour<0 || hour>23 || minute<0 || minute>59) { warn("Use YYYY-MM-DD HH:MM"); return; }
            tm localDate{}; localDate.tm_year=year-1900; localDate.tm_mon=month-1;
            localDate.tm_mday=day; localDate.tm_hour=hour; localDate.tm_min=minute; localDate.tm_isdst=-1;
            time_t epoch=mktime(&localDate);
            if (epoch<0 || localDate.tm_mday!=day) { warn("Invalid date"); return; }
            timeval local{static_cast<decltype(timeval{}.tv_sec)>(epoch),0};
            if (settimeofday(&local,nullptr)!=0) { warn("Clock update failed"); return; }
            closeModal();
        },0,193);
        createKeyboard(modal,false);
        lv_obj_add_event_cb(editor,focus,LV_EVENT_CLICKED,nullptr);
    });
    detail=section("Wi-Fi and connection",LV_SYMBOL_WIFI);
    metrics[1]=label(detail,"",muted);
    button(detail,"Change network or controller",gotoPage,Setup);
    button(detail,"Retry saved connection",[](lv_event_t*) { submit({Action::Refresh}); });
    detail=section("Controller diagnostics",LV_SYMBOL_SETTINGS);
    metrics[2]=label(detail,"",muted);
    metrics[3]=lv_obj_get_child(button(detail,"System LED",[](lv_event_t*) { dispatch({Action::Led,0,!state.sysLed}); },0,LV_PCT(100),44,true),0);
    metrics[4]=lv_obj_get_child(button(detail,"Radar status LED",[](lv_event_t*) { dispatch({Action::Led,1,!state.radarLed}); },0,LV_PCT(100),44,true),0);
    metrics[5]=lv_obj_get_child(button(detail,"Radar output LED",[](lv_event_t*) { dispatch({Action::Led,2,!state.radarOut}); },0,LV_PCT(100),44,true),0);
    metrics[6]=lv_obj_get_child(button(detail,"Power logging",[](lv_event_t*) { dispatch({Action::Logging,0,!state.logging}); },0,LV_PCT(100),44,true),0);
    detail=section("Device names",LV_SYMBOL_EDIT);
    for (int i=0;i<8;i++) {
        const auto& c=i<6?state.relay[i]:state.fan[i-6];
        deviceText[i]=lv_obj_get_child(button(detail,c.name,[](lv_event_t* e) {
            int i=eventId(e);
            edit("Rename channel",(i<6?state.relay[i]:state.fan[i-6]).name,{false,nullptr,0,nameKeys[i],0,0,false,false,true});
        },i,LV_PCT(100),44,true),0);
    }
    detail=section("Advanced settings",LV_SYMBOL_WARNING);
    button(detail,"Restore panel settings",[](lv_event_t*) {
        confirm("Restore panel settings?","This erases this panel's saved network and display preferences.",{Action::Reset});
    });
}

void morePage() {
    label(body,"Panel preferences & diagnostics",muted);
    constexpr const char* entries[]={"Automation  ·  Presets and rules","Settings  ·  Network and display",
                                     "Advanced automation  ·  Safety","Pairing  ·  Controller connection"};
    constexpr Page destinations[]={AutomationPage,Settings,Advanced,Setup};
    for (unsigned i=0;i<4;i++) {
        auto* b=button(body,entries[i],gotoPage,destinations[i],LV_PCT(100),62);
        lv_obj_set_style_text_align(lv_obj_get_child(b,0),LV_TEXT_ALIGN_LEFT,0);
    }
    auto* foot=card(body,46);
    label(foot,"SenseCAP Indicator D1  ·  Controller remains authoritative",muted);
}
void renderChart() {
    if (chartVersion==state.historyVersion) return;
    chartVersion=state.historyVersion;
    if (!state.historyCount) {
        text(historyLabel,"24 h  /  No samples available");
        lv_obj_add_flag(chart,LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(chart,LV_OBJ_FLAG_HIDDEN);
    float maxWatts=1;
    for (unsigned i=0;i<state.historyCount;i++) maxWatts=max(maxWatts,state.history[i]);
    const int top=max(10,static_cast<int>(ceilf(maxWatts/10))*10);
    print(historyLabel,"24 h history  /  0 to %d W",top);
    lv_chart_set_point_count(chart,max(2u,state.historyCount));
    lv_chart_set_range(chart,LV_CHART_AXIS_PRIMARY_Y,0,top);
    for (unsigned i=0;i<state.historyCount;i++)
        lv_chart_set_value_by_id(chart,line,i,state.history[i]<0?LV_CHART_POINT_NONE:lroundf(state.history[i]));
}
void refresh() {
    const uint32_t now=millis();
    const bool wasOnline=online;
    online=state.ready(now);
    if (online && !wasOnline) chartVersion=UINT32_MAX;
    const bool outputsFresh=state.controlsReady(now);
    const bool allowed=outputsFresh;
    for (unsigned i=0;i<interactiveCount;i++) {
        if (allowed) lv_obj_remove_state(interactive[i],LV_STATE_DISABLED);
        else lv_obj_add_state(interactive[i],LV_STATE_DISABLED);
    }
    time_t t=time(nullptr);
    panelClock::service(state.wifi);
    if (page==Home && pageTitle) {
        const char* greeting="Good morning";
        if (t>1700000000) { tm local{}; localtime_r(&t,&local); greeting=local.tm_hour<12?"Good morning":local.tm_hour<17?"Good afternoon":"Good evening"; }
        text(pageTitle,greeting);
    }
    char clock[40]; tm local{};
    localtime_r(&t,&local);
    panelClock::formatLocal(clock,sizeof clock,t,local);
    print(header,"%s  %s",state.wifi?LV_SYMBOL_WIFI:LV_SYMBOL_CLOSE,clock);
    lv_obj_set_style_text_color(header,color(state.wifi?cyan:muted),0);
    const char* connectionLabel=online?"ON":state.paired?"!":"--";
    text(status,connectionLabel);
    lv_obj_set_style_text_color(status,color(online?green:state.paired?red:cyan),0);
    const bool localWarning=localNotice[0] && now-localNoticeAt<3500;
    text(notice,state.busy?"Pending":localWarning?localNotice:(page==Setup?state.message:""));
    lv_obj_set_style_text_color(notice,color(state.busy || localWarning?amber:muted),0);
    const bool powerFresh=state.wifi && state.socket && state.powerKnown && now-state.powerAt<staleAfter;
    const bool climate=state.wifi && state.socket && state.envKnown && state.climate && now-state.envAt<staleAfter, radar=state.radarStatus(now)==State::Availability::Online,
               meter=powerFresh && state.ac && state.voltage>=0 && state.current>=0;
    char temperature[24]="--",humidity[24]="--",co2[16]="--";
    if (climate && state.temperatureKnown) snprintf(temperature,sizeof temperature,"%.1f C",state.temperature);
    if (climate && state.humidityKnown) snprintf(humidity,sizeof humidity,"%.0f%%",state.humidity);
    if (climate && state.co2Known) snprintf(co2,sizeof co2,"%d",state.co2);
    const char* occupancy=!radar || !state.occupancyKnown?"Unknown":state.occupied?"Occupied":"Vacant";
    const char* preset=online && state.preset>=0 && state.preset<4?presets[state.preset]:"Unknown";
    if (masterControl && page==AutomationPage)
        print(lv_obj_get_child(masterControl,0),"Automation master   %s",online?(state.master?"ON":"OFF"):"STALE");
    if (page==AutomationPage) {
        const int8_t selected=online?state.preset:-1;
        if (selected!=paintedPreset) {
            for (int i=0;i<4;i++) if (presetCards[i]) {
                const bool active=selected==i;
                lv_obj_set_style_bg_color(presetCards[i],color(active?0x16483f:raised),0);
                lv_obj_set_style_border_width(presetCards[i],active?1:0,0);
                if (active) lv_obj_set_style_border_color(presetCards[i],color(green),0);
            }
            paintedPreset=selected;
        }
    }
    for (int i=0;i<8;i++) {
        if (!deviceText[i]) continue;
        const auto& c=i<6?state.relay[i]:state.fan[i-6];
        const bool optimistic=state.optimisticActive && state.optimisticFan==(i>=6) &&
            state.optimisticId==(i<6?i+1:i-5);
        const bool shownOn=optimistic?state.optimisticOn:c.on;
        char fallback[12]; snprintf(fallback,sizeof fallback,"%s %d",i<6?"Relay":"Fan",i<6?i+1:i-5);
        const char* name=c.name[0]?c.name:fallback;
        if (page==Settings) print(deviceText[i],"Rename  %.33s",name);
        else if (page==Fans) print(deviceText[i],"%.19s",name);
        else if (page==Home) {
            const Channel& quick=c;
            const char* quickName=name;
            print(deviceText[i],"%s\n%.11s\n%s",i>=6?LV_SYMBOL_LOOP:LV_SYMBOL_CHARGE,quickName,
                  outputsFresh?(optimistic?"PENDING":(quick.on?"ON":"OFF")):"STALE");
            const int8_t visual=outputsFresh?shownOn: -1;
            if (paintedDevice[i]!=visual) {
        lv_obj_set_style_bg_color(device[i],color(visual==1?0x123f3b:raised),0);
                paintedDevice[i]=visual;
            }
        } else text(deviceText[i],name);
        if (device[i] && (page!=Home || i==1 || i==3 || i==4 || i==5)) {
            const int8_t visual=outputsFresh?shownOn:-1;
            if (paintedDevice[i]!=visual) {
                lv_obj_set_style_bg_color(device[i],color(visual==1?0x16483f:raised),0);
                paintedDevice[i]=visual;
            }
        }
        if (page==Lights && i<6) {
            if (c.on) lv_obj_add_state(relayIndicators[i],LV_STATE_CHECKED);
            else lv_obj_remove_state(relayIndicators[i],LV_STATE_CHECKED);
        }
        if (page==Home && i<6 && i!=0 && i!=2) print(deviceText[i],"%s\n%.11s\n%s",LV_SYMBOL_CHARGE,name,outputsFresh?(optimistic?"PENDING":(c.on?"ON":"OFF")):"STALE");
        if (deviceMeta[i]) print(deviceMeta[i],"%s / Wall %s / %s",
            outputsFresh?(c.on?"On":"Off"):"Stale",outputsFresh?(c.wall?"on":"off"):"--",
            outputsFresh?(c.automatic?"Auto":"Manual"):"--");
    }
    switch (page) {
        case MorePage: break;
        case Devices: {
            int active=0;
            for (const auto& relay:state.relay) active+=relay.on;
            if (outputsFresh) {
                print(metrics[0],"%d of 6 on",active);
                print(metrics[1],"Fan 1   %d%%       Fan 2   %d%%",
                      state.fan[0].on?state.fan[0].level:0,state.fan[1].on?state.fan[1].level:0);
            } else {
                text(metrics[0],"Controller offline");
                text(metrics[1],"Controller offline");
            }
            break;
        }
        case Lights: {
            int count=0;
            for (const auto& relay:state.relay) count+=relay.on;
            if (outputsFresh) print(metrics[5],"%d of 6 devices on",count);
            else text(metrics[5],"Controller state stale");
            break;
        }
        case Home: {
            text(metrics[6],occupancy);
            if (climate) {
                text(metrics[0],temperature);
                print(metrics[1],"%s humidity\n%s ppm CO2",humidity,co2);
            } else { text(metrics[0],"--"); text(metrics[1],"Climate unavailable"); }
            int n=0; for (const auto& relay:state.relay) n+=relay.on;
            const char* powerText=meter && state.watts==0?"0 W":meter?nullptr:"-- W";
            const char* lightsText=outputsFresh?nullptr:"-- / 6";
            char capsule[100],powerValue[16],lightValue[16];
            if (powerText) copy(powerValue,powerText); else snprintf(powerValue,sizeof powerValue,"%.0f W",state.watts);
            if (lightsText) copy(lightValue,lightsText); else snprintf(lightValue,sizeof lightValue,"%d / 6",n);
            print(metrics[7],"Power %s",powerValue);
            print(metrics[8],"Lights %s",lightValue);
            print(metrics[5],"Scene  %s",preset);
            break;
        }
        case Fans: {
            const int confirmed=outputsFresh?(state.fan[chosenFan].on?state.fan[chosenFan].level:0):-1;
            if (confirmed!=paintedSpeed) {
                for (int i=0;i<5;++i)
                    lv_obj_set_style_bg_color(fanPresets[i],color(confirmed==25*i?0x116075:raised),0);
                paintedSpeed=confirmed;
            }
            auto& f=state.fan[chosenFan];
            int pct=f.on?f.level:0;
            auto* arc=gauge[chosenFan];
            const bool dragging=lv_obj_has_state(arc,LV_STATE_PRESSED) && outputsFresh;
            const bool previewFan=dragging || fanDue || fanCommandPending;
            const int shownPct=previewFan && fanPreviewLevel>=0?fanPreviewLevel:pct;
            if (!previewFan && lv_arc_get_value(arc)!=pct) {
                if (reduced || !outputsFresh) lv_arc_set_value(arc,pct);
                else {
                    lv_anim_t a;
                    lv_anim_init(&a);
                    lv_anim_set_var(&a,arc);
                    lv_anim_set_values(&a,lv_arc_get_value(arc),pct);
                    lv_anim_set_duration(&a,100);
                    lv_anim_set_exec_cb(&a,[](void* target,int32_t speed) {
                        lv_arc_set_value(static_cast<lv_obj_t*>(target),speed);
                    });
                    lv_anim_start(&a);
                }
            }
            if (!outputsFresh && !state.snapshotAt) text(metrics[0],"--");
            else print(metrics[0],"%d%%%s",shownPct,fanCommandPending?"  ?":"");
            print(metrics[1],"%s\nFan relay %s\n%s",outputsFresh?(f.automatic?"Automatic active":state.fanConfigured[chosenFan]?"Auto configured":"Manual"):"Last confirmed",
                  outputsFresh?(f.interlock?"on":"off"):"--",outputsFresh?f.automation:"");
            text(metrics[2],(previewFan?shownPct>0:f.on)?"Turn OFF":"Turn ON");
            break;
        }
        case Climate: {
            text(metrics[6],occupancy);
            if (climate) {
                text(metrics[0],temperature);
                text(metrics[1],humidity);
                text(metrics[3],co2);
                if (!state.co2Known) text(metrics[4],"Awaiting CO2");
                else print(metrics[4],"%s  /  %d ppm",state.co2<800?"Good":state.co2<1200?"Moderate":"Elevated",state.co2);
            } else { text(metrics[0],"--"); text(metrics[1],"--"); text(metrics[3],"--"); text(metrics[4],"Sensor unavailable"); }
            lv_obj_set_style_bg_color(gauge[0],color(state.co2<800?green:state.co2<1200?amber:red),LV_PART_INDICATOR);
            lv_bar_set_value(gauge[0],climate && state.co2Known?state.co2:0,LV_ANIM_OFF);
            if (radar) {
                const bool speedFresh=state.speedKnown && now-state.presenceAt<20000;
                char speed[28]="--",distance[24]="--",energy[24]="--";
                if (state.distanceKnown) snprintf(distance,sizeof distance,"%.2f m",state.distance);
                if (state.energyKnown) snprintf(energy,sizeof energy,"%.0f",state.energy);
                if (speedFresh) snprintf(speed,sizeof speed,"%.2f m/s",state.speed);
                print(metrics[2],"Online / %s  |  Motion %s  |  Still %s\n%s  |  %s  |  Signal %s",
                      occupancy,!state.motionKnown?"--":state.moving?"yes":"no",!state.stillKnown?"--":state.stationary?"yes":"no",
                      distance,speed,energy);
            } else text(metrics[2],state.radarStatus(now)==State::Availability::Offline?"Radar offline":state.radarStatus(now)==State::Availability::Stale?"Radar data stale":"Radar status unknown");
            break;
        }
        case Energy:
            if (meter) {
                print(metrics[0],"%.1f W",state.watts);
                print(metrics[1],"%.1f V   %.2f A\n%.1f VA   PF %.2f\n%.1f Hz   AC %s",state.voltage,state.current,state.va,state.pf,state.hz,state.acState);
            } else { text(metrics[0],"-- W"); print(metrics[1],"AC %s",powerFresh?state.acState:"stale"); }
            if (powerFresh) print(metrics[2],"Today %.3f kWh   |   Month %.3f kWh\nPeak %.1f W   |   Average %.1f W",
                              state.daily,state.monthly,state.peak,state.average);
            else text(metrics[2],"Energy summaries unavailable");
            renderChart();
            if (!online && state.historyCount) text(historyLabel,"Cached history  /  Controller offline");
            if (online && !state.busy && (nextHistory==0 || static_cast<int32_t>(now-nextHistory)>=0) && submit({Action::History}))
                nextHistory=now+300000;
            break;
        case AutomationPage: print(metrics[0],"Preset %s  |  Night %s  |  Boost %s",preset,
                                  online?(state.quiet?"on":"off"):"--",online?(state.boost?"on":"off"):"--"); break;
        case Advanced:
            if (online) {
                print(metrics[0],"Recovery %s   |   %d s remaining",state.stabilized?"ready":"stabilizing",state.stabilization);
                print(metrics[1],"Override seconds\nL1 %d  L2 %d  L3 %d  L4 %d\nL5 %d  L6 %d  F1 %d  F2 %d",
                      state.relay[0].overrideSeconds,state.relay[1].overrideSeconds,state.relay[2].overrideSeconds,
                      state.relay[3].overrideSeconds,state.relay[4].overrideSeconds,state.relay[5].overrideSeconds,
                      state.fan[0].overrideSeconds,state.fan[1].overrideSeconds);
                char temp[20]="--", co2[20]="--";
                if (state.tempValid) snprintf(temp,sizeof temp,"%.1f C",state.filteredTemp);
                if (state.co2Valid) snprintf(co2,sizeof co2,"%.0f ppm",state.filteredCo2);
                print(metrics[2],"Filtered temperature %s  |  CO2 %s",temp,co2);
            } else { text(metrics[0],"Safety status stale"); text(metrics[1],""); text(metrics[2],""); }
            if (online && !state.busy && static_cast<int32_t>(now-nextAdvanced)>=0 && submit({Action::Safety}))
                nextAdvanced=now+12000;
            break;
        case Settings: {
            const time_t sync=panelClock::lastSync();
            char stamp[64]; snprintf(stamp,sizeof stamp,"%s",state.wifi?"Waiting for NTP":"Waiting for Wi-Fi");
            if (sync) { tm dt{}; localtime_r(&sync,&dt); strftime(stamp,sizeof stamp,"Last sync %b %d %H:%M",&dt); }
            print(metrics[7],"%s\nServer: %.42s",panelClock::automatic?stamp:"Automatic sync off",panelClock::server);
            print(metrics[0],"Brightness %d%%",brightness);
            char snapshotAge[20]="stale";
            if (online) snprintf(snapshotAge,sizeof snapshotAge,"%lu s ago",
                                 static_cast<unsigned long>((now-state.snapshotAt)/1000));
            print(metrics[1],"SSID %.32s   RSSI %d dBm\nPanel %s   WS %s\nTarget %.37s\nController IP %s\nSnapshot %s   Panel heap %lu B",
                  state.ssid,state.rssi,state.ip,state.socket?"connected":"offline",state.host,state.controllerIp,
                  snapshotAge,static_cast<unsigned long>(ESP.getFreeHeap()));
            char diagnosticLine[170];
            const bool diagnosticFresh=state.diagnosticsAt && now-state.diagnosticsAt<30000;
            if (diagnosticFresh)
                snprintf(diagnosticLine,sizeof diagnosticLine,"RX %lu bad %lu reconnects %lu HTTP %lu/%lu ms largest %lu B",
                    static_cast<unsigned long>(state.receivedMessages),static_cast<unsigned long>(state.malformedMessages),
                    static_cast<unsigned long>(state.wsReconnects),static_cast<unsigned long>(state.httpLastMs),
                    static_cast<unsigned long>(state.httpMaxMs),static_cast<unsigned long>(state.largestBlock));
            else snprintf(diagnosticLine,sizeof diagnosticLine,"Network diagnostic samples are stale");
            char connectivity[360];
            snprintf(connectivity,sizeof connectivity,"SSID %.32s RSSI %d dBm\nPanel %s WS %s\nTarget %.37s\nController IP %s\nSnapshot %s Panel heap %lu B\n%s",
                state.ssid,state.rssi,state.ip,state.socket?"connected":"offline",state.host,state.controllerIp,
                snapshotAge,static_cast<unsigned long>(ESP.getFreeHeap()),diagnosticLine);
            text(metrics[1],connectivity);
            if (online) print(metrics[2],"Controller uptime %lu s  /  Heap %lu B\nMQTT %s  /  STCC4 %s (%d)\nRadar %s  /  ACS37800 %s",
                              static_cast<unsigned long>(state.uptime),static_cast<unsigned long>(state.heap),
                              state.mqtt?"connected":"offline",state.climate?"online":"offline",state.envStatus,
                              state.radar?"online":"offline",state.meter?"online":"offline");
            else text(metrics[2],"Controller diagnostics unavailable");
            print(metrics[3],"System LED   %s",state.sysLed?"ON":"OFF");
            print(metrics[4],"Radar status LED   %s",state.radarLed?"ON":"OFF");
            print(metrics[5],"Radar output LED   %s",state.radarOut?"ON":"OFF");
            print(metrics[6],"Power logging   %s",state.logging?"ON":"OFF");
            break;
        }
        case Setup:
            if (displayedNetworks!=state.networkCount || state.scanning) {
                displayedNetworks=state.scanning?UINT32_MAX:state.networkCount;
                String options=state.scanning?"Scanning...":"Select scanned network";
                for (unsigned i=0;i<state.networkCount && !state.scanning;i++) {
                    options+='\n'; String sanitized=state.networks[i]; sanitized.replace("\n"," "); options+=sanitized;
                }
                lv_dropdown_set_options(networkList,options.c_str());
            }
            break;
    }
}

void show(Page p) {
    if (p!=Fans) { fanCommandPending=false; fanPreviewLevel=-1; }
    if (rendered) {
        closeModal();
        lv_obj_clean(lv_screen_active());
    }
    rendered=true; page=p; fanDue=0; paintedSpeed=-2;
    memset(metrics,0,sizeof metrics); memset(device,0,sizeof device);
    memset(deviceText,0,sizeof deviceText); memset(deviceMeta,0,sizeof deviceMeta);
    memset(gauge,0,sizeof gauge); interactiveCount=0;
    memset(presetCards,0,sizeof presetCards); memset(paintedDevice,-1,sizeof paintedDevice); paintedPreset=-2;
    masterControl=nullptr;
    keyboard=nullptr; chartVersion=UINT32_MAX; displayedNetworks=UINT32_MAX;
    auto* root=lv_screen_active();
    style(root,background,0);
    lv_obj_clear_flag(root,LV_OBJ_FLAG_SCROLLABLE);
    pageTitle=label(root,titles[p],white,&lv_font_montserrat_20);
    lv_obj_set_pos(pageTitle,18,14);
    header=label(root,"",muted); lv_obj_align(header,LV_ALIGN_TOP_RIGHT,-58,14);
    status=label(root,"",cyan); lv_obj_align(status,LV_ALIGN_TOP_RIGHT,-17,14);
    notice=label(root,"",muted); lv_obj_set_pos(notice,18,32);
    lv_obj_set_width(notice,444); lv_label_set_long_mode(notice,LV_LABEL_LONG_DOT);
    body=lv_obj_create(root);
    size(body,480,368); lv_obj_set_pos(body,0,55);
    style(body,background,0);
    lv_obj_set_style_pad_hor(body,18,0); lv_obj_set_style_pad_ver(body,6,0);
    lv_obj_set_style_pad_row(body,8,0); lv_obj_set_flex_flow(body,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(body,LV_DIR_VER);
    navigation=row(root,52); size(navigation,480,52); lv_obj_set_pos(navigation,0,426);
    style(navigation,surface,0);
    lv_obj_set_flex_align(navigation,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(navigation,12,0);
    for (unsigned i=0;i<4;i++) {
        auto* b=button(navigation,nav[i],[](lv_event_t* e) {
            switch (eventId(e)) {
                case 0: go(Home); break;
                case 1: go(Devices); break;
                case 2: go(Climate); break;
                default: go(MorePage); break;
            }
        },i,102,44);
        const bool selected=(i==0 && page==Home) || (i==1 && (page==Devices || page==Lights || page==Fans)) ||
            (i==2 && (page==Climate || page==Energy)) ||
            (i==3 && (page==MorePage || page==AutomationPage || page==Advanced || page==Settings || page==Setup));
        lv_obj_set_style_text_align(lv_obj_get_child(b,0),LV_TEXT_ALIGN_CENTER,0);
        lv_obj_set_style_bg_color(b,color(selected?0x116075:background),0);
        lv_obj_set_style_border_width(b,0,0);
    }
    switch (page) {
        case Devices: layoutDevices(); break; case Home: layoutHome(); break; case Lights: layoutLights(); break;
        case Fans: layoutFans(); break; case Climate: layoutClimate(); break;
        case Energy: layoutEnergy(); break;
        case AutomationPage: automationPage(false); break; case Advanced: automationPage(true); break;
        case Settings: settingsPage(); break; case Setup: setupPage(); break; case MorePage: morePage(); break;
    }
    shownConfig=state.configVersion;
    refresh();
}

void screenTick() {
    readState(state);
    if (strcmp(lastWorkerMessage,state.message)) {
        copy(lastWorkerMessage,state.message);
        const bool controlPage=page==Lights || page==Fans;
        const bool configPage=page==AutomationPage || page==Advanced || page==Settings;
        const bool commandResult=strstr(state.message,"Confirmed") || strstr(state.message,"confirmed") || strstr(state.message,"unconfirmed") ||
            strstr(state.message,"failed") || strstr(state.message,"Failed") || strstr(state.message,"Group stopped") ||
            strstr(state.message,"partially applied") || strstr(state.message,"discarded");
        if ((controlPage && commandResult) || (configPage && commandResult)) warn(state.message);
    }
    if (state.paired && page==Setup && (!pairedBefore || shownPairingRevision!=state.pairingRevision)) go(Home);
    pairedBefore=state.paired;
    shownPairingRevision=state.pairingRevision;
    if (pageRequested) {
        Page p=requestedPage;
        pageRequested=false;
        const int oldScroll=rendered && page==p?lv_obj_get_scroll_y(body):0;
        const bool entering=p!=page;
        if (entering && p==Advanced) nextAdvanced=0;
        if (entering && p==AutomationPage) nextConfigTry=0;
        show(p);
        if (oldScroll) lv_obj_scroll_to_y(body,oldScroll,LV_ANIM_OFF);
    }
    if ((page==AutomationPage || page==Advanced) && !modal && shownConfig!=state.configVersion) go(page);
    if (page==AutomationPage && state.ready(millis()) && !state.busy &&
        static_cast<int32_t>(millis()-nextConfigTry)>=0 && submit({Action::Automation})) nextConfigTry=millis()+15000;
    if (!state.ready(millis())) {
        if (fanDue || fanDragSession) warn("Connection lost. Fan adjustment canceled.");
        fanDue=0; fanDragSession=0;
    }
    if (fanDue && static_cast<int32_t>(millis()-fanDue)>=0) {
        fanDue=0;
        dispatch(followup);
    }
    refresh();
}
}

void startUi() {
    panelClock::load();
    Preferences prefs;
    if (prefs.begin("panel-display",true)) {
        brightness=constrain(prefs.getUChar("bright",75),5,100);
        screenSeconds=prefs.getUShort("timeout",60);
        reduced=prefs.getBool("reduced",false);
        meterVerified=prefs.getBool("verified",false);
    }
    auto* display=lv_display_get_default();
    lv_display_set_theme(display,lv_theme_default_init(display,color(cyan),color(green),true,&lv_font_montserrat_16));
    displayBrightness(brightness); displayTimeout(screenSeconds);
    show(Setup);
    sleepClockOverlay=lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(sleepClockOverlay);
    size(sleepClockOverlay,480,480);
    style(sleepClockOverlay,background,0);
    lv_obj_clear_flag(sleepClockOverlay,LV_OBJ_FLAG_SCROLLABLE);
    auto* sleepDate=label(sleepClockOverlay,"",muted);
    lv_obj_align(sleepDate,LV_ALIGN_TOP_MID,0,58);
    sleepHour=label(sleepClockOverlay,"03",white,&lv_font_montserrat_48);
    sleepMinute=label(sleepClockOverlay,"05",white,&lv_font_montserrat_48);
    sleepSeparator=label(sleepClockOverlay,":",cyan,&lv_font_montserrat_48);
    for (auto* digit:{sleepHour,sleepMinute,sleepSeparator}) {
        lv_obj_set_style_transform_scale_x(digit,768,0);
        lv_obj_set_style_transform_scale_y(digit,768,0);
    }
    lv_obj_align(sleepHour,LV_ALIGN_CENTER,-130,0);
    lv_obj_align(sleepSeparator,LV_ALIGN_CENTER,0,0);
    lv_obj_align(sleepMinute,LV_ALIGN_CENTER,80,0);
    lv_obj_add_flag(sleepClockOverlay,LV_OBJ_FLAG_HIDDEN);
    lv_timer_create([](lv_timer_t*) {
        if (!sleepClockOverlay || lv_obj_has_flag(sleepClockOverlay,LV_OBJ_FLAG_HIDDEN)) return;
        const time_t epoch=time(nullptr); tm local{}; localtime_r(&epoch,&local);
        char hour[8],minute[8],date[32];
        if (epoch<=1700000000) { copy(hour,"--"); copy(minute,"--"); copy(date,"Time not synchronized"); }
        else {
            strftime(hour,sizeof hour,panelClock::twelve?"%I":"%H",&local);
            strftime(minute,sizeof minute,"%M",&local);
            strftime(date,sizeof date,panelClock::date?"%A, %B %d":"%A, %B %d",&local);
        }
        text(sleepHour,hour);
        text(sleepMinute,minute);
        text(lv_obj_get_child(sleepClockOverlay,0),date);
    },1000,nullptr);
    lv_timer_create([](lv_timer_t*) { screenTick(); },120,nullptr);
}

void displaySleepOverlay(bool show) {
    if (!sleepClockOverlay) return;
    if (show) lv_obj_remove_flag(sleepClockOverlay,LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(sleepClockOverlay,LV_OBJ_FLAG_HIDDEN);
}

#ifdef ROOM_PANEL_PREVIEW
void previewTimeSettings() {
    auto* panel=lv_obj_get_parent(metrics[7]);
    lv_obj_remove_flag(panel,LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(body);
    auto* heading=lv_obj_get_child(body,lv_obj_get_index(panel)-1);
    lv_obj_scroll_to_y(body,lv_obj_get_y(heading),LV_ANIM_OFF);
}
void previewSleepClock() {
    displaySleepOverlay(true);
    lv_obj_update_layout(sleepClockOverlay);
}
void previewPage(unsigned index) {
    if (index==7) index=Devices;
    if (index>Settings && index!=Devices) return;
    readState(state);
    pairedBefore=state.paired;
    shownPairingRevision=state.pairingRevision;
    show(static_cast<Page>(index));
    lv_obj_update_layout(lv_screen_active());
    lv_area_t content,dock;
    lv_obj_get_coords(body,&content);
    lv_obj_get_coords(navigation,&dock);
    if (content.y2>=dock.y1 || dock.y2>=480 || dock.x2>=480) abort();
    if (page==Lights) for (unsigned i=0;i<6;++i) {
        lv_area_t name,metadata,indicator;
        lv_obj_get_coords(deviceText[i],&name);
        lv_obj_get_coords(deviceMeta[i],&metadata);
        lv_obj_get_coords(relayIndicators[i],&indicator);
        if (name.y2>=metadata.y1 || name.x2>=indicator.x1) abort();
    }
}
#endif
