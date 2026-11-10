#include "CommuteClient.h"
#include "Platform.h"
#include "Clock.h"
#include <ArduinoJson.h>
#include <memory>

static Weather     g_wx = {};
static Trip        g_trip[MAX_TRIPS];
static uint8_t     g_tripCount = 0;
static uint32_t    g_seq = 0;
static bool        g_depError = false;
static const char* g_depStatus = "idle";
static uint32_t    g_nextWxMs = 0;
static uint32_t    g_nextDepMs = 0;
static uint32_t    g_wxOkMs = 0;
static uint32_t    g_tripOkMs = 0;

// Diagnostics of the last trip fetch (GET /api/commute).
static int         g_lastHttp = 0;
static const char* g_lastParse = "";     // ArduinoJson error, "" = none
static uint32_t    g_lastFetchMs = 0;    // how long the last trip fetch took
static uint8_t     g_lastResults = 0;    // journeys in the last response

// Västtrafik bearer token (~1.6 KB), valid 24 h. Kept in RAM only.
static String   g_token;
static uint32_t g_tokenUntilMs = 0;

const Weather&   commuteWeather()        { return g_wx; }
uint8_t          commuteTripCount()      { return g_tripCount; }
const Trip&      commuteTripAt(uint8_t i) { return g_trip[i]; }
uint32_t         commuteChangeSeq()      { return g_seq; }
bool             commuteTripError()      { return g_depError; }
const char*      commuteTripStatus()     { return g_depStatus; }
uint32_t         commuteWeatherOkMs()    { return g_wxOkMs; }
uint32_t         commuteTripOkMs()       { return g_tripOkMs; }

void commuteInit() {
  g_tripCount = 0;
  g_depError = false;
  g_depStatus = "idle";
  g_nextWxMs = g_nextDepMs = millis();
  g_token = "";            // the key may have changed
  g_seq++;
}

void commuteForceRefresh() { g_nextWxMs = g_nextDepMs = millis(); }

// ---- parsing helpers ---------------------------------------------------------
// Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm).
static long daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  long yoe = y - era * 400;
  long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

time_t commuteParseTime(const char* iso) {
  int Y, M, D, h, m, sec;
  if (!iso || sscanf(iso, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &sec) != 6) return 0;
  long t = daysFromCivil(Y, M, D) * 86400L + h * 3600L + m * 60L + sec;
  // The offset follows the seconds and any fraction: "+01:00", "-05:00" or "Z".
  const char* p = iso + 19;
  while (*p == '.' || (*p >= '0' && *p <= '9')) p++;
  if (*p == '+' || *p == '-') {
    int oh = 0, om = 0;
    sscanf(p + 1, "%d:%d", &oh, &om);
    long off = oh * 3600L + om * 60L;
    t += (*p == '+') ? -off : off;
  }
  return (time_t)t;
}

int commuteWeekday(const char* date) {
  int Y, M, D;
  if (!date || sscanf(date, "%d-%d-%d", &Y, &M, &D) != 3) return 0;
  long days = daysFromCivil(Y, M, D);
  return (int)(((days % 7) + 11) % 7);   // 1970-01-01 was a Thursday (4)
}

uint16_t commuteParseColor(const char* hex, uint16_t fallback) {
  if (!hex || hex[0] != '#' || strlen(hex) != 7) return fallback;
  char* end = nullptr;
  unsigned long v = strtoul(hex + 1, &end, 16);
  if (!end || *end) return fallback;
  uint8_t r = v >> 16, g = (v >> 8) & 0xFF, b = v & 0xFF;
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// ---- buffered reads ------------------------------------------------------------
// ArduinoJson pulls one byte at a time; on a TLS client every one of those is a
// separate decrypt-and-copy call, which made a 40-80 KB journey reply slow
// enough to time out. This reads whatever has arrived, up to 512 bytes, at once.
class ChunkReader : public Stream {
 public:
  ChunkReader(Client& c, uint32_t timeoutMs) : c_(c), timeout_(timeoutMs) {}
  int available() override { return (n_ - i_) + c_.available(); }
  int read() override { return (i_ < n_ || fill()) ? buf_[i_++] : -1; }
  int peek() override { return (i_ < n_ || fill()) ? buf_[i_] : -1; }
  size_t write(uint8_t) override { return 0; }

 private:
  bool fill() {
    uint32_t start = millis();
    for (;;) {
      int a = c_.available();
      if (a > 0) {
        int got = c_.read(buf_, a < (int)sizeof(buf_) ? a : (int)sizeof(buf_));
        if (got > 0) { n_ = got; i_ = 0; return true; }
      }
      if (!c_.connected() && c_.available() <= 0) return false;
      if (millis() - start > timeout_) return false;
      delay(1);
    }
  }
  Client&  c_;
  uint32_t timeout_;
  uint8_t  buf_[512];
  int      n_ = 0, i_ = 0;
};

// ---- one HTTPS request, body handed to `parse` as a stream -----------------
// POST when `body` is set. Returns the HTTP status, or a negative local error.
template <typename F>
static int httpsRequest(const Settings& s, const String& url, const char* auth,
                        const char* body, F parse) {
  if (platformMaxFreeBlock() < 16000) return -100;
  std::unique_ptr<NetClient> client(platformMakeSecureClient(4096));
  HTTPClient http;
  http.useHTTP10(true);   // 1.0: never chunked, so the stream is plain JSON
  http.setTimeout(s.httpTimeout);
  http.setReuse(false);
  if (!http.begin(*client, url)) return -101;
  http.addHeader("Accept", "application/json");
  if (auth) http.addHeader("Authorization", auth);
  http.setUserAgent(F("smalltv-mod"));
  int code;
  if (body) {
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    code = http.POST(body);
  } else {
    code = http.GET();
  }
  if (code == HTTP_CODE_OK) {
    ChunkReader body(http.getStream(), s.httpTimeout);
    if (!parse(body)) code = -102;
  }
  http.end();
  return code;
}

// ---- weather (Open-Meteo, no key) --------------------------------------------
static int8_t r8(float v) { return (int8_t)constrain(lroundf(v), -99, 99); }

static void fetchWeather(const Settings& s) {
  String url = F("https://" OPENMETEO_HOST "/v1/forecast?latitude=");
  url += String(s.clock.lat, 3);
  url += F("&longitude=");
  url += String(s.clock.lon, 3);
  url += F("&current=temperature_2m,apparent_temperature,relative_humidity_2m,wind_speed_10m,"
           "weather_code,is_day"
           "&hourly=temperature_2m,weather_code,precipitation_probability,is_day&forecast_hours=");
  url += COMMUTE_HOURS;
  url += F("&daily=weather_code,temperature_2m_max,temperature_2m_min,"
           "precipitation_probability_max,sunrise,sunset&forecast_days=");
  url += COMMUTE_DAYS;
  url += F("&timezone=auto&wind_speed_unit=ms");

  int code = httpsRequest(s, url, nullptr, nullptr, [](Stream& st) {
    JsonDocument doc;   // ~2 KB of JSON, no filter needed
    if (deserializeJson(doc, st)) return false;
    JsonObjectConst cur = doc["current"];
    if (cur.isNull() || cur["temperature_2m"].isNull()) return false;
    Weather w = {};
    w.temp     = cur["temperature_2m"].as<float>();
    w.feels    = cur["apparent_temperature"] | w.temp;
    w.humidity = cur["relative_humidity_2m"] | 0;
    w.wind     = cur["wind_speed_10m"] | 0.0f;
    w.code     = cur["weather_code"] | -1;
    w.isDay    = (cur["is_day"] | 1) != 0;

    JsonObjectConst h = doc["hourly"];
    JsonArrayConst ht = h["time"];
    for (size_t i = 0; i < ht.size() && w.hourCount < COMMUTE_HOURS; i++) {
      HourWx& o = w.hours[w.hourCount++];
      const char* t = ht[i] | "";
      o.hour  = strlen(t) >= 13 ? (int8_t)atoi(t + 11) : 0;
      o.code  = h["weather_code"][i] | -1;
      o.temp  = r8(h["temperature_2m"][i] | 0.0f);
      o.rain  = h["precipitation_probability"][i] | 0;
      o.isDay = (h["is_day"][i] | 1) != 0;
    }

    JsonObjectConst d = doc["daily"];
    JsonArrayConst dt = d["time"];
    for (size_t i = 0; i < dt.size() && w.dayCount < COMMUTE_DAYS; i++) {
      DayWx& o = w.days[w.dayCount++];
      const char* t = dt[i] | "";
      o.wday = (int8_t)commuteWeekday(t);
      o.mday = strlen(t) >= 10 ? (int8_t)atoi(t + 8) : 0;
      o.code = d["weather_code"][i] | -1;
      o.tMax = r8(d["temperature_2m_max"][i] | 0.0f);
      o.tMin = r8(d["temperature_2m_min"][i] | 0.0f);
      o.rain = d["precipitation_probability_max"][i] | -1;
    }
    const char* sr = d["sunrise"][0] | "";
    const char* ss = d["sunset"][0] | "";
    if (strlen(sr) >= 16) strlcpy(w.sunrise, sr + 11, sizeof(w.sunrise));
    if (strlen(ss) >= 16) strlcpy(w.sunset, ss + 11, sizeof(w.sunset));
    w.valid = true;
    g_wx = w;
    return true;
  });
  if (code == HTTP_CODE_OK) { g_wxOkMs = millis(); g_seq++; }
}

// ---- Västtrafik token (OAuth client credentials) ------------------------------
static int fetchToken(const Settings& s) {
  String auth = F("Basic ");
  auth += s.commute.authKey;
  uint32_t expiresSec = 0;
  int code = httpsRequest(s, F("https://" VT_HOST "/token"), auth.c_str(),
                          "grant_type=client_credentials", [&expiresSec](Stream& st) {
    JsonDocument filter;
    filter["access_token"] = true;
    filter["expires_in"] = true;
    JsonDocument doc;
    if (deserializeJson(doc, st, DeserializationOption::Filter(filter))) return false;
    const char* tok = doc["access_token"] | "";
    if (!tok[0]) return false;
    g_token = F("Bearer ");
    g_token += tok;
    expiresSec = doc["expires_in"] | 3600;
    return true;
  });
  if (code == HTTP_CODE_OK) {
    // Renew an hour early (or halfway, for short-lived tokens).
    uint32_t keep = expiresSec > 7200 ? expiresSec - 3600 : expiresSec / 2;
    g_tokenUntilMs = millis() + keep * 1000UL;
  }
  return code;
}

// ---- trips (journey planner) -------------------------------------------------
// Keep a trip only if no other trip leaves no earlier and arrives no later
// (and is better in one of the two): waiting for that one is never worse.
static void keepUseful(const Trip& t) {
  for (uint8_t i = 0; i < g_tripCount; i++) {
    const Trip& o = g_trip[i];
    if (o.when >= t.when && o.arrive <= t.arrive) return;   // t is dominated
  }
  uint8_t w = 0;                                            // drop what t dominates
  for (uint8_t i = 0; i < g_tripCount; i++)
    if (!(t.when >= g_trip[i].when && t.arrive <= g_trip[i].arrive)) g_trip[w++] = g_trip[i];
  g_tripCount = w;
  if (g_tripCount == MAX_TRIPS) return;
  uint8_t i = g_tripCount++;                                // insert, sorted by departure
  while (i > 0 && g_trip[i - 1].when > t.when) { g_trip[i] = g_trip[i - 1]; i--; }
  g_trip[i] = t;
}

static int fetchTripList(const Settings& s) {
  const CommuteSettings& c = s.commute;
  String url = F("https://" VT_HOST "/pr/v4/journeys?originGid=");
  url += c.stopGid;
  url += F("&destinationGid=");
  url += c.toGid;
  url += F("&limit=");
  url += COMMUTE_TRIP_LIMIT;

  return httpsRequest(s, url, g_token.c_str(), nullptr, [](Stream& st) {
    JsonDocument filter;
    JsonObject fl = filter["results"][0]["tripLegs"][0].to<JsonObject>();
    fl["estimatedDepartureTime"] = true;
    fl["plannedDepartureTime"] = true;
    fl["origin"]["stopPoint"]["name"] = true;
    fl["estimatedOtherwisePlannedDepartureTime"] = true;
    fl["estimatedOtherwisePlannedArrivalTime"] = true;
    fl["isCancelled"] = true;
    fl["serviceJourney"]["line"]["shortName"] = true;
    fl["serviceJourney"]["line"]["backgroundColor"] = true;
    fl["serviceJourney"]["line"]["foregroundColor"] = true;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, st, DeserializationOption::Filter(filter));
    g_lastParse = err ? err.c_str() : "";
    if (err) return false;
    JsonArrayConst arr = doc["results"];
    if (arr.isNull()) { g_lastParse = "no results array"; return false; }
    g_lastResults = arr.size();

    g_tripCount = 0;
    for (JsonObjectConst r : arr) {
      JsonArrayConst legs = r["tripLegs"];
      if (legs.isNull() || legs.size() == 0) continue;   // walk-only "trip"
      Trip t = {};
      JsonObjectConst first = legs[0];
      JsonObjectConst last = legs[legs.size() - 1];
      t.when = commuteParseTime(first["estimatedOtherwisePlannedDepartureTime"] | "");
      t.arrive = commuteParseTime(last["estimatedOtherwisePlannedArrivalTime"] | "");
      if (!t.when || !t.arrive) continue;
      t.realtime = !first["estimatedDepartureTime"].isNull();
      for (JsonObjectConst L : legs) {
        if (L["isCancelled"] | false) t.canceled = true;
        if (t.legCount == MAX_TRIP_LEGS) continue;
        TripLeg& out = t.legs[t.legCount++];
        out.dep = commuteParseTime(L["estimatedOtherwisePlannedDepartureTime"] | "");
        time_t planned = commuteParseTime(L["plannedDepartureTime"] | "");
        out.delayMin = (out.dep && planned) ? (int8_t)constrain((out.dep - planned) / 60, -99, 99) : 0;
        // "Järntorget, Göteborg" -> "Järntorget"
        strlcpy(out.from, L["origin"]["stopPoint"]["name"] | "", sizeof(out.from));
        char* comma = strchr(out.from, ',');
        if (comma) *comma = 0;
        JsonObjectConst line = L["serviceJourney"]["line"];
        strlcpy(out.line, line["shortName"] | "?", sizeof(out.line));
        out.bg = commuteParseColor(line["backgroundColor"] | "", 0x041F);
        out.fg = commuteParseColor(line["foregroundColor"] | "", 0xFFFF);
      }
      keepUseful(t);
    }
    return true;
  });
}

static void fetchTrips(const Settings& s) {
  uint32_t started = millis();
  int code = 200;
  if (!g_token.length() || (int32_t)(millis() - g_tokenUntilMs) >= 0) code = fetchToken(s);
  if (code == HTTP_CODE_OK) {
    code = fetchTripList(s);
    if (code == 401) {                     // token revoked or expired early: once more
      code = fetchToken(s);
      if (code == HTTP_CODE_OK) code = fetchTripList(s);
    }
  } else {
    g_token = "";
    if (code == 400 || code == 401) code = 4010;   // the token call itself said no
  }

  g_lastHttp = code;
  g_lastFetchMs = millis() - started;
  g_depError = (code != HTTP_CODE_OK);
  if (code == HTTP_CODE_OK) g_tripOkMs = millis();
  g_depStatus = (code == HTTP_CODE_OK) ? (g_tripCount ? "ok" : "No trips found")
              : (code == 4010) ? "Auth key rejected"
              : (code == 401)  ? "Token rejected"
              : (code == 403)  ? "API not subscribed"
              : (code == 404 || code == 400) ? "Check stop ids"
              : (code == 429)  ? "Rate limited"
              : (code == -100) ? "Low memory"
              : (code == -102) ? "Bad reply"
              : (code < 0)     ? "No connection" : "Server error";
  g_seq++;
}

// ---------------------------------------------------------------------------
void commuteService(const Settings& s) {
  uint32_t now = millis();
  bool hasLoc = s.clock.lat != 0.0f || s.clock.lon != 0.0f;

  if (hasLoc && (int32_t)(now - g_nextWxMs) >= 0) {
    g_nextWxMs = now + COMMUTE_WEATHER_SEC * 1000UL;
    fetchWeather(s);
    return;   // one fetch per loop pass keeps the UI responsive
  }

  if (!s.commute.authKey.length() || !s.commute.stopGid.length() || !s.commute.toGid.length()) return;
  // Trip times are absolute, so the countdown needs the clock first.
  if (!clockSynced()) return;
  if ((int32_t)(now - g_nextDepMs) < 0) return;
  uint16_t every = clockNightActive() ? COMMUTE_NIGHT_POLL_SEC : s.commute.pollSec;
  g_nextDepMs = now + (uint32_t)every * 1000UL;
  fetchTrips(s);
}

void commuteDiag(JsonObject o) {
  o["maxBlock"]    = ESP.getMaxAllocHeap();   // largest free block (screen buffer needs 115 KB)
  o["tripStatus"]  = g_depStatus;
  o["tripHttp"]    = g_lastHttp;
  o["tripParse"]   = g_lastParse;
  o["tripMs"]      = g_lastFetchMs;
  o["tripResults"] = g_lastResults;
  o["tripsKept"]   = g_tripCount;
  o["tokenHeld"]   = g_token.length() > 0;
  o["weatherOk"]   = g_wx.valid;
  o["weatherAgeS"] = g_wxOkMs ? (millis() - g_wxOkMs) / 1000 : -1;
}
