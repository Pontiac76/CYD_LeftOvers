#pragma once

#include "Arduino.h"

bool isLeftoversQrActive();
bool isLeftoversSessionActive();
const String &getLeftoversSessionToken();
void startLeftoversEditQr();
void cancelLeftoversSession(bool showTerminatedNotice = false);
bool validateLeftoversSessionToken(const String &token);
void markLeftoversSessionOpened();
void processLeftoversSession();
void handleLeftoversScreenTouch();
