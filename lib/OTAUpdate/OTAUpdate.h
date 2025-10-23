#ifndef OTAUPDATE_H
#define OTAUPDATE_H

#include "Connectivity.h"
#include <HTTPClient.h>
#include <Update.h>
#include <LiquidCrystal.h>
#include "CACerts.h"

extern const int OTA_CHECK_INTERVAL;
extern bool isUpdating;
extern const char* currentVersion;

bool isNewerVersionAvailable(LiquidCrystal& lcd);
int parseVersion(const char* ver);
bool performSecureOTA(LiquidCrystal& lcd);

#endif