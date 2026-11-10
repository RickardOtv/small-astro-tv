// CommuteDraw.h — drawing helpers shared by the commute screens: text in the
// Inter GFX fonts (UTF-8 in, cursor y = baseline), weather icons, line badges.
#pragma once
#include <Arduino.h>
#include <time.h>
#include <gfxfont.h>   // GFXfont is an anonymous-struct typedef: no forward declaration
#include "CommuteClient.h"

class Arduino_GFX;

extern const GFXfont commuteTime;     // 68 px digits + ':'
extern const GFXfont commuteNum;      // 44 px digits + '-'
extern const GFXfont commuteBold18;
extern const GFXfont commuteBold16;
extern const GFXfont commuteReg16;
extern const GFXfont commuteMin;
extern const GFXfont commuteBold13;
extern const GFXfont commuteReg12;

uint16_t cdRgb(uint8_t r, uint8_t g, uint8_t b);   // tinted RGB565

int  cdTextW(Arduino_GFX* g, const GFXfont* font, const char* s);
void cdText(Arduino_GFX* g, const GFXfont* font, const char* s, int x, int baseline, uint16_t color);
void cdTextCentered(Arduino_GFX* g, const GFXfont* font, const char* s, int cx, int baseline,
                    uint16_t color);
void cdTextRight(Arduino_GFX* g, const GFXfont* font, const char* s, int right, int baseline,
                 uint16_t color);
// Copy `s` into `out`, shortened with ".." until it fits `maxW` pixels.
void cdFit(Arduino_GFX* g, const GFXfont* font, const char* s, int maxW, char* out, size_t n);

// Weather icon centred on (cx,cy); k = 1 is about 48x40 px.
void cdWeatherIcon(Arduino_GFX* g, int cx, int cy, int code, bool day, float k);
// Big number in the digit font with a drawn degree ring; returns the width used.
int  cdBigTemp(Arduino_GFX* g, int x, int baseline, int value);

// Line badges in Västtrafik's colours.
int  cdBadgeW(Arduino_GFX* g, const char* line, int h);
void cdBadge(Arduino_GFX* g, int x, int y, int h, const TripLeg& l);
// Badges of one trip with small arrows between; anchored left (x) or right.
int  cdLegChainW(Arduino_GFX* g, const Trip& t, int h);
void cdLegChain(Arduino_GFX* g, const Trip& t, int x, int y, int h);

void cdHHMM(time_t t, char* out, size_t n);   // local "23:41"

extern const char* const kCdDays[7];          // "Sunday" ...
extern const char* const kCdDaysShort[7];     // "Sun" ...
extern const char* const kCdMonths[12];
