// CommutePages.h — the commute screens. Each draws the whole 240x240 panel
// from cached data (CommuteClient) and never fetches. CommuteMode picks one.
#pragma once
#include <Arduino.h>
#include <time.h>
#include "Settings.h"

class Arduino_GFX;

enum CommutePage : uint8_t {
  PAGE_HOME = 0,       // time, today's weather, quickest trip, astronaut
  PAGE_HOURS,          // today: next 12 hours, icon, rain %, temperature
  PAGE_WEEK,           // 7-day forecast
  PAGE_TRIPS,          // next trips in detail
  PAGE_PLANTS,         // watering schedule; hold the button to water
  PAGE_INFO,           // network and device status
  PAGE_COUNT
};

// Device facts for the info screen, gathered by CommuteMode.
struct CommuteInfo {
  const char* ssid;
  const char* ip;
  const char* host;       // mDNS name, without ".local"
  int         rssi;       // dBm, 0 = not connected
  const char* fw;
  uint32_t    uptimeSec;
  uint32_t    freeHeap;   // bytes
  uint32_t    nowMs;      // millis(), to age the last fetches
};

// `t` is null until the clock is set; `now` is the matching epoch.
void cpHome(Arduino_GFX* g, const Settings& s, const struct tm* t, time_t now);
void cpHours(Arduino_GFX* g, const Settings& s, const struct tm* t);
void cpWeek(Arduino_GFX* g, const Settings& s);
void cpTrips(Arduino_GFX* g, const Settings& s, time_t now);
void cpInfo(Arduino_GFX* g, const Settings& s, const CommuteInfo& info);
// `watered`: bit mask of plants just watered (splash), 0 = none.
// showWatered() flag: hold found nothing to water (plant bits are 0..MAX_PLANTS-1).
static const uint8_t kPlantsNoneDue = 0x80;
void cpPlants(Arduino_GFX* g, const Settings& s, time_t now, uint8_t watered);

// The astronaut dresses for the weather and the season, waves when it is time
// to leave, sleeps in night mode and facepalms at a cancelled trip.
enum AstroSky : uint8_t { SKY_STARS, SKY_SUN, SKY_RAIN, SKY_SNOW };
enum AstroHat : uint8_t { HAT_NONE, HAT_SANTA, HAT_LUCIA, HAT_PARTY, HAT_FLOWERS };
struct AstroLook {
  AstroSky sky;
  bool     scarf;      // cold
  bool     hurry;      // the next trip leaves within 2 min of the walk time
  bool     sleep;      // night mode is dimming the screen
  bool     facepalm;   // the next trip is cancelled
  AstroHat hat;        // December, Lucia, birthday, Midsummer
};
AstroLook cpAstroLook(const Settings& s, time_t now);   // now = 0: clock not set

// Home screen: the astronaut's corner, right of the trip box, under the line.
// Drawn at (x0, y0): the corner on the screen, or 0,0 in its own buffer.
static const int kAstroX = 180, kAstroY = 156, kAstroW = 60, kAstroH = 84;
void cpAstronaut(Arduino_GFX* g, uint32_t step, const AstroLook& look,
                 int x0 = kAstroX, int y0 = kAstroY);
