#include <Arduino.h>
#include <algorithm>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WebSocketsClient.h>
#include "Controller.h"

namespace room {
namespace {
constexpr char settingsNamespace[]="room-panel";
State state, published;
JsonDocument automation, safety;
SemaphoreHandle_t lock;
QueueHandle_t queue;
QueueHandle_t restRequests, restResults;
struct RestJob { char ip[16]; uint32_t session; Request command; };
struct RestResult { uint32_t session, duration; Action action; bool safety; JsonDocument data; bool ok; };
WebSocketsClient ws;
String ssid, password, host, address;
bool mdns{}, configured{}, wsStarted{};
uint32_t nextWifi{}, nextWs{}, nextRefresh{}, nextConfig{}, reconcileAt{}, serial{}, generation{1};
uint32_t delayWifi{1000}, delayWs{1000}, generationPublished{1};
uint32_t categoryCount[6]{};
uint32_t wsOpenedAt{}, lastWsRx{}, lastDiag{}, reconnectCount{}, httpLast{}, httpMax{};
TextFrame wsFrame;
bool wsFragmentActive{};
struct Command {
    bool active{}, group{};
    int id{}, value{};
    Action action{};
    uint32_t until{}, after{};
    int64_t sentAtUs{};
} pending;

void message(const char* text) { copy(state.message,text); }
void publish() {
    state.session=generation;
    if (xSemaphoreTake(lock, 0) != pdTRUE) return;
    published=state;
    generationPublished=generation;
    xSemaphoreGive(lock);
}

bool local(IPAddress ip) {
    return ip[0]==10 || (ip[0]==172 && ip[1]>=16 && ip[1]<=31) ||
           (ip[0]==192 && ip[1]==168) || (ip[0]==169 && ip[1]==254);
}
String resolve(const String& name) {
    IPAddress ip;
    if (!ip.fromString(name)) {
        if (name.endsWith(".local") && mdns) ip=MDNS.queryHost(name.substring(0,name.length()-6).c_str(),900);
        else if (name.indexOf('/')<0 && name.indexOf(':')<0) WiFi.hostByName(name.c_str(),ip);
    }
    return local(ip)?ip.toString():String();
}

bool fetch(const String& target,const char* path,JsonDocument& output,const char* body,size_t limit) {
    if (target.isEmpty() || WiFi.status()!=WL_CONNECTED) return false;
    WiFiClient transport;
    HTTPClient client;
    client.setConnectTimeout(1000);
    client.setTimeout(1500);
    client.useHTTP10(true);
    if (!client.begin(transport,"http://"+target+path)) return false;
    if (body) client.addHeader("Content-Type","application/json");
    const int code=body?client.POST(String(body)):client.GET();
    const int size=client.getSize();
    bool ok=false;
    if (code==200 && size>0 && static_cast<size_t>(size)<=limit) {
        String data=client.getString();
        ok=data.length()==static_cast<unsigned>(size) &&
           !deserializeJson(output,data,DeserializationOption::NestingLimit(9)) &&
           output.is<JsonObject>();
    }
    client.end();
    return ok;
}

bool http(const char* path,JsonDocument& output,const char* body=nullptr,size_t limit=12000) {
    const uint32_t started=millis();
    const bool ok=fetch(address,path,output,body,limit);
    httpLast=millis()-started;
    httpMax=std::max(httpMax,httpLast);
    state.httpLastMs=httpLast;
    state.httpMaxMs=httpMax;
    return ok;
}

bool acceptConfig(bool advanced,JsonDocument& data) {
    if (advanced?!data["t_hyst"].is<float>():!data["master"].is<bool>()) return false;
    xSemaphoreTake(lock,portMAX_DELAY);
    state.configAt=millis(); ++categoryCount[5];
    JsonDocument& dest=advanced?safety:automation;
    // Live fields also arrive on WebSocket. Retain the REST schema for editing.
    if (dest.as<JsonVariantConst>()!=data.as<JsonVariantConst>()) {
        dest=std::move(data);
        ++state.configVersion;
    }
    xSemaphoreGive(lock);
    if (!advanced) {
        field(state.preset,automation["preset"]);
        field(state.master,automation["master"]);
        state.fanConfigured[0]=automation["fan1"]["enabled"] | false;
        state.fanConfigured[1]=(automation["fan2"]["mode"] | 0) != 0;
    }
    return true;
}

void clearCommand(const char* status) {
    if (pending.active) state.optimisticActive=false;
    pending={};
    state.busy=false;
    message(status);
}

void disconnected() {
    if (state.socket || state.snapshot) {
        ++generation;
        if (pending.active) clearCommand("Connection ended; command unconfirmed");
    }
    state.socket=false;
    state.snapshot=false;

}

void requestSnapshot() {
    ws.sendTXT("{\"type\":\"get_status\"}");
    nextRefresh=millis()+10000;
}

bool matches(int id) {
    if (pending.action==Action::Fan) {
        const auto& fan=state.fan[id-1];
        return fan.on==(pending.value>0) && fan.interlock==(pending.value>0) &&
               (pending.value==0 || fan.level==pending.value);
    }
    return state.relay[id-1].on==(pending.value!=0);
}

bool sendChannel() {
    if (!state.controlsReady(millis())) return false;
    char body[100];
    if (pending.action==Action::Fan && pending.value>0)
        snprintf(body,sizeof body,"{\"type\":\"dimmer\",\"id\":%d,\"level\":%d,\"state\":true}",pending.id,pending.value);
    else
        snprintf(body,sizeof body,"{\"type\":\"%s\",\"id\":%d,\"state\":%s}",
                 pending.action==Action::Fan?"dimmer":"relay",pending.id,pending.value?"true":"false");
    const int64_t startedUs=esp_timer_get_time();
    if (!ws.sendTXT(body)) return false;
    pending.sentAtUs=esp_timer_get_time();
    Serial.printf("[TRACE] sent channel=%d send_us=%lld\n",pending.id,
                  static_cast<long long>(pending.sentAtUs-startedUs));
    pending.until=millis()+4200;
    pending.after=serial;
    reconcileAt=millis()+120;
    return true;
}

void receiveText(const uint8_t* payload,size_t length) {
    lastWsRx=millis();
    ++state.receivedMessages;
    JsonDocument data;
    const auto err=deserializeJson(data,reinterpret_cast<const char*>(payload),length,
                                   DeserializationOption::NestingLimit(9));
    const uint32_t accepted=millis();
    uint8_t categories=0;
    if (err || !apply(state,data.as<JsonObjectConst>(),accepted,&categories)) {
        ++state.malformedMessages;
        Serial.printf("[%lu] [WS] ignored session=%lu reason=%s type=%.24s\n",accepted,generation,
                      err?err.c_str():"unsupported/incomplete payload",data["type"]|"untyped");
        return;
    }
    for (unsigned i=0;i<5;++i) if (categories & (1u<<i)) ++categoryCount[i];
    const char* type=data["type"] | "";
    const bool full=!strcmp(type,"full_status");
    const bool update=pending.active && (data["id"] | 0)==pending.id &&
        !strcmp(type,pending.action==Action::Fan?"dimmer_update":"relay_update");
    if (state.optimisticActive) {
        const bool matchesPending=state.optimisticFan
            ? state.fan[state.optimisticId-1].on==state.optimisticOn &&
              (!state.optimisticOn || state.fan[state.optimisticId-1].level==state.optimisticLevel)
            : state.relay[state.optimisticId-1].on==state.optimisticOn;
        if (full || update || !matchesPending) state.optimisticActive=false;
    }
    if (!full && !update) return;
    ++serial;
    if (pending.active && serial!=pending.after && matches(pending.id)) {
        Serial.printf("[TRACE] %s_match channel=%d elapsed_us=%lld\n",full?"snapshot":"event",pending.id,
                      static_cast<long long>(esp_timer_get_time()-pending.sentAtUs));
        if (pending.group && pending.id<6) {
            ++pending.id;
            if (!sendChannel()) clearCommand("Group stopped. Some lights may have changed.");
        } else {
            bool all=true;
            if (pending.group) for (int i=1;i<=6;i++) all &= matches(i);
            clearCommand(all?"Confirmed by controller":"Group only partially applied");
        }
    }
}

bool appendFragment(const uint8_t* payload,size_t length) {
    if (!wsFrame.append(payload,length)) {
        ++state.malformedMessages;
        wsFragmentActive=false;
        Serial.printf("[%lu] [WS] fragmented message exceeded %u bytes\n",millis(),static_cast<unsigned>(TextFrame::capacity));
        return false;
    }
    return true;
}

void onSocket(WStype_t type, uint8_t* payload,size_t length) {
    if (type==WStype_CONNECTED) {
        wsFragmentActive=false; wsFrame.reset();
        state.socket=true; state.snapshot=false; delayWs=1000;
        memset(categoryCount,0,sizeof categoryCount);
        state.temperatureKnown=state.humidityKnown=state.co2Known=state.motionKnown=state.stillKnown=state.distanceKnown=state.energyKnown=state.radarKnown=state.envKnown=state.powerKnown=state.occupancyKnown=state.speedKnown=false;
        wsOpenedAt=millis(); lastWsRx=wsOpenedAt; ++reconnectCount;
        Serial.printf("[%lu] [WS] connected host=%s ip=%s reconnect=%lu heap=%u largest=%u\n",
                      millis(),host.c_str(),address.c_str(),reconnectCount,ESP.getFreeHeap(),heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        ++generation;
        requestSnapshot();
        nextConfig=millis()+250;
        return;
    }
    if (type==WStype_DISCONNECTED) {
        wsFragmentActive=false; wsFrame.reset();
        Serial.printf("[%lu] [WS] disconnected uptime=%lu last_rx_age=%lu heap=%u\n",millis(),millis()-wsOpenedAt,millis()-lastWsRx,ESP.getFreeHeap());
        disconnected(); return;
    }
    if (type==WStype_TEXT) { wsFragmentActive=false; wsFrame.reset(); receiveText(payload,length); return; }
    if (type==WStype_FRAGMENT_TEXT_START) {
        wsFrame.reset(); wsFragmentActive=true;
        if (!appendFragment(payload,length)) return;
        return;
    }
    if (type==WStype_FRAGMENT && wsFragmentActive) { appendFragment(payload,length); return; }
    if (type==WStype_FRAGMENT_FIN && wsFragmentActive) {
        if (appendFragment(payload,length)) receiveText(wsFrame.bytes,wsFrame.length);
        wsFragmentActive=false; wsFrame.reset();
        return;
    }
    if (type==WStype_FRAGMENT_BIN_START || type==WStype_BIN) {
        wsFragmentActive=false; wsFrame.reset();
    }
}

bool hostValid(const char* name) {
    const size_t n=strlen(name);
    return n>0 && n<=64 && strspn(name,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-")==n;
}

void stopSocket() {
    disconnected();
    if (wsStarted) ws.disconnect();
    wsStarted=false;
}
void startSocket() {
    if (address.isEmpty()) return;
    ws.begin(address.c_str(),80,"/ws");
    ws.onEvent(onSocket);
    // The worker owns reconnect timing. The library still owns the socket.
    // Heartbeat traffic is independent of 500 ms telemetry. Use a generous
    // interval so a brief blocked HTTP transaction cannot cause false expiry.
    ws.setReconnectInterval(5000);
    ws.enableHeartbeat(15000,3000,2);
    wsStarted=true;
    nextWs=millis()+4000;
}
void backoff(uint32_t& next,uint32_t& step) {
    next=millis()+step;
    step=std::min<uint32_t>(step*2,30000);
}

void pair(const Request& request) {
    if (!request.first[0] || strlen(request.first)>32 || strlen(request.second)>64 ||
        !hostValid(request.host)) { message("Enter a Wi-Fi name and controller hostname or IP."); return; }
    const String candidatePass=(!request.second[0] && configured && ssid==request.first)
                                ? password : String(request.second);
    stopSocket();
    state.busy=true;
    message("Joining Wi-Fi and testing controller...");
    publish();
    WiFi.disconnect(false,false);
    WiFi.begin(request.first,candidatePass.c_str());
    const uint32_t start=millis();
    while (WiFi.status()!=WL_CONNECTED && millis()-start<12000) vTaskDelay(pdMS_TO_TICKS(100));
    bool joined=WiFi.status()==WL_CONNECTED;
    bool savedConnection=false;
    if (joined && !mdns) mdns=MDNS.begin("sensecap-room-panel");
    address=joined?resolve(request.host):String();
    JsonDocument reply;
    if (joined && http("/api/status",reply) && !strcmp(reply["type"] | "","full_status") &&
        apply(state,reply.as<JsonObjectConst>(),millis())) {
        Preferences prefs;
        JsonDocument settings;
        settings["ssid"]=request.first; settings["pass"]=candidatePass; settings["host"]=request.host;
        String saved; serializeJson(settings,saved);
        if (prefs.begin(settingsNamespace,false) && prefs.putString("connection",saved)==saved.length()) {
            ssid=request.first; password=candidatePass; host=request.host;
            configured=true; state.paired=true; savedConnection=true;
            ++state.pairingRevision;
            copy(state.host,host.c_str());
            copy(state.savedSsid,ssid.c_str());
            message("Network and controller saved");
        } else message("Nonvolatile storage failed. Settings were not saved.");
    } else message(joined?"Controller test failed. Check address and network.":"Wi-Fi connection failed. Check password.");
    state.busy=false;
    state.snapshot=false;
    state.temperatureKnown=state.humidityKnown=state.co2Known=state.motionKnown=state.stillKnown=state.distanceKnown=state.energyKnown=state.radarKnown=state.envKnown=state.powerKnown=state.occupancyKnown=state.speedKnown=false;
    if (!savedConnection) {
        WiFi.disconnect(false,false);
        address.clear();
    }
    nextWifi=nextWs=millis();
}

void scan() {
    state.scanning=true; message("Scanning Wi-Fi..."); publish();
    WiFi.scanDelete();
    WiFi.scanNetworks(true,false);
}
void pollScan() {
    if (!state.scanning) return;
    int found=WiFi.scanComplete();
    if (found==WIFI_SCAN_RUNNING) return;
    state.networkCount=0;
    for (int i=0;i<found && state.networkCount<16;i++) {
        String name=WiFi.SSID(i);
        bool duplicate=false;
        for (unsigned k=0;k<state.networkCount;k++) duplicate |= name==state.networks[k];
        if (!name.isEmpty() && !duplicate) copy(state.networks[state.networkCount++],name.c_str());
    }
    WiFi.scanDelete(); state.scanning=false;
    message(state.networkCount?"Select a network or enter an SSID.":"No networks found. Enter an SSID or rescan.");
}


bool currentSession(uint32_t session) {
    xSemaphoreTake(lock,portMAX_DELAY);
    const bool current=published.session==session && published.ready(millis());
    xSemaphoreGive(lock);
    return current;
}

bool writeConfig(const RestJob& job, JsonDocument& actual) {
    const Request& r=job.command;
    const char* getPath=nullptr; const char* postPath=nullptr;
    char body[240];
    switch (r.action) {
        case Action::Patch:
            getPath=r.id?"/api/automation/safety":"/api/automation";
            copy(body,r.payload); break;
        case Action::Preset:
            getPath="/api/automation"; postPath="/api/automation/preset";
            snprintf(body,sizeof body,"{\"preset\":%d}",r.value); break;
        case Action::Clear:
            getPath="/api/automation/safety"; postPath="/api/automation/safety/clear_override"; copy(body,"{}"); break;
        case Action::Names: getPath="/api/names"; copy(body,r.payload); break;
        case Action::Led:
            getPath=r.id==0?"/api/system/led":"/api/mmwave/led";
            snprintf(body,sizeof body,"{\"%s\":%s}",r.id==2?"output_led":"status_led",r.value?"true":"false"); break;
        case Action::Logging:
            getPath="/api/power/config";
            snprintf(body,sizeof body,"{\"logging_enabled\":%s}",r.value?"true":"false"); break;
        default: return false;
    }
    JsonDocument expected,reply,baseline;
    if (deserializeJson(expected,body)) return false;
    bool ok=fetch(job.ip,getPath,baseline,nullptr,12000) && currentSession(job.session) &&
            commandIsCurrent(millis(),r.created,r.session,job.session) &&
            fetch(job.ip,postPath?postPath:getPath,reply,body,12000) &&
            fetch(job.ip,getPath,actual,nullptr,12000);
    if (ok && r.action==Action::Clear) {
        ok=actual["override_relays"].size()==6 && actual["override_fans"].size()==2;
        for (const char* key:{"override_relays","override_fans"})
            for (JsonObjectConst row:actual[key].as<JsonArrayConst>()) ok &= row["active"].is<bool>() && !row["active"].as<bool>();
    } else if (ok) ok=settingsMatch(expected.as<JsonVariantConst>(),actual.as<JsonVariantConst>());
    return ok;
}

bool scheduleHttp(Request command) {
    RestJob job{};
    copy(job.ip,address.c_str());
    job.session=generation;
    job.command=command;
    return restRequests && xQueueSend(restRequests,&job,0)==pdTRUE;
}

void restWorker(void*) {
    RestJob request;
    while (xQueueReceive(restRequests,&request,portMAX_DELAY)==pdTRUE) {
        auto* result=new (std::nothrow) RestResult{};
        if (!result) continue;
        result->session=request.session;
        const auto started=millis();
        result->action=request.command.action;
        result->safety=result->action==Action::Safety || result->action==Action::Clear ||
                       (result->action==Action::Patch && request.command.id);
        if (currentSession(request.session)) {
            switch (result->action) {
                case Action::History: result->ok=fetch(request.ip,"/api/power/history",result->data,nullptr,26000); break;
                case Action::Safety:
                case Action::Automation: result->ok=fetch(request.ip,result->safety?"/api/automation/safety":"/api/automation",result->data,nullptr,12000); break;
                default: result->ok=writeConfig(request,result->data); break;
            }
        }
        result->duration=millis()-started;
        xQueueSend(restResults,&result,portMAX_DELAY);
    }
    vTaskDelete(nullptr);
}

void acceptHistory(const JsonDocument& response) {
    auto arr=response["history"].as<JsonArrayConst>();
    uint32_t latest=0;
    for (auto row:arr) latest=std::max<uint32_t>(latest,row["t"]|uint32_t{0});
    constexpr unsigned bins=120;
    float total[bins]{}; uint16_t count[bins]{};
    unsigned earliest=bins;
    for (auto row:arr) {
        const auto t=row["t"]|0u;
        if (!t || latest-t>=86400 || !row["w"].is<float>() ||
            !isfinite(row["w"].as<float>()) || row["w"].as<float>()>1000000) continue;
        unsigned i=bins-1-(latest-t)/(86400/bins);
        total[i]+=row["w"].as<float>(); ++count[i];
        earliest=min(earliest,i);
    }
    state.historyCount=earliest==bins?0:bins-earliest;
    for (unsigned i=earliest;i<bins;i++) state.history[i-earliest]=count[i]?total[i]/count[i]:-1;
    ++state.historyVersion;
}

void process(const Request& r) {
    if (r.action==Action::Refresh) {
        if (state.socket && !state.busy) requestSnapshot();
        else if (!state.busy) {
            stopSocket();
            nextWifi=nextWs=millis();
            delayWifi=delayWs=1000;
            message("Retrying saved connection");
        }
        return;
    }
    if (r.action==Action::Scan) { if (!state.scanning) scan(); return; }
    if (r.action==Action::Pair) { pair(r); return; }
    if (r.action==Action::Reset) {
        Preferences prefs;
        if (!prefs.begin(settingsNamespace,false) || !prefs.clear()) {
            message("Could not reset connection settings");
            return;
        }
        prefs.end();
        if (prefs.begin("panel-display",false)) { prefs.clear(); prefs.end(); }
        if (prefs.begin("panel-clock",false)) { prefs.clear(); prefs.end(); }
        ESP.restart();
        return;
    }
    if (!state.ready(millis()) || !commandIsCurrent(millis(),r.created,r.session,generation)) {
        message("Controller unavailable. Action discarded."); return;
    }
    if (state.busy) { message("Wait for the pending command"); return; }
    switch (r.action) {
        case Action::Relay: case Action::Fan: case Action::Group:
            if (r.action==Action::Relay) {
                if (r.id<1 || r.id>6) return;
                if (state.relay[r.id-1].on==(r.value!=0)) {
                    message("Output changed elsewhere. Check its current state.");
                    return;
                }
            }
            if (r.action==Action::Fan && (r.id<1 || r.id>2 || r.value<0 || r.value>100)) return;
            pending={true,r.action==Action::Group,r.action==Action::Group?1:r.id,r.value,r.action,0,0};
            if (r.action==Action::Relay || r.action==Action::Fan) {
                state.optimisticActive=true;
                state.optimisticFan=r.action==Action::Fan;
                state.optimisticId=r.id;
                state.optimisticOn=r.value>0;
                state.optimisticLevel=r.value;
            }
            state.busy=true; message("Awaiting controller read-back...");
            if (!sendChannel()) clearCommand("Send failed. Check controller state.");
            break;
        default:
            if (!scheduleHttp(r)) message("Configuration queue busy");
            break;
    }
}

void worker(void*) {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);
    Preferences prefs;
    JsonDocument saved;
    if (prefs.begin(settingsNamespace,true)) {
        deserializeJson(saved,prefs.getString("connection",""));
        prefs.end();
    }
    ssid=saved["ssid"]|""; password=saved["pass"]|""; host=saved["host"]|"";
    configured=!ssid.isEmpty() && hostValid(host.c_str());
    state.paired=configured; copy(state.host,host.c_str()); copy(state.savedSsid,ssid.c_str());
    message(configured?"Connecting to saved network":"Set up your room panel");
    while (true) {
        const uint32_t now=millis();
        state.wifi=WiFi.status()==WL_CONNECTED;
        if (!state.wifi) {
            if (wsStarted) stopSocket();
            address.clear();
            state.ip[0]=0; state.ssid[0]=0; state.rssi=0;
            if (configured && !state.scanning && static_cast<int32_t>(now-nextWifi)>=0) {
                WiFi.begin(ssid.c_str(),password.c_str());
                backoff(nextWifi,delayWifi);
                if (delayWifi<8000) nextWifi=now+8000;
            }
        } else {
            delayWifi=1000;
            if (!mdns) mdns=MDNS.begin("sensecap-room-panel");
            copy(state.ip,WiFi.localIP().toString().c_str()); copy(state.ssid,WiFi.SSID().c_str());
            state.rssi=WiFi.RSSI();
            if (configured && !wsStarted && static_cast<int32_t>(now-nextWs)>=0) {
                address=resolve(host);
                if (address.isEmpty()) backoff(nextWs,delayWs); else startSocket();
            }
            if (wsStarted) {
                static uint32_t previousService=0, maxGap=0, windowStart=0;
                const uint32_t service=millis();
                if (previousService && state.socket) maxGap=std::max(maxGap,service-previousService);
                previousService=service;
                ws.loop();
                if (service-windowStart>=10000) {
                    Serial.printf("[TRACE] ws_service_max_ms=%lu\n",maxGap);
                    maxGap=0; windowStart=service;
                }
            }
            if (wsStarted && !state.socket && static_cast<int32_t>(millis()-nextWs)>=0) {
                stopSocket(); backoff(nextWs,delayWs);
            }
            // Freshness is a separate state from transport. Do not destroy a
            // healthy WebSocket because a single telemetry frame was delayed.
            if (state.socket && static_cast<int32_t>(millis()-nextRefresh)>=0) requestSnapshot();
            if (pending.active && static_cast<int32_t>(millis()-reconcileAt)>=0) {
                if (static_cast<int32_t>(millis()-pending.until)>=0) {
                    const bool succeeded=pending.action!=Action::Group && matches(pending.id);
                    clearCommand(pending.group?"Group not fully confirmed":"Command unconfirmed or superseded");
                    if (succeeded) message("Confirmed by controller read-back");
                    else message("Controller kept its previous state");
                } else {
                    requestSnapshot();
                    reconcileAt=millis()+450;
                }
            }
        }
        pollScan();
        RestResult* result=nullptr;
        if (restResults && xQueueReceive(restResults,&result,0)==pdTRUE) {
            if (result->session==generation && state.socket) {
                state.httpLastMs=httpLast=result->duration;
                state.httpMaxMs=httpMax=std::max(httpMax,httpLast);
                if (!result->ok) message("HTTP request failed or change unconfirmed");
                else if (result->action==Action::History) acceptHistory(result->data);
                else {
                    if (result->action==Action::Automation || result->action==Action::Safety ||
                        result->action==Action::Patch || result->action==Action::Clear || result->action==Action::Preset)
                        acceptConfig(result->safety,result->data);
                    if (result->action!=Action::Automation && result->action!=Action::Safety) {
                        requestSnapshot();
                        message("Controller read-back confirmed");
                    }
                }
            }
            delete result;
        }
        Request r;
        if (xQueueReceive(queue,&r,0)==pdTRUE) {
            if (r.action==Action::Relay || r.action==Action::Fan || r.action==Action::Group)
                Serial.printf("[TRACE] dequeued queue_ms=%lu\n",millis()-r.created);
            process(r);
        } else if (state.ready(millis()) && !pending.active &&
                   static_cast<int32_t>(millis()-nextConfig)>=0) {
            nextConfig=millis()+(scheduleHttp({Action::Automation})?120000:1000);
        }
        const uint32_t sampled=millis();
        static bool expired[6]{};
        const uint32_t times[]={state.liveAt,state.outputsAt,state.envAt,state.radarAt,state.powerAt,state.configAt};
        const bool known[]={state.snapshot,state.snapshot,state.envKnown,state.radarKnown,state.powerKnown,state.configVersion!=0};
        const char* categories[]={"transport","outputs","environment","radar","power","configuration"};
        for (unsigned i=0;i<6;++i) {
            const uint32_t age=sampled-times[i];
            const bool stale=known[i] && (!state.socket || age>=(i==5?150000u:i==1?20000u:staleAfter));
            if (sampled-lastDiag>=10000) Serial.printf("[%lu] [FRESH] %s session=%lu rx=%lu last=%lu age=%lu known=%d\n",
                sampled,categories[i],generation,categoryCount[i],times[i],age,known[i]);
            if (expired[i]!=stale) Serial.printf("[%lu] [FRESH] %s %s age_ms=%lu ws=%d\n",sampled,categories[i],stale?"stale":"reset/fresh",age,state.socket);
            expired[i]=stale;
        }
        publish();
        if (sampled-lastDiag>=10000) {
            lastDiag=sampled;
            Serial.printf("[%lu] [SENSORS] generation=%lu env=%d radar=%d power=%d snapshot_age=%lu raw_rx_age=%lu\n",
                sampled,generation,state.climate,state.radar,state.meter,sampled-state.snapshotAt,sampled-lastWsRx);
            state.largestBlock=heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
            state.wsReconnects=reconnectCount;
            state.diagnosticsAt=now;
            Serial.printf("[%lu] [NET] wifi=%d ip=%s rssi=%d ws=%d snapshot=%d telemetry_age=%lu rx=%lu bad=%lu reconnects=%lu pending=%d http_ms=%lu/%lu heap=%u largest=%u\n",
                now,state.wifi,state.ip,state.rssi,state.socket,state.snapshot,
                state.liveAt?sampled-state.liveAt:0,state.receivedMessages,state.malformedMessages,reconnectCount,
                pending.active,state.httpLastMs,state.httpMaxMs,ESP.getFreeHeap(),state.largestBlock);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
}

void startClient() {
    lock=xSemaphoreCreateMutex();
    queue=xQueueCreate(4,sizeof(Request));
    restRequests=xQueueCreate(4,sizeof(RestJob));
    restResults=xQueueCreate(1,sizeof(RestResult*));
    if (!restRequests || !restResults ||
        xTaskCreatePinnedToCore(restWorker,"room-http",10240,nullptr,1,nullptr,0)!=pdPASS) {
        if (restRequests) vQueueDelete(restRequests);
        if (restResults) vQueueDelete(restResults);
        restRequests=nullptr;
        restResults=nullptr;
    }
    if (lock && queue) xTaskCreatePinnedToCore(worker,"room-network",14336,nullptr,2,nullptr,0);
}
bool submit(Request r) {
    if (!lock || !queue || xSemaphoreTake(lock,0)!=pdTRUE) return false;
    if (!r.session) r.session=generationPublished;
    r.created=millis();
    bool ok=xQueueSend(queue,&r,0)==pdTRUE;
    xSemaphoreGive(lock);
    return ok;
}
bool readState(State& result) {
    if (!lock || xSemaphoreTake(lock,0)!=pdTRUE) return false;
    result=published;
    xSemaphoreGive(lock);
    return true;
}
bool readConfig(bool advanced,JsonDocument& result) {
    if (!lock || xSemaphoreTake(lock,0)!=pdTRUE) return false;
    result=advanced?safety:automation;
    xSemaphoreGive(lock);
    return !result.isNull();
}
}

