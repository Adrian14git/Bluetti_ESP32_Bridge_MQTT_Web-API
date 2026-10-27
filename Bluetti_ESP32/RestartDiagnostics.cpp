#include "RestartDiagnostics.h"

#include <Preferences.h>
#include <esp_system.h>

namespace {
  Preferences restartPrefs;

  String lastRestartCause = "UNKNOWN";
  String lastResetReason = "UNKNOWN";
  String lastRestartUptime = "unavailable";

  const char* resetReasonName(esp_reset_reason_t reason) {
    switch (reason) {
      case ESP_RST_POWERON:   return "POWER_ON";
      case ESP_RST_EXT:       return "EXTERNAL_RESET";
      case ESP_RST_SW:        return "SOFTWARE_RESET";
      case ESP_RST_PANIC:     return "PANIC";
      case ESP_RST_INT_WDT:   return "INTERRUPT_WATCHDOG";
      case ESP_RST_TASK_WDT:  return "TASK_WATCHDOG";
      case ESP_RST_WDT:       return "WATCHDOG";
      case ESP_RST_DEEPSLEEP: return "DEEP_SLEEP_WAKE";
      case ESP_RST_BROWNOUT:  return "BROWNOUT";
      case ESP_RST_SDIO:      return "SDIO_RESET";
      default:                return "OTHER_RESET";
    }
  }
}

void initRestartDiagnostics() {
  restartPrefs.begin("restartdiag", false);

  const String pendingCause = restartPrefs.getString("pending", "");
  const String pendingUptime = restartPrefs.getString("pending_up", "");

  lastResetReason = resetReasonName(esp_reset_reason());

  if (pendingCause.length() > 0) {
    // A known code path requested a restart before the previous boot ended.
    lastRestartCause = pendingCause;
    lastRestartUptime = pendingUptime.length() > 0 ? pendingUptime : "unavailable";
  } else {
    // No instrumented restart path recorded a cause. Preserve the hardware reset
    // reason so watchdogs, brownouts and unexpected resets remain distinguishable.
    lastRestartCause = "NO_SOFTWARE_CAUSE_RECORDED";
    lastRestartUptime = "unavailable";
  }

  restartPrefs.putString("last_cause", lastRestartCause);
  restartPrefs.putString("last_reason", lastResetReason);
  restartPrefs.putString("last_uptime", lastRestartUptime);

  // Consume the pending marker so it cannot be mistaken for a later reset.
  restartPrefs.remove("pending");
  restartPrefs.remove("pending_up");
  restartPrefs.end();
}

void recordRestartCause(const char* cause) {
  if (!restartPrefs.begin("restartdiag", false)) {
    return;
  }

  restartPrefs.putString("pending", (cause != nullptr) ? cause : "UNKNOWN");
  restartPrefs.putString("pending_up", String(millis()));
  restartPrefs.end();
}

String getLastRestartCause() {
  return lastRestartCause;
}

String getLastResetReason() {
  return lastResetReason;
}

String getLastRestartUptime() {
  return lastRestartUptime;
}
