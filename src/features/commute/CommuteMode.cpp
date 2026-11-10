#include "CommuteMode.h"
#include <Arduino_GFX_Library.h>
#include <time.h>
#include "Gfx.h"
#include "Clock.h"
#include "Net.h"
#include "Platform.h"
#include "CommuteClient.h"
#include "CommutePages.h"
#include "CommuteStrip.h"
#include "CommutePlants.h"

CommuteMode g_commuteMode;

static const uint32_t kInfoRefreshMs = 5000;   // the info screen's live values

static const uint32_t kSplashMs = 2500;   // "Watered!" stays this long

static void drawPage(Arduino_GFX* g, uint8_t page, const Settings& s, const struct tm* t,
                     time_t now, uint8_t watered) {
  g->fillScreen(C_BLACK);
  g->setTextSize(1);
  switch (page) {
    case PAGE_HOURS: cpHours(g, s, t); break;
    case PAGE_WEEK:  cpWeek(g, s); break;
    case PAGE_TRIPS: if (t) cpTrips(g, s, now); break;
    case PAGE_PLANTS: cpPlants(g, s, t ? now : 0, watered); break;
    case PAGE_INFO: {
      String ssid = netSSID(), ip = netIP();
      CommuteInfo in = {ssid.c_str(), ip.c_str(), s.hostname.c_str(), netRSSI(),
                        FW_VERSION " mod", millis() / 1000, ESP.getFreeHeap(), millis()};
      cpInfo(g, s, in);
      break;
    }
    default: cpHome(g, s, t, now); break;
  }
}

// The screen is drawn in strips (CommuteStrip.h) so it never flashes black.
// The 29 KB buffer is borrowed for the redraw only; without it, draw directly.
static const int16_t kStripRows = 60;

static void render(uint8_t page, const Settings& s, uint8_t watered) {
  Arduino_GFX* out = gfxDev();
  if (!out) return;
  struct tm t;
  bool haveTime = clockNow(t);
  struct tm tc = t;
  time_t now = haveTime ? mktime(&tc) : 0;   // same clock as the minute shown
  const struct tm* tp = haveTime ? &t : nullptr;

  int16_t w = out->width(), h = out->height();
  uint16_t* buf = (uint16_t*)malloc((size_t)w * kStripRows * 2);
  if (buf) {
    CommuteStrip strip(w, h, buf, kStripRows);
    for (int16_t y0 = 0; y0 < h; y0 += kStripRows) {
      int16_t rows = h - y0 < kStripRows ? h - y0 : kStripRows;
      strip.setStrip(y0);
      drawPage(&strip, page, s, tp, now, watered);
      out->draw16bitRGBBitmap(0, y0, buf, w, rows);
    }
    free(buf);
  } else {
    drawPage(out, page, s, tp, now, watered);
  }
  out->setFont((const GFXfont*)nullptr);   // back to the built-in font for everything else
}

void CommuteMode::nextPage() {
  page_ = (page_ + 1) % PAGE_COUNT;
  needRender_ = true;
}

void CommuteMode::setPage(uint8_t page) {
  page_ = page < PAGE_COUNT ? page : PAGE_HOME;
  needRender_ = true;
}

bool CommuteMode::longPress(Settings& s) {
  if (page_ != PAGE_PLANTS) return false;
  uint8_t mask = plantsWater(s.commute, -1, clockSynced() ? time(nullptr) : 0);
  if (mask) saveSettings(s);
  showWatered(mask ? mask : kPlantsNoneDue);
  needRender_ = true;
  return true;
}

void CommuteMode::showWatered(uint8_t mask) {
  wateredMask_ = mask;
  wateredAtMs_ = millis();
  needRender_ = true;
}

void CommuteMode::home() {
  page_ = PAGE_HOME;
  needRender_ = true;
}

void CommuteMode::begin(const Settings& s) {
  (void)s;
  commuteInit();
  needRender_ = true;
}

void CommuteMode::invalidate(const Settings& s) {
  (void)s;
  commuteInit();
  needRender_ = true;
}

void CommuteMode::service(const Settings& s) {
  commuteService(s);

  // What changes on each screen: data everywhere, the minute where a clock or
  // a countdown shows, live values on the info screen.
  struct tm t;
  int minute = clockNow(t) ? t.tm_hour * 60 + t.tm_min : -1;
  uint32_t seq = commuteChangeSeq();
  bool usesMinute = page_ == PAGE_HOME || page_ == PAGE_TRIPS || page_ == PAGE_PLANTS;
  if (seq != renderedSeq_) needRender_ = true;
  if (usesMinute && minute != renderedMin_) needRender_ = true;
  if (page_ == PAGE_INFO && millis() - renderedAtMs_ >= kInfoRefreshMs) needRender_ = true;
  if (wateredMask_ && millis() - wateredAtMs_ >= kSplashMs) { wateredMask_ = 0; needRender_ = true; }

  if (needRender_) {
    render(page_, s, page_ == PAGE_PLANTS ? wateredMask_ : 0);
    renderedSeq_ = seq;
    renderedMin_ = minute;
    renderedAtMs_ = millis();
    needRender_ = false;
    animAtMs_ = millis();
    return;
  }

  // Home screen: animate the astronaut corner only.
  if (page_ == PAGE_HOME && commuteWeather().valid && millis() - animAtMs_ >= COMMUTE_ANIM_MS) {
    animAtMs_ = millis();
    Arduino_GFX* out = gfxDev();
    if (!out) return;
    if (!astro_) {
      astro_ = new Arduino_Canvas(kAstroW, kAstroH, out, kAstroX, kAstroY);
      if (!astro_->begin(GFX_SKIP_OUTPUT_BEGIN)) { delete astro_; astro_ = nullptr; return; }
    }
    cpAstronaut(astro_, ++animStep_, cpAstroLook(s, clockSynced() ? time(nullptr) : 0), 0, 0);
    astro_->flush();
  }
}
