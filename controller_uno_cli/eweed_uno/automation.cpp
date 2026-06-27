#include "automation.h"
#include "actuators.h"
#include "alarms.h"
#include "config_pins.h"
#include "sensors.h"
#include "stage_profiles.h"
#include "uart_proto.h"
#include <stdio.h>
#include <string.h>

namespace Automation {

static OperatingMode  s_mode            = OperatingMode::kMaintenance;
static MachineState     s_state           = MachineState::kBoot;
static RecheckReason    s_recheck         = RecheckReason::kNone;
static uint8_t          s_stage           = 0;
static uint8_t          s_phCorr          = 0;
static uint8_t          s_tdsCorr         = 0;
static unsigned long    s_stateSinceMs    = 0;
static unsigned long    s_timedActionMs   = 0;
static unsigned long    s_recircToggleMs  = 0;
static bool             s_safeStartup     = true;
static char             s_lastAction[64]  = "Boot";

static void setState(MachineState next, const char *reason) {
  if (s_state == next && reason != nullptr && reason[0] != '\0') {
    strncpy(s_lastAction, reason, sizeof(s_lastAction) - 1);
    s_lastAction[sizeof(s_lastAction) - 1] = '\0';
    return;
  }
  s_state         = next;
  s_stateSinceMs  = millis();
  if (reason != nullptr && reason[0] != '\0') {
    strncpy(s_lastAction, reason, sizeof(s_lastAction) - 1);
  } else {
    strncpy(s_lastAction, fsmToShortName(next), sizeof(s_lastAction) - 1);
  }
  s_lastAction[sizeof(s_lastAction) - 1] = '\0';
}

static const SensorData &sd() { return Sensors::data(); }

static bool stageNeedsLightNow() {
  if (!Sensors::rtcAvailable()) {
    return false;
  }
  const StageProfile &p = stageProfile(s_stage);
  const uint16_t      nowMin =
      static_cast<uint16_t>(Sensors::rtcHour()) * 60U + Sensors::rtcMinute();
  const uint16_t startMin = static_cast<uint16_t>(p.lightStartHour) * 60U;
  const uint16_t endMin = (startMin + static_cast<uint16_t>(p.lightHours) * 60U) % 1440U;
  if (p.lightHours >= 24) {
    return true;
  }
  if (startMin < endMin) {
    return nowMin >= startMin && nowMin < endMin;
  }
  return nowMin >= startMin || nowMin < endMin;
}

static void controlLightForStage() {
  if (!isAuto() || !Sensors::rtcAvailable()) {
    return;
  }
  Actuators::setRelay(RelayId::kLight, stageNeedsLightNow(), sd());
}

static void controlVentilationForStage() {
  if (!isAuto() || !sd().dhtValid) {
    return;
  }
  const StageProfile &p = stageProfile(s_stage);
  const bool          heatHigh = sd().airTempC >= p.ventTempOn;
  const bool          humHigh  = sd().airHumidity >= p.ventHumOn;
  const bool          heatLow  = sd().airTempC <= p.ventTempOff;
  const bool          humLow   = sd().airHumidity <= p.ventHumOff;

  bool shouldRun = Actuators::getRelay(RelayId::kIntractor) || Actuators::getRelay(RelayId::kExtractor);
  if (heatHigh || humHigh) {
    shouldRun = true;
  } else if (heatLow && humLow) {
    shouldRun = false;
  }
  Actuators::setRelay(RelayId::kIntractor, shouldRun, sd());
  Actuators::setRelay(RelayId::kExtractor, shouldRun, sd());
}

static void updateScheduledRecirculation() {
  if (!isAuto() || isMaintenance() || Alarms::active() || isStateManagingRecirculation()) {
    return;
  }
  const StageProfile &p   = stageProfile(s_stage);
  const unsigned long now = millis();
  const unsigned long onMs =
      static_cast<unsigned long>(p.recircOnSec) * 1000UL;
  const unsigned long offMs =
      static_cast<unsigned long>(p.recircOffSec) * 1000UL;

  if (Actuators::getRelay(RelayId::kRecirculation)) {
    if (now - s_recircToggleMs >= onMs) {
      Actuators::setRelay(RelayId::kRecirculation, false, sd());
      s_recircToggleMs = now;
    }
  } else {
    if (now - s_recircToggleMs >= offMs && !sd().levelMinActive) {
      Actuators::setRelay(RelayId::kRecirculation, true, sd());
      s_recircToggleMs = now;
    }
  }
}

bool isStateManagingRecirculation() {
  return s_state == MachineState::kRecircAfterPh || s_state == MachineState::kRecircAfterA ||
         s_state == MachineState::kRecircAfterB || s_state == MachineState::kFillWater ||
         s_state == MachineState::kDoseA || s_state == MachineState::kDoseB ||
         s_state == MachineState::kDosePhUp || s_state == MachineState::kDosePhDown;
}

static void startPhDose(bool up) {
  Actuators::disableCriticalOutputs(sd());
  Actuators::setRelay(up ? RelayId::kPhUp : RelayId::kPhDown, true, sd());
  s_timedActionMs = stageProfile(s_stage).phDoseMs;
  setState(up ? MachineState::kDosePhUp : MachineState::kDosePhDown,
           up ? "Dosificando pH+" : "Dosificando pH-");
}

static void startDoseA() {
  Actuators::disableCriticalOutputs(sd());
  Actuators::setRelay(RelayId::kPumpA, true, sd());
  s_timedActionMs = stageProfile(s_stage).nutrientDoseMs;
  setState(MachineState::kDoseA, "Microdosis A");
}

static void startDoseB() {
  Actuators::disableCriticalOutputs(sd());
  Actuators::setRelay(RelayId::kPumpB, true, sd());
  s_timedActionMs = stageProfile(s_stage).nutrientDoseMs;
  setState(MachineState::kDoseB, "Microdosis B");
}

static void startRecirculationState(MachineState st, const char *reason) {
  Actuators::disableCriticalOutputs(sd());
  Actuators::setRelay(RelayId::kRecirculation, true, sd());
  s_timedActionMs = HwConfig::kRecircAfterDoseMs;
  setState(st, reason);
}

static void startWaterFill(const char *reason) {
  Actuators::disableCriticalOutputs(sd());
  Actuators::setRelay(RelayId::kWaterIn, true, sd());
  s_timedActionMs = HwConfig::kFillTimeoutMs;
  setState(MachineState::kFillWater, reason);
}

static void validateSensorsForAuto() {
  if (Sensors::invalidPhCount() >= HwConfig::kMaxSensorFailures) {
    UartProto_notifyAlarmRaised(AlarmCode::kSensorPh);
    return;
  }
  if (Sensors::invalidTdsCount() >= HwConfig::kMaxSensorFailures) {
    UartProto_notifyAlarmRaised(AlarmCode::kSensorTds);
    return;
  }
  if (Sensors::invalidDs18Count() >= HwConfig::kMaxSensorFailures) {
    UartProto_notifyAlarmRaised(AlarmCode::kSensorDs18);
    return;
  }
  if (Sensors::invalidDhtCount() >= HwConfig::kMaxSensorFailures) {
    UartProto_notifyAlarmRaised(AlarmCode::kSensorDht);
    return;
  }
}

void begin() {
  s_mode        = OperatingMode::kMaintenance;
  s_state       = MachineState::kMaintenance;
  s_safeStartup = true;
  strncpy(s_lastAction, "Mantenimiento inicial (SAFE)", sizeof(s_lastAction) - 1);
  s_lastAction[sizeof(s_lastAction) - 1] = '\0';
}

void setLastAction(const char *text) {
  if (text == nullptr) {
    return;
  }
  strncpy(s_lastAction, text, sizeof(s_lastAction) - 1);
  s_lastAction[sizeof(s_lastAction) - 1] = '\0';
}

const char *lastAction() { return s_lastAction; }

OperatingMode mode() { return s_mode; }
bool          isAuto() { return s_mode == OperatingMode::kAuto; }
bool          isMaintenance() { return s_mode == OperatingMode::kMaintenance; }
bool          safeStartupActive() { return s_safeStartup; }
void          releaseSafeStartup() { s_safeStartup = false; }

MachineState fsmState() { return s_state; }

const char *fsmToShortName(MachineState st) {
  switch (st) {
    case MachineState::kBoot: return "BOOT";
    case MachineState::kSafeIdle: return "SAFE_IDLE";
    case MachineState::kManualIdle: return "MANUAL_IDLE";
    case MachineState::kAutoIdle: return "AUTO_IDLE";
    case MachineState::kReadSensors: return "READ_SENSORS";
    case MachineState::kValidateSensors: return "VALIDATE_SENSORS";
    case MachineState::kCheckLevel: return "CHECK_LEVEL";
    case MachineState::kFillWater: return "FILL_WATER";
    case MachineState::kEvaluatePh: return "EVALUATE_PH";
    case MachineState::kDosePhUp: return "DOSE_PH_UP";
    case MachineState::kDosePhDown: return "DOSE_PH_DOWN";
    case MachineState::kRecircAfterPh: return "RECIRC_AFTER_PH";
    case MachineState::kEvaluateTds: return "EVALUATE_TDS";
    case MachineState::kDoseA: return "DOSE_A";
    case MachineState::kRecircAfterA: return "RECIRC_AFTER_A";
    case MachineState::kDoseB: return "DOSE_B";
    case MachineState::kRecircAfterB: return "RECIRC_AFTER_B";
    case MachineState::kWaitStabilization: return "WAIT_STABILIZATION";
    case MachineState::kRemeasure: return "REMEASURE";
    case MachineState::kControlLight: return "CONTROL_LIGHT";
    case MachineState::kControlVentilation: return "CONTROL_VENTILATION";
    case MachineState::kSendStatus: return "SEND_STATUS";
    case MachineState::kAlarm: return "ALARM";
    case MachineState::kMaintenance: return "MAINTENANCE";
    default: return "UNKNOWN";
  }
}

const char *fsmToDisplayName(MachineState st) {
  switch (st) {
    case MachineState::kBoot: return "Arranque";
    case MachineState::kSafeIdle: return "Reposo seguro";
    case MachineState::kManualIdle: return "Manual en reposo";
    case MachineState::kAutoIdle: return "Auto en reposo";
    case MachineState::kReadSensors: return "Leyendo sensores";
    case MachineState::kValidateSensors: return "Validando sensores";
    case MachineState::kCheckLevel: return "Comprobando nivel";
    case MachineState::kFillWater: return "Llenando agua";
    case MachineState::kEvaluatePh: return "Evaluando pH";
    case MachineState::kDosePhUp: return "Dosificando pH+";
    case MachineState::kDosePhDown: return "Dosificando pH-";
    case MachineState::kRecircAfterPh: return "Recirculando (post pH)";
    case MachineState::kEvaluateTds: return "Evaluando TDS";
    case MachineState::kDoseA: return "Dosificando nutriente A";
    case MachineState::kRecircAfterA: return "Recirculando (post A)";
    case MachineState::kDoseB: return "Dosificando nutriente B";
    case MachineState::kRecircAfterB: return "Recirculando (post B)";
    case MachineState::kWaitStabilization: return "Esperando estabilizacion";
    case MachineState::kRemeasure: return "Re-medición";
    case MachineState::kControlLight: return "Control de luz";
    case MachineState::kControlVentilation: return "Control de ventilacion";
    case MachineState::kSendStatus: return "Enviando estado";
    case MachineState::kAlarm: return "Alarma";
    case MachineState::kMaintenance: return "Mantenimiento";
    default: return "Desconocido";
  }
}

uint8_t stageIndex() { return s_stage; }
void    setStageIndex(uint8_t idx) {
  if (idx >= kNumStages) {
    idx = 0;
  }
  s_stage = idx;
}

uint8_t phCorrectionCount() { return s_phCorr; }
uint8_t tdsCorrectionCount() { return s_tdsCorr; }
void    resetCorrectionCounters() {
  s_phCorr  = 0;
  s_tdsCorr = 0;
}

bool tryEnterAuto(char *errTag, size_t errLen) {
  if (errTag != nullptr && errLen > 0) {
    errTag[0] = '\0';
  }
  if (s_safeStartup) {
    if (errTag != nullptr && errLen > 16) {
      strncpy(errTag, "SAFE_STARTUP", errLen - 1);
      errTag[errLen - 1] = '\0';
    }
    return false;
  }
  if (!Sensors::rtcAvailable()) {
    UartProto_notifyAlarmRaised(AlarmCode::kRtcMissing);
    if (errTag != nullptr && errLen > 8) {
      strncpy(errTag, "NO_RTC", errLen - 1);
      errTag[errLen - 1] = '\0';
    }
    return false;
  }
  s_mode = OperatingMode::kAuto;
  setState(MachineState::kReadSensors, "Modo automatico");
  return true;
}

void setOperatingMode(OperatingMode m) {
  s_mode = m;
  if (m != OperatingMode::kAuto) {
    resetCorrectionCounters();
  }
  if (m == OperatingMode::kMaintenance) {
    Actuators::allOutputsOff(sd());
    setState(MachineState::kMaintenance, "Modo mantenimiento");
  } else if (m == OperatingMode::kAuto) {
    // Entrada a AUTO debe hacerse con tryEnterAuto() desde UART.
    setState(MachineState::kReadSensors, "AUTO");
  } else {
    if (s_safeStartup) {
      setState(MachineState::kSafeIdle, "Manual (arranque seguro)");
    } else {
      setState(MachineState::kManualIdle, "Modo manual");
    }
  }
}

void onAlarmRaised() { setState(MachineState::kAlarm, "Alarma activa"); }

void onAlarmCleared() {
  resetCorrectionCounters();
  if (isMaintenance()) {
    setState(MachineState::kMaintenance, "Alarma reconocida");
  } else if (isAuto()) {
    setState(MachineState::kReadSensors, "Alarma reconocida");
  } else {
    setState(MachineState::kManualIdle, "Alarma reconocida");
  }
}

void tick() {
  if (Alarms::active()) {
    if (s_state != MachineState::kAlarm) {
      setState(MachineState::kAlarm, "Alarma activa");
    }
    return;
  }

  if (isMaintenance()) {
    if (s_state != MachineState::kMaintenance) {
      setState(MachineState::kMaintenance, "Mantenimiento");
    }
    return;
  }

  if (!isAuto()) {
    if (s_safeStartup) {
      if (s_state != MachineState::kSafeIdle) {
        setState(MachineState::kSafeIdle, "Reposo seguro (liberar con SAFE_RELEASE)");
      }
    } else {
      if (s_state != MachineState::kManualIdle) {
        setState(MachineState::kManualIdle, "Manual en reposo");
      }
    }
    return;
  }

  const StageProfile &p       = stageProfile(s_stage);
  const unsigned long elapsed = millis() - s_stateSinceMs;

  switch (s_state) {
    case MachineState::kBoot:
    case MachineState::kSafeIdle:
    case MachineState::kManualIdle:
    case MachineState::kAutoIdle:
      setState(MachineState::kReadSensors, "Ciclo automatico");
      break;

    case MachineState::kReadSensors:
      Sensors::poll(true);
      setState(MachineState::kValidateSensors, "Sensores actualizados");
      break;

    case MachineState::kValidateSensors:
      if (!Sensors::rtcAvailable()) {
        UartProto_notifyAlarmRaised(AlarmCode::kRtcMissing);
        break;
      }
      if (Sensors::levelInconsistentCount() >= HwConfig::kLevelInconsistentReads) {
        UartProto_notifyAlarmRaised(AlarmCode::kLevelInconsistent);
        break;
      }
      validateSensorsForAuto();
      if (Alarms::active()) {
        break;
      }
      setState(MachineState::kCheckLevel, "Validacion OK");
      break;

    case MachineState::kCheckLevel:
      if (sd().levelMinActive && !sd().levelMaxActive) {
        UartProto_sendAlarm("NIVEL_BAJO");
        setLastAction("Nivel bajo, llenado");
        s_recheck = RecheckReason::kPh;
        startWaterFill("Reposicion de agua");
      } else {
        setState(MachineState::kEvaluatePh, "Evaluando pH");
      }
      break;

    case MachineState::kFillWater:
      if (sd().levelMaxActive) {
        Actuators::setRelay(RelayId::kWaterIn, false, sd());
        s_recheck = RecheckReason::kPh;
        setState(MachineState::kWaitStabilization, "Nivel recuperado");
      } else if (elapsed >= s_timedActionMs) {
        Actuators::setRelay(RelayId::kWaterIn, false, sd());
        UartProto_notifyAlarmRaised(AlarmCode::kFillTimeout);
      }
      break;

    case MachineState::kEvaluatePh:
      if (!sd().phValid) {
        UartProto_notifyAlarmRaised(AlarmCode::kSensorPh);
        break;
      }
      if (sd().phValue < p.phTarget - p.phTolerance) {
        if (s_phCorr >= p.maxCorrections) {
          UartProto_notifyAlarmRaised(AlarmCode::kCorrectionLimit);
          break;
        }
        ++s_phCorr;
        s_recheck = RecheckReason::kPh;
        startPhDose(true);
      } else if (sd().phValue > p.phTarget + p.phTolerance) {
        if (s_phCorr >= p.maxCorrections) {
          UartProto_notifyAlarmRaised(AlarmCode::kCorrectionLimit);
          break;
        }
        ++s_phCorr;
        s_recheck = RecheckReason::kPh;
        startPhDose(false);
      } else {
        s_phCorr = 0;
        setState(MachineState::kEvaluateTds, "pH en rango");
      }
      break;

    case MachineState::kDosePhUp:
    case MachineState::kDosePhDown:
      if (elapsed >= s_timedActionMs || elapsed >= HwConfig::kDoseTimeoutMs) {
        Actuators::setRelay(RelayId::kPhUp, false, sd());
        Actuators::setRelay(RelayId::kPhDown, false, sd());
        startRecirculationState(MachineState::kRecircAfterPh, "Recirc post pH");
      }
      break;

    case MachineState::kRecircAfterPh:
      if (elapsed >= s_timedActionMs) {
        Actuators::setRelay(RelayId::kRecirculation, false, sd());
        setState(MachineState::kWaitStabilization, "Espera estabilizacion pH");
      }
      break;

    case MachineState::kEvaluateTds:
      if (!sd().tdsValid) {
        UartProto_notifyAlarmRaised(AlarmCode::kSensorTds);
        break;
      }
      if (sd().tdsValue < p.tdsTarget - p.tdsTolerance) {
        if (s_tdsCorr >= p.maxCorrections) {
          UartProto_notifyAlarmRaised(AlarmCode::kCorrectionLimit);
          break;
        }
        ++s_tdsCorr;
        s_recheck = RecheckReason::kTds;
        startDoseA();
      } else {
        s_tdsCorr = 0;
        setState(MachineState::kControlLight, "TDS en rango");
      }
      break;

    case MachineState::kDoseA:
      if (elapsed >= s_timedActionMs || elapsed >= HwConfig::kDoseTimeoutMs) {
        Actuators::setRelay(RelayId::kPumpA, false, sd());
        startRecirculationState(MachineState::kRecircAfterA, "Mezcla post A");
      }
      break;

    case MachineState::kRecircAfterA:
      if (elapsed >= s_timedActionMs) {
        Actuators::setRelay(RelayId::kRecirculation, false, sd());
        startDoseB();
      }
      break;

    case MachineState::kDoseB:
      if (elapsed >= s_timedActionMs || elapsed >= HwConfig::kDoseTimeoutMs) {
        Actuators::setRelay(RelayId::kPumpB, false, sd());
        startRecirculationState(MachineState::kRecircAfterB, "Mezcla post B");
      }
      break;

    case MachineState::kRecircAfterB:
      if (elapsed >= s_timedActionMs) {
        Actuators::setRelay(RelayId::kRecirculation, false, sd());
        setState(MachineState::kWaitStabilization, "Espera estabilizacion TDS");
      }
      break;

    case MachineState::kWaitStabilization:
      if (elapsed > HwConfig::kStabilizationMs + 90000UL) {
        UartProto_notifyAlarmRaised(AlarmCode::kStabilizationTimeout);
      } else if (elapsed >= HwConfig::kStabilizationMs) {
        setState(MachineState::kRemeasure, "Remidiendo");
      }
      break;

    case MachineState::kRemeasure:
      Sensors::poll(true);
      if (s_recheck == RecheckReason::kPh) {
        setState(MachineState::kEvaluatePh, "Reeval pH");
      } else {
        setState(MachineState::kEvaluateTds, "Reeval TDS");
      }
      break;

    case MachineState::kControlLight:
      controlLightForStage();
      setState(MachineState::kControlVentilation, "Luz aplicada");
      break;

    case MachineState::kControlVentilation:
      controlVentilationForStage();
      setState(MachineState::kSendStatus, "Ventilacion aplicada");
      break;

    case MachineState::kSendStatus:
      UartProto_sendStatus();
      s_recheck = RecheckReason::kNone;
      setState(MachineState::kAutoIdle, "Ciclo completado");
      break;

    default:
      break;
  }

  if (isAuto() && !isMaintenance() && !Alarms::active()) {
    updateScheduledRecirculation();
  }
}

}  // namespace Automation
