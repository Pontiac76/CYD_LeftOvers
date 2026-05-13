#include "leftovers_web.h"

#include "leftovers_session.h"

#include <Arduino.h>
#include <WebServer.h>

WebServer leftoversWebServer(80);
bool leftovers_web_server_running = false;

String escapeLeftoversHtmlText(String value)
{
  value.replace("&", "&amp;");
  value.replace("<", "&lt;");
  value.replace(">", "&gt;");
  value.replace("\"", "&quot;");
  return value;
}

void sendInvalidSessionPage()
{
  leftoversWebServer.send(
    403,
    "text/html",
    "<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>CYD LeftOvers</title></head><body>"
    "<h1>Invalid session</h1>"
    "<p>This edit session is invalid or expired. Touch the CYD and rescan the QR code.</p>"
    "</body></html>");
}

void handleEditPage()
{
  String token = leftoversWebServer.arg("t");
  if (!validateLeftoversSessionToken(token))
  {
    sendInvalidSessionPage();
    return;
  }

  markLeftoversSessionOpened();

  String html =
    "<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>CYD LeftOvers</title>"
    "<style>body{font-family:sans-serif;margin:1.5rem;line-height:1.4}code{word-break:break-all}</style>"
    "</head><body>"
    "<h1>CYD LeftOvers</h1>"
    "<p>It works.</p>"
    "<p>Token valid.</p>"
    "<p><code>";
  html += escapeLeftoversHtmlText(token);
  html +=
    "</code></p>"
    "</body></html>";

  leftoversWebServer.send(200, "text/html", html);
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
