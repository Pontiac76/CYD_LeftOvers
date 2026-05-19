#include "network_manager.h"

#include "app_state.h"
#include "config_manager.h"
#include "schedule_display.h"
#include "storage_manager.h"

#include <HTTPClient.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_system.h>
#include <sys/time.h>
#include <time.h>

unsigned long next_update_check = 0;
int next_update_modular = 15;
int ntp_sync_frequency_minutes = 1;
int ntp_sync_random_delay_seconds = 0;
int ntp_retry_frequency_minutes = 1;
int ntp_retry_random_delay_seconds = 0;
unsigned long next_ntp_sync_ms = 0;
bool ntp_sync_scheduled = false;
unsigned long last_ntp_success_ms = 0;
int consecutive_ntp_failures = 0;
bool ntp_last_sync_succeeded = false;
bool ntp_ever_synced = false;

void recordNtpSyncResult(bool syncSucceeded)
{
  ntp_last_sync_succeeded = syncSucceeded;
  if (syncSucceeded)
  {
    last_ntp_success_ms = millis();
    consecutive_ntp_failures = 0;
    ntp_ever_synced = true;
    Serial.println("NTP health: sync OK");
  }
  else
  {
    ++consecutive_ntp_failures;
    Serial.print("NTP health: sync failed count=");
    Serial.println(consecutive_ntp_failures);
  }
}

bool wifi_start_STA() //Start WiFi Mode STA
{
  int sync_count = 0;
  WiFi.mode(WIFI_STA);
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi Start");
    tft.println("WiFi Start");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
      delay(100);
      Serial.print(".");
      tft.print(".");
      sync_count = ++sync_count;
      if (sync_count == 40)
      {
        Serial.println();
        tft.println();
        return 0;
        break;
      }
      if (sync_count == 6)
      {
        WiFi.begin(ssid, password); //second try
      }
      if (sync_count == 20)
      {
        WiFi.begin(ssid, password); //second try
      }
    }
  }
  Serial.println();
  tft.println();
  Serial.print(F("IP address STA: "));
  tft.print(F("IP address STA: "));
  Serial.println(WiFi.localIP());
  tft.println(WiFi.localIP());
  Serial.print(F("SSID: "));
  tft.print(F("SSID: "));
  Serial.println(WiFi.SSID());
  tft.println(WiFi.SSID());
  Serial.printf("BSSID: %s\n", WiFi.BSSIDstr().c_str());
  tft.printf("BSSID: %s\n", WiFi.BSSIDstr().c_str());
  // Serial.print(F("PW: "));
  // tft.print(F("PW: "));
  // Serial.println(WiFi.psk());
  // tft.println(WiFi.psk());
  return 1;
}

bool isLocalTimePlausible(const struct tm &local)
{
  return (local.tm_year + 1900) >= 2024;
}

String getPrimaryNtpServer()
{
  String server = ntpserver;
  server.trim();

  int commaIndex = server.indexOf(',');
  if (commaIndex >= 0)
  {
    server = server.substring(0, commaIndex);
    server.trim();
  }

  int spaceIndex = server.indexOf(' ');
  if (spaceIndex >= 0)
  {
    server = server.substring(0, spaceIndex);
    server.trim();
  }

  return server;
}

bool queryNtpServerAndSetClock(struct tm &local, unsigned long timeoutMs)
{
  constexpr unsigned long NTP_UNIX_EPOCH_OFFSET = 2208988800UL;
  constexpr int NTP_PACKET_SIZE = 48;

  String server = getPrimaryNtpServer();
  Serial.println("[NTP] query begin");
  Serial.print("[NTP] configured server='");
  Serial.print(ntpserver);
  Serial.print("' primary='");
  Serial.print(server);
  Serial.println("'");
  Serial.print("[NTP] timeoutMs=");
  Serial.print(timeoutMs);
  Serial.print(" wifiStatus=");
  Serial.println(WiFi.status());

  if (server == "")
  {
    Serial.println("[NTP] server is not configured");
    return false;
  }

  IPAddress ntpAddress;
  if (!WiFi.hostByName(server.c_str(), ntpAddress))
  {
    Serial.print("[NTP] DNS/host lookup failed: ");
    Serial.println(server);
    return false;
  }

  Serial.print("[NTP] target ");
  Serial.print(server);
  Serial.print(" -> ");
  Serial.println(ntpAddress);

  uint8_t packetBuffer[NTP_PACKET_SIZE] = {0};
  packetBuffer[0] = 0b11100011; // LI, Version, Mode
  packetBuffer[1] = 0;          // Stratum
  packetBuffer[2] = 6;          // Polling Interval
  packetBuffer[3] = 0xEC;       // Peer Clock Precision
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;

  WiFiUDP udp;
  uint16_t localPort = uint16_t(random(49152, 65535));
  Serial.print("[NTP] udp begin localPort=");
  Serial.println(localPort);
  if (!udp.begin(localPort))
  {
    Serial.println("[NTP] UDP begin failed");
    return false;
  }

  Serial.println("[NTP] sending UDP packet to port 123");
  udp.beginPacket(ntpAddress, 123);
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  if (!udp.endPacket())
  {
    Serial.println("[NTP] UDP send failed");
    udp.stop();
    return false;
  }

  unsigned long startMs = millis();
  while (millis() - startMs < timeoutMs)
  {
    int packetSize = udp.parsePacket();
    if (packetSize > 0)
    {
      Serial.print("[NTP] UDP packet received size=");
      Serial.println(packetSize);
    }

    if (packetSize >= NTP_PACKET_SIZE)
    {
      udp.read(packetBuffer, NTP_PACKET_SIZE);
      udp.stop();

      Serial.print("[NTP] response LI/VN/mode=0x");
      Serial.print(packetBuffer[0], HEX);
      Serial.print(" stratum=");
      Serial.println(packetBuffer[1]);

      unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
      unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
      unsigned long ntpSeconds = (highWord << 16) | lowWord;
      Serial.print("[NTP] raw seconds=");
      Serial.print(ntpSeconds);
      Serial.print(" unix=");
      Serial.println(ntpSeconds > NTP_UNIX_EPOCH_OFFSET ? ntpSeconds - NTP_UNIX_EPOCH_OFFSET : 0);
      if (ntpSeconds <= NTP_UNIX_EPOCH_OFFSET)
      {
        Serial.println("[NTP] response timestamp invalid");
        return false;
      }

      time_t unixSeconds = time_t(ntpSeconds - NTP_UNIX_EPOCH_OFFSET);
      timeval tv = { unixSeconds, 0 };
      settimeofday(&tv, nullptr);
      setenv("TZ", tzinfo.c_str(), 1);
      tzset();
      localtime_r(&unixSeconds, &local);
      Serial.println(&local, "[NTP] local after set: %Y-%m-%d %H:%M:%S");
      bool plausible = isLocalTimePlausible(local);
      Serial.print("[NTP] plausible=");
      Serial.println(plausible ? "true" : "false");
      return plausible;
    }
    delay(10);
  }

  udp.stop();
  Serial.print("[NTP] query timed out: ");
  Serial.print(server);
  Serial.print(" after ");
  Serial.print(timeoutMs);
  Serial.println("ms");
  return false;
}

bool timesync(bool drawStatus)
{
  bool exit_status = 1;
  Serial.println("Get NTP Time");
  if (drawStatus)
  {
    tft.println("Get NTP Time");
  }
  if (WiFi.status() == WL_CONNECTED)
  {
    struct tm local;
    if (!queryNtpServerAndSetClock(local, 10000)) // Explicitly query NTP; local clock alone is not proof NTP is alive.
    {
      Serial.println("Timeserver cannot be reached !!!");
      if (drawStatus)
      {
        tft.println("Timeserver cannot be reached !!!");
      }
      exit_status = 0;
    }
    else
    {
      Serial.print("Timeserver: ");
      Serial.println(&local, "Datum: %d.%m.%y  Zeit: %H:%M:%S Test: %a,%B");
      if (drawStatus)
      {
        tft.print("Timeserver: ");
        tft.println(&local, "Datum: %d.%m.%y  Zeit: %H:%M:%S Test: %a,%B");
      }
      Serial.flush();
    }
  }
  else
  {
    Serial.println("WiFi not connected !!!");
    if (drawStatus)
    {
      tft.println("WiFi not connected !!!");
    }
    exit_status = 0;
  }
  return exit_status;
}

unsigned long computeNtpDelayMs(int baseMinutes, int randomDelaySeconds)
{
  int sanitizedBaseMinutes = int(max(1L, long(baseMinutes)));
  int sanitizedRandomSeconds = int(min(long(MAX_NTP_RANDOM_DELAY_SECONDS), max(0L, long(randomDelaySeconds))));
  unsigned long delayMs = (unsigned long)sanitizedBaseMinutes * MINUTE_MS;

  if (sanitizedRandomSeconds > 0)
  {
    delayMs += (unsigned long)random(sanitizedRandomSeconds + 1) * 1000UL;
  }

  return delayMs;
}

void scheduleNextNtpSync(bool lastSyncSucceeded)
{
  int baseMinutes = lastSyncSucceeded ? ntp_sync_frequency_minutes : ntp_retry_frequency_minutes;
  int randomSeconds = lastSyncSucceeded ? ntp_sync_random_delay_seconds : ntp_retry_random_delay_seconds;

  Serial.print("[NTP] schedule request lastSyncSucceeded=");
  Serial.print(lastSyncSucceeded ? "true" : "false");
  Serial.print(" configuredBaseMinutes=");
  Serial.print(baseMinutes);
  Serial.print(" configuredRandomSeconds=");
  Serial.println(randomSeconds);

  unsigned long delayMs = computeNtpDelayMs(baseMinutes, randomSeconds);

  next_ntp_sync_ms = millis() + delayMs;
  ntp_sync_scheduled = true;

  Serial.print("[NTP] effective schedule baseMinutes=");
  Serial.print(baseMinutes);
  Serial.print(" randomSeconds=");
  Serial.println(randomSeconds);
  Serial.print("Next NTP sync in ");
  Serial.print(delayMs / 60000UL);
  Serial.print("m ");
  Serial.print((delayMs % 60000UL) / 1000UL);
  Serial.println("s");
}

void processScheduledNtpSync()
{
  static unsigned long nextWaitingLogMs = 0;

  if (!ntp_ever_synced)
  {
    struct tm local;
    if (getLocalTime(&local, 10) && isLocalTimePlausible(local))
    {
      Serial.println("[NTP] background/local time became plausible before explicit poll");
      Serial.println(&local, "[NTP] local clock says: %Y-%m-%d %H:%M:%S");
    }
  }

  if (!ntp_sync_scheduled)
  {
    Serial.println("[NTP] no scheduled sync; scheduling one");
    scheduleNextNtpSync(ntp_ever_synced);
    return;
  }

  long msUntilSync = long(next_ntp_sync_ms - millis());
  if (msUntilSync > 0)
  {
    if (long(millis() - nextWaitingLogMs) >= 0)
    {
      Serial.print("[NTP] waiting ");
      Serial.print(msUntilSync / 1000L);
      Serial.print("s until next poll; everSynced=");
      Serial.print(ntp_ever_synced ? "true" : "false");
      Serial.print(" failures=");
      Serial.println(consecutive_ntp_failures);
      nextWaitingLogMs = millis() + 15000UL;
    }
    return;
  }

  Serial.print("[NTP] scheduled poll due; everSynced=");
  Serial.print(ntp_ever_synced ? "true" : "false");
  Serial.print(" failures=");
  Serial.println(consecutive_ntp_failures);

  bool syncSucceeded = timesync(false);
  Serial.print("[NTP] scheduled poll result=");
  Serial.println(syncSucceeded ? "success" : "failure");
  recordNtpSyncResult(syncSucceeded);
  scheduleNextNtpSync(syncSucceeded);
}

String build_update_request_url()
{
  return build_update_request_url_for_system_id(system_id);
}

String build_update_request_url_for_system_id(const String &id)
{
  String requestUrl = updateurl;

  if (id == "")
  {
    return requestUrl;
  }

  if (requestUrl.indexOf('?') == -1)
  {
    requestUrl += "/?systemid=";
  }
  else
  {
    requestUrl += "/&systemid=";
  }

  requestUrl += id;
  return requestUrl;
}

void ensure_default_update_url()
{
  if (updateurl == "")
  {
    updateurl = DEFAULT_UPDATE_URL;
    Serial.print("updateurl missing, using default: ");
    Serial.println(updateurl);
  }
}

void preload_all_cached_configs_from_server()
{
  HTTPClient http;
  String payload;
  String requestUrl;
  String cachePath;

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("prefetch skipped: WiFi not connected");
    return;
  }

  ensure_default_update_url();
  if (updateurl == "")
  {
    Serial.println("prefetch skipped: no updateurl");
    return;
  }

  for (int index = 0; index < system_id_count; ++index)
  {
    requestUrl = build_update_request_url_for_system_id(system_id_list[index]);
    Serial.print("Prefetch ");
    Serial.println(requestUrl);
    http.begin(requestUrl);
    http.setTimeout(1500);
    int httpCode = http.GET();

    if ((httpCode == HTTP_CODE_OK))
    {
      payload = http.getString();
      http.end();
      if (payload.length() > 0)
      {
        bool changed = !cached_config_loaded[index] ||
                       !configContentEqualsNormalized(cached_config_texts[index], payload);
        cached_config_texts[index] = payload;
        cached_config_loaded[index] = true;
        config_source_state[index] = "LIVE";

        if (!ram_only_mode && changed)
        {
          cachePath = get_config_cache_path_for_id(system_id_list[index]);
          if (!write_text_file_to_sd(cachePath, payload))
          {
            Serial.print("Prefetch write failed for ");
            Serial.println(cachePath);
          }
        }
        if (index == active_system_id_index)
        {
          if (!ram_only_mode && !write_config_to_sd(payload))
          {
            Serial.println("Prefetch legacy SD /config.txt write failed");
          }
          if (!writeLittleFsTextMounted("/config.txt", payload))
          {
            Serial.println("Prefetch LittleFS /config.txt write failed");
          }
        }
        continue;
      }
    }
    else if (httpCode > 0)
    {
      Serial.print("Prefetch HTTP code ");
      Serial.print(httpCode);
      Serial.print(" for ");
      Serial.println(system_id_list[index]);
      http.end();
    }
    else
    {
      Serial.print("Prefetch GET failed for ");
      Serial.print(system_id_list[index]);
      Serial.print(": ");
      Serial.println(http.errorToString(httpCode));
      http.end();
    }

    if (!cached_config_loaded[index])
    {
      load_cached_config_for_index_from_storage(index, (index == 0));
    }
    if (cached_config_loaded[index] && config_source_state[index] == "MISSING")
    {
      config_source_state[index] = "CACHED";
    }
    if (!cached_config_loaded[index])
    {
      config_source_state[index] = "ERROR";
    }
  }
}

bool bootstrap_config_from_server()
{
  HTTPClient http;
  String payload;
  int httpCode;
  String requestUrl;

  Serial.println("Bootstrap config check");
  tft.println("Bootstrap config check");

  if (updateurl == "")
  {
    updateurl = DEFAULT_UPDATE_URL;
    Serial.print("updateurl missing, using default: ");
    Serial.println(updateurl);
    tft.println("using default updateurl");
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi not connected, bootstrap skipped");
    tft.println("WiFi not connected");
    return false;
  }

  requestUrl = build_update_request_url();
  http.begin(requestUrl);
  http.setTimeout(5000);
  httpCode = http.GET();

  if (httpCode <= 0)
  {
    Serial.print("HTTP GET failed: ");
    Serial.println(http.errorToString(httpCode));
    tft.println("HTTP GET failed");
    http.end();
    return false;
  }

  if (httpCode != HTTP_CODE_OK)
  {
    Serial.print("HTTP response code: ");
    Serial.println(httpCode);
    tft.println("HTTP bad response");
    http.end();
    return false;
  }

  payload = http.getString();
  http.end();

  if (payload.length() == 0)
  {
    Serial.println("Downloaded config is empty");
    tft.println("Downloaded config empty");
    return false;
  }

  bool changed = false;
  if (ram_only_mode)
  {
    if (current_config_text == payload)
    {
      Serial.println("Bootstrap config unchanged (RAM_ONLY)");
      tft.println("Bootstrap unchanged");
      return false;
    }
    current_config_text = payload;
    apply_config_from_string(current_config_text);
    Serial.println("Bootstrap config applied in RAM_ONLY mode");
    tft.println("Bootstrap applied (RAM_ONLY)");
    return true;
  }

  if (!sync_config_to_sd_and_memory(payload, changed))
  {
    Serial.println("Failed to write downloaded config");
    tft.println("Write config failed");
    return false;
  }

  if (!changed)
  {
    Serial.println("Bootstrap config unchanged");
    tft.println("Bootstrap unchanged");
    return false;
  }

  Serial.println("Bootstrap config written, rebooting");
  tft.println("Bootstrap written");
  delay(1000);
  ESP.restart();
  return true;
}

void apply_runtime_NTP_config()
{
  if ((WiFi.status() == WL_CONNECTED) && (tzinfo != "") && (ntpserver != ""))
  {
    Serial.println("Reapplying TZ/NTP config");
    configTzTime(tzinfo.c_str(), ntpserver.c_str());
  }
}

bool poll_update_server()
{
  HTTPClient http;
  String payload;
  int httpCode;
  String requestUrl;
  String cachePath;
  int activeIndex = active_system_id_index;

  ensure_default_update_url();

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("Update check skipped: WiFi not connected");
    next_update_modular = min(next_update_modular,1440);
    return false;
  }

  Serial.print("Update check: ");
  requestUrl = build_update_request_url();
  Serial.println(requestUrl);

  http.begin(requestUrl);
  http.setTimeout(1000);
  httpCode = http.GET();

  if (httpCode <= 0)
  {
    Serial.print("Update GET failed: ");
    Serial.println(http.errorToString(httpCode));
    http.end();
    return false;
  }

  if (httpCode != HTTP_CODE_OK)
  {
    Serial.print("Update HTTP code: ");
    Serial.println(httpCode);
    http.end();
    return false;
  }

  payload = http.getString();
  http.end();

  if (payload.length() == 0)
  {
    Serial.println("Update check: empty payload");
    return false;
  }

  bool changed = !configContentEqualsNormalized(current_config_text, payload);

  if ((activeIndex >= 0) && (activeIndex < system_id_count))
  {
    bool cacheChanged = !cached_config_loaded[activeIndex] ||
                        !configContentEqualsNormalized(cached_config_texts[activeIndex], payload);
    cached_config_texts[activeIndex] = payload;
    cached_config_loaded[activeIndex] = true;
    config_source_state[activeIndex] = "LIVE";
    Serial.print("Config state ");
    Serial.print(system_id_list[activeIndex]);
    Serial.println(": LIVE");

    if (!ram_only_mode && cacheChanged)
    {
      cachePath = get_config_cache_path_for_id(system_id_list[activeIndex]);
      if (!write_text_file_to_sd(cachePath, payload))
      {
        Serial.print("Update check: failed to write ");
        Serial.println(cachePath);
      }
    }
  }

  if (!changed)
  {
    Serial.println("Update check: no config delta");
    reportScheduleEntriesForCurrentTime();
    return true;
  }

  current_config_text = payload;
  apply_current_config_with_runtime_state();
  reportScheduleEntriesForCurrentTime();

  return true;
}
