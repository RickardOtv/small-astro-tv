#include "CommuteDraw.h"
#include <Arduino_GFX_Library.h>
#include "Gfx.h"
#include "CommuteFonts.h"   // the only place the font data is compiled in

const char* const kCdDays[7]      = {"Sunday", "Monday", "Tuesday", "Wednesday",
                                     "Thursday", "Friday", "Saturday"};
const char* const kCdDaysShort[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char* const kCdMonths[12]   = {"January", "February", "March", "April", "May", "June",
                                     "July", "August", "September", "October", "November",
                                     "December"};

uint16_t cdRgb(uint8_t r, uint8_t g, uint8_t b) {
  return gfxTint((uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)));
}

// ---- text ----------------------------------------------------------------------
// The fonts cover Latin-1, so UTF-8 from the APIs is folded to it first.
static void toLatin1(const char* in, char* out, size_t n) {
  size_t o = 0;
  for (const uint8_t* p = (const uint8_t*)in; *p && o + 1 < n; p++) {
    uint8_t c = *p;
    if (c < 0x80) { out[o++] = (char)c; continue; }
    if ((c & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {        // 2-byte sequence
      uint16_t cp = ((c & 0x1F) << 6) | (p[1] & 0x3F);
      out[o++] = cp <= 0xFF ? (char)cp : '?';
      p++;
      continue;
    }
    while ((p[1] & 0xC0) == 0x80) p++;                        // longer: skip it
    out[o++] = '?';
  }
  out[o] = 0;
}

int cdTextW(Arduino_GFX* g, const GFXfont* font, const char* s) {
  char l1[64];
  toLatin1(s, l1, sizeof(l1));
  g->setFont(font);
  int16_t x1, y1;
  uint16_t w, h;
  g->getTextBounds(l1, 0, 100, &x1, &y1, &w, &h);
  return w;
}

void cdText(Arduino_GFX* g, const GFXfont* font, const char* s, int x, int baseline,
            uint16_t color) {
  char l1[64];
  toLatin1(s, l1, sizeof(l1));
  g->setFont(font);
  g->setTextColor(color);
  g->setCursor(x, baseline);
  g->print(l1);
}

void cdTextCentered(Arduino_GFX* g, const GFXfont* font, const char* s, int cx, int baseline,
                    uint16_t color) {
  cdText(g, font, s, cx - cdTextW(g, font, s) / 2, baseline, color);
}

void cdTextRight(Arduino_GFX* g, const GFXfont* font, const char* s, int right, int baseline,
                 uint16_t color) {
  cdText(g, font, s, right - cdTextW(g, font, s), baseline, color);
}

void cdFit(Arduino_GFX* g, const GFXfont* font, const char* s, int maxW, char* out, size_t n) {
  strlcpy(out, s, n);
  if (cdTextW(g, font, out) <= maxW) return;
  size_t len = strlen(out);
  while (len > 0) {
    do { len--; } while (len > 0 && (out[len] & 0xC0) == 0x80);   // UTF-8 char boundary
    char tmp[48];
    snprintf(tmp, sizeof(tmp), "%.*s..", (int)len, out);
    if (cdTextW(g, font, tmp) <= maxW || len == 0) { strlcpy(out, tmp, n); return; }
  }
}

void cdHHMM(time_t t, char* out, size_t n) {
  struct tm lt;
  localtime_r(&t, &lt);
  snprintf(out, n, "%02d:%02d", lt.tm_hour, lt.tm_min);
}

// ---- weather icons ---------------------------------------------------------------
static int sc(float v, float k) { return (int)lroundf(v * k); }

static void sun(Arduino_GFX* g, int cx, int cy, float r, float k) {
  uint16_t c = cdRgb(255, 200, 40);
  for (int a = 0; a < 360; a += 45) {
    float t = a * (float)PI / 180.0f;
    g->drawLine(cx + sc((r + 4) * cosf(t), k), cy + sc((r + 4) * sinf(t), k),
                cx + sc((r + 9) * cosf(t), k), cy + sc((r + 9) * sinf(t), k), c);
  }
  g->fillCircle(cx, cy, sc(r, k), c);
}

static void moon(Arduino_GFX* g, int cx, int cy, float r, float k) {
  g->fillCircle(cx, cy, sc(r, k), cdRgb(230, 230, 200));
  g->fillCircle(cx + sc(r / 2, k), cy - sc(r / 3, k), sc(r - 2, k), C_BLACK);
}

static void cloud(Arduino_GFX* g, int cx, int cy, uint16_t c, float k) {
  g->fillCircle(cx - sc(10, k), cy + sc(4, k), sc(9, k), c);
  g->fillCircle(cx + sc(2, k), cy - sc(3, k), sc(12, k), c);
  g->fillCircle(cx + sc(13, k), cy + sc(5, k), sc(8, k), c);
  g->fillRoundRect(cx - sc(19, k), cy + sc(3, k), sc(40, k), sc(11, k), sc(5, k), c);
}

void cdWeatherIcon(Arduino_GFX* g, int cx, int cy, int code, bool day, float k) {
  uint16_t light = cdRgb(200, 205, 215), dark = cdRgb(140, 145, 160);
  if (code <= 1) {                                    // clear / mainly clear
    if (day) sun(g, cx, cy, 11, k); else moon(g, cx, cy, 13, k);
  } else if (code == 2) {                             // partly cloudy
    if (day) sun(g, cx - sc(9, k), cy - sc(8, k), 8, k);
    else     moon(g, cx - sc(9, k), cy - sc(8, k), 9, k);
    cloud(g, cx + sc(3, k), cy + sc(4, k), light, k);
  } else if (code == 3) {                             // overcast
    cloud(g, cx, cy, light, k);
  } else if (code == 45 || code == 48) {              // fog
    for (int i = 0; i < 4; i++)
      g->fillRoundRect(cx - sc(20 - (i & 1) * 4, k), cy - sc(12 - i * 8, k), sc(36, k),
                       sc(4, k) < 2 ? 2 : sc(4, k), 2, dark);
  } else if ((code >= 71 && code <= 77) || code == 85 || code == 86) {   // snow
    cloud(g, cx, cy - sc(6, k), light, k);
    for (int i = 0; i < 4; i++)
      g->fillCircle(cx - sc(12 - i * 8, k), cy + sc(16 + (i & 1) * 4, k), sc(2, k) < 1 ? 1 : sc(2, k),
                    C_WHITE);
  } else if (code >= 95) {                            // thunderstorm
    cloud(g, cx, cy - sc(6, k), dark, k);
    uint16_t y = cdRgb(255, 210, 0);
    g->fillTriangle(cx + sc(2, k), cy + sc(6, k), cx - sc(6, k), cy + sc(18, k), cx + sc(1, k),
                    cy + sc(17, k), y);
    g->fillTriangle(cx + sc(1, k), cy + sc(15, k), cx - sc(2, k), cy + sc(26, k), cx + sc(7, k),
                    cy + sc(13, k), y);
  } else {                                            // drizzle / rain / showers
    cloud(g, cx, cy - sc(6, k), dark, k);
    uint16_t b = cdRgb(80, 160, 255);
    for (int i = 0; i < 4; i++) {
      int x = cx - sc(12 - i * 8, k);
      g->drawLine(x, cy + sc(13, k), x - sc(3, k), cy + sc(22, k), b);
      if (k >= 0.8f) g->drawLine(x + 1, cy + sc(13, k), x + 1 - sc(3, k), cy + sc(22, k), b);
    }
  }
}

int cdBigTemp(Arduino_GFX* g, int x, int baseline, int value) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", value);
  cdText(g, &commuteNum, buf, x, baseline, C_WHITE);
  int w = cdTextW(g, &commuteNum, buf);
  g->drawCircle(x + w + 7, baseline - 30, 4, C_WHITE);
  g->drawCircle(x + w + 7, baseline - 30, 3, C_WHITE);
  return w + 12;
}

// ---- line badges -------------------------------------------------------------------
int cdBadgeW(Arduino_GFX* g, const char* line, int h) {
  int w = cdTextW(g, &commuteBold16, line) + 10;
  return w < h ? h : w;
}

void cdBadge(Arduino_GFX* g, int x, int y, int h, const TripLeg& l) {
  int w = cdBadgeW(g, l.line, h);
  g->fillRoundRect(x, y, w, h, 4, gfxTint(l.bg));
  int tw = cdTextW(g, &commuteBold16, l.line);
  cdText(g, &commuteBold16, l.line, x + (w - tw) / 2, y + h / 2 + 6, gfxTint(l.fg));
}

static const int kChainGap = 11;

int cdLegChainW(Arduino_GFX* g, const Trip& t, int h) {
  int total = 0;
  for (uint8_t i = 0; i < t.legCount; i++) total += cdBadgeW(g, t.legs[i].line, h) + (i ? kChainGap : 0);
  return total;
}

void cdLegChain(Arduino_GFX* g, const Trip& t, int x, int y, int h) {
  for (uint8_t i = 0; i < t.legCount; i++) {
    if (i) {
      int ax = x + 3, ay = y + h / 2;
      g->fillTriangle(ax, ay - 4, ax, ay + 4, ax + 5, ay, C_GRAY);
      x += kChainGap;
    }
    cdBadge(g, x, y, h, t.legs[i]);
    x += cdBadgeW(g, t.legs[i].line, h);
  }
}
