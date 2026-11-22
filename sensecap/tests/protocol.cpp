#include "Protocol.h"
#include <cassert>
#include <iostream>

using namespace room;
JsonDocument decode(const char* data) {
    JsonDocument result;
    assert(!deserializeJson(result,data));
    return result;
}
bool ingest(State& s,const char* payload,uint32_t now) {
    JsonDocument d;
    return !deserializeJson(d,payload) && apply(s,d.as<JsonObjectConst>(),now);
}
int main() {
    TextFrame fragments;
    const uint8_t first[]={'{','"','t','y','p','e','"',':','"','f','u','l','l','_'};
    const uint8_t last[]={'s','t','a','t','u','s','"','}'};
    assert(fragments.append(first,sizeof first));
    assert(fragments.append(last,sizeof last));
    assert(fragments.length==sizeof first+sizeof last && fragments.bytes[fragments.length]==0);
    assert(!strcmp(reinterpret_cast<const char*>(fragments.bytes),R"({"type":"full_status"})"));
    fragments.reset();
    uint8_t oversized[TextFrame::capacity+1]{};
    assert(!fragments.append(oversized,sizeof oversized) && fragments.length==0);
    State s;
    s.socket=s.wifi=true;
    assert(!s.ready(100));
    assert(ingest(s,R"({
      "type":"full_status",
      "relays":[{"id":1,"state":false,"sw":true},{"id":2,"state":true},{"id":3,"state":false},
                {"id":4,"state":false},{"id":5,"state":false},{"id":6,"state":true}],
      "dimmers":[{"id":1,"state":true,"level":45,"fan_relay":true},
                 {"id":2,"state":false,"level":50,"fan_relay":false}],
      "names":{"relay1":"Desk","fan1":"Ceiling fan"},
      "environment":{"online":true,"temp":24.8,"humidity":54,"co2_ppm":692,"status":0},
      "presence":{"online":true,"state":3,"speed":0.28,"dist":1.2,"energy":12},
      "power":{"online":true,"ac_state":"present","ac_present":true,"v_rms":248.9,"i_rms":0.23,"p_active":56},
      "system":{"ip":"192.168.1.20","heap":123456,"uptime":19},"mqtt":{"connected":false}
    })",100));
    assert(s.ready(101) && s.climate && s.radar && s.ac && s.co2==692);
    assert(!strcmp(s.relay[0].name,"Desk") && s.relay[0].wall && !s.relay[0].on);
    assert(s.fan[0].level==45 && s.fan[0].interlock && s.presence==3 && s.speed>0.27f);
    assert(ingest(s,R"({"type":"relay_update","id":1,"state":true})",200) && s.relay[0].on);
    assert(ingest(s,R"({"type":"dimmer_update","id":1,"state":true,"level":75,"fan_relay":true})",210));
    assert(s.fan[0].level==75 && s.fan[0].interlock);
    assert(!ingest(s,R"({"type":"dimmer_update","id":1,"state":true,"level":101,"fan_relay":true})",211));
    assert(!ingest(s,R"({"type":"relay_update","id":7,"state":true})",220));
    assert(!ingest(s,R"({"type":"relay_update","id":1,"state":"off"})",221));
    assert(!ingest(s,"{bad json",230));
    assert(!ingest(s,R"({"type":"full_status","relays":[{"id":1,"state":false}]})",240));
    assert(s.relay[0].on && s.snapshotAt==100);
    assert(!ingest(s,R"({"type":"telemetry","co2_ppm":999999})",250));
    assert(ingest(s,R"({
      "type":"telemetry","env_online":false,"pwr_online":true,"ac_present":false,
      "ac_state":"idle","v_rms":-1,"i_rms":-1,"presence_online":true,"presence_state":1,
      "presence_dist":1.3,"presence_energy":17,"fan1_auto":true,"fan1_ov_rem":42,
      "auto_preset":2,"relay6_auto":true,"relay6_auto_st":"Scheduled","daily_kwh":0.024
    })",300));
    assert(!s.climate && !s.ac && s.voltage<0 && s.current<0 && s.watts==56 && s.presence==1);
    assert(s.fan[0].automatic && s.fan[0].overrideSeconds==42 && s.relay[5].automatic && s.preset==2);
    assert(s.ready(10299) && !s.ready(10300));
    assert(ingest(s,R"({"type":"telemetry","humidity":48})",11000));
    assert(s.ready(11000) && s.radarStatus(11000)==State::Availability::Stale);
    assert(ingest(s,R"({"type":"telemetry","presence_online":false,"presence_dist":2.53})",11001));
    assert(s.radarStatus(11001)==State::Availability::Offline && !s.occupancyKnown);
    assert(ingest(s,R"({"type":"telemetry","presence_online":true,"presence_state":0})",11002));
    assert(s.radarStatus(11002)==State::Availability::Online && s.occupancyKnown && !s.presence);
    assert(ingest(s,R"({"type":"telemetry","presence_state":7})",11003));
    assert(!s.occupancyKnown && s.radar);
    assert(ingest(s,R"({"type":"telemetry","p_active":42})",UINT32_MAX-3));
    assert(s.ready(3) && s.radarAt==11003 && s.watts==42);
    s.socket=false;
    assert(!s.ready(301));
    s.socket=true; s.snapshot=false;
    assert(!s.ready(302));
    State stream;
    stream.wifi=stream.socket=stream.snapshot=true;
    assert(ingest(stream,R"({"type":"telemetry","presence_online":true,"presence_state":1,"presence_dist":2.53,"presence_energy":202})",100));
    for (uint32_t t=500;t<31000;t+=500) {
        assert(ingest(stream,R"({"type":"telemetry","env_online":true,"temp":26})",t));
        assert(stream.ready(t) && stream.presence==1);
        if (t>10100) assert(stream.radarStatus(t)==State::Availability::Stale);
    }
    assert(!stream.controlsReady(30500));
    assert(ingest(stream,R"({"type":"telemetry","presence_online":true,"presence_state":2,"presence_dist":0})",31000));
    assert(stream.radarStatus(31000)==State::Availability::Online && stream.distanceKnown && stream.distance==0 && stream.energy==202);
    stream.socket=false;
    assert(stream.radarStatus(31001)==State::Availability::Stale);
    assert(!ingest(stream,R"({"type":"telemetry","presence_online":"false"})",31002) && stream.liveAt==31000);
    assert(commandIsCurrent(1000,900,7,7));
    assert(!commandIsCurrent(1000,900,7,8));
    assert(!commandIsCurrent(3600,900,7,7));
    auto automation=decode(R"({"master":true,"off_delay":120,"preset":2,"fan1":{"enabled":true,"target_temp":26},"fan2":{"mode":3},"scenarios":{"night_quiet":true},"relays":[{"id":6,"mode":3,"night_allowed":false}]})");
    assert(automation["relays"][0]["mode"]==3 && automation["fan2"]["mode"]==3);
    auto safety=decode(R"({"t_hyst":0.5,"c_hyst":80,"f_min_on":30,"override_fans":[{"id":1,"active":true,"rem_sec":42}]})");
    assert(safety["override_fans"][0]["rem_sec"]==42);
    auto unchanged=decode(R"({"fan2":{"mode":3},"relays":[{"id":6,"mode":3}]})");
    assert(settingsMatch(unchanged.as<JsonVariantConst>(),automation.as<JsonVariantConst>()));
    auto unconfirmed=decode(R"({"relays":[{"id":6,"mode":2}]})");
    assert(!settingsMatch(unconfirmed.as<JsonVariantConst>(),automation.as<JsonVariantConst>()));
    auto matchingSafety=decode(R"({"t_hyst":0.50})");
    assert(settingsMatch(matchingSafety.as<JsonVariantConst>(),safety.as<JsonVariantConst>()));
    auto missing=decode(R"({"man_dur":600})");
    assert(!settingsMatch(missing.as<JsonVariantConst>(),safety.as<JsonVariantConst>()));
    // Several rapid touch events from the previous socket cannot survive a reconnect.
    for (uint32_t touchTime: {1000u,1040u,1080u}) {
        assert(commandIsCurrent(1200,touchTime,11,11));
        assert(!commandIsCurrent(1200,touchTime,11,12));
    }
    std::cout << "Protocol fixtures passed\n";
}
