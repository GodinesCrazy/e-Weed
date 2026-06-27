#ifndef EWEED_ALARMS_H
#define EWEED_ALARMS_H

#include "system_types.h"

namespace Alarms {

void begin();
void raise(AlarmCode code);
void clear();
bool active();
AlarmCode current();

}  // namespace Alarms

#endif
