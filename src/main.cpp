#include "Arduino.h"
#include <TFT_eSPI.h>
#include <time.h>

#include "app_state.h"
#include "brightness_manager.h"
#include "config_manager.h"
#include "display_manager.h"
#include "leftovers_data.h"
#include "leftovers_display.h"
#include "leftovers_session.h"
#include "leftovers_web.h"
#include "network_manager.h"
#include "setup_portal.h"
#include "storage_manager.h"
#include "touch_manager.h"

#include <esp_system.h>

constexpr uint8_t LEFTOVERS_DISPLAY_ROTATION = 2;

bool wifi_connected_at_boot = false;
bool ntp_synced_at_boot = false;

void setup()
{
  Serial.begin(115200);
  randomSeed((uint32_t)esp_random());

  pinMode(XPT2046_CS, OUTPUT);
  digitalWrite(XPT2046_CS, HIGH);

  tft.init();
  tft.setRotation(LEFTOVERS_DISPLAY_ROTATION);
  tft.fillScreen(TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextColor(bootTextColor, TFT_BLACK);
  tft.setCursor(0, 0);
  tft.println("POST");
  build_version_code = getBuildVersionCode();

  ram_only_mode = !detect_sd_available_at_boot();
  Serial.println(ram_only_mode ? "Boot mode: RAM_ONLY" : "Boot mode: SD");
  tft.println(ram_only_mode ? "Boot mode: RAM_ONLY" : "Boot mode: SD");

  list_sd_files_to_serial();
  list_littlefs_files_to_serial();

  read_sd();

  pinMode(backlightPin, OUTPUT);
  analogWrite(backlightPin, brightness);
  pinMode(photoResistorPin, INPUT);
  analogSetPinAttenuation(photoResistorPin, ADC_11db);

  Serial.print("Photoresistor configured on GPIO");
  Serial.println(photoResistorPin);

  if (ssid == "")
  {
    loadFirstWifiProfileFromLittleFs();
  }

  bool setupPortalRequired = (ssid == "");

  if (!setupPortalRequired)
  {
    wifi_connected_at_boot = wifi_start_STA();
    if (wifi_connected_at_boot)
    {
      Serial.println("Time Sync ...");
      tft.println("Time Sync ...");
      ntp_synced_at_boot = timesync();
      recordNtpSyncResult(ntp_synced_at_boot);
      scheduleNextNtpSync(ntp_synced_at_boot);
    }
    else
    {
      Serial.println("WiFi unavailable; continuing local display mode");
      tft.println("WiFi unavailable");
      delay(2000);
    }
  }

#if ENABLE_TOUCH
  initialize_touch();
#endif

  if (setupPortalRequired)
  {
    startSetupPortal();
    refreshSetupPortalDisplay();
    return;
  }

  loadKnownFoodsFromDisk();
  loadLeftoversFromDisk();

  startLeftoversWebServer();
  initializeLeftoversDisplay();
  renderLeftoversDisplayFull();
}

void loop()
{
  if (isSetupPortalRunning())
  {
    processSetupPortal();
    return;
  }

  processPhotoBrightness();
  processLeftoversWebServer();
  processLeftoversSession();

  if (wifi_connected_at_boot)
  {
    processScheduledNtpSync();
  }

  if (!isLeftoversAnyQrActive())
  {
    processLeftoversDisplay();
  }
  processTouchInput();
}
