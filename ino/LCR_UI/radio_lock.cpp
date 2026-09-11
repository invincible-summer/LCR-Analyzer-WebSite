// radio_lock.cpp —— 互斥标志实现（无依赖，host/固件共用）
#include "radio_lock.h"

static bool s_measurementActive = false;
static bool s_radioActive = false;

void radioLockNotifyMeasurementActive(bool active) { s_measurementActive = active; }
void radioLockNotifyRadioActive(bool active) { s_radioActive = active; }

bool radioLockInvariantOk() { return !(s_measurementActive && s_radioActive); }

void radioLockReset()
{
    s_measurementActive = false;
    s_radioActive = false;
}
