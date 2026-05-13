#include "leftovers_display.h"

#include "app_state.h"
#include "display_manager.h"
#include "leftovers_session.h"
#include "network_manager.h"
#include "status_icon_art.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <time.h>

constexpr int DATETIME_REGION_X = 0;
constexpr int DATETIME_REGION_Y = 0;
constexpr int DATETIME_REGION_H = 42;
constexpr int LIST_REGION_X = 0;
constexpr int LIST_REGION_Y = DATETIME_REGION_H;
constexpr int LIST_REGION_H = 88;
constexpr int STATUS_REGION_X = 0;
constexpr int STATUS_REGION_H = 24;
constexpr int STATUS_ICON_Y = 4;
constexpr int STATUS_ICON_GAP = 10;

int last_drawn_year = -1;
int last_drawn_mon = -1;
int last_drawn_mday = -1;
int last_drawn_hour = -1;
int last_drawn_min = -1;
bool full_draw_required = true;
bool last_wifi_available = true;
bool last_ntp_available = true;
int last_ntp_failure_count = -1;
bool last_edit_session_active = false;
bool last_record_blink_on = false;
bool last_cached_data_dirty = false;

extern bool wifi_connected_at_boot;
extern bool ntp_synced_at_boot;

void logSpriteMemory(const char *label, const char *phase)
{
  Serial.printf(
    "[MEM] %s %s free=%u min=%u maxAlloc=%u\n",
    label,
    phase,
    ESP.getFreeHeap(),
    ESP.getMinFreeHeap(),
    ESP.getMaxAllocHeap()
  );
}

String formatLosDateTime(const struct tm &localtime)
{
  char buffer[32];
  int hour12 = localtime.tm_hour % 12;
  if (hour12 == 0)
  {
    hour12 = 12;
  }

  String month = MonthName[localtime.tm_mon];
  if (month.length() > 3)
  {
    month = month.substring(0, 3);
  }

  snprintf(buffer, sizeof(buffer), "%s %d - %d:%02d",
           month.c_str(),
           localtime.tm_mday,
           hour12,
           localtime.tm_min);
  return String(buffer);
}

void renderDateTimeRegion(const struct tm *localtime)
{
  //logSpriteMemory("datetime", "before-create");

  TFT_eSprite sprite = TFT_eSprite(&tft);
  if (!sprite.createSprite(tft.width(), DATETIME_REGION_H))
  {
    Serial.println("[MEM] datetime sprite create failed");
    return;
  }

  //logSpriteMemory("datetime", "after-create");

  sprite.fillSprite(TFT_BLACK);
  sprite.setTextDatum(MC_DATUM);
  sprite.setTextFont(4);
  sprite.setTextSize(1);
  sprite.setTextColor(dateTextColor, TFT_BLACK);

  if (localtime != nullptr)
  {
    sprite.drawString(formatLosDateTime(*localtime), sprite.width() / 2, DATETIME_REGION_H / 2, 4);
  }
  else
  {
    sprite.drawString("Time unavailable", sprite.width() / 2, DATETIME_REGION_H / 2, 4);
  }

  sprite.pushSprite(DATETIME_REGION_X, DATETIME_REGION_Y);
  sprite.deleteSprite();

  //logSpriteMemory("datetime", "after-delete");
}

void renderListRegion()
{
  //logSpriteMemory("list", "before-create");

  TFT_eSprite sprite = TFT_eSprite(&tft);
  if (!sprite.createSprite(tft.width(), LIST_REGION_H))
  {
    Serial.println("[MEM] list sprite create failed");
    return;
  }

  //logSpriteMemory("list", "after-create");

  sprite.fillSprite(TFT_BLACK);
  sprite.setTextDatum(TL_DATUM);
  sprite.setTextFont(2);
  sprite.setTextSize(2);
  sprite.setTextColor(scheduleTextColor, TFT_BLACK);
  sprite.drawString("No leftovers", 6, 6, 2);
  sprite.drawString("listed", 6, 42, 2);

  sprite.pushSprite(LIST_REGION_X, LIST_REGION_Y);
  sprite.deleteSprite();

  //logSpriteMemory("list", "after-delete");
}

int getIconWidth(const char *const rows[], int rowCount)
{
  if (rowCount <= 0 || rows[0] == nullptr)
  {
    return 0;
  }
  return strlen(rows[0]);
}

void drawThresholdIcon(TFT_eSprite &sprite,
                       int x,
                       int y,
                       const char *const rows[],
                       int rowCount,
                       int activeLevel,
                       uint16_t activeColor,
                       uint16_t inactiveColor,
                       uint16_t secondaryColor)
{
  for (int row = 0; row < rowCount; ++row)
  {
    const char *line = rows[row];
    if (line == nullptr)
    {
      continue;
    }

    for (int column = 0; line[column] != '\0'; ++column)
    {
      char pixel = line[column];
      if ((pixel == '.') || (pixel == ' '))
      {
        continue;
      }

      if ((pixel >= '0') && (pixel <= '9'))
      {
        int threshold = pixel - '0';
        sprite.drawPixel(x + column, y + row, threshold <= activeLevel ? activeColor : inactiveColor);
      }
      else if (pixel == '#')
      {
        sprite.drawPixel(x + column, y + row, activeColor);
      }
      else if (pixel == '+')
      {
        sprite.drawPixel(x + column, y + row, secondaryColor);
      }
    }
  }
}

bool isRecordBlinkOn()
{
  return ((millis() / 500UL) % 2UL) == 0;
}

bool shouldShowEditSessionActive()
{
  return isLeftoversSessionActive() && !isLeftoversQrActive();
}

int getWifiSignalLevel()
{
  if (!wifi_connected_at_boot || WiFi.status() != WL_CONNECTED)
  {
    return -1;
  }

  int rssi = WiFi.RSSI();
  int level = map(constrain(rssi, -90, -50), -90, -50, 0, 9);
  return constrain(level, 0, 9);
}

void renderStatusRegion()
{
  int statusY = tft.height() - STATUS_REGION_H;
  uint16_t goodColor = createColor(0, 220, 80);
  uint16_t warnColor = createColor(255, 190, 0);
  uint16_t badColor = createColor(255, 60, 60);
  uint16_t inactiveColor = createColor(55, 55, 55);
  uint16_t secondaryColor = createColor(120, 120, 120);
  uint16_t ntpColor = goodColor;
  bool editSessionActive = shouldShowEditSessionActive();
  bool recordBlinkOn = isRecordBlinkOn();
  bool cachedDataDirty = false;

  if (!ntp_ever_synced || consecutive_ntp_failures >= 3)
  {
    ntpColor = badColor;
  }
  else if (consecutive_ntp_failures > 0)
  {
    ntpColor = warnColor;
  }

  //logSpriteMemory("status", "before-create");

  TFT_eSprite sprite = TFT_eSprite(&tft);
  if (!sprite.createSprite(tft.width(), STATUS_REGION_H))
  {
    Serial.println("[MEM] status sprite create failed");
    return;
  }

  //logSpriteMemory("status", "after-create");

  sprite.fillSprite(TFT_BLACK);

  int x = 4;
  int wifiLevel = getWifiSignalLevel();
  drawThresholdIcon(sprite,
                    x,
                    STATUS_ICON_Y,
                    WIFI_ICON,
                    WIFI_ICON_ROWS,
                    wifi_connected_at_boot ? wifiLevel : 9,
                    wifi_connected_at_boot ? goodColor : badColor,
                    inactiveColor,
                    secondaryColor);

  x += getIconWidth(WIFI_ICON, WIFI_ICON_ROWS) + STATUS_ICON_GAP;
  drawThresholdIcon(sprite,
                    x,
                    STATUS_ICON_Y,
                    CLOCK_ICON,
                    CLOCK_ICON_ROWS,
                    9,
                    ntpColor,
                    inactiveColor,
                    secondaryColor);

  x += getIconWidth(CLOCK_ICON, CLOCK_ICON_ROWS) + STATUS_ICON_GAP;
  drawThresholdIcon(sprite,
                    x,
                    STATUS_ICON_Y,
                    RECORD_ICON,
                    RECORD_ICON_ROWS,
                    9,
                    (editSessionActive && recordBlinkOn) ? badColor : inactiveColor,
                    inactiveColor,
                    secondaryColor);

  x += getIconWidth(RECORD_ICON, RECORD_ICON_ROWS) + STATUS_ICON_GAP;
  drawThresholdIcon(sprite,
                    x,
                    STATUS_ICON_Y,
                    DIRTY_ICON,
                    DIRTY_ICON_ROWS,
                    9,
                    cachedDataDirty ? warnColor : inactiveColor,
                    inactiveColor,
                    secondaryColor);

  sprite.pushSprite(STATUS_REGION_X, statusY);
  sprite.deleteSprite();

  //logSpriteMemory("status", "after-delete");
}

bool getCurrentLocalTime(struct tm &localtime)
{
  return getLocalTime(&localtime, 250);
}

void rememberDrawnDateTime(const struct tm &localtime)
{
  last_drawn_year = localtime.tm_year;
  last_drawn_mon = localtime.tm_mon;
  last_drawn_mday = localtime.tm_mday;
  last_drawn_hour = localtime.tm_hour;
  last_drawn_min = localtime.tm_min;
}

bool dateTimeChanged(const struct tm &localtime)
{
  return (localtime.tm_year != last_drawn_year) ||
         (localtime.tm_mon != last_drawn_mon) ||
         (localtime.tm_mday != last_drawn_mday) ||
         (localtime.tm_hour != last_drawn_hour) ||
         (localtime.tm_min != last_drawn_min);
}

void initializeLeftoversDisplay()
{
  full_draw_required = true;
  last_wifi_available = wifi_connected_at_boot;
  last_ntp_available = ntp_synced_at_boot;
  last_ntp_failure_count = consecutive_ntp_failures;
  last_edit_session_active = shouldShowEditSessionActive();
  last_record_blink_on = isRecordBlinkOn();
}

void renderLeftoversDisplayFull()
{
  struct tm localtime;
  bool timeAvailable = getCurrentLocalTime(localtime);

  tft.fillScreen(TFT_BLACK);

  if (timeAvailable)
  {
    renderDateTimeRegion(&localtime);
    rememberDrawnDateTime(localtime);
  }
  else
  {
    renderDateTimeRegion(nullptr);
  }

  renderListRegion();
  renderStatusRegion();

  last_wifi_available = wifi_connected_at_boot;
  last_ntp_available = ntp_synced_at_boot;
  last_ntp_failure_count = consecutive_ntp_failures;
  last_edit_session_active = shouldShowEditSessionActive();
  last_record_blink_on = isRecordBlinkOn();
  full_draw_required = false;
}

void processLeftoversDisplay()
{
  struct tm localtime;
  bool timeAvailable;
  bool statusChanged;
  bool editSessionActive;
  bool recordBlinkOn;

  if (full_draw_required)
  {
    renderLeftoversDisplayFull();
    return;
  }

  timeAvailable = getCurrentLocalTime(localtime);
  if (timeAvailable && dateTimeChanged(localtime))
  {
    renderDateTimeRegion(&localtime);
    rememberDrawnDateTime(localtime);
  }

  editSessionActive = shouldShowEditSessionActive();
  recordBlinkOn = isRecordBlinkOn();
  statusChanged = (last_wifi_available != wifi_connected_at_boot) ||
                  (last_ntp_available != ntp_synced_at_boot) ||
                  (last_ntp_failure_count != consecutive_ntp_failures) ||
                  (last_edit_session_active != editSessionActive) ||
                  (editSessionActive && (last_record_blink_on != recordBlinkOn));
  if (statusChanged)
  {
    renderStatusRegion();
    last_wifi_available = wifi_connected_at_boot;
    last_ntp_available = ntp_synced_at_boot;
    last_ntp_failure_count = consecutive_ntp_failures;
    last_edit_session_active = editSessionActive;
    last_record_blink_on = recordBlinkOn;
  }
}
