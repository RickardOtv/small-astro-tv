#include "CommutePages.h"
#include <Arduino_GFX_Library.h>
#include "Gfx.h"
#include "CommuteDraw.h"
#include "CommuteClient.h"

static const uint16_t kRainBlue = 0x551F;   // ~ (80,160,255)

static bool hasLocation(const Settings& s) { return s.clock.lat != 0.0f || s.clock.lon != 0.0f; }

// A centred one-line message when weather is not available yet.
static bool weatherMissing(Arduino_GFX* g, const Settings& s, int baseline) {
  if (!hasLocation(s)) {
    cdTextCentered(g, &commuteReg16, "Set location for weather", 120, baseline, C_GRAY);
    return true;
  }
  if (!commuteWeather().valid) {
    cdTextCentered(g, &commuteReg16, "Loading weather...", 120, baseline, C_GRAY);
    return true;
  }
  return false;
}

static bool tripsUnconfigured(const Settings& s) {
  const CommuteSettings& c = s.commute;
  return !c.authKey.length() || !c.stopGid.length() || !c.toGid.length();
}

// Trips not gone yet and not sooner than the walk time; returns how many.
static uint8_t upcomingTrips(const Settings& s, time_t now, const Trip** out, uint8_t max) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < commuteTripCount() && n < max; i++) {
    const Trip& t = commuteTripAt(i);
    if (t.when - now < (time_t)s.commute.walkMin * 60 - 30) continue;
    out[n++] = &t;
  }
  return n;
}

static const char* noTripsReason() {
  return commuteTripError() ? commuteTripStatus()
       : commuteTripCount() ? "No more trips soon"
       : !strcmp(commuteTripStatus(), "idle") ? "Loading trips..."
       : "No trips found";
}

// ===========================================================================
// Home: date, big time, today's weather, the quickest trip, the astronaut
// ===========================================================================
// The trip box: bottom left, beside the astronaut (x 10..176, y 158..240).
static const int kTripRight = 176;

static void homeTrip(Arduino_GFX* g, const Settings& s, time_t now) {
  char buf[48], fitted[48];
  if (tripsUnconfigured(s)) {
    cdText(g, &commuteReg16, "Add the V\xC3\xA4sttrafik", 10, 184, C_GRAY);
    cdText(g, &commuteReg16, "key in the web UI", 10, 206, C_GRAY);
    return;
  }
  snprintf(buf, sizeof(buf), "To %s", s.commute.toName.length() ? s.commute.toName.c_str() : "destination");
  cdFit(g, &commuteBold16, buf, kTripRight - 10, fitted, sizeof(fitted));
  cdText(g, &commuteBold16, fitted, 10, 168, C_GRAY);

  const Trip* list[3];
  uint8_t n = upcomingTrips(s, now, list, 3);
  if (!n) {
    cdFit(g, &commuteReg16, noTripsReason(), kTripRight - 10, fitted, sizeof(fitted));
    cdText(g, &commuteReg16, fitted, 10, 194, commuteTripError() ? C_RED : C_GRAY);
    return;
  }

  // Rides of the first trip, then when the next ones leave (minutes).
  const Trip& t0 = *list[0];
  cdLegChain(g, t0, 10, 174, 20);
  int x = 10 + cdLegChainW(g, t0, 20) + 8;
  int late = t0.legCount ? t0.legs[0].delayMin : 0;
  if (late > 0 && !t0.canceled) {
    snprintf(buf, sizeof(buf), "+%d", late);
    cdText(g, &commuteBold16, buf, x, 190, C_RED);
    x += cdTextW(g, &commuteBold16, buf) + 6;
  }
  char later[24] = "";
  for (uint8_t i = 1; i < n; i++) {
    char part[12];
    snprintf(part, sizeof(part), "%s%ld", i > 1 ? ", " : "then ", (long)((list[i]->when - now) / 60));
    strlcat(later, part, sizeof(later));
  }
  if (later[0]) {
    cdFit(g, &commuteReg16, later, kTripRight - x, fitted, sizeof(fitted));
    cdText(g, &commuteReg16, fitted, x, 190, C_GRAY);
  }

  // Minutes until it leaves (big), and the arrival time on the right.
  long m0 = (long)((t0.when - now) / 60);
  if (m0 < 0) m0 = 0;
  if (t0.canceled) {
    cdText(g, &commuteBold18, "Cancelled", 10, 224, C_RED);
  } else if (m0 == 0) {
    cdText(g, &commuteBold18, "Leave now", 10, 224, C_GREEN);
  } else if (m0 >= 100) {                     // far off (night): show the clock time
    cdHHMM(t0.when, buf, sizeof(buf));
    cdText(g, &commuteBold18, buf, 10, 224, C_WHITE);
  } else {
    snprintf(buf, sizeof(buf), "%ld", m0);
    cdText(g, &commuteMin, buf, 9, 228, C_WHITE);
    x = 9 + cdTextW(g, &commuteMin, buf) + 4;
    cdText(g, &commuteBold16, "min", x, 228, C_GRAY);
    g->fillCircle(x + cdTextW(g, &commuteBold16, "min") + 7, 222, 3, t0.realtime ? C_GREEN : C_DGRAY);
  }
  char at[8];
  cdHHMM(t0.arrive, at, sizeof(at));
  int aw = cdTextW(g, &commuteBold16, at);
  cdTextRight(g, &commuteBold16, at, kTripRight, 228, C_GRAY);
  int ax = kTripRight - aw - 10;
  g->fillTriangle(ax, 218, ax, 226, ax + 5, 222, C_GRAY);
}

void cpHome(Arduino_GFX* g, const Settings& s, const struct tm* t, time_t now) {
  char buf[40];
  if (t) {
    cdText(g, &commuteBold16, kCdDays[t->tm_wday], 10, 22, C_WHITE);
    snprintf(buf, sizeof(buf), "%d %s", t->tm_mday, kCdMonths[t->tm_mon]);
    cdTextRight(g, &commuteReg16, buf, 230, 22, C_GRAY);
    snprintf(buf, sizeof(buf), "%02d:%02d", t->tm_hour, t->tm_min);
  } else {
    strlcpy(buf, "--:--", sizeof(buf));
  }
  cdTextCentered(g, &commuteTime, buf, 120, 88, C_WHITE);

  if (!weatherMissing(g, s, 128)) {
    const Weather& w = commuteWeather();
    cdWeatherIcon(g, 36, 120, w.code, w.isDay, 1.0f);
    cdBigTemp(g, 72, 140, (int)lroundf(w.temp));
    if (w.dayCount) {
      snprintf(buf, sizeof(buf), "%d\xC2\xB0 / %d\xC2\xB0", w.days[0].tMax, w.days[0].tMin);
      cdTextRight(g, &commuteBold16, buf, 230, 108, C_WHITE);
      if (w.days[0].rain >= 0) {
        snprintf(buf, sizeof(buf), "Rain %d%%", w.days[0].rain);
        cdTextRight(g, &commuteReg16, buf, 230, 126, w.days[0].rain >= 50 ? gfxTint(kRainBlue) : C_GRAY);
      }
    }
    snprintf(buf, sizeof(buf), "Wind %d m/s", (int)lroundf(w.wind));
    cdTextRight(g, &commuteReg16, buf, 230, 144, C_GRAY);
  }
  g->drawFastHLine(10, 153, 220, C_DGRAY);
  if (t) homeTrip(g, s, now);
  cpAstronaut(g, 0, cpAstroLook(s, t ? now : 0));
}

// ---- the astronaut: original 16x16 pixel art, drawn 3x -------------------------------
static const char* const kAstroDown[16] = {
  ".....WWWWWW.....", "....WWWWWWWW....", "...WWDDDDDDWW...", "...WDVVVVVLDW...",
  "...WDVVVVLLDW...", "...WDVVVVVVDW...", "...WWDDDDDDWW...", "....WWWWWWWW....",
  "..OWWWWWWWWWWO..", ".WWOWWWRRWWWOWW.", ".WW.OWWBBWWO.WW.", ".GG.OWWWWWWO.GG.",
  "....OWWWWWWO....", "....WWW..WWW....", "....WWW..WWW....", "....DDD..DDD....",
};
static const char* const kAstroWave[16] = {
  ".....WWWWWW.....", "....WWWWWWWW....", "...WWDDDDDDWW...", "...WDVVVVVLDW...",
  "...WDVVVVLLDW...", "...WDVVVVVVDW.GG", "...WWDDDDDDWW.WW", "....WWWWWWWW.WW.",
  "..OWWWWWWWWWWW..", ".WWOWWWRRWWWO...", ".WW.OWWBBWWO....", ".GG.OWWWWWWO....",
  "....OWWWWWWO....", "....WWW..WWW....", "....WWW..WWW....", "....DDD..DDD....",
};

static uint16_t astroColor(char c) {
  switch (c) {
    case 'W': return cdRgb(230, 232, 238);
    case 'D': return cdRgb(95, 100, 118);
    case 'V': return cdRgb(30, 60, 130);
    case 'L': return cdRgb(150, 210, 255);
    case 'O': return cdRgb(240, 140, 40);
    case 'G': return cdRgb(170, 175, 190);
    case 'R': return cdRgb(240, 70, 70);
    case 'B': return cdRgb(60, 140, 240);
    default:  return 0;
  }
}

AstroLook cpAstroLook(const Settings& s, time_t now) {
  AstroLook l = {SKY_STARS, false, false};
  const Weather& w = commuteWeather();
  if (w.valid) {
    int c = w.code;
    if ((c >= 51 && c <= 67) || (c >= 80 && c <= 82) || c >= 95) l.sky = SKY_RAIN;
    else if ((c >= 71 && c <= 77) || c == 85 || c == 86)          l.sky = SKY_SNOW;
    else if (c <= 1 && w.isDay)                                    l.sky = SKY_SUN;
    l.scarf = w.temp <= 2.0f;
  }
  if (now && !tripsUnconfigured(s)) {
    const Trip* first;
    if (upcomingTrips(s, now, &first, 1) && !first->canceled)
      l.hurry = (first->when - now) / 60 - (time_t)s.commute.walkMin <= 2;
  }
  return l;
}

void cpAstronaut(Arduino_GFX* g, uint32_t step, const AstroLook& look, int x0, int y0) {
  const int X0 = x0, Y0 = y0, W = kAstroW, H = kAstroH;
  static const int8_t kBob[16] = {0, 0, 1, 2, 3, 4, 4, 5, 5, 5, 4, 4, 3, 2, 1, 0};
  static const uint8_t kStars[][2] = {{4, 10}, {52, 6}, {6, 70}, {56, 62}, {28, 2}};
  static const uint8_t kDropX[8] = {2, 9, 17, 25, 33, 41, 49, 56};
  uint8_t i = step % 16;
  bool umbrella = look.sky == SKY_RAIN;
  // Arm up: holding the umbrella, waving when it is time to go, or now and then.
  bool armUp = umbrella || (look.hurry ? (step / 2) % 2 == 0
                                       : (step / 16) % 2 == 1 && (i / 2) % 2 == 0);
  int ox = X0 + (umbrella ? 0 : 4), oy = Y0 + 14 + kBob[look.hurry ? (i * 2) % 16 : i];

  g->fillRect(X0, Y0, W, H, C_BLACK);

  // Sky.
  if (look.sky == SKY_STARS) {
    for (uint8_t k = 0; k < 5; k++) {
      bool bright = ((step / 3) + k) % 4 == 0;
      g->fillRect(X0 + kStars[k][0], Y0 + kStars[k][1], bright ? 2 : 1, bright ? 2 : 1,
                  bright ? C_WHITE : C_DGRAY);
    }
  } else if (look.sky == SKY_SUN && !look.hurry) {
    uint16_t c = cdRgb(255, 200, 40);
    g->fillCircle(X0 + 9, Y0 + 9, 5, c);
    for (int k = 0; k < 8; k++) {
      if ((k + step / 4) % 2) continue;              // rays twinkle
      float a = k * (float)PI / 4;
      g->drawLine(X0 + 9 + (int)(8 * cosf(a)), Y0 + 9 + (int)(8 * sinf(a)),
                  X0 + 9 + (int)(11 * cosf(a)), Y0 + 9 + (int)(11 * sinf(a)), c);
    }
  }
  // The umbrella: canopy over the raised hand, rain does not fall under it.
  const int ux = ox + 44, uy = oy - 1, ur = 14;
  if (umbrella) {
    g->fillCircle(ux, uy, ur, cdRgb(225, 60, 70));
    g->fillRect(ux - ur - 1, uy + 1, 2 * ur + 3, ur + 1, C_BLACK);
    for (int k = -ur + 4; k < ur; k += 7) g->fillCircle(ux + k + 3, uy + 1, 3, C_BLACK);   // scallops
    g->drawFastVLine(ux, uy, 18, C_GRAY);
  }
  if (look.sky == SKY_RAIN || look.sky == SKY_SNOW) {
    bool snow = look.sky == SKY_SNOW;
    for (uint8_t k = 0; k < 8; k++) {
      int y = (int)((k * 37 + step * (snow ? 2 : 6)) % (H - 4));
      int x = X0 + kDropX[k] + (snow ? (int)((step / 4 + k) % 3) - 1 : 0);
      if (umbrella && x >= ux - ur && x <= ux + ur && Y0 + y > uy - ur) continue;
      if (snow) { g->fillRect(x - 1, Y0 + y, 3, 1, C_WHITE); g->fillRect(x, Y0 + y - 1, 1, 3, C_WHITE); }
      else      g->drawFastVLine(x, Y0 + y, 5, cdRgb(80, 160, 255));
    }
  }
  if (look.hurry && (step / 3) % 2 == 0) cdText(g, &commuteBold16, "GO!", X0 + 2, Y0 + 16, C_GREEN);

  const char* const* rows = armUp ? kAstroWave : kAstroDown;
  for (int r = 0; r < 16; r++)
    for (int c = 0; c < 16; c++) {
      char ch = rows[r][c];
      if (ch != '.') g->fillRect(ox + c * 3, oy + r * 3, 3, 3, astroColor(ch));
    }
  if (look.scarf) {                                   // striped scarf, tail on the left
    for (int c = 4; c < 12; c++)
      g->fillRect(ox + c * 3, oy + 7 * 3, 3, 3, c % 2 ? cdRgb(220, 50, 60) : cdRgb(250, 240, 240));
    g->fillRect(ox + 4 * 3, oy + 8 * 3, 3, 6, cdRgb(220, 50, 60));
    g->fillRect(ox + 3 * 3, oy + 10 * 3, 3, 3, cdRgb(220, 50, 60));
  }
}

// ===========================================================================
// Hours: today's high and low, the next 8 hours in one row (hour, icon,
// chance of rain, temperature), then when rain starts, wind and the sun
// ===========================================================================
static const uint8_t kHourCols = 8;

// "40%" centred on cx, with a 5x8 px percent sign: the font's is too wide for
// a 30 px column.
static void hourRain(Arduino_GFX* g, int cx, int baseline, int pct, uint16_t c) {
  char num[6];
  snprintf(num, sizeof(num), "%d", pct);
  int nw = cdTextW(g, &commuteBold13, num), w = nw + 7;
  int x = cx - w / 2;
  cdText(g, &commuteBold13, num, x, baseline, c);
  x += nw + 2;
  g->fillRect(x, baseline - 9, 2, 2, c);
  g->fillRect(x + 3, baseline - 2, 2, 2, c);
  g->drawLine(x + 4, baseline - 9, x, baseline - 1, c);
}

void cpHours(Arduino_GFX* g, const Settings& s, const struct tm* t) {
  char buf[32];
  cdText(g, &commuteBold16, t ? kCdDays[t->tm_wday] : "Today", 10, 22, C_WHITE);
  if (weatherMissing(g, s, 120)) return;
  const Weather& w = commuteWeather();
  if (w.dayCount) {
    snprintf(buf, sizeof(buf), "H %d\xC2\xB0  L %d\xC2\xB0", w.days[0].tMax, w.days[0].tMin);
    cdTextRight(g, &commuteReg16, buf, 230, 22, C_GRAY);
  }
  g->drawFastHLine(10, 31, 220, C_GRAY);

  // The row: 30 px per hour.
  uint8_t n = w.hourCount < kHourCols ? w.hourCount : kHourCols;
  for (uint8_t i = 0; i < n; i++) {
    const HourWx& h = w.hours[i];
    int cx = 15 + i * 30;
    if (i == 0) {
      cdTextCentered(g, &commuteBold13, "Now", cx, 54, C_WHITE);
    } else {
      snprintf(buf, sizeof(buf), "%02d", h.hour);
      cdTextCentered(g, &commuteReg12, buf, cx, 54, C_GRAY);
    }
    cdWeatherIcon(g, cx, 78, h.code, h.isDay, 0.5f);
    // Chance of rain under the icon, like Apple: only when it matters, in tens.
    if (h.rain >= 20) {
      hourRain(g, cx, 110, (h.rain + 5) / 10 * 10,
               h.rain >= 50 ? gfxTint(kRainBlue) : cdRgb(70, 130, 200));
    }
    snprintf(buf, sizeof(buf), "%d\xC2\xB0", h.temp);
    cdTextCentered(g, &commuteBold13, buf, cx + 2, 134, C_WHITE);
  }
  g->drawFastHLine(10, 146, 220, C_DGRAY);

  // Rain: when the chance first reaches 50% in the next 12 hours.
  int8_t rainAt = -1;
  for (uint8_t i = 0; i < w.hourCount; i++)
    if (w.hours[i].rain >= 50) { rainAt = (int8_t)i; break; }
  if (rainAt == 0)      strlcpy(buf, "Rain likely now", sizeof(buf));
  else if (rainAt > 0)  snprintf(buf, sizeof(buf), "Rain likely from %02d:00", w.hours[rainAt].hour);
  else                  snprintf(buf, sizeof(buf), "Dry the next %u hours", w.hourCount);
  cdText(g, &commuteBold16, buf, 10, 172, rainAt >= 0 ? gfxTint(kRainBlue) : C_WHITE);

  snprintf(buf, sizeof(buf), "Feels like %d\xC2\xB0", (int)lroundf(w.feels));
  cdText(g, &commuteReg16, buf, 10, 200, C_GRAY);
  snprintf(buf, sizeof(buf), "Wind %d m/s", (int)lroundf(w.wind));
  cdTextRight(g, &commuteReg16, buf, 230, 200, C_GRAY);

  if (w.sunrise[0]) {
    snprintf(buf, sizeof(buf), "Sunrise %s", w.sunrise);
    cdText(g, &commuteReg16, buf, 10, 226, C_GRAY);
  }
  if (w.sunset[0]) {
    snprintf(buf, sizeof(buf), "Sunset %s", w.sunset);
    cdTextRight(g, &commuteReg16, buf, 230, 226, C_GRAY);
  }
}

// ===========================================================================
// Week: one row per day
// ===========================================================================
void cpWeek(Arduino_GFX* g, const Settings& s) {
  cdText(g, &commuteBold16, "This week", 10, 24, C_WHITE);
  if (weatherMissing(g, s, 120)) return;
  const Weather& w = commuteWeather();

  int lo = 99, hi = -99;
  for (uint8_t i = 0; i < w.dayCount; i++) {
    if (w.days[i].tMin < lo) lo = w.days[i].tMin;
    if (w.days[i].tMax > hi) hi = w.days[i].tMax;
  }
  if (hi <= lo) hi = lo + 1;
  const int barX = 160, barW = 36;

  char buf[16];
  for (uint8_t i = 0; i < w.dayCount && i < 7; i++) {
    const DayWx& d = w.days[i];
    int y0 = 34 + i * 29, base = y0 + 20;
    g->drawFastHLine(10, y0, 220, i ? C_DGRAY : C_GRAY);
    cdText(g, &commuteBold16, i ? kCdDaysShort[d.wday] : "Today", 10, base, C_WHITE);
    cdWeatherIcon(g, 76, y0 + 13, d.code, true, 0.42f);
    if (d.rain >= 0) {
      snprintf(buf, sizeof(buf), "%d%%", d.rain);
      cdTextRight(g, &commuteReg16, buf, 132, base, d.rain >= 50 ? gfxTint(kRainBlue) : C_GRAY);
    }
    snprintf(buf, sizeof(buf), "%d\xC2\xB0", d.tMin);
    cdTextRight(g, &commuteReg16, buf, barX - 4, base, C_GRAY);
    // Temperature range bar, scaled to the week's lowest and highest.
    int by = base - 6;
    g->fillRoundRect(barX, by, barW, 4, 2, C_DGRAY);
    int a = barX + (d.tMin - lo) * barW / (hi - lo);
    int b = barX + (d.tMax - lo) * barW / (hi - lo);
    g->fillRoundRect(a, by, b - a < 4 ? 4 : b - a, 4, 2, cdRgb(255, 170, 60));
    snprintf(buf, sizeof(buf), "%d\xC2\xB0", d.tMax);
    cdTextRight(g, &commuteBold16, buf, 232, base, C_WHITE);
  }
}

// ===========================================================================
// Trips in detail: the next three, with times, rides and the change
// ===========================================================================
void cpTrips(Arduino_GFX* g, const Settings& s, time_t now) {
  const CommuteSettings& c = s.commute;
  char buf[64], fitted[48];
  // Header: "From > To"
  int fromW = cdTextW(g, &commuteBold16, c.stopName.c_str());
  cdFit(g, &commuteBold16, c.stopName.c_str(), 100, fitted, sizeof(fitted));
  cdText(g, &commuteBold16, fitted, 10, 24, C_GRAY);
  int ax = 10 + (fromW < 100 ? fromW : cdTextW(g, &commuteBold16, fitted)) + 6;
  g->fillTriangle(ax, 14, ax, 22, ax + 5, 18, C_GRAY);
  cdFit(g, &commuteBold16, c.toName.c_str(), 230 - ax - 12, fitted, sizeof(fitted));
  cdText(g, &commuteBold16, fitted, ax + 11, 24, C_WHITE);
  g->drawFastHLine(10, 34, 220, C_GRAY);

  if (tripsUnconfigured(s)) {
    cdTextCentered(g, &commuteReg16, "Add the V\xC3\xA4sttrafik key", 120, 110, C_GRAY);
    cdTextCentered(g, &commuteReg16, "in the web UI, Commute tab", 120, 132, C_GRAY);
    return;
  }
  const Trip* list[3];
  uint8_t n = upcomingTrips(s, now, list, 3);
  if (!n) {
    cdTextCentered(g, &commuteReg16, noTripsReason(), 120, 120, commuteTripError() ? C_RED : C_GRAY);
    return;
  }

  for (uint8_t i = 0; i < n; i++) {
    const Trip& t = *list[i];
    int y = 40 + i * 66;
    // Line 1: leave -> arrive, delay, minutes left.
    char dep[8], arr[8];
    cdHHMM(t.when, dep, sizeof(dep));
    cdHHMM(t.arrive, arr, sizeof(arr));
    cdText(g, &commuteBold18, dep, 10, y + 20, C_WHITE);
    int x = 10 + cdTextW(g, &commuteBold18, dep) + 6;
    g->fillTriangle(x, y + 9, x, y + 17, x + 5, y + 13, C_GRAY);
    x += 11;
    cdText(g, &commuteBold18, arr, x, y + 20, C_GRAY);
    x += cdTextW(g, &commuteBold18, arr) + 6;
    int late = t.legCount ? t.legs[0].delayMin : 0;
    if (late > 0) {
      snprintf(buf, sizeof(buf), "+%d", late);
      cdText(g, &commuteReg16, buf, x, y + 20, C_RED);
    }
    long m = (long)((t.when - now) / 60);
    if (t.canceled)      strlcpy(buf, "Cancelled", sizeof(buf));
    else if (m <= 0)     strlcpy(buf, "Now", sizeof(buf));
    else if (m >= 100)   snprintf(buf, sizeof(buf), "%ldh", (m + 30) / 60);
    else                 snprintf(buf, sizeof(buf), "%ld min", m);
    int mw = cdTextW(g, &commuteBold16, buf);
    cdTextRight(g, &commuteBold16, buf, 230, y + 20, t.canceled ? C_RED : C_WHITE);
    g->fillCircle(230 - mw - 8, y + 14, 3, t.realtime ? C_GREEN : C_DGRAY);

    // Line 2: the rides, then where to change (or "direct").
    cdLegChain(g, t, 10, y + 30, 22);
    int cx = 10 + cdLegChainW(g, t, 22) + 8;
    if (t.legCount > 1) snprintf(buf, sizeof(buf), "change %s", t.legs[1].from);
    else                strlcpy(buf, "direct", sizeof(buf));
    cdFit(g, &commuteReg16, buf, 230 - cx, fitted, sizeof(fitted));
    cdText(g, &commuteReg16, fitted, cx, y + 47, C_GRAY);
    if (i + 1 < n) g->drawFastHLine(10, y + 60, 220, C_DGRAY);
  }
}

// ===========================================================================
// Info: network and device status
// ===========================================================================
static void ago(uint32_t okMs, uint32_t nowMs, char* out, size_t n) {
  if (!okMs) { strlcpy(out, "never", n); return; }
  uint32_t m = (nowMs - okMs) / 60000UL;
  if (m == 0)       strlcpy(out, "just now", n);
  else if (m < 120) snprintf(out, n, "%lu min ago", (unsigned long)m);
  else              snprintf(out, n, "%lu h ago", (unsigned long)(m / 60));
}

void cpInfo(Arduino_GFX* g, const Settings& s, const CommuteInfo& in) {
  cdText(g, &commuteBold18, "Info", 10, 26, C_WHITE);
  g->drawFastHLine(10, 36, 220, C_GRAY);

  char v[48], fitted[48];
  const char* labels[9] = {"Wi-Fi", "Signal", "IP", "Address", "Firmware", "Uptime", "Memory",
                           "Weather", "Trips"};
  for (uint8_t i = 0; i < 9; i++) {
    uint16_t col = C_WHITE;
    switch (i) {
      case 0: strlcpy(v, in.ssid && in.ssid[0] ? in.ssid : "-", sizeof(v)); break;
      case 1:
        if (!in.rssi) { strlcpy(v, "-", sizeof(v)); break; }
        snprintf(v, sizeof(v), "%d dBm %s", in.rssi,
                 in.rssi > -60 ? "good" : in.rssi > -72 ? "ok" : "weak");
        col = in.rssi > -60 ? C_GREEN : in.rssi > -72 ? C_YELLOW : C_RED;
        break;
      case 2: strlcpy(v, in.ip, sizeof(v)); break;
      case 3: snprintf(v, sizeof(v), "%s.local", in.host); break;
      case 4: strlcpy(v, in.fw, sizeof(v)); break;
      case 5: {
        uint32_t d = in.uptimeSec / 86400, h = (in.uptimeSec / 3600) % 24, m = (in.uptimeSec / 60) % 60;
        if (d) snprintf(v, sizeof(v), "%lu d %lu h", (unsigned long)d, (unsigned long)h);
        else   snprintf(v, sizeof(v), "%lu h %lu min", (unsigned long)h, (unsigned long)m);
        break;
      }
      case 6: snprintf(v, sizeof(v), "%lu KB free", (unsigned long)(in.freeHeap / 1024)); break;
      case 7:
        ago(commuteWeatherOkMs(), in.nowMs, v, sizeof(v));
        if (!commuteWeatherOkMs()) col = C_GRAY;
        break;
      case 8:
        if (tripsUnconfigured(s))   { strlcpy(v, "not set up", sizeof(v)); col = C_GRAY; }
        else if (commuteTripError()) { strlcpy(v, commuteTripStatus(), sizeof(v)); col = C_RED; }
        else { ago(commuteTripOkMs(), in.nowMs, v, sizeof(v)); if (!commuteTripOkMs()) col = C_GRAY; }
        break;
    }
    int base = 56 + i * 21;
    cdText(g, &commuteReg16, labels[i], 10, base, C_GRAY);
    cdFit(g, &commuteBold16, v, 140, fitted, sizeof(fitted));
    cdTextRight(g, &commuteBold16, fitted, 230, base, col);
  }
}
