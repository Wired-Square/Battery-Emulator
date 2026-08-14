#include <gtest/gtest.h>

#include "../Software/src/battery/BATTERIES.h"
#include "../Software/src/battery/BMW-SBOX.h"
#include "../Software/src/battery/Shunt.h"
#include "../Software/src/datalayer/datalayer.h"
#include "../Software/src/inverter/INVERTERS.h"
#include "../Software/src/devboard/hal/hal.h"
#include "../Software/src/devboard/hal/hw_lilygo.h"

namespace {

class ShuntSetupTest : public testing::Test {
 protected:
  void SetUp() override {
    init_hal();
    saved_shunt_ = shunt;
    saved_type_ = user_selected_shunt_type;
    saved_inverter_ = inverter;
    shunt = nullptr;  // setup_shunt early-returns when non-null; isolate each test
  }
  void TearDown() override {
    if (shunt) {
      delete shunt;
    }
    shunt = saved_shunt_;
    user_selected_shunt_type = saved_type_;
    inverter = saved_inverter_;
  }

 private:
  CanShunt* saved_shunt_;
  ShuntType saved_type_;
  InverterProtocol* saved_inverter_;
};

TEST_F(ShuntSetupTest, NamesAreGolden) {
  EXPECT_STREQ(name_for_shunt_type(ShuntType::None), "None");
  EXPECT_STREQ(name_for_shunt_type(ShuntType::BmwSbox), "BMW SBOX");
  EXPECT_STREQ(name_for_shunt_type(ShuntType::Inverter), "Using inverter values");
  EXPECT_STREQ(name_for_shunt_type(ShuntType::CustomClamp), "Custom Clamp");
}

TEST_F(ShuntSetupTest, NoneConstructsNothing) {
  user_selected_shunt_type = ShuntType::None;
  setup_shunt();
  EXPECT_EQ(shunt, nullptr);
}

TEST_F(ShuntSetupTest, CustomClampConstructsNoShunt) {
  user_selected_shunt_type = ShuntType::CustomClamp;
  setup_shunt();
  EXPECT_EQ(shunt, nullptr);
}

TEST_F(ShuntSetupTest, BmwSboxConstructsAndSetsProtocol) {
  user_selected_shunt_type = ShuntType::BmwSbox;
  setup_shunt();
  ASSERT_NE(shunt, nullptr);
  EXPECT_STREQ(datalayer.system.info.shunt_protocol, "BMW SBOX");
}

TEST_F(ShuntSetupTest, InverterSourceWithoutInverterIsSafe) {
  inverter = nullptr;
  user_selected_shunt_type = ShuntType::Inverter;
  setup_shunt();
  EXPECT_EQ(shunt, nullptr);
}

}  // namespace
