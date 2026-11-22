#pragma once
#include <ctime>
#include <cstdint>
#include <cstdio>
#include <cstring>
#ifndef ROOM_PANEL_PREVIEW
#include <Arduino.h>
#include <Preferences.h>
#include <esp_sntp.h>
#include <atomic>
#endif

namespace panelClock {
inline const char* zones[]={"PKT-5","UTC0","GMT0BST,M3.5.0/1,M10.5.0","EST5EDT,M3.2.0,M11.1.0"};
inline unsigned zone=0;
inline bool automatic=true, twelve=true, date=false, dirty=true, connected=false;
inline char server[65]="pool.ntp.org";
enum class SyncAction { None, Start, Stop };
inline SyncAction nextAction(bool wifi) {
    if (!dirty && wifi==connected) return SyncAction::None;
    const bool wasConnected=connected;
    dirty=false; connected=wifi;
    if (automatic && wifi) return SyncAction::Start;
    return wasConnected?SyncAction::Stop:SyncAction::None;
}
inline void formatLocal(char* buffer,size_t capacity,time_t epoch,const tm& local) {
    if (epoch<=1700000000) { snprintf(buffer,capacity,"Time unset"); return; }
    strftime(buffer,capacity,twelve?(date?"%I:%M %p\n%a, %b %d":"%I:%M %p"):
                                (date?"%H:%M\n%a, %b %d":"%H:%M"),&local);
}
#ifndef ROOM_PANEL_PREVIEW
inline std::atomic<uint32_t> synced{0};
inline void synchronized(timeval* value) { synced.store(value->tv_sec); }
inline void load() {
    Preferences prefs;
    if (prefs.begin("panel-clock",true)) {
        zone=prefs.getUChar("zone",0)%4; twelve=prefs.getBool("12h",true);
        automatic=prefs.getBool("auto",true); date=prefs.getBool("date",false);
        prefs.getString("server","pool.ntp.org").toCharArray(server,sizeof server);
    }
    setenv("TZ",zones[zone],1); tzset();
    sntp_set_time_sync_notification_cb(synchronized);
}
inline void save() {
    Preferences prefs;
    if (prefs.begin("panel-clock",false)) {
        prefs.putUChar("zone",zone); prefs.putBool("12h",twelve); prefs.putBool("auto",automatic);
        prefs.putBool("date",date); prefs.putString("server",server);
    }
    setenv("TZ",zones[zone],1); tzset(); dirty=true;
}
inline uint32_t lastSync() { return synced.load(); }
inline void service(bool wifi) {
    const auto action=nextAction(wifi);
    if (action==SyncAction::Start) configTzTime(zones[zone],server);
    if (action==SyncAction::Stop) esp_sntp_stop();
}
#else
inline void load() {}
inline void save() {}
inline void service(bool) {}
inline uint32_t lastSync() { return 0; }
#endif
}
