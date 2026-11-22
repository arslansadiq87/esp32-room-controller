#define ROOM_PANEL_PREVIEW
#include "TimeService.h"
#include <cassert>
#include <iostream>
int main() {
    using namespace panelClock;
    assert(!strcmp(zones[zone],"PKT-5"));
    assert(nextAction(false)==SyncAction::None);
    assert(nextAction(true)==SyncAction::Start);
    assert(nextAction(true)==SyncAction::None);
    dirty=true;
    assert(nextAction(true)==SyncAction::Start);
    assert(nextAction(false)==SyncAction::Stop);
    assert(nextAction(true)==SyncAction::Start);
    automatic=false; dirty=true;
    assert(nextAction(true)==SyncAction::Stop);
    tm local{}; local.tm_hour=22; local.tm_min=24;
    char output[40];
    formatLocal(output,sizeof output,0,local);
    assert(!strcmp(output,"Time unset"));
    formatLocal(output,sizeof output,1800000000,local);
    assert(!strcmp(output,"10:24 PM"));
    twelve=false; ++local.tm_min;
    formatLocal(output,sizeof output,1800000060,local);
    assert(!strcmp(output,"22:25"));
    date=true; local.tm_mon=10; local.tm_mday=21; local.tm_wday=6;
    formatLocal(output,sizeof output,1800000060,local);
    assert(!strcmp(output,"22:25\nSat, Nov 21"));
    std::cout << "Clock policy and formatting passed\n";
}
