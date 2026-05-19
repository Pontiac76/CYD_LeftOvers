#include "leftovers_data.h"

#include "storage_manager.h"

#include <SD.h>
#include <time.h>

LeftoverItem leftover_items[MAX_LEFTOVER_ITEMS];
int leftover_item_count = 0;
String known_foods[MAX_KNOWN_FOODS];
int known_food_count = 0;
bool leftovers_dirty = false;
String leftovers_storage_source;
String leftovers_last_message;

constexpr const char *LEFTOVERS_PATH = "/leftovers.psv";
constexpr const char *KNOWN_FOODS_PATH = "/known_foods.txt";

bool readActiveTextFile(const String &path, String &content)
{
  if (detect_sd_available_at_boot() && read_text_file_from_sd(path, content))
  {
    leftovers_storage_source = "SD";
    return true;
  }
  if (readLittleFsTextMounted(path.c_str(), content))
  {
    leftovers_storage_source = "LittleFS";
    return true;
  }
  leftovers_storage_source = detect_sd_available_at_boot() ? "SD" : "LittleFS";
  content = "";
  return false;
}

bool writeActiveTextFile(const String &path, const String &content)
{
  if (detect_sd_available_at_boot())
  {
    leftovers_storage_source = "SD";
    return write_text_file_to_sd(path, content);
  }
  leftovers_storage_source = "LittleFS";
  return writeLittleFsTextMounted(path.c_str(), content);
}

String canonicalizeFoodName(String value)
{
  String out;
  value.replace("|", "_");
  value.replace("\r", " ");
  value.replace("\n", " ");
  value.trim();

  bool lastSpace = false;
  for (int i = 0; i < value.length() && out.length() < 40; ++i)
  {
    char c = value.charAt(i);
    if (c < 32 || c > 126) continue;
    if (c == '\t') c = ' ';
    if (c == ' ')
    {
      if (lastSpace) continue;
      lastSpace = true;
    }
    else
    {
      lastSpace = false;
    }
    out += char(tolower(c));
  }
  out.trim();
  return out;
}

String displayFoodName(const String &canonical)
{
  String out = canonical;
  bool cap = true;
  for (int i = 0; i < out.length(); ++i)
  {
    char c = out.charAt(i);
    if (cap && isAlpha(c))
    {
      out.setCharAt(i, toupper(c));
      cap = false;
    }
    else if (c == ' ' || c == '-')
    {
      cap = true;
    }
  }
  return out;
}

String displayDateHeading(const String &date)
{
  int y = date.substring(0, 4).toInt();
  int m = date.substring(5, 7).toInt();
  int d = date.substring(8, 10).toInt();
  static const char *months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
  String heading = (m >= 1 && m <= 12) ? months[m - 1] : "???";
  heading += " ";
  heading += String(d);

  struct tm nowTm;
  if (getLocalTime(&nowTm, 20))
  {
    struct tm todayTm = {};
    todayTm.tm_year = nowTm.tm_year;
    todayTm.tm_mon = nowTm.tm_mon;
    todayTm.tm_mday = nowTm.tm_mday;
    todayTm.tm_isdst = -1;

    struct tm itemTm = {};
    itemTm.tm_year = y - 1900;
    itemTm.tm_mon = m - 1;
    itemTm.tm_mday = d;
    itemTm.tm_isdst = -1;

    time_t todayMidnight = mktime(&todayTm);
    time_t itemMidnight = mktime(&itemTm);
    long days = long(difftime(todayMidnight, itemMidnight) / 86400);

    heading += " - ";
    if (days <= 0)
    {
      heading += "Today";
    }
    else if (days == 1)
    {
      heading += "Yesterday";
    }
    else
    {
      heading += String(days);
      heading += " days ago";
    }
  }
  return heading;
}

void sortLeftovers()
{
  for (int i = 0; i < leftover_item_count - 1; ++i)
    for (int j = i + 1; j < leftover_item_count; ++j)
      if ((leftover_items[j].date < leftover_items[i].date) ||
          (leftover_items[j].date == leftover_items[i].date && leftover_items[j].name < leftover_items[i].name))
      {
        LeftoverItem t = leftover_items[i]; leftover_items[i] = leftover_items[j]; leftover_items[j] = t;
      }
}

void sortKnownFoods()
{
  for (int i = 0; i < known_food_count - 1; ++i)
    for (int j = i + 1; j < known_food_count; ++j)
      if (known_foods[j] < known_foods[i]) { String t = known_foods[i]; known_foods[i] = known_foods[j]; known_foods[j] = t; }
}

bool loadLeftoversFromDisk()
{
  String content;
  leftover_item_count = 0;
  readActiveTextFile(LEFTOVERS_PATH, content);
  int start = 0;
  while (start < content.length() && leftover_item_count < MAX_LEFTOVER_ITEMS)
  {
    int end = content.indexOf('\n', start);
    String line = end == -1 ? content.substring(start) : content.substring(start, end);
    start = end == -1 ? content.length() : end + 1;
    line.replace("\r", ""); line.trim();
    int sep = line.indexOf('|');
    if (sep != 10) continue;
    String date = line.substring(0, sep);
    String name = canonicalizeFoodName(line.substring(sep + 1));
    if (name == "") continue;
    leftover_items[leftover_item_count++] = {date, name};
  }
  sortLeftovers();
  leftovers_dirty = false;
  return true;
}

bool saveLeftoversToDisk()
{
  sortLeftovers();
  String content;
  for (int i = 0; i < leftover_item_count; ++i)
    content += leftover_items[i].date + "|" + leftover_items[i].name + "\n";
  bool ok = writeActiveTextFile(LEFTOVERS_PATH, content);
  if (ok) leftovers_dirty = false;
  return ok;
}

bool loadKnownFoodsFromDisk()
{
  String content;
  known_food_count = 0;
  readActiveTextFile(KNOWN_FOODS_PATH, content);
  int start = 0;
  while (start < content.length() && known_food_count < MAX_KNOWN_FOODS)
  {
    int end = content.indexOf('\n', start);
    String name = canonicalizeFoodName(end == -1 ? content.substring(start) : content.substring(start, end));
    start = end == -1 ? content.length() : end + 1;
    if (name == "") continue;
    bool exists = false;
    for (int i = 0; i < known_food_count; ++i) if (known_foods[i] == name) exists = true;
    if (!exists) known_foods[known_food_count++] = name;
  }
  sortKnownFoods();
  return true;
}

bool saveKnownFoodsToDisk()
{
  sortKnownFoods();
  String content;
  for (int i = 0; i < known_food_count; ++i) content += known_foods[i] + "\n";
  return writeActiveTextFile(KNOWN_FOODS_PATH, content);
}

bool addLeftoverItem(const String &date, const String &name, bool saveKnown, String &message)
{
  String canonical = canonicalizeFoodName(name);
  int alnum = 0; for (int i = 0; i < canonical.length(); ++i) if (isAlphaNumeric(canonical.charAt(i))) ++alnum;
  if (date.length() != 10 || canonical == "" || alnum < 3) { message = "Skipped invalid: " + name; return false; }
  for (int i = 0; i < leftover_item_count; ++i)
    if (leftover_items[i].date == date && leftover_items[i].name == canonical) { message = "Skipped duplicate: " + displayFoodName(canonical); return false; }
  if (leftover_item_count >= MAX_LEFTOVER_ITEMS) { message = "List full"; return false; }
  leftover_items[leftover_item_count++] = {date, canonical};
  leftovers_dirty = true;
  if (saveKnown)
  {
    bool exists = false; for (int i = 0; i < known_food_count; ++i) if (known_foods[i] == canonical) exists = true;
    if (!exists && known_food_count < MAX_KNOWN_FOODS) known_foods[known_food_count++] = canonical;
  }
  sortLeftovers(); sortKnownFoods();
  message = "Added: " + displayFoodName(canonical);
  return true;
}

int deleteLeftoverItem(const String &date, const String &name)
{
  String canonical = canonicalizeFoodName(name);
  int removed = 0;
  for (int i = 0; i < leftover_item_count;)
  {
    if (leftover_items[i].date == date && leftover_items[i].name == canonical)
    {
      for (int j = i; j < leftover_item_count - 1; ++j) leftover_items[j] = leftover_items[j + 1];
      --leftover_item_count; ++removed; leftovers_dirty = true;
    }
    else ++i;
  }
  return removed;
}

bool deleteKnownFood(const String &name)
{
  String canonical = canonicalizeFoodName(name);
  for (int i = 0; i < known_food_count; ++i)
  {
    if (known_foods[i] == canonical)
    {
      for (int j = i; j < known_food_count - 1; ++j) known_foods[j] = known_foods[j + 1];
      --known_food_count;
      leftovers_dirty = true;
      return true;
    }
  }
  return false;
}

int deleteLeftoverDay(const String &date)
{
  int removed = 0;
  for (int i = 0; i < leftover_item_count;)
  {
    if (leftover_items[i].date == date)
    {
      for (int j = i; j < leftover_item_count - 1; ++j) leftover_items[j] = leftover_items[j + 1];
      --leftover_item_count; ++removed; leftovers_dirty = true;
    }
    else ++i;
  }
  return removed;
}

String getLeftoversRawPreview() { String s; readActiveTextFile(LEFTOVERS_PATH, s); return s; }
String getKnownFoodsRawPreview() { String s; readActiveTextFile(KNOWN_FOODS_PATH, s); return s; }
