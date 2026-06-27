#ifndef EWEED_AUTOMATION_H
#define EWEED_AUTOMATION_H

#include "system_types.h"

namespace Automation {

void begin();

/** Un paso de FSM (llamar desde loop). */
void tick();

void setLastAction(const char *text);
const char *lastAction();

OperatingMode mode();
bool          isAuto();
bool          isMaintenance();
bool          safeStartupActive();
void          releaseSafeStartup();

MachineState fsmState();
const char  *fsmToShortName(MachineState s);
const char  *fsmToDisplayName(MachineState s);

uint8_t stageIndex();
void    setStageIndex(uint8_t idx);

uint8_t phCorrectionCount();
uint8_t tdsCorrectionCount();
void    resetCorrectionCounters();

/** Intenta AUTO; devuelve false si falta RTC o arranque seguro sin liberar. */
bool tryEnterAuto(char *errTag, size_t errLen);

void setOperatingMode(OperatingMode m);

void onAlarmRaised();
void onAlarmCleared();

bool isStateManagingRecirculation();

}  // namespace Automation

#endif
