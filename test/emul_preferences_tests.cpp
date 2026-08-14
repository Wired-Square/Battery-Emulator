#include <gtest/gtest.h>
#include "emul/Preferences.h"

TEST(EmulPreferences, RoundTripsTypedValuesAndHonoursIsKey) {
  Preferences p;
  p.begin("ns", false);
  EXPECT_FALSE(p.isKey("B"));
  p.putBool("B", true);
  p.putUInt("U", 42);
  p.putString("S", "hi");
  EXPECT_TRUE(p.isKey("B"));
  EXPECT_TRUE(p.getBool("B", false));
  EXPECT_EQ(p.getUInt("U", 0u), 42u);
  EXPECT_EQ(p.getString("S", String("def")), String("hi"));
  EXPECT_EQ(p.getUInt("absent", 7u), 7u);
  p.clear();
  EXPECT_FALSE(p.isKey("B"));
}
