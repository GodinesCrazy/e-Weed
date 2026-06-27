#ifndef EWEED_STAGE_PROFILES_H
#define EWEED_STAGE_PROFILES_H

#include "system_types.h"

/** Cinco etapas: índices 0..4 (HMI / CMD:SET_STAGE compatible con 0-4 o 1-5). */
static const uint8_t kNumStages = 5;

static const StageProfile kStageProfiles[kNumStages] = {
    {"Inicio/Plántula", 5.80f, 0.30f, 350, 60, 6, 18, 45, 600, 1200, 500, 3, 27.0f, 25.0f, 75.0f, 68.0f},
    {"Crecimiento", 5.90f, 0.25f, 650, 80, 6, 18, 60, 420, 1500, 600, 3, 28.0f, 26.0f, 72.0f, 66.0f},
    {"Transición", 6.00f, 0.25f, 850, 90, 6, 16, 75, 360, 1700, 650, 4, 28.0f, 26.0f, 70.0f, 64.0f},
    {"Producción", 6.10f, 0.20f, 1050, 100, 6, 12, 90, 300, 1900, 700, 4, 29.0f, 27.0f, 68.0f, 62.0f},
    {"Maduración/Mant.", 6.00f, 0.25f, 700, 80, 7, 10, 45, 900, 900, 500, 2, 27.0f, 25.0f, 70.0f, 64.0f},
};

static inline const StageProfile &stageProfile(uint8_t index) {
  if (index >= kNumStages) {
    index = 0;
  }
  return kStageProfiles[index];
}

#endif  // EWEED_STAGE_PROFILES_H
