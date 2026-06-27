#ifndef EWEED_UART_PROTO_H
#define EWEED_UART_PROTO_H

#include "system_types.h"
#include <Arduino.h>

void UartProto_begin();
void UartProto_pollUsb();
void UartProto_pollHmi();
void UartProto_periodicStatusIfDue();

void UartProto_sendStatus();
void UartProto_sendAck(const char *payload);
void UartProto_sendErr(const char *payload);
void UartProto_sendAlarm(const char *payload);

/** Eleva alarma hardware + mensajes UART (ERR/ALM) una sola vez por código. */
void UartProto_notifyAlarmRaised(AlarmCode code);

void UartProto_processCommandLine(char *line, bool fromHmi);

#endif
