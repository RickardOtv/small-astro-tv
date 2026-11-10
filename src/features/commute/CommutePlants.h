// CommutePlants.h — the plant watering schedule behind the Plants screen.
//
// Up to MAX_PLANTS plants, each with a name, how often it wants water and
// when it last got some (CommuteSettings::plants, saved with the settings).
// Holding the button on the Plants screen waters every plant due by
// tomorrow; the web UI can water one at a time.
#pragma once
#include <Arduino.h>
#include <time.h>
#include "Settings.h"
#include "config.h"

// Whole days until the plant wants water: 2 = in two days, 0 = today,
// -1 = a day late. A plant never watered counts as due today.
int  plantDueIn(const PlantSettings& p, time_t now);
bool plantUsed(const PlantSettings& p);

// Waters plant i (or every plant due by tomorrow when i < 0). Returns a bit
// mask of the plants watered; the caller saves the settings.
uint8_t plantsWater(CommuteSettings& c, int i, time_t now);

// Something to show on Home: a plant is due today or late.
// How many plants are due today or overdue; *first gets the first one's slot.
uint8_t plantsDueCount(const CommuteSettings& c, time_t now, int* first);
bool plantsNeedWater(const CommuteSettings& c, time_t now);
