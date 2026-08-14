#include "WString.h"

// Host fake for the fork's wifi.cpp default_hostname(): the real one derives the
// name from the eFuse MAC, unavailable off-device. Fixed so tests can assert on it.
String default_hostname() {
  return String("battery-emulator-test");
}
