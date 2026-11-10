// CommuteStrip.h — draws a full screen in horizontal strips held in RAM.
//
// A whole 240x240 frame is 115 KB, more than the largest free block. A strip
// of rows is small: the page is drawn once per strip (pixels outside the strip
// are dropped), and each finished strip goes to the panel in one transfer. The
// panel never shows a half-drawn screen, so a redraw does not flash black.
#pragma once
#include <Arduino_GFX_Library.h>

class CommuteStrip : public Arduino_GFX {
 public:
  CommuteStrip(int16_t w, int16_t h, uint16_t* buf, int16_t rows)
      : Arduino_GFX(w, h), buf_(buf), rows_(rows) {}
  bool begin(int32_t) override { return true; }
  void setStrip(int16_t y0) { y0_ = y0; }

  void writePixelPreclipped(int16_t x, int16_t y, uint16_t c) override {
    y -= y0_;
    if (y >= 0 && y < rows_) buf_[y * _width + x] = c;
  }
  void writeFillRectPreclipped(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) override {
    int16_t top = y - y0_, bot = top + h;
    if (top < 0) top = 0;
    if (bot > rows_) bot = rows_;
    for (int16_t r = top; r < bot; r++) {
      uint16_t* p = buf_ + r * _width + x;
      for (int16_t i = 0; i < w; i++) p[i] = c;
    }
  }
  void writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) override {
    if (y < 0 || y >= _height || w <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > _width) w = _width - x;
    if (w > 0) writeFillRectPreclipped(x, y, w, 1, c);
  }

 private:
  uint16_t* buf_;
  int16_t   rows_;
  int16_t   y0_ = 0;
};
