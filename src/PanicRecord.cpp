// PanicRecord.cpp — keeps where an ESP32 panic happened across the reboot, so
// the crash screen and /api/status can show it (Pro build only).
//
// The Arduino core calls our hook from its panic handler with the faulting PC
// and a backtrace. The hook copies the PC and the first callers into RTC memory,
// which a reset does not clear. Decode them on the Mac with:
//   ~/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32-elf-addr2line \
//     -pfiaC -e .pio/build/smalltv_esp32_8mb/firmware.elf <addresses>
#if defined(SMALLTV_ESP32_PRO)
#include <Arduino.h>
#include <esp_attr.h>

static const uint32_t kPanicMagic = 0x50414E43;   // "PANC"
static const int kPanicDepth = 4;                 // PC + callers kept
static RTC_NOINIT_ATTR uint32_t s_magic, s_trace[kPanicDepth];

static void onPanic(arduino_panic_info_t* info, void*) {
  s_trace[0] = (uint32_t)info->pc;
  for (int i = 1; i < kPanicDepth; i++)
    s_trace[i] = (unsigned)i < info->backtrace_len ? info->backtrace[i] : 0;
  s_magic = kPanicMagic;
}

// Installed before setup() runs.
static struct PanicHook {
  PanicHook() { set_arduino_panic_handler(onPanic, nullptr); }
} s_hook;

// Read once at boot: true when the last reset was a panic we recorded. `out`
// gets the PC and the callers, space separated.
bool platformPanicTake(char* out, size_t n) {
  if (s_magic != kPanicMagic) return false;
  s_magic = 0;
  size_t o = 0;
  out[0] = 0;
  for (int i = 0; i < kPanicDepth && s_trace[i] && o + 12 < n; i++)
    o += snprintf(out + o, n - o, "%s0x%08x", i ? " " : "", (unsigned)s_trace[i]);
  return true;
}
#endif
