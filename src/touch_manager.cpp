#include "touch_manager.h"

#include "leftovers_session.h"

#include <SPI.h>

SPIClass mySpi = SPIClass(HSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
bool touch_ready = false;
bool touch_initialized = false;

void initialize_touch()
{
#if ENABLE_TOUCH
  if (!touch_initialized)
  {
    mySpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    ts.begin(mySpi);
    ts.setRotation(1);
    touch_initialized = true;
  }

  digitalWrite(XPT2046_CS, HIGH);
  touch_ready = true;
#endif
}

void suspend_touch_for_sd()
{
#if ENABLE_TOUCH
  if (touch_ready)
  {
    digitalWrite(XPT2046_CS, HIGH);
    touch_ready = false;
  }
#endif
}

void resume_touch_after_sd()
{
#if ENABLE_TOUCH
  if (!touch_ready)
  {
    initialize_touch();
  }
#endif
}

void printTouchToSerial(TS_Point p)
{
  Serial.print("Pressure = ");
  Serial.print(p.z);
  Serial.print(", x = ");
  Serial.print(p.x);
  Serial.print(", y = ");
  Serial.print(p.y);
  Serial.println();
}

void handleSystemIdSwitchTouch()
{
  Serial.println("System ID touch ignored: not used by CYD_LeftOvers");
}

void processTouchInput()
{
#if ENABLE_TOUCH
  if (touch_ready && ts.tirqTouched() && ts.touched())
  {
    TS_Point p = ts.getPoint();
    printTouchToSerial(p);
    handleLeftoversScreenTouch();
    delay(300);
  }
#endif
}
