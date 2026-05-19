#include "leftovers_session.h"

#include "app_state.h"
#include "display_manager.h"
#include "leftovers_data.h"
#include "leftovers_display.h"

#include <TFT_eSPI.h>
#include <WiFi.h>
#include <esp_system.h>

constexpr int TOKEN_HEX_BYTES = 32;

int edit_session_minutes = 5;
int admin_session_minutes = 5;
int qr_visible_seconds = 60;

String leftovers_session_token;
String leftovers_admin_token;
String leftovers_admin_requested_tab;
unsigned long leftovers_session_started_ms = 0;
unsigned long leftovers_qr_expires_ms = 0;
unsigned long leftovers_admin_qr_expires_ms = 0;
unsigned long leftovers_admin_started_ms = 0;
bool leftovers_qr_active = false;
bool leftovers_admin_qr_active = false;
bool leftovers_session_opened = false;
bool leftovers_admin_mode = false;

String generateSessionToken()
{
  static const char *HEX_CHARS = "0123456789abcdef";
  String token;
  token.reserve(TOKEN_HEX_BYTES * 2);

  for (int index = 0; index < TOKEN_HEX_BYTES; ++index)
  {
    uint8_t value = static_cast<uint8_t>(esp_random() & 0xFF);
    token += HEX_CHARS[(value >> 4) & 0x0F];
    token += HEX_CHARS[value & 0x0F];
  }

  return token;
}

String buildEditUrl()
{
  String url = "http://";
  url += WiFi.localIP().toString();
  url += "/edit?t=";
  url += leftovers_session_token;
  return url;
}

String buildAdminUrl()
{
  String url = "http://";
  url += WiFi.localIP().toString();
  url += "/admin?a=";
  url += leftovers_admin_token;
  url += "&tab=";
  url += leftovers_admin_requested_tab;
  return url;
}

bool isLeftoversQrActive()
{
  return leftovers_qr_active;
}

bool isLeftoversAdminQrActive()
{
  return leftovers_admin_qr_active;
}

bool isLeftoversAnyQrActive()
{
  return leftovers_qr_active || leftovers_admin_qr_active;
}

bool isLeftoversSessionActive()
{
  return leftovers_session_token != "";
}

bool isLeftoversAdminModeActive()
{
  if (!leftovers_admin_mode)
  {
    return false;
  }

  if (long(millis() - (leftovers_admin_started_ms + (unsigned long)admin_session_minutes * 60UL * 1000UL)) >= 0)
  {
    leftovers_admin_mode = false;
    leftovers_admin_token = "";
    leftovers_admin_started_ms = 0;
    Serial.println("Leftovers admin mode expired");
    return false;
  }

  return true;
}

const String &getLeftoversSessionToken()
{
  return leftovers_session_token;
}

void startLeftoversEditQr()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    renderLeftoversDisplayFull();
    Serial.println("Edit QR unavailable: WiFi not connected");
    return;
  }

  leftovers_session_token = generateSessionToken();
  leftovers_session_started_ms = millis();
  leftovers_qr_expires_ms = leftovers_session_started_ms + (unsigned long)qr_visible_seconds * 1000UL;
  leftovers_qr_active = true;
  leftovers_session_opened = false;

  String url = buildEditUrl();
  Serial.print("Edit URL: ");
  Serial.println(url);
  drawQrCode(url.c_str(), "Scan to edit leftovers");
}

String startLeftoversAdminQr(const String &requestedTab)
{
  if (!isLeftoversSessionActive() || WiFi.status() != WL_CONNECTED)
  {
    Serial.println("Admin QR unavailable: no active session or WiFi not connected");
    return "";
  }

  leftovers_admin_token = generateSessionToken();
  leftovers_admin_requested_tab = requestedTab;
  leftovers_admin_qr_expires_ms = millis() + (unsigned long)qr_visible_seconds * 1000UL;
  leftovers_admin_qr_active = true;

  String url = buildAdminUrl();
  Serial.print("Admin URL: ");
  Serial.println(url);
  drawQrCode(url.c_str(), "Scan for admin mode");
  return url;
}

void showSessionTerminatedNotice()
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);
  tft.setTextSize(2);
  tft.setTextColor(errorTextColor, TFT_BLACK);
  tft.drawString("Session", tft.width() / 2, (tft.height() / 2) - 18, 2);
  tft.drawString("terminated", tft.width() / 2, (tft.height() / 2) + 18, 2);
  tft.setTextDatum(TL_DATUM);
  delay(2000);
}

void cancelLeftoversSession(bool showTerminatedNotice)
{
  bool hadOpenedSession = leftovers_session_opened;

  if (leftovers_session_token != "")
  {
    Serial.println("Leftovers edit session cancelled");
  }

  leftovers_session_token = "";
  leftovers_admin_token = "";
  leftovers_admin_requested_tab = "";
  leftovers_session_started_ms = 0;
  leftovers_qr_expires_ms = 0;
  leftovers_admin_qr_expires_ms = 0;
  leftovers_admin_started_ms = 0;
  leftovers_qr_active = false;
  leftovers_admin_qr_active = false;
  leftovers_session_opened = false;
  leftovers_admin_mode = false;

  if (showTerminatedNotice && hadOpenedSession)
  {
    showSessionTerminatedNotice();
  }

  initializeLeftoversDisplay();
  renderLeftoversDisplayFull();
}

void cancelLeftoversAdminQr()
{
  if (!leftovers_admin_qr_active)
  {
    return;
  }

  Serial.println("Admin QR cancelled");
  leftovers_admin_token = "";
  leftovers_admin_requested_tab = "";
  leftovers_admin_qr_expires_ms = 0;
  leftovers_admin_qr_active = false;
  initializeLeftoversDisplay();
  renderLeftoversDisplayFull();
}

void exitLeftoversAdminMode()
{
  if (leftovers_admin_qr_active)
  {
    cancelLeftoversAdminQr();
  }

  if (leftovers_admin_mode)
  {
    Serial.println("Leftovers admin mode exited");
  }

  leftovers_admin_token = "";
  leftovers_admin_requested_tab = "";
  leftovers_admin_mode = false;
  leftovers_admin_started_ms = 0;
}

bool validateLeftoversSessionToken(const String &token)
{
  if (leftovers_session_token == "" || token == "")
  {
    return false;
  }

  if (token != leftovers_session_token)
  {
    return false;
  }

  if (long(millis() - (leftovers_session_started_ms + (unsigned long)edit_session_minutes * 60UL * 1000UL)) >= 0)
  {
    cancelLeftoversSession();
    return false;
  }

  return true;
}

bool validateLeftoversAdminToken(const String &token)
{
  if (leftovers_admin_token == "" || token == "")
  {
    return false;
  }

  if (token != leftovers_admin_token)
  {
    return false;
  }

  if (long(millis() - leftovers_admin_qr_expires_ms) >= 0)
  {
    leftovers_admin_token = "";
    leftovers_admin_qr_active = false;
    return false;
  }

  return true;
}

void markLeftoversSessionOpened()
{
  if (!leftovers_qr_active)
  {
    return;
  }

  leftovers_qr_active = false;
  leftovers_session_opened = true;
  initializeLeftoversDisplay();
  renderLeftoversDisplayFull();
}

void markLeftoversAdminOpened()
{
  leftovers_admin_qr_active = false;
  leftovers_admin_token = "";
  leftovers_admin_mode = true;
  leftovers_admin_started_ms = millis();
  initializeLeftoversDisplay();
  renderLeftoversDisplayFull();
  Serial.println("Leftovers admin mode unlocked");
}

void processLeftoversSession()
{
  if (leftovers_session_token == "")
  {
    return;
  }

  unsigned long nowMs = millis();

  if (leftovers_admin_qr_active && long(nowMs - leftovers_admin_qr_expires_ms) >= 0)
  {
    Serial.println("Admin QR timed out");
    cancelLeftoversAdminQr();
    return;
  }

  if (leftovers_qr_active && long(nowMs - leftovers_qr_expires_ms) >= 0)
  {
    Serial.println("Edit QR timed out");
    cancelLeftoversSession();
    return;
  }

  if (long(nowMs - (leftovers_session_started_ms + (unsigned long)edit_session_minutes * 60UL * 1000UL)) >= 0)
  {
    Serial.println("Leftovers edit session expired");
    if (leftovers_dirty)
    {
      Serial.println("Leftovers dirty on timeout; saving RAM to disk");
      bool leftoversSaved = saveLeftoversToDisk();
      bool knownSaved = saveKnownFoodsToDisk();
      Serial.print("Leftovers timeout save result leftovers=");
      Serial.print(leftoversSaved ? "ok" : "fail");
      Serial.print(" known=");
      Serial.println(knownSaved ? "ok" : "fail");
    }
    cancelLeftoversSession();
  }
}

void handleLeftoversScreenTouch()
{
  if (isLeftoversAdminQrActive())
  {
    Serial.println("Admin QR cancelled by touch; edit session remains active");
    cancelLeftoversAdminQr();
    return;
  }

  if (isLeftoversSessionActive())
  {
    cancelLeftoversSession(true);
    return;
  }

  startLeftoversEditQr();
}
