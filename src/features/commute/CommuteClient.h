// CommuteClient.h — weather (Open-Meteo) + trips (Västtrafik Planera Resa v4 journeys).
//
// Both are fetched on their own timers from service(); the screens read the
// cached results. Trip times are stored as absolute epoch seconds, so the
// "minutes left" countdown stays right between polls without refetching.
// A trip is the planner's best way from one stop to another, changes included:
// that keeps working when a line is diverted or replaced by buses.
#pragma once
#include <Arduino.h>
#include <time.h>
#include <ArduinoJson.h>
#include "Settings.h"
#include "config.h"

struct HourWx {
  int8_t  hour;       // local hour of day
  int16_t code;       // WMO weather code
  int8_t  temp;       // deg C
  int8_t  rain;       // precipitation probability %
  bool    isDay;
};

struct DayWx {
  int8_t  wday;       // 0 = Sunday
  int8_t  mday;
  int16_t code;
  int8_t  tMax, tMin;
  int8_t  rain;       // max precipitation probability %
};

struct Weather {
  bool     valid;
  float    temp, feels;   // current, deg C
  uint8_t  humidity;      // %
  float    wind;          // m/s
  int16_t  code;          // current WMO weather code
  bool     isDay;
  HourWx   hours[COMMUTE_HOURS];
  uint8_t  hourCount;
  DayWx    days[COMMUTE_DAYS];   // days[0] = today
  uint8_t  dayCount;
  char     sunrise[6], sunset[6];   // "07:43"
};

struct TripLeg {
  char     line[6];     // short name, e.g. "1", "6E"
  uint16_t bg, fg;      // line colours from Västtrafik, RGB565
  time_t   dep;         // leaves its first stop (estimate when known)
  int8_t   delayMin;    // estimate minus timetable, minutes
  char     from[24];    // where this ride starts, e.g. "Järntorget"
};

struct Trip {
  time_t  when;         // leaves the first stop (estimate when known), UTC epoch
  time_t  arrive;       // reaches the destination
  bool    realtime;     // `when` is a live estimate
  bool    canceled;     // any leg cancelled
  uint8_t legCount;     // rides (walks between them are not counted)
  TripLeg legs[MAX_TRIP_LEGS];
};

void commuteInit();
void commuteService(const Settings& s);   // call often; self-times both fetches
void commuteForceRefresh();

const Weather&   commuteWeather();
uint8_t          commuteTripCount();
const Trip&      commuteTripAt(uint8_t i);
uint32_t         commuteChangeSeq();      // bumps whenever cached data changes
bool             commuteTripError();      // last trip fetch failed
const char*      commuteTripStatus();     // short reason, shown on screen
uint32_t         commuteWeatherOkMs();    // millis() of the last good fetch, 0 = never
uint32_t         commuteTripOkMs();

// Last-fetch diagnostics for GET /api/commute.
void commuteDiag(JsonObject o);

// Parsing helpers, exposed for host-side tests.
time_t   commuteParseTime(const char* iso);   // "2026-11-09T22:53:00.0000000+01:00" -> epoch
uint16_t commuteParseColor(const char* hex, uint16_t fallback);   // "#006c93" -> RGB565
int      commuteWeekday(const char* date);    // "2026-11-09" -> 0..6, Sunday = 0
