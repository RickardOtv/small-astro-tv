// CommuteMode.h — the commute screens: home (time, weather, quickest trip,
// astronaut), the next hours, the week, trips in detail and device info. A
// button tap steps to the next screen (nextPage), a long press returns home.
// Nothing rotates on its own. Screens are drawn off-screen, then sent to the
// panel in one go, so they never flicker.
#pragma once
#include "Mode.h"
#include "config.h"

class Arduino_Canvas;

class CommuteMode : public DisplayMode {
 public:
  const char* id() const override { return "commute"; }
  uint8_t     modeConst() const override { return MODE_COMMUTE; }

  void begin(const Settings& s) override;
  void service(const Settings& s) override;
  void invalidate(const Settings& s) override;
  void wake(const Settings& s) override { needRender_ = true; }   // repaint only

  void nextPage();
  void home();
  void setPage(uint8_t page);
  // A long press: on the Plants screen it waters what is due (and saves);
  // returns false elsewhere, where a long press goes home.
  bool longPress(Settings& s);
  void showWatered(uint8_t mask);   // splash after watering from the web UI
  uint8_t page() const { return page_; }

 private:
  uint8_t  page_ = 0;
  uint32_t renderedSeq_ = 0xFFFFFFFF;
  int      renderedMin_ = -1;
  uint32_t renderedAtMs_ = 0;
  uint32_t animStep_ = 0;
  uint32_t animAtMs_ = 0;
  bool     needRender_ = true;
  Arduino_Canvas* astro_ = nullptr;   // off-screen buffer for the astronaut corner
  uint8_t  wateredMask_ = 0;          // plants in the watering splash
  uint32_t wateredAtMs_ = 0;
};

extern CommuteMode g_commuteMode;
