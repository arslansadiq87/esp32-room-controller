#pragma once
#include <ArduinoJson.h>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cmath>

namespace room {
// Telemetry is nominally 500 ms, but allow transient ESPAsyncWebServer queue
// and Wi-Fi scheduling delays without flapping the transport badge.
constexpr uint32_t staleAfter = 10000;
struct TextFrame {
    static constexpr size_t capacity=12000;
    uint8_t bytes[capacity+1]{};
    size_t length{};
    void reset() { length=0; }
    bool append(const uint8_t* part,size_t count) {
        if (count>capacity-length) { reset(); return false; }
        memcpy(bytes+length,part,count); length+=count; bytes[length]=0; return true;
    }
};
inline bool commandIsCurrent(uint32_t now,uint32_t created,uint32_t commandSession,uint32_t currentSession) {
    return commandSession==currentSession && now-created<=2500;
}
struct Channel {
    char name[49]{}, automation[64]{};
    bool on{}, wall{}, interlock{}, automatic{};
    int level{}, overrideSeconds{};
};
struct State {
    Channel relay[6], fan[2];
    bool snapshot{}, socket{}, wifi{}, busy{}, paired{}, scanning{};
    bool optimisticActive{}, optimisticFan{}, optimisticOn{};
    int optimisticId{}, optimisticLevel{};
    uint32_t liveAt{}, snapshotAt{}, uptime{}, heap{}, largestBlock{}, session{}, pairingRevision{};
    uint32_t receivedMessages{}, malformedMessages{}, wsReconnects{}, httpLastMs{}, httpMaxMs{};
    uint32_t diagnosticsAt{};
    uint32_t presenceAt{}, radarAt{}, envAt{}, powerAt{}, outputsAt{}, configAt{};
    bool distanceKnown{}, energyKnown{}, motionKnown{}, stillKnown{}, moving{}, stationary{}, occupied{};
    bool temperatureKnown{}, humidityKnown{}, co2Known{};
    bool radarKnown{}, envKnown{}, powerKnown{}, occupancyKnown{}, speedKnown{};
    enum class Availability { Unknown, Online, Offline, Stale };
    Availability radarStatus(uint32_t now) const {
        if (!radarKnown) return Availability::Unknown;
        if (!wifi || !socket || now-radarAt>=staleAfter) return Availability::Stale;
        return radar?Availability::Online:Availability::Offline;
    }
    int rssi{}, preset{-1}, presence{}, co2{}, envStatus{}, stabilization{}, inactivity{};
    float temperature{}, humidity{}, distance{}, speed{}, energy{}, filteredTemp{}, filteredCo2{};
    bool tempValid{}, co2Valid{}, fanConfigured[2]{};
    float voltage{-1}, current{-1}, watts{}, va{}, pf{}, hz{}, daily{}, monthly{}, peak{}, average{};
    bool climate{}, radar{}, meter{}, ac{}, logging{}, mqtt{}, sysLed{}, radarLed{}, radarOut{};
    bool master{}, quiet{}, boost{}, stabilized{};
    char ip[16]{}, controllerIp[16]{}, ssid[33]{}, savedSsid[33]{}, host[65]{}, acState[16]{}, message[120]{};
    char networks[16][33]{};
    unsigned networkCount{};
    uint32_t historyVersion{}, configVersion{};
    unsigned historyCount{};
    float history[120]{};
    bool controlsReady(uint32_t now) const { return ready(now) && now-snapshotAt<20000; }
    bool ready(uint32_t now) const {
        return wifi && socket && snapshot && now-liveAt < staleAfter;
    }
};

template <size_t N> void copy(char (&out)[N], const char* in) {
    snprintf(out,N,"%s",in ? in : "");
}
template <typename T> void field(T& out, JsonVariantConst value) {
    if (value.is<T>()) out=value.as<T>();
}
template <size_t N> void field(char (&out)[N], JsonVariantConst value) {
    if (value.is<const char*>()) copy(out,value.as<const char*>());
}

inline bool channels(JsonArrayConst list, Channel* dest, int count, bool fan) {
    unsigned seen=0;
    for (JsonObjectConst item : list) {
        int id=item["id"] | 0;
        if (id<1 || id>count || (seen & (1u<<id)) || !item["state"].is<bool>()) return false;
        if (fan && (!item["level"].is<int>() || item["level"].as<int>()<0 || item["level"].as<int>()>100 ||
                    !item["fan_relay"].is<bool>())) return false;
        seen |= 1u<<id;
        auto& ch=dest[id-1];
        field(ch.on,item["state"]); field(ch.wall,item["sw"]);
        if (fan) { field(ch.level,item["level"]); field(ch.interlock,item["fan_relay"]); }
    }
    return seen == ((2u<<count)-2);
}

inline bool apply(State& s, JsonObjectConst j, uint32_t now, uint8_t* acceptedCategories=nullptr) {
    const char* type=j["type"] | "";
    const bool full=!strcmp(type,"full_status"), live=!strcmp(type,"telemetry");
    if (!full && !live) {
        const bool fan=!strcmp(type,"dimmer_update");
        if (!fan && strcmp(type,"relay_update")) return false;
        const int id=j["id"] | 0;
        if (id<1 || id>(fan?2:6) || !j["state"].is<bool>()) return false;
        if (fan && (!j["level"].is<int>() || j["level"].as<int>()<0 || j["level"].as<int>()>100 ||
                    !j["fan_relay"].is<bool>())) return false;
        auto& c=fan?s.fan[id-1]:s.relay[id-1];
        field(c.on,j["state"]);
        if (fan) { field(c.level,j["level"]); field(c.interlock,j["fan_relay"]); }
        s.outputsAt=s.liveAt=now;
        if (acceptedCategories) *acceptedCategories=3;
        return true;
    }
    if (full) {
        Channel relays[6], fans[2];
        memcpy(relays,s.relay,sizeof relays); memcpy(fans,s.fan,sizeof fans);
        if (!channels(j["relays"],relays,6,false) || !channels(j["dimmers"],fans,2,true)) return false;
        memcpy(s.relay,relays,sizeof relays); memcpy(s.fan,fans,sizeof fans);
        s.snapshot=true; s.outputsAt=s.snapshotAt=now;
        for (int i=0;i<8;i++) {
            char key[16]; snprintf(key,sizeof key,"%s%d",i<6?"relay":"fan",i<6?i+1:i-5);
            field((i<6?s.relay[i]:s.fan[i-6]).name,j["names"][key]);
        }
        field(s.mqtt,j["mqtt"]["connected"]);
        if (j["presence"]["speed"].is<float>()) {
            field(s.speed,j["presence"]["speed"]); s.presenceAt=now; s.speedKnown=true;
        }
        field(s.envStatus,j["environment"]["status"]);
    }
    auto e=full?j["environment"].as<JsonObjectConst>():j;
    auto r=full?j["presence"].as<JsonObjectConst>():j;
    auto p=full?j["power"].as<JsonObjectConst>():j;
    auto sys=full?j["system"].as<JsonObjectConst>():j;
    const auto envFlag=e[full?"online":"env_online"];
    const auto radarFlag=r[full?"online":"presence_online"];
    const auto target=r[full?"state":"presence_state"];
    const auto powerFlag=p[full?"online":"pwr_online"];
    bool envUpdate=envFlag.is<bool>() || e["temp"].is<float>() || e["humidity"].is<float>() ||
        (e["co2_ppm"].is<int>() && e["co2_ppm"].as<int>()>=0 && e["co2_ppm"].as<int>()<=65535);
    bool radarUpdate=radarFlag.is<bool>() || target.is<int>() || r[full?"dist":"presence_dist"].is<float>() ||
        r[full?"energy":"presence_energy"].is<float>() || r[full?"breathing":"presence_breathing"].is<bool>();
    bool powerUpdate=powerFlag.is<bool>() || p["ac_present"].is<bool>() || p["ac_state"].is<const char*>();
    for (const char* key: {"v_rms","i_rms","p_active","p_apparent","pf","freq","daily_kwh","monthly_kwh","peak_power","avg_power"})
        powerUpdate |= p[key].is<float>();
    if (!full && !envUpdate && !radarUpdate && !powerUpdate && !j["auto_preset"].is<int>() && !j["auto_master"].is<bool>()) return false;
    if (envFlag.is<bool>()) { s.envKnown=true; field(s.climate,envFlag); }
    if (envUpdate) s.envAt=now;
    if (radarFlag.is<bool>()) {
        field(s.radar,radarFlag); s.radarKnown=true;
        if (!s.radar) { s.motionKnown=s.stillKnown=s.distanceKnown=s.energyKnown=false; s.speedKnown=false; s.occupancyKnown=false; }
    }
    if (s.radar && target.is<int>()) {
        const int mode=target.as<int>();
        s.motionKnown=s.stillKnown=s.occupancyKnown=mode>=0 && mode<=3;
        s.occupied=mode!=0; s.moving=(mode&2)!=0; s.stationary=(mode&1)!=0;
    }
    if (s.radar && full && r["present"].is<bool>()) { s.occupied=r["present"]; s.occupancyKnown=true; }
    if (s.radar && full && r["motion"].is<bool>()) { s.moving=r["motion"]; s.motionKnown=true; }
    const auto breathing=r[full?"breathing":"presence_breathing"];
    if (s.radar && breathing.is<bool>()) { s.stationary=breathing.as<bool>(); s.stillKnown=true; }
    if (radarUpdate) s.radarAt=now;
    if (powerFlag.is<bool>()) { field(s.meter,powerFlag); s.powerKnown=true; }
    if (powerUpdate) s.powerAt=now;
    field(s.acState,p["ac_state"]);
    if (p["ac_present"].is<bool>()) field(s.ac,p["ac_present"]);
    if (!s.meter || strcmp(s.acState,"present")) s.ac=false;
    field(s.voltage,p["v_rms"]); field(s.current,p["i_rms"]);
    if (!s.climate) s.temperatureKnown=s.humidityKnown=s.co2Known=false;
    else {
        if (e["temp"].is<float>()) { field(s.temperature,e["temp"]); s.temperatureKnown=true; }
        if (e["humidity"].is<float>()) { field(s.humidity,e["humidity"]); s.humidityKnown=true; }
        if (e["co2_ppm"].is<int>() && e["co2_ppm"].as<int>()>=0 && e["co2_ppm"].as<int>()<=65535) {
            field(s.co2,e["co2_ppm"]); s.co2Known=true;
        }
    }
    field(s.presence,r[full?"state":"presence_state"]); field(s.distance,r[full?"dist":"presence_dist"]);
    if (s.radar && r[full?"dist":"presence_dist"].is<float>()) s.distanceKnown=true;
    if (s.radar && r[full?"energy":"presence_energy"].is<float>()) s.energyKnown=true;
    field(s.energy,r[full?"energy":"presence_energy"]);
    field(s.watts,p["p_active"]); field(s.va,p["p_apparent"]);
    field(s.pf,p["pf"]); field(s.hz,p["freq"]);
    field(s.daily,p["daily_kwh"]); field(s.monthly,p["monthly_kwh"]); field(s.peak,p["peak_power"]); field(s.average,p["avg_power"]);
    field(s.logging,p[full?"logging":"power_logging"]);
    field(s.heap,sys["heap"]); field(s.uptime,sys["uptime"]); field(s.controllerIp,sys["ip"]);
    field(s.sysLed,sys[full?"status_led":"system_status_led"]);
    field(s.radarLed,r[full?"status_led":"radar_status_led"]);
    field(s.radarOut,r[full?"output_led":"radar_output_led"]);
    if (live) {
        field(s.preset,j["auto_preset"]); field(s.master,j["auto_master"]);
        field(s.quiet,j["night_quiet_active"]); field(s.boost,j["air_boost_active"]);
        field(s.stabilized,j["pwr_stabilized"]); field(s.stabilization,j["pwr_rem"]);
        field(s.inactivity,j["inactivity_rem"]);
        field(s.filteredTemp,j["filtered_temp"]); field(s.filteredCo2,j["filtered_co2"]);
        field(s.tempValid,j["temp_valid"]); field(s.co2Valid,j["co2_valid"]);
        for (int i=0;i<8;i++) {
            const char* prefix=i<6?"relay":"fan"; const int id=i<6?i+1:i-5;
            auto& c=i<6?s.relay[i]:s.fan[i-6];
            char key[25];
            snprintf(key,sizeof key,"%s%d_auto",prefix,id); field(c.automatic,j[key]);
            snprintf(key,sizeof key,"%s%d_%s",prefix,id,i<6?"auto_st":"status"); field(c.automation,j[key]);
            snprintf(key,sizeof key,"%s%d_ov_rem",prefix,id); field(c.overrideSeconds,j[key]);
        }
    }
    if (acceptedCategories) *acceptedCategories=1 | (full?2:0) | (envUpdate?4:0) | (radarUpdate?8:0) | (powerUpdate?16:0);
    s.liveAt=now;
    return true;
}
inline bool settingsMatch(JsonVariantConst expected,JsonVariantConst actual) {
    if (expected.is<JsonObjectConst>()) {
        if (!actual.is<JsonObjectConst>()) return false;
        for (JsonPairConst kv : expected.as<JsonObjectConst>())
            if (!settingsMatch(kv.value(),actual[kv.key()])) return false;
        return true;
    }
    if (expected.is<JsonArrayConst>()) {
        if (!actual.is<JsonArrayConst>()) return false;
        for (JsonVariantConst row : expected.as<JsonArrayConst>()) {
            bool match=false;
            for (JsonVariantConst candidate : actual.as<JsonArrayConst>())
                if ((row["id"]|0)==(candidate["id"]|-1) && settingsMatch(row,candidate)) match=true;
            if (!match) return false;
        }
        return true;
    }
    if (expected.is<float>() && actual.is<float>()) return fabsf(expected.as<float>()-actual.as<float>())<0.001f;
    return !actual.isNull() && expected==actual;
}

}
