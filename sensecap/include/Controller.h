#pragma once
#include "Protocol.h"

namespace room {
enum class Action : uint8_t { Pair, Scan, Reset, Relay, Fan, Group, Refresh, History,
                              Automation, Safety, Patch, Preset, Clear, Names, Led, Logging };
struct Request {
    Action action{};
    int id{}, value{};
    uint32_t created{}, session{};
    char first[65]{}, second[65]{}, host[65]{}, payload[220]{};
};
void startClient();
bool submit(Request request);
bool readState(State&);
bool readConfig(bool safety, JsonDocument&);
}
