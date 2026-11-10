#include "CommutePlants.h"

bool plantUsed(const PlantSettings& p) { return p.name.length() > 0 && p.everyDays > 0; }

// Local calendar day number: noon of that date / 86400, so DST shifts don't matter.
static long localDay(time_t t) {
  struct tm l;
  localtime_r(&t, &l);
  l.tm_hour = 12; l.tm_min = 0; l.tm_sec = 0; l.tm_isdst = -1;
  return (long)(mktime(&l) / 86400L);
}

int plantDueIn(const PlantSettings& p, time_t now) {
  if (!p.wateredAt || now <= (time_t)p.wateredAt) return p.wateredAt ? p.everyDays : 0;
  long days = localDay(now) - localDay((time_t)p.wateredAt);
  return (int)p.everyDays - (int)days;
}

uint8_t plantsWater(CommuteSettings& c, int i, time_t now) {
  if (now < 1700000000) return 0;   // the clock is not set yet
  uint8_t mask = 0;
  for (uint8_t k = 0; k < MAX_PLANTS; k++) {
    PlantSettings& p = c.plants[k];
    if (!plantUsed(p)) continue;
    if (i >= 0 ? k == i : plantDueIn(p, now) <= 1) {
      p.wateredAt = (uint32_t)now;
      mask |= 1 << k;
    }
  }
  return mask;
}

uint8_t plantsDueCount(const CommuteSettings& c, time_t now, int* first) {
  uint8_t n = 0;
  if (first) *first = -1;
  if (now < 1700000000) return 0;   // the clock is not set yet
  for (uint8_t k = 0; k < MAX_PLANTS; k++) {
    if (!plantUsed(c.plants[k]) || plantDueIn(c.plants[k], now) > 0) continue;
    if (first && *first < 0) *first = k;
    n++;
  }
  return n;
}

bool plantsNeedWater(const CommuteSettings& c, time_t now) { return plantsDueCount(c, now, nullptr) > 0; }
