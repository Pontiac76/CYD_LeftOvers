#pragma once

#include "Arduino.h"

constexpr int MAX_LEFTOVER_ITEMS = 80;
constexpr int MAX_KNOWN_FOODS = 80;

struct LeftoverItem
{
  String date;
  String name;
};

extern LeftoverItem leftover_items[MAX_LEFTOVER_ITEMS];
extern int leftover_item_count;
extern String known_foods[MAX_KNOWN_FOODS];
extern int known_food_count;
extern bool leftovers_dirty;
extern String leftovers_storage_source;
extern String leftovers_last_message;

String canonicalizeFoodName(String value);
String displayFoodName(const String &canonical);
String displayDateHeading(const String &date);
bool loadLeftoversFromDisk();
bool saveLeftoversToDisk();
bool loadKnownFoodsFromDisk();
bool saveKnownFoodsToDisk();
void sortLeftovers();
void sortKnownFoods();
bool addLeftoverItem(const String &date, const String &name, bool saveKnown, String &message);
int deleteLeftoverItem(const String &date, const String &name);
int deleteLeftoverDay(const String &date);
bool deleteKnownFood(const String &name);
String getLeftoversRawPreview();
String getKnownFoodsRawPreview();
