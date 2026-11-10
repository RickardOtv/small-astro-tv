#include "CommutePlants.h"

bool plantUsed(const PlantSettings& p) { return p.name.length() > 0 && p.everyDays > 0; }

int plantDueIn(const PlantSettings& p, time_t now) {
  if (!p.wateredAt || now <= (time_t)p.wateredAt) return p.wateredAt ? p.everyDays : 0;
  long days = (long)(now - (time_t)p.wateredAt) / 86400L;
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

bool plantsNeedWater(const CommuteSettings& c, time_t now) {
  if (now < 1700000000) return false;
  for (uint8_t k = 0; k < MAX_PLANTS; k++)
    if (plantUsed(c.plants[k]) && plantDueIn(c.plants[k], now) <= 0) return true;
  return false;
}
