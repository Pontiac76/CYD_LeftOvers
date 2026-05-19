#pragma once

#include "Arduino.h"

constexpr const char *DEFAULT_UPDATE_URL = "http://192.168.4.2:8080";
constexpr unsigned long MINUTE_MS = 60UL * 1000UL;
constexpr int MAX_NTP_RANDOM_DELAY_SECONDS = 24 * 60 * 60;

bool wifi_start_STA();
extern unsigned long last_ntp_success_ms;
extern int consecutive_ntp_failures;
extern bool ntp_last_sync_succeeded;
extern bool ntp_ever_synced;

bool isLocalTimePlausible(const struct tm &local);
String getPrimaryNtpServer();
bool queryNtpServerAndSetClock(struct tm &local, unsigned long timeoutMs);
bool timesync(bool drawStatus = true);
void recordNtpSyncResult(bool syncSucceeded);
unsigned long computeNtpDelayMs(int baseMinutes, int randomDelaySeconds);
void scheduleNextNtpSync(bool lastSyncSucceeded);
void processScheduledNtpSync();
String build_update_request_url();
String build_update_request_url_for_system_id(const String &id);
void ensure_default_update_url();
void preload_all_cached_configs_from_server();
bool bootstrap_config_from_server();
void apply_runtime_NTP_config();
bool poll_update_server();
