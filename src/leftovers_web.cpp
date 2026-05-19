#include "leftovers_web.h"

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

#include "app_state.h"
#include "config_manager.h"
#include "display_manager.h"
#include "leftovers_data.h"
#include "leftovers_display.h"
#include "leftovers_session.h"
#include "network_manager.h"
#include "storage_manager.h"

#include <WiFiClient.h>
#include <esp_system.h>
#include <time.h>

WebServer leftoversWebServer(80);
bool leftovers_web_server_running = false;

struct PoiCheck
{
  const char *label;
  const char *host;
  uint16_t port;
};

struct TimezoneOption
{
  const char *label;
  const char *posix;
};

constexpr TimezoneOption TIMEZONE_OPTIONS[] = {
  {"UTC", "UTC0"},
  {"Newfoundland - St. John's", "NST3:30NDT,M3.2.0/02:00:00,M11.1.0/02:00:00"},
  {"Atlantic - Halifax", "AST4ADT,M3.2.0/02:00:00,M11.1.0/02:00:00"},
  {"Eastern - Toronto/New York", "EST5EDT,M3.2.0/02:00:00,M11.1.0/02:00:00"},
  {"Central - Winnipeg/Chicago", "CST6CDT,M3.2.0/02:00:00,M11.1.0/02:00:00"},
  {"Mountain - Edmonton/Denver", "MST7MDT,M3.2.0/02:00:00,M11.1.0/02:00:00"},
  {"Arizona/Yukon - Mountain no DST", "MST7"},
  {"Saskatchewan - Central no DST", "CST6"},
  {"Pacific - Vancouver/Los Angeles", "PST8PDT,M3.2.0/02:00:00,M11.1.0/02:00:00"},
  {"Alaska - Anchorage", "AKST9AKDT,M3.2.0/02:00:00,M11.1.0/02:00:00"},
  {"Hawaii - Honolulu no DST", "HST10"},
  {"Mexico Central - Mexico City", "CST6"},
  {"Colombia/Panama/Ecuador/Peru - no DST", "EST5"},
  {"Venezuela/Bolivia - no DST", "VET4"},
  {"Atlantic Caribbean - no DST", "AST4"},
  {"Argentina/Uruguay/Brazil East - no DST", "ART3"},
  {"Fernando de Noronha/South Georgia - no DST", "FNT2"},
};
constexpr int TIMEZONE_OPTION_COUNT = sizeof(TIMEZONE_OPTIONS) / sizeof(TIMEZONE_OPTIONS[0]);

constexpr PoiCheck DEFAULT_POIS[] = {
  {"Cloudflare DNS", "1.1.1.1", 53},
  {"Google DNS", "8.8.8.8", 53},
  {"example.com HTTP", "example.com", 80},
};
constexpr int DEFAULT_POI_COUNT = sizeof(DEFAULT_POIS) / sizeof(DEFAULT_POIS[0]);

String last_network_check_html;
String last_network_save_message;
String last_color_save_message;

String escapeLeftoversHtmlText(String value)
{
  value.replace("&", "&amp;");
  value.replace("<", "&lt;");
  value.replace(">", "&gt;");
  value.replace("\"", "&quot;");
  return value;
}

String checkedAttr(bool checked)
{
  return checked ? " checked" : "";
}

String currentSessionTokenOrArg()
{
  String token = leftoversWebServer.arg("t");
  if (token == "")
  {
    token = getLeftoversSessionToken();
  }
  return token;
}

bool requireValidSession(String &token)
{
  token = currentSessionTokenOrArg();
  if (!validateLeftoversSessionToken(token))
  {
    return false;
  }

  markLeftoversSessionOpened();
  return true;
}

String tokenUrl(const String &path, const String &token, const String &extra = "")
{
  String url = path;
  url += "?t=";
  url += token;
  if (extra != "")
  {
    url += "&";
    url += extra;
  }
  return url;
}

String formatDuration(unsigned long ageMs)
{
  unsigned long seconds = ageMs / 1000UL;
  unsigned long minutes = seconds / 60UL;
  unsigned long hours = minutes / 60UL;
  seconds %= 60UL;
  minutes %= 60UL;

  String text;
  if (hours > 0)
  {
    text += String(hours);
    text += "h ";
  }
  if (hours > 0 || minutes > 0)
  {
    text += String(minutes);
    text += "m ";
  }
  text += String(seconds);
  text += "s";
  return text;
}

String escapeConfigValue(String value)
{
  value.replace("\r", "");
  value.replace("\n", "");
  value.trim();
  return value;
}

int boundedIntArg(const String &name, int fallback, int minValue, int maxValue)
{
  if (!leftoversWebServer.hasArg(name))
  {
    return fallback;
  }

  long value = leftoversWebServer.arg(name).toInt();
  if (value < minValue) value = minValue;
  if (value > maxValue) value = maxValue;
  return int(value);
}

String upsertConfigLine(String content, const String &key, const String &value)
{
  String output;
  bool replaced = false;
  int start = 0;

  while (start < content.length())
  {
    int end = content.indexOf('\n', start);
    String line;
    if (end == -1)
    {
      line = content.substring(start);
      start = content.length();
    }
    else
    {
      line = content.substring(start, end);
      start = end + 1;
    }

    String trimmed = line;
    trimmed.replace("\r", "");
    trimmed.trim();
    int separatorIndex = trimmed.indexOf('=');
    bool keyMatches = false;
    if (separatorIndex > 0 && !trimmed.startsWith("#"))
    {
      String existingKey = sanitizeConfigKey(trimmed.substring(0, separatorIndex));
      keyMatches = configKeyEquals(existingKey, key.c_str());
    }

    if (keyMatches)
    {
      if (!replaced)
      {
        output += key;
        output += "=";
        output += value;
        output += "\n";
        replaced = true;
      }
    }
    else if (line != "")
    {
      output += line;
      output += "\n";
    }
  }

  if (!replaced)
  {
    output += key;
    output += "=";
    output += value;
    output += "\n";
  }

  return output;
}

bool writeConfigToActiveStorage(const String &content, String &target, String &error)
{
  String verifyContent;
  bool success = false;

  if (active_config_source.startsWith("SD") || (active_config_source == "defaults" && !ram_only_mode))
  {
    target = "SD /config.txt";
    success = write_config_to_sd(content) && read_config_text_from_sd(verifyContent);
  }
  else
  {
    target = "LittleFS /config.txt";
    success = writeLittleFsTextMounted("/config.txt", content) && read_config_text_from_littlefs(verifyContent);
  }

  if (!success)
  {
    error = "Write or verify read failed for " + target;
    return false;
  }

  if (verifyContent != content)
  {
    error = "Verify mismatch after writing " + target;
    return false;
  }

  return true;
}

String buildNetworkingConfigFromRequest()
{
  String newConfig = current_config_text;
  String requestedNtpServer = escapeConfigValue(leftoversWebServer.arg("ntpserver"));
  String requestedTzInfo = escapeConfigValue(leftoversWebServer.arg("tzinfo"));
  int requestedSyncMinutes = boundedIntArg("ntpsyncminutes", ntp_sync_frequency_minutes, 1, 1440);
  int requestedSyncRandomSeconds = boundedIntArg("ntpsyncrandomseconds", ntp_sync_random_delay_seconds, 0, MAX_NTP_RANDOM_DELAY_SECONDS);
  int requestedRetryMinutes = boundedIntArg("ntpretryminutes", ntp_retry_frequency_minutes, 1, 1440);
  int requestedRetryRandomSeconds = boundedIntArg("ntpretryrandomseconds", ntp_retry_random_delay_seconds, 0, MAX_NTP_RANDOM_DELAY_SECONDS);

  if (requestedNtpServer == "") requestedNtpServer = ntpserver;
  if (requestedTzInfo == "") requestedTzInfo = tzinfo;

  newConfig = upsertConfigLine(newConfig, "ntpserver", requestedNtpServer);
  newConfig = upsertConfigLine(newConfig, "tzinfo", requestedTzInfo);
  newConfig = upsertConfigLine(newConfig, "ntpsyncminutes", String(requestedSyncMinutes));
  newConfig = upsertConfigLine(newConfig, "ntpsyncrandomseconds", String(requestedSyncRandomSeconds));
  newConfig = upsertConfigLine(newConfig, "ntpretryminutes", String(requestedRetryMinutes));
  newConfig = upsertConfigLine(newConfig, "ntpretryrandomseconds", String(requestedRetryRandomSeconds));
  return newConfig;
}

void showWebRebootNotice(const String &reason)
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);
  tft.setTextSize(2);
  tft.setTextColor(statusTextColor, TFT_BLACK);
  tft.drawString("Rebooting", tft.width() / 2, (tft.height() / 2) - 28, 2);
  tft.drawString("to apply", tft.width() / 2, tft.height() / 2, 2);
  tft.drawString(reason, tft.width() / 2, (tft.height() / 2) + 28, 2);
  tft.setTextDatum(TL_DATUM);
}

bool parseWebHexColor(const String &value, uint16_t &color)
{
  String clean = value;
  clean.trim();
  if (clean.startsWith("#")) clean = clean.substring(1);
  if (clean.length() != 6) return false;

  unsigned long raw = strtoul(clean.c_str(), nullptr, 16);
  uint8_t red = (raw >> 16) & 0xFF;
  uint8_t green = (raw >> 8) & 0xFF;
  uint8_t blue = raw & 0xFF;
  color = createColor(red, green, blue);
  return true;
}

void applyColorArgsToRuntime()
{
  uint16_t parsed;
  if (parseWebHexColor(leftoversWebServer.arg("datecolor"), parsed)) dateTextColor = parsed;
  if (parseWebHexColor(leftoversWebServer.arg("schedulecolor"), parsed)) scheduleTextColor = parsed;
  if (parseWebHexColor(leftoversWebServer.arg("bootcolor"), parsed)) bootTextColor = parsed;
  if (parseWebHexColor(leftoversWebServer.arg("errorcolor"), parsed)) errorTextColor = parsed;

  event_tm_hour = -1;
  event_tm_min = -1;
  event_tm_sec = -1;
  initializeLeftoversDisplay();
  renderLeftoversDisplayFull();
}

String rgb565ToHex(uint16_t color)
{
  uint8_t red = ((color >> 11) & 0x1F) * 255 / 31;
  uint8_t green = ((color >> 5) & 0x3F) * 255 / 63;
  uint8_t blue = (color & 0x1F) * 255 / 31;
  char buffer[8];
  snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", red, green, blue);
  return String(buffer);
}

String colorInput(const String &name, const String &label, uint16_t currentColor, const String &sampleText)
{
  String value = rgb565ToHex(currentColor);
  String html;
  html += "<label>" + label + "</label>";
  html += "<input type='color' name='" + name + "' value='" + value + "' oninput='handleColorInput(this)' onchange='handleColorInput(this)'>";
  html += "<p class='hint'><span class='swatch' data-preview='" + name + "' style='display:inline-block;width:1.25rem;height:1.25rem;border:1px solid var(--border);vertical-align:middle;background:" + value + "'></span> ";
  html += "<span data-preview='" + name + "' style='color:" + value + "'>" + escapeLeftoversHtmlText(sampleText) + "</span> <code>" + value + "</code></p>";
  return html;
}

String buildColorConfigFromRequest()
{
  String newConfig = current_config_text;
  newConfig = upsertConfigLine(newConfig, "datecolor", escapeConfigValue(leftoversWebServer.arg("datecolor")));
  newConfig = upsertConfigLine(newConfig, "schedulecolor", escapeConfigValue(leftoversWebServer.arg("schedulecolor")));
  newConfig = upsertConfigLine(newConfig, "bootcolor", escapeConfigValue(leftoversWebServer.arg("bootcolor")));
  newConfig = upsertConfigLine(newConfig, "errorcolor", escapeConfigValue(leftoversWebServer.arg("errorcolor")));
  return newConfig;
}

String formatLocalTimeForWeb()
{
  struct tm local;
  if (!getLocalTime(&local, 50) || !isLocalTimePlausible(local))
  {
    return "Unavailable";
  }

  char buffer[32];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local);
  return String(buffer);
}

bool tcpPoiCheck(const char *host, uint16_t port, unsigned long timeoutMs, unsigned long &elapsedMs)
{
  WiFiClient client;
  client.setTimeout(timeoutMs / 1000UL);

  unsigned long startMs = millis();
  bool ok = client.connect(host, port, timeoutMs);
  elapsedMs = millis() - startMs;
  client.stop();
  return ok;
}

void runNetworkPoiChecks()
{
  String html = "<div class='notice'><strong>Network check results</strong><ul>";

  if (WiFi.status() != WL_CONNECTED)
  {
    html += "<li><span class='bad'>WiFi is not connected.</span></li>";
  }
  else
  {
    IPAddress gateway = WiFi.gatewayIP();
    if (gateway != IPAddress(0, 0, 0, 0))
    {
      unsigned long elapsedMs = 0;
      bool ok = tcpPoiCheck(gateway.toString().c_str(), 53, 1200, elapsedMs);
      html += "<li>Gateway DNS/TCP ";
      html += escapeLeftoversHtmlText(gateway.toString());
      html += ": <span class='";
      html += ok ? "good'>OK" : "bad'>FAIL";
      html += "</span> ";
      html += String(elapsedMs);
      html += "ms</li>";
    }

    if (ntpserver != "")
    {
      struct tm local;
      unsigned long startMs = millis();
      bool ok = queryNtpServerAndSetClock(local, 1500);
      unsigned long elapsedMs = millis() - startMs;
      html += "<li>NTP UDP/123 ";
      html += escapeLeftoversHtmlText(getPrimaryNtpServer());
      html += ": <span class='";
      html += ok ? "good'>OK" : "bad'>FAIL";
      html += "</span> ";
      html += String(elapsedMs);
      html += "ms</li>";
      if (ok)
      {
        recordNtpSyncResult(true);
        scheduleNextNtpSync(true);
      }
    }

    for (int index = 0; index < DEFAULT_POI_COUNT; ++index)
    {
      unsigned long elapsedMs = 0;
      bool ok = tcpPoiCheck(DEFAULT_POIS[index].host, DEFAULT_POIS[index].port, 1200, elapsedMs);
      html += "<li>";
      html += escapeLeftoversHtmlText(DEFAULT_POIS[index].label);
      html += " (";
      html += escapeLeftoversHtmlText(DEFAULT_POIS[index].host);
      html += ":";
      html += String(DEFAULT_POIS[index].port);
      html += "): <span class='";
      html += ok ? "good'>OK" : "bad'>FAIL";
      html += "</span> ";
      html += String(elapsedMs);
      html += "ms</li>";
    }
  }

  html += "</ul><p class='hint'>These are TCP/UDP reachability checks, not ICMP ping. ESP32 firmware does not have ping built in here, because apparently that would be too civilized.</p></div>";
  last_network_check_html = html;
}

String renderCssAndScript()
{
  return R"HTML(
<style>
:root{--bg:#f4f4f5;--fg:#171717;--card:#fff;--muted:#666;--border:#d4d4d8;--accent:#2563eb;--good:#15803d;--warn:#b45309;--bad:#dc2626;--input:#fff}
body.dark{--bg:#111827;--fg:#e5e7eb;--card:#1f2937;--muted:#9ca3af;--border:#374151;--accent:#60a5fa;--good:#4ade80;--warn:#fbbf24;--bad:#f87171;--input:#111827}
*{box-sizing:border-box}body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;margin:0;background:var(--bg);color:var(--fg);line-height:1.4}a{color:var(--accent)}
header{position:sticky;top:0;background:var(--card);border-bottom:1px solid var(--border);padding:1rem;z-index:2}.top{display:flex;gap:.75rem;align-items:center;justify-content:space-between;max-width:1100px;margin:0 auto}.title{font-size:1.25rem;font-weight:700}.actions{display:flex;gap:.5rem;flex-wrap:wrap}
main{max-width:1100px;margin:0 auto;padding:1rem}.tabs{display:flex;gap:.5rem;flex-wrap:wrap;margin-bottom:1rem}.tab{display:inline-block;padding:.65rem .9rem;border:1px solid var(--border);border-radius:.75rem;background:var(--card);text-decoration:none;color:var(--fg)}.tab.active{border-color:var(--accent);box-shadow:0 0 0 2px color-mix(in srgb,var(--accent) 25%,transparent)}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:1rem}.card,.notice{background:var(--card);border:1px solid var(--border);border-radius:1rem;padding:1rem;margin-bottom:1rem}h1,h2,h3{margin-top:0}.muted,.hint{color:var(--muted);font-size:.92rem}.good{color:var(--good);font-weight:700}.warn{color:var(--warn);font-weight:700}.bad{color:var(--bad);font-weight:700}code{word-break:break-all}
button,.button,input,textarea,select{font:inherit}button,.button{border:1px solid var(--border);border-radius:.65rem;background:var(--card);color:var(--fg);padding:.55rem .8rem;text-decoration:none;cursor:pointer}button.primary,.button.primary{background:var(--accent);border-color:var(--accent);color:white}button.danger{background:var(--bad);border-color:var(--bad);color:white}label{display:block;margin:.75rem 0 .25rem}input,textarea,select{width:100%;border:1px solid var(--border);border-radius:.5rem;background:var(--input);color:var(--fg);padding:.55rem}input[type=checkbox]{width:auto;padding:0}.checklabel{display:flex;align-items:center;gap:.5rem;margin:.5rem 0}.checklabel input{flex:0 0 auto}.daygroup{border:1px solid var(--border);border-radius:.85rem;padding:.75rem;margin:1rem 0;background:color-mix(in srgb,var(--card) 90%,var(--bg))}.daygroup h3{margin-bottom:.4rem}.dayitems{display:flex;flex-wrap:wrap;gap:.4rem .75rem;margin-left:1.5rem}.itempill{display:inline-flex;align-items:center;gap:.35rem;border:1px solid var(--border);border-radius:999px;padding:.25rem .55rem;background:var(--bg);margin:0}.daydelete{font-weight:600}ul{padding-left:1.2rem}.kv{display:grid;grid-template-columns:minmax(130px,auto) 1fr;gap:.35rem .75rem}.kv div:nth-child(odd){color:var(--muted)}
@media(max-width:650px){.top{align-items:flex-start;flex-direction:column}.actions{width:100%}.actions button,.actions .button{flex:1}.kv{grid-template-columns:1fr}.kv div:nth-child(odd){font-weight:700}}
</style>
<script>
function getCookie(name){return document.cookie.split('; ').find(r=>r.startsWith(name+'='))?.split('=')[1]||''}
function setTheme(mode){document.body.classList.toggle('dark',mode==='dark');document.cookie='theme='+mode+'; Max-Age=31536000; Path=/; SameSite=Lax'}
function toggleTheme(){setTheme(document.body.classList.contains('dark')?'light':'dark')}
function updateTimezoneSelection(select){const out=document.getElementById('tz-selected');if(out)out.textContent=select.value||'(none)'}
let colorPreviewTimer=null;
function updateColorPreview(input){document.querySelectorAll('[data-preview='+input.name+']').forEach(el=>{el.style.color=input.value;if(el.classList.contains('swatch'))el.style.background=input.value})}
function sendColorPreview(){const f=document.getElementById('colors-form');if(!f||!f.dataset.previewUrl)return;fetch(f.dataset.previewUrl,{method:'POST',body:new FormData(f)}).catch(()=>{})}
function scheduleColorPreview(){clearTimeout(colorPreviewTimer);colorPreviewTimer=setTimeout(sendColorPreview,250)}
function handleColorInput(input){updateColorPreview(input);scheduleColorPreview()}
function resetColorForm(){const f=document.getElementById('colors-form');if(!f)return;f.reset();f.querySelectorAll('input[type=color]').forEach(updateColorPreview);sendColorPreview()}
document.addEventListener('DOMContentLoaded',()=>{setTheme(getCookie('theme')||'dark');const tz=document.getElementById('timezone');if(tz)updateTimezoneSelection(tz);document.querySelectorAll('input[type=color]').forEach(updateColorPreview)});
</script>
)HTML";
}

String renderHeader(const String &token, const String &activeTab)
{
  String html;
  html += "<header><div class='top'><div><div class='title'>CYD LeftOvers</div><div class='muted'>Web control panel</div></div><div class='actions'>";
  html += "<button type='button' onclick='toggleTheme()'>Dark / Light</button>";
  if (isLeftoversAdminModeActive())
  {
    html += "<form method='post' action='" + tokenUrl("/admin/exit", token) + "'><button type='submit'>Exit Admin</button></form>";
  }
  html += "<form method='post' action='" + tokenUrl("/reboot", token) + "' onsubmit='return confirm(\"Reboot the CYD now?\")'><button class='danger' type='submit'>Force reboot</button></form>";
  html += "</div></div></header><main>";
  html += "<nav class='tabs'>";

  const char *tabs[][2] = {{"leftovers", "Leftovers"}, {"network", "Networking"}, {"colors", "UI Colors"}, {"diagnostics", "Diagnostics"}};
  bool adminMode = isLeftoversAdminModeActive();
  for (auto &tab : tabs)
  {
    bool locked = String(tab[0]) != "leftovers" && !adminMode;
    html += "<a class='tab";
    if (activeTab == tab[0]) html += " active";
    html += "' href='" + tokenUrl("/edit", token, String("tab=") + tab[0]) + "'>";
    if (locked) html += "&#128274; ";
    html += tab[1];
    html += "</a>";
  }

  html += "</nav>";
  return html;
}

String todayDateString()
{
  struct tm local;
  if (getLocalTime(&local, 50))
  {
    char buffer[12];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d", &local);
    return String(buffer);
  }
  return "2026-05-17";
}

String renderLeftoversTab()
{
  String token = currentSessionTokenOrArg();
  String html = "<section class='grid'><div class='card'><h2>Current leftovers</h2>";
  if (leftovers_last_message != "") html += "<div class='notice'>" + escapeLeftoversHtmlText(leftovers_last_message) + "</div>";
  html += "<p class='hint'>Storage: " + escapeLeftoversHtmlText(leftovers_storage_source) + " / RAM dirty: " + String(leftovers_dirty ? "yes" : "no") + "</p>";
  html += "<form method='post' action='" + tokenUrl("/leftovers/apply", token) + "'>";
  if (leftover_item_count <= 0) html += "<p>No leftovers listed.</p>";
  String currentDate;
  bool dayOpen = false;
  for (int i = 0; i < leftover_item_count; ++i)
  {
    if (leftover_items[i].date != currentDate)
    {
      if (dayOpen) html += "</div></div>";
      currentDate = leftover_items[i].date;
      dayOpen = true;
      html += "<div class='daygroup'><h3>" + escapeLeftoversHtmlText(displayDateHeading(currentDate)) + "</h3>";
      html += "<label class='checklabel daydelete'><input type='checkbox' name='delday' value='" + currentDate + "'><span>Delete all from this day</span></label>";
      html += "<div class='dayitems'>";
    }
    String key = leftover_items[i].date + "|" + leftover_items[i].name;
    html += "<label class='checklabel itempill'><input type='checkbox' name='delitem' value='" + escapeLeftoversHtmlText(key) + "'><span>" + escapeLeftoversHtmlText(displayFoodName(leftover_items[i].name)) + "</span></label>";
  }
  if (dayOpen) html += "</div></div>";
  html += "<p><button class='primary' type='submit'>Apply Deletes to Screen</button></p></form>";
  html += "<form method='post' action='" + tokenUrl("/leftovers/save", token) + "'><button type='submit'>Save RAM to Disk</button></form> ";
  html += "<form method='post' action='" + tokenUrl("/leftovers/recall", token) + "'><button type='submit'>Recall from Disk</button></form>";
  html += "</div><div class='card'><h2>Add items</h2><form method='post' action='" + tokenUrl("/leftovers/apply", token) + "'>";
  html += "<label>Date</label><input type='date' name='date' value='" + todayDateString() + "'>";
  html += "<label>One item per line</label><textarea name='items' rows='8' placeholder='pizza&#10;chili&#10;rice'></textarea>";
  html += "<label class='checklabel'><input type='checkbox' name='saveknown' value='1' checked><span>Save to quick add</span></label>";
  html += "<p><button class='primary' type='submit'>Apply Adds to Screen</button></p></form></div>";
  html += "<div class='card'><h2>Quick Add / Known Foods</h2>";
  if (known_food_count <= 0)
  {
    html += "<p class='muted'>No known foods saved yet.</p>";
  }
  else
  {
    html += "<div class='dayitems'>";
    for (int i = 0; i < known_food_count; ++i)
    {
      String display = escapeLeftoversHtmlText(displayFoodName(known_foods[i]));
      html += "<span class='itempill'>";
      html += "<button type='button' onclick='document.querySelector(\"textarea[name=items]\").value += \"" + display + "\\n\"'>Add</button>";
      html += "<span>" + display + "</span>";
      html += "<form method='post' action='" + tokenUrl("/known/delete", token) + "' style='display:inline'><input type='hidden' name='name' value='" + escapeLeftoversHtmlText(known_foods[i]) + "'><button type='submit' title='Delete known food'>&times;</button></form>";
      html += "</span>";
    }
    html += "</div>";
  }
  html += "<p class='hint'>Deleting a known food only removes it from quick-add. Current leftovers are untouched.</p></div></section>";
  return html;
}

String renderAdminUnlockTab(const String &token, const String &requestedTab)
{
  String adminUrl = startLeftoversAdminQr(requestedTab);
  String html = "<section class='card'><h2>Admin unlock required</h2>";
  html += "<p>Networking, UI Colors, and Diagnostics are locked behind a second QR step to prevent accidental tinkering.</p>";
  html += "<p>Scan the admin QR on the CYD, or use the serial console URL while debugging from the PC.</p>";
  if (adminUrl != "")
  {
    html += "<p class='hint'>Debug URL printed to serial as <code>Admin URL</code>.</p>";
  }
  html += "<form method='post' action='" + tokenUrl("/admin/cancel", token) + "'><button type='submit'>Back to Leftovers</button></form>";
  html += "</section>";
  return html;
}

String friendlyTimezoneLabel(const String &posixTz)
{
  for (int index = 0; index < TIMEZONE_OPTION_COUNT; ++index)
  {
    if (posixTz == TIMEZONE_OPTIONS[index].posix)
    {
      return String(TIMEZONE_OPTIONS[index].label);
    }
  }

  return String("Custom POSIX TZ");
}

String renderTimezoneOptions(const String &selectedPosix)
{
  String html;
  bool matched = false;
  for (int index = 0; index < TIMEZONE_OPTION_COUNT; ++index)
  {
    bool selected = selectedPosix == TIMEZONE_OPTIONS[index].posix;
    matched = matched || selected;
    html += "<option value='";
    html += escapeLeftoversHtmlText(TIMEZONE_OPTIONS[index].posix);
    html += "'";
    if (selected) html += " selected";
    html += ">";
    html += escapeLeftoversHtmlText(TIMEZONE_OPTIONS[index].label);
    html += "</option>";
  }

  if (!matched && selectedPosix != "")
  {
    html += "<option selected value='";
    html += escapeLeftoversHtmlText(selectedPosix);
    html += "'>";
    html += escapeLeftoversHtmlText(friendlyTimezoneLabel(selectedPosix));
    html += "</option>";
  }

  return html;
}

String renderNetworkTab()
{
  String html = "<section class='grid'><div class='card'><h2>Networking</h2><div class='kv'>";
  html += "<div>WiFi</div><div>" + String(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected") + "</div>";
  html += "<div>SSID</div><div>" + escapeLeftoversHtmlText(WiFi.SSID()) + "</div>";
  html += "<div>IP</div><div>" + escapeLeftoversHtmlText(WiFi.localIP().toString()) + "</div>";
  html += "<div>Gateway</div><div>" + escapeLeftoversHtmlText(WiFi.gatewayIP().toString()) + "</div>";
  html += "<div>RSSI</div><div>" + String(WiFi.RSSI()) + " dBm</div>";
  html += "</div></div><div class='card'><h2>NTP config</h2>";
  html += last_network_save_message;
  html += "<form method='post' action='" + tokenUrl("/network", currentSessionTokenOrArg()) + "' onsubmit='return confirm(\"Save networking config and reboot the CYD?\")'>";
  html += "<label>NTP server</label><input name='ntpserver' value='" + escapeLeftoversHtmlText(ntpserver) + "'>";
  html += "<label>Timezone</label><select id='timezone' name='tzinfo' onchange='updateTimezoneSelection(this)'>";
  html += renderTimezoneOptions(tzinfo);
  html += "</select><p class='hint'>Selected POSIX TZ: <code id='tz-selected'>" + escapeLeftoversHtmlText(tzinfo) + "</code></p>";
  html += "<label>NTP check interval after success (minutes)</label><input name='ntpsyncminutes' type='number' min='1' max='1440' value='" + String(ntp_sync_frequency_minutes) + "'>";
  html += "<label>NTP random delay after success (seconds)</label><input name='ntpsyncrandomseconds' type='number' min='0' max='" + String(MAX_NTP_RANDOM_DELAY_SECONDS) + "' value='" + String(ntp_sync_random_delay_seconds) + "'>";
  html += "<label>NTP retry interval after failure (minutes)</label><input name='ntpretryminutes' type='number' min='1' max='1440' value='" + String(ntp_retry_frequency_minutes) + "'>";
  html += "<label>NTP random delay after failure (seconds)</label><input name='ntpretryrandomseconds' type='number' min='0' max='" + String(MAX_NTP_RANDOM_DELAY_SECONDS) + "' value='" + String(ntp_retry_random_delay_seconds) + "'>";
  html += "<p><button class='primary' type='submit'>Save Networking & Reboot</button></p>";
  html += "</form><p class='hint'>Saves to the active config device, then reboots so POST starts clean with the new network/time settings.</p></div></section>";
  return html;
}

String renderColorsTab()
{
  String html = "<section class='grid'><div class='card'><h2>UI Colors</h2>";
  html += last_color_save_message;
  html += "<form id='colors-form' method='post' action='" + tokenUrl("/colors", currentSessionTokenOrArg()) + "' data-preview-url='" + tokenUrl("/colors/preview", currentSessionTokenOrArg()) + "'>";
  html += colorInput("datecolor", "Current date/time text", dateTextColor, "May 17 - 8:42");
  html += colorInput("schedulecolor", "Leftover item text", scheduleTextColor, "May 10 - 7d  Chili");
  html += colorInput("bootcolor", "Boot/POST text", bootTextColor, "POST OK");
  html += colorInput("errorcolor", "Error/alert/session text", errorTextColor, "NTP failed");
  html += "<p class='hint'>Background/status text color settings exist in the old clock firmware config, but the current LeftOvers display mostly paints black backgrounds and primitive status icons. Hiding those controls here until they actually do something useful.</p>";
  html += "<p><button class='primary' type='submit'>Save Colors</button> <button type='button' onclick='resetColorForm()'>Reset to loaded values</button></p>";
  html += "</form><p class='hint'>Color changes are applied live after save and written to the active config device. No reboot needed unless a future setting says otherwise.</p></div>";
  html += "<div class='card'><h2>Preview</h2><p data-preview='datecolor' style='color:" + rgb565ToHex(dateTextColor) + ";font-size:1.4rem;font-weight:700'>May 17 - 8:42</p>";
  html += "<p data-preview='schedulecolor' style='color:" + rgb565ToHex(scheduleTextColor) + "'>May 10 - 7d&nbsp;&nbsp;Chili</p>";
  html += "<p><span data-preview='bootcolor' style='color:" + rgb565ToHex(bootTextColor) + "'>Boot text</span> / <span data-preview='errorcolor' style='color:" + rgb565ToHex(errorTextColor) + "'>Error text</span></p>";
  html += "<p class='hint'>Preview is browser-side. The CYD screen updates after Save Colors.</p></div></section>";
  return html;
}

String renderDiagnosticsTab(const String &token)
{
  String html = "<section class='grid'><div class='card'><h2>Diagnostics</h2><div class='kv'>";
  html += "<div>Build</div><div>" + escapeLeftoversHtmlText(build_version_code) + "</div>";
  html += "<div>SD detected</div><div>" + String(ram_only_mode ? "false" : "true") + "</div>";
  html += "<div>Config source</div><div>" + escapeLeftoversHtmlText(active_config_source) + "</div>";
  html += "<div>Local time</div><div>" + escapeLeftoversHtmlText(formatLocalTimeForWeb()) + "</div>";
  html += "<div>NTP ever synced</div><div>" + String(ntp_ever_synced ? "true" : "false") + "</div>";
  html += "<div>NTP last result</div><div>" + String(ntp_last_sync_succeeded ? "success" : "failure") + "</div>";
  html += "<div>NTP failures</div><div>" + String(consecutive_ntp_failures) + "</div>";
  html += "<div>Last NTP success age</div><div>" + String(last_ntp_success_ms == 0 ? "Never" : formatDuration(millis() - last_ntp_success_ms)) + "</div>";
  html += "<div>Free heap</div><div>" + String(ESP.getFreeHeap()) + "</div>";
  html += "</div></div><div class='card'><h2>Network POIs</h2><p class='muted'>Default reachability checks for DNS, Internet-ish access, and your configured NTP server.</p>";
  html += "<form method='post' action='" + tokenUrl("/netcheck", token) + "'><button class='primary' type='submit'>Run network checks</button></form></div></section>";
  html += last_network_check_html;
  return html;
}

void sendInvalidSessionPage()
{
  leftoversWebServer.send(
    403,
    "text/html",
    "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>CYD LeftOvers</title></head><body>"
    "<h1>Invalid session</h1>"
    "<p>This edit session is invalid or expired. Touch the CYD and rescan the QR code.</p>"
    "</body></html>");
}

void handleEditPage()
{
  String token;
  if (!requireValidSession(token))
  {
    sendInvalidSessionPage();
    return;
  }

  String tab = leftoversWebServer.arg("tab");
  if (tab == "") tab = "leftovers";

  String html = "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>CYD LeftOvers</title>";
  html += renderCssAndScript();
  html += "</head><body>";
  html += renderHeader(token, tab);

  bool adminTab = (tab == "network") || (tab == "colors") || (tab == "diagnostics");
  if (adminTab && !isLeftoversAdminModeActive()) html += renderAdminUnlockTab(token, tab);
  else if (tab == "network") html += renderNetworkTab();
  else if (tab == "colors") html += renderColorsTab();
  else if (tab == "diagnostics") html += renderDiagnosticsTab(token);
  else html += renderLeftoversTab();

  html += "</main></body></html>";
  leftoversWebServer.send(200, "text/html", html);
}

void redirectToLeftovers(const String &token)
{
  initializeLeftoversDisplay();
  renderLeftoversDisplayFull();
  leftoversWebServer.sendHeader("Location", tokenUrl("/edit", token, "tab=leftovers"));
  leftoversWebServer.send(303, "text/plain", "Leftovers updated");
}

void handleLeftoversApply()
{
  String token;
  if (!requireValidSession(token)) { sendInvalidSessionPage(); return; }
  String msg;
  int removed = 0;
  for (int i = 0; i < leftoversWebServer.args(); ++i)
  {
    if (leftoversWebServer.argName(i) == "delday") removed += deleteLeftoverDay(leftoversWebServer.arg(i));
    if (leftoversWebServer.argName(i) == "delitem")
    {
      String key = leftoversWebServer.arg(i);
      int sep = key.indexOf('|');
      if (sep == 10) removed += deleteLeftoverItem(key.substring(0, sep), key.substring(sep + 1));
    }
  }
  if (removed > 0) msg += "Deleted " + String(removed) + " item(s). ";
  String date = leftoversWebServer.arg("date");
  String items = leftoversWebServer.arg("items");
  bool saveKnown = leftoversWebServer.hasArg("saveknown");
  int start = 0;
  while (start < items.length())
  {
    int end = items.indexOf('\n', start);
    String line = end == -1 ? items.substring(start) : items.substring(start, end);
    start = end == -1 ? items.length() : end + 1;
    String lineMsg;
    if (addLeftoverItem(date, line, saveKnown, lineMsg)) msg += lineMsg + ". ";
    else if (lineMsg != "") msg += lineMsg + ". ";
  }
  leftovers_last_message = msg == "" ? "No changes." : msg;
  redirectToLeftovers(token);
}

void handleKnownDelete()
{
  String token;
  if (!requireValidSession(token)) { sendInvalidSessionPage(); return; }
  String name = leftoversWebServer.arg("name");
  if (deleteKnownFood(name)) leftovers_last_message = "Deleted known food: " + displayFoodName(canonicalizeFoodName(name));
  else leftovers_last_message = "Known food not found.";
  redirectToLeftovers(token);
}

void handleLeftoversSave()
{
  String token;
  if (!requireValidSession(token)) { sendInvalidSessionPage(); return; }
  bool ok1 = saveLeftoversToDisk();
  bool ok2 = saveKnownFoodsToDisk();
  leftovers_last_message = (ok1 && ok2) ? "Saved RAM leftovers to disk." : "Save failed.";
  redirectToLeftovers(token);
}

void handleLeftoversRecall()
{
  String token;
  if (!requireValidSession(token)) { sendInvalidSessionPage(); return; }
  loadKnownFoodsFromDisk();
  loadLeftoversFromDisk();
  leftovers_last_message = "Recalled leftovers from disk.";
  redirectToLeftovers(token);
}

void handleAdminUnlock()
{
  String adminToken = leftoversWebServer.arg("a");
  String requestedTab = leftoversWebServer.arg("tab");
  if (requestedTab == "") requestedTab = "network";

  if (!validateLeftoversAdminToken(adminToken))
  {
    sendInvalidSessionPage();
    return;
  }

  markLeftoversAdminOpened();
  leftoversWebServer.sendHeader("Location", tokenUrl("/edit", getLeftoversSessionToken(), String("tab=") + requestedTab));
  leftoversWebServer.send(303, "text/plain", "Admin unlocked");
}

void handleAdminCancel()
{
  String token;
  if (!requireValidSession(token))
  {
    sendInvalidSessionPage();
    return;
  }

  cancelLeftoversAdminQr();
  leftoversWebServer.sendHeader("Location", tokenUrl("/edit", token, "tab=leftovers"));
  leftoversWebServer.send(303, "text/plain", "Admin QR cancelled");
}

void handleAdminExit()
{
  String token;
  if (!requireValidSession(token))
  {
    sendInvalidSessionPage();
    return;
  }

  exitLeftoversAdminMode();
  leftoversWebServer.sendHeader("Location", tokenUrl("/edit", token, "tab=leftovers"));
  leftoversWebServer.send(303, "text/plain", "Admin exited");
}

void handleNetworkSave()
{
  String token;
  if (!requireValidSession(token) || !isLeftoversAdminModeActive())
  {
    sendInvalidSessionPage();
    return;
  }

  String target;
  String error;
  String newConfig = buildNetworkingConfigFromRequest();

  Serial.println("[WEB] Networking config save requested");
  Serial.print("[WEB] Active config source: ");
  Serial.println(active_config_source);

  if (!writeConfigToActiveStorage(newConfig, target, error))
  {
    Serial.print("[WEB] Networking config save failed: ");
    Serial.println(error);
    last_network_save_message = "<div class='notice'><span class='bad'>Networking config save failed: ";
    last_network_save_message += escapeLeftoversHtmlText(error);
    last_network_save_message += "</span></div>";
    leftoversWebServer.sendHeader("Location", tokenUrl("/edit", token, "tab=network"));
    leftoversWebServer.send(303, "text/plain", "Networking save failed");
    return;
  }

  Serial.print("[WEB] Networking config written to ");
  Serial.println(target);
  current_config_text = newConfig;
  active_config_source = target;
  last_network_save_message = "";

  leftoversWebServer.send(
    200,
    "text/html",
    "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>CYD LeftOvers - Rebooting</title></head><body>"
    "<h1>Networking saved</h1>"
    "<p>Configuration written. CYD is rebooting to apply changes.</p>"
    "</body></html>");
  delay(250);
  showWebRebootNotice("networking");
  delay(1250);
  ESP.restart();
}

void handleColorsPreview()
{
  String token;
  if (!requireValidSession(token) || !isLeftoversAdminModeActive())
  {
    leftoversWebServer.send(403, "text/plain", "Invalid session");
    return;
  }

  applyColorArgsToRuntime();
  leftoversWebServer.send(204, "text/plain", "");
}

void handleColorsSave()
{
  String token;
  if (!requireValidSession(token) || !isLeftoversAdminModeActive())
  {
    sendInvalidSessionPage();
    return;
  }

  String target;
  String error;
  String newConfig = buildColorConfigFromRequest();

  Serial.println("[WEB] Color config save requested");
  if (!writeConfigToActiveStorage(newConfig, target, error))
  {
    Serial.print("[WEB] Color config save failed: ");
    Serial.println(error);
    last_color_save_message = "<div class='notice'><span class='bad'>Color config save failed: ";
    last_color_save_message += escapeLeftoversHtmlText(error);
    last_color_save_message += "</span></div>";
    leftoversWebServer.sendHeader("Location", tokenUrl("/edit", token, "tab=colors"));
    leftoversWebServer.send(303, "text/plain", "Color save failed");
    return;
  }

  current_config_text = newConfig;
  active_config_source = target;
  applyColorArgsToRuntime();

  last_color_save_message = "<div class='notice'><span class='good'>Colors saved to ";
  last_color_save_message += escapeLeftoversHtmlText(target);
  last_color_save_message += " and applied.</span></div>";
  leftoversWebServer.sendHeader("Location", tokenUrl("/edit", token, "tab=colors"));
  leftoversWebServer.send(303, "text/plain", "Colors saved");
}

void handleNetworkCheck()
{
  String token;
  if (!requireValidSession(token) || !isLeftoversAdminModeActive())
  {
    sendInvalidSessionPage();
    return;
  }

  runNetworkPoiChecks();
  leftoversWebServer.sendHeader("Location", tokenUrl("/edit", token, "tab=diagnostics"));
  leftoversWebServer.send(303, "text/plain", "See diagnostics");
}

void handleReboot()
{
  String token;
  if (!requireValidSession(token) || !isLeftoversAdminModeActive())
  {
    sendInvalidSessionPage();
    return;
  }

  leftoversWebServer.send(200, "text/html", "<!doctype html><html><head><meta charset='utf-8'></head><body><h1>Rebooting CYD...</h1></body></html>");
  delay(250);
  Serial.println("[WEB] Force reboot requested from web UI");
  showWebRebootNotice("requested");
  delay(1250);
  ESP.restart();
}

void handleNotFound()
{
  leftoversWebServer.send(404, "text/plain", "Not found");
}

void startLeftoversWebServer()
{
  if (leftovers_web_server_running)
  {
    return;
  }

  leftoversWebServer.on("/edit", HTTP_GET, handleEditPage);
  leftoversWebServer.on("/leftovers/apply", HTTP_POST, handleLeftoversApply);
  leftoversWebServer.on("/known/delete", HTTP_POST, handleKnownDelete);
  leftoversWebServer.on("/leftovers/save", HTTP_POST, handleLeftoversSave);
  leftoversWebServer.on("/leftovers/recall", HTTP_POST, handleLeftoversRecall);
  leftoversWebServer.on("/admin", HTTP_GET, handleAdminUnlock);
  leftoversWebServer.on("/admin/cancel", HTTP_POST, handleAdminCancel);
  leftoversWebServer.on("/admin/exit", HTTP_POST, handleAdminExit);
  leftoversWebServer.on("/network", HTTP_POST, handleNetworkSave);
  leftoversWebServer.on("/colors", HTTP_POST, handleColorsSave);
  leftoversWebServer.on("/colors/preview", HTTP_POST, handleColorsPreview);
  leftoversWebServer.on("/netcheck", HTTP_POST, handleNetworkCheck);
  leftoversWebServer.on("/reboot", HTTP_POST, handleReboot);
  leftoversWebServer.onNotFound(handleNotFound);
  leftoversWebServer.begin();
  leftovers_web_server_running = true;
  Serial.println("Leftovers web server started");
}

void processLeftoversWebServer()
{
  if (!leftovers_web_server_running)
  {
    return;
  }

  leftoversWebServer.handleClient();
}
