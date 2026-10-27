#ifndef RESTART_DIAGNOSTICS_H
#define RESTART_DIAGNOSTICS_H

#include <Arduino.h>

// Call once at the beginning of setup(), before network initialization.
void initRestartDiagnostics();

// Call immediately before a controlled ESP.restart() or deep-sleep reset.
void recordRestartCause(const char* cause);

// Values for the fixed web page status card.
String getLastRestartCause();
String getLastResetReason();
String getLastRestartUptime();

#endif
