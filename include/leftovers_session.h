#pragma once

#include "Arduino.h"

extern int edit_session_minutes;
extern int admin_session_minutes;
extern int qr_visible_seconds;

bool isLeftoversQrActive();
bool isLeftoversAdminQrActive();
bool isLeftoversAnyQrActive();
bool isLeftoversSessionActive();
bool isLeftoversAdminModeActive();
const String &getLeftoversSessionToken();
void startLeftoversEditQr();
String startLeftoversAdminQr(const String &requestedTab);
void cancelLeftoversSession(bool showTerminatedNotice = false);
void cancelLeftoversAdminQr();
void exitLeftoversAdminMode();
bool validateLeftoversSessionToken(const String &token);
bool validateLeftoversAdminToken(const String &token);
void markLeftoversSessionOpened();
void markLeftoversAdminOpened();
void processLeftoversSession();
void handleLeftoversScreenTouch();
