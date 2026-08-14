#include <gtest/gtest.h>

#include <Wire.h>

#include "../Software/src/lib/tca9538/tca9538.h"

namespace {
constexpr uint8_t kTestAddress = 0x20;
}

class Tca9538Test : public testing::Test {
 public:
  void SetUp() override {
    Wire.transmissions.clear();
    Wire.fail_writes = false;
  }
  Tca9538 expander{Wire, kTestAddress};
};

TEST_F(Tca9538Test, BeginWritesOutputStateBeforePinDirections) {
  EXPECT_TRUE(expander.begin(0x03, 0x40));
  EXPECT_TRUE(expander.ready());
  ASSERT_EQ(Wire.transmissions.size(), 2u);
  EXPECT_EQ(Wire.transmissions[0].address, kTestAddress);
  EXPECT_EQ(Wire.transmissions[0].bytes, (std::vector<uint8_t>{TCA9538_REG_OUTPUT, 0x40}));
  EXPECT_EQ(Wire.transmissions[1].address, kTestAddress);
  EXPECT_EQ(Wire.transmissions[1].bytes, (std::vector<uint8_t>{TCA9538_REG_CONFIG, 0x03}));
}

TEST_F(Tca9538Test, BeginFailsWhenTheBusErrors) {
  Wire.fail_writes = true;
  EXPECT_FALSE(expander.begin(0x03, 0x00));
  EXPECT_FALSE(expander.ready());
}

TEST_F(Tca9538Test, WritePinSetsAndClearsOutputBits) {
  ASSERT_TRUE(expander.begin(0x00, 0x00));
  Wire.transmissions.clear();

  EXPECT_TRUE(expander.write_pin(2, true));
  ASSERT_EQ(Wire.transmissions.size(), 1u);
  EXPECT_EQ(Wire.transmissions[0].bytes, (std::vector<uint8_t>{TCA9538_REG_OUTPUT, 0x04}));

  EXPECT_TRUE(expander.write_pin(7, true));
  EXPECT_EQ(Wire.transmissions[1].bytes, (std::vector<uint8_t>{TCA9538_REG_OUTPUT, 0x84}));

  EXPECT_TRUE(expander.write_pin(2, false));
  EXPECT_EQ(Wire.transmissions[2].bytes, (std::vector<uint8_t>{TCA9538_REG_OUTPUT, 0x80}));
}

TEST_F(Tca9538Test, WritePinRejectsOutOfRangePins) {
  ASSERT_TRUE(expander.begin(0x00, 0x00));
  EXPECT_FALSE(expander.write_pin(TCA9538_PIN_COUNT, true));
}

TEST_F(Tca9538Test, FailedWritePinLeavesCacheUnchanged) {
  ASSERT_TRUE(expander.begin(0x00, 0x00));
  Wire.transmissions.clear();
  Wire.fail_writes = true;
  EXPECT_FALSE(expander.write_pin(2, true));
  Wire.fail_writes = false;
  EXPECT_TRUE(expander.write_pin(7, true));
  ASSERT_EQ(Wire.transmissions.size(), 1u);
  EXPECT_EQ(Wire.transmissions[0].bytes, (std::vector<uint8_t>{TCA9538_REG_OUTPUT, 0x80}));
}

TEST_F(Tca9538Test, WritePinFailsBeforeBegin) {
  EXPECT_FALSE(expander.write_pin(0, true));
  EXPECT_TRUE(Wire.transmissions.empty());
}
