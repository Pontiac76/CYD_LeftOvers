#include "leftovers_session.h"

#include "app_state.h"
#include "display_manager.h"
#include "leftovers_display.h"

#include <TFT_eSPI.h>
#include <WiFi.h>
#include <esp_system.h>

constexpr unsigned long EDIT_QR_VISIBLE_MS = 60UL * 1000UL;
constexpr unsigned long EDIT_SESSION_VALID_MS = 60UL * 60UL * 1000UL;
constexpr int TOKEN_HEX_BYTES = 32;

String leftovers_session_token;
unsigned long leftovers_session_started_ms = 0;
unsigned long leftovers_qr_expires_ms = 0;
bool leftovers_qr_active = false;
bool leftovers_session_opened = false;

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

bool isLeftoversQrActive()
{
  return leftovers_qr_active;
}

bool isLeftoversSessionActive()
{
  return leftovers_session_token != "";
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
  leftovers_qr_expires_ms = leftovers_session_started_ms + EDIT_QR_VISIBLE_MS;
  leftovers_qr_active = true;
  leftovers_session_opened = false;

  String url = buildEditUrl();
  Serial.print("Edit URL: ");
  Serial.println(url);
  drawQrCode(url.c_str(), "Scan to edit leftovers");
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
  leftovers_session_started_ms = 0;
  leftovers_qr_expires_ms = 0;
  leftovers_qr_active = false;
  leftovers_session_opened = false;

  if (showTerminatedNotice && hadOpenedSession)
  {
    showSessionTerminatedNotice();
  }

  initializeLeftoversDisplay();
  renderLeftoversDisplayFull();
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

  if (long(millis() - (leftovers_session_started_ms + EDIT_SESSION_VALID_MS)) >= 0)
  {
    cancelLeftoversSession();
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

void processLeftoversSession()
{
  if (leftovers_session_token == "")
  {
    return;
  }

  unsigned long nowMs = millis();

  if (leftovers_qr_active && long(nowMs - leftovers_qr_expires_ms) >= 0)
  {
    Serial.println("Edit QR timed out");
    cancelLeftoversSession();
    return;
  }

  if (long(nowMs - (leftovers_session_started_ms + EDIT_SESSION_VALID_MS)) >= 0)
  {
    cancelLeftoversSession();
  }
}

void handleLeftoversScreenTouch()
{
  if (isLeftoversSessionActive())
  {
    cancelLeftoversSession(true);
    return;
  }

  startLeftoversEditQr();
}
