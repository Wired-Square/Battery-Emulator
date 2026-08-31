#include <gtest/gtest.h>

#include "../Software/src/communication/contactorcontrol/comm_contactorcontrol.h"
#include "../Software/src/devboard/hal/hal.h"
#include "../Software/src/devboard/hal/hw_lilygo.h"
#include "../Software/src/devboard/utils/events.h"
#include "../Software/src/lib/vn9dxxxxxx/vn9dxxxxxx.h"

#include "Arduino.h"

namespace {

std::vector<uint8_t> make_response(uint8_t status, uint16_t data) {
  int parity = vn9dx_calculate_parity(((uint32_t)status << 16) | data);
  data = (uint16_t)((data & ~1u) | parity);
  return {status, (uint8_t)(data >> 8), (uint8_t)data};
}

// Init consumes: CTRL read, UNLOCK, EN, SOCR feed, ROM PC3, CHLOFFTCR0/1,
// then per channel an OUTCFGR read and (divisor differs from reset) an
// UNLOCK+OUTCFGR write pair.
void script_init_responses(uint8_t pc3) {
  for (int i = 0; i < 4; i++) {
    SPIClass::rx_queue.push_back(make_response(0, 0));
  }
  SPIClass::rx_queue.push_back({0, pc3, 0});  // READ_ROM: parity not checked
  // Remaining frames use the emul's parity-valid default response.
}

}  // namespace

class Vn9dTest : public testing::Test {
 public:
  void SetUp() override {
    esp32hal = new LilyGoHal();
    init_events();
    gpio_events.clear();
    gpio_levels = {};
    SPIClass::frames.clear();
    SPIClass::rx_queue.clear();
    pwm_contactor_control = false;
    device = new Vn9d(FSPI, {GPIO_NUM_6, GPIO_NUM_2, GPIO_NUM_7, GPIO_NUM_5}, GPIO_NUM_0, 0);
  }
  Vn9d* device = nullptr;
};

TEST_F(Vn9dTest, ParityVectorsMatchDatasheetExamples) {
  // Odd parity over bits 23..1.
  EXPECT_EQ(vn9dx_calculate_parity(0x540000), 0);  // READ CTRL
  EXPECT_EQ(vn9dx_calculate_parity(0x144000), 0);  // CTRL UNLOCK
  EXPECT_EQ(vn9dx_calculate_parity(0x130002), 1);  // SOCR, WDTB set
  EXPECT_EQ(vn9dx_calculate_parity(0x000000), 1);
  EXPECT_EQ(vn9dx_calculate_parity(0x000001), 1);  // bit 0 excluded
}

TEST_F(Vn9dTest, InitSendsFailSafeExitFeedAndDetectsDevice) {
  script_init_responses(PC3_VN9D5D20FN);
  ASSERT_TRUE(device->init());
  EXPECT_EQ(device->status().channel_count, 4);
  EXPECT_TRUE(device->status().device_ok);

  ASSERT_GE(SPIClass::frames.size(), 7u);
  EXPECT_EQ(SPIClass::frames[0], (std::vector<uint8_t>{0x54, 0x00, 0x00}));  // READ CTRL
  EXPECT_EQ(SPIClass::frames[1], (std::vector<uint8_t>{0x14, 0x40, 0x00}));  // UNLOCK, EN=0
  EXPECT_EQ(SPIClass::frames[2], (std::vector<uint8_t>{0x14, 0x08, 0x00}));  // EN=1
  EXPECT_EQ(SPIClass::frames[3], (std::vector<uint8_t>{0x13, 0x00, 0x03}));  // SOCR feed, WDTB=1
  EXPECT_EQ(SPIClass::frames[4][0], 0xC4);                                   // READ_ROM PC3
  EXPECT_EQ(SPIClass::frames[5][0], 0x10);                                   // CHLOFFTCR0 = 0
  EXPECT_EQ(SPIClass::frames[6][0], 0x11);                                   // CHLOFFTCR1 = 0
}

TEST_F(Vn9dTest, InitProgramsDefaultDivisorViaProtectedWrite) {
  script_init_responses(PC3_VN9D5D20FN);
  ASSERT_TRUE(device->init());
  // Default LSDIV=0 → PWMFCY=3 (÷512); chip reset reads back 0 (÷1024), so
  // every channel gets UNLOCK+EN then OUTCFGR with PWMFCY bits = 0x30.
  bool found_pair = false;
  for (size_t i = 0; i + 1 < SPIClass::frames.size(); i++) {
    if (SPIClass::frames[i][0] == 0x14 && SPIClass::frames[i][1] == 0x48 &&
        SPIClass::frames[i + 1][0] == VN9DX_RAM_REG_OUTCFGR0) {
      EXPECT_EQ(SPIClass::frames[i + 1][2] & 0x30, 0x30);
      found_pair = true;
      break;
    }
  }
  EXPECT_TRUE(found_pair);
}

TEST_F(Vn9dTest, UnknownDeviceFailsInitAndDisablesTick) {
  script_init_responses(0x42);
  EXPECT_FALSE(device->init());
  EXPECT_FALSE(device->status().device_ok);
  EXPECT_EQ(get_event_pointer(EVENT_LOAD_SWITCH_INIT_FAILURE)->state, EVENT_STATE_ACTIVE);
  SPIClass::frames.clear();
  device->tick();
  EXPECT_TRUE(SPIClass::frames.empty());
}

TEST_F(Vn9dTest, WdtbTogglesOnlyOnSocrAndOutctrcrWrites) {
  script_init_responses(PC3_VN9D5D20FN);
  ASSERT_TRUE(device->init());
  SPIClass::frames.clear();

  device->engage(0);  // OUTCTRCR0 write + SOCR write: both carry WDTB
  ASSERT_EQ(SPIClass::frames.size(), 2u);
  uint8_t wdtb_first = SPIClass::frames[0][2] & 0x02;
  uint8_t wdtb_second = SPIClass::frames[1][2] & 0x02;
  EXPECT_NE(wdtb_first, wdtb_second);

  SPIClass::frames.clear();
  device->request_divisor(0, 1);
  device->tick();
  // The UNLOCK (CTRL) and OUTCFGR frames never carry WDTB.
  for (const auto& frame : SPIClass::frames) {
    if (frame[0] == VN9DX_RAM_REG_CTRL || frame[0] == VN9DX_RAM_REG_OUTCFGR0) {
      EXPECT_EQ(frame[2] & 0x02, 0);
    }
  }
}

TEST_F(Vn9dTest, EngageHoldDisengageDriveDutyAndSocr) {
  script_init_responses(PC3_VN9D5D20FN);
  device->set_channel_role(0, LoadSwitchRole::PositiveContactor);
  device->request_duty(0, 300);
  ASSERT_TRUE(device->init());
  device->tick();
  SPIClass::frames.clear();

  device->engage(0);
  // OUTCTRCR0: duty 1023 << 4 = 0x3FF0 (plus WDTB/parity in low bits).
  EXPECT_EQ(SPIClass::frames[0][0], 0x00);
  EXPECT_EQ(SPIClass::frames[0][1], 0x3F);
  EXPECT_EQ(SPIClass::frames[0][2] & 0xF0, 0xF0);
  // SOCR: channel 0 enabled.
  EXPECT_EQ(SPIClass::frames[1][0], 0x13);
  EXPECT_EQ(SPIClass::frames[1][1], 0x01);
  EXPECT_TRUE(device->status().channels[0].on);

  SPIClass::frames.clear();
  pwm_contactor_control = true;
  device->hold(0);
  // Steady duty 300 << 4 = 0x12C0.
  EXPECT_EQ(SPIClass::frames[0][1], 0x12);
  EXPECT_EQ(SPIClass::frames[0][2] & 0xF0, 0xC0);

  SPIClass::frames.clear();
  pwm_contactor_control = false;
  device->hold(0);  // without PWM control hold re-asserts full duty
  EXPECT_EQ(SPIClass::frames[0][1], 0x3F);

  device->disengage(0);
  EXPECT_FALSE(device->status().channels[0].on);
  EXPECT_EQ(device->status().channels[0].duty, 0);
}

TEST_F(Vn9dTest, TickFeedsWatchdogReadsTelemetryAndDecodesFaults) {
  script_init_responses(PC3_VN9D5D20FN);
  device->set_channel_role(0, LoadSwitchRole::Manual);
  ASSERT_TRUE(device->init());
  device->request_manual(0, true);
  SPIClass::frames.clear();

  // Tick frame order: SOCR feed, (pending: OUTCTRCR0+SOCR), ADC0-3, ADC9, OUTSR0-3.
  SPIClass::rx_queue.push_back(make_response(0, 0));                    // SOCR feed
  SPIClass::rx_queue.push_back(make_response(0, 0));                    // OUTCTRCR0
  SPIClass::rx_queue.push_back(make_response(0, 0));                    // SOCR
  SPIClass::rx_queue.push_back(make_response(0, (uint16_t)(100 << 4))); // ADC0: raw 100
  for (int i = 0; i < 3; i++) {
    SPIClass::rx_queue.push_back(make_response(0, 0));                  // ADC1-3
  }
  SPIClass::rx_queue.push_back(make_response(0, (uint16_t)(350 << 4))); // ADC9: raw 350
  SPIClass::rx_queue.push_back(make_response(0, (uint16_t)(1u << VN9DX_OUTSR_CHFBSR_BIT)));  // OUTSR0
  device->tick();

  EXPECT_EQ(SPIClass::frames[0][0], 0x13);
  EXPECT_TRUE(device->status().channels[0].on);
  // raw 100 on channel 0: 100 * 1000 / 36 = 2777 mA.
  EXPECT_EQ(device->status().channels[0].current_mA, 2777u);
  // raw 350: 4018 - ((66127*350)>>16)*10 = 488 (48.8 °C).
  EXPECT_EQ(device->status().frame_temperature_dC, 488);
  EXPECT_TRUE(device->status().channels[0].fault);
  EXPECT_FALSE(device->status().channels[1].fault);
}

TEST_F(Vn9dTest, FailSafeReexitAfterWatchdogLapse) {
  script_init_responses(PC3_VN9D5D20FN);
  ASSERT_TRUE(device->init());
  SPIClass::frames.clear();
  // SOCR response reports FS set (GSB bit 0).
  SPIClass::rx_queue.push_back(make_response(0x01, 0));
  device->tick();
  ASSERT_GE(SPIClass::frames.size(), 3u);
  EXPECT_EQ(SPIClass::frames[1], (std::vector<uint8_t>{0x14, 0x40, 0x00}));
  EXPECT_EQ(SPIClass::frames[2], (std::vector<uint8_t>{0x14, 0x08, 0x00}));
}

TEST_F(Vn9dTest, ManualRequestIgnoredOnNonManualChannelAndBootsOff) {
  script_init_responses(PC3_VN9D5D20FN);
  device->set_channel_role(0, LoadSwitchRole::PositiveContactor);
  ASSERT_TRUE(device->init());
  EXPECT_FALSE(device->status().channels[0].on);  // boot OFF
  device->request_manual(0, true);
  device->tick();
  EXPECT_FALSE(device->status().channels[0].on);
  EXPECT_FALSE(device->status().channels[0].pending);
}

TEST_F(Vn9dTest, DuplicateContactorRoleDemotedWithEvent) {
  script_init_responses(PC3_VN9D5D20FN);
  device->set_channel_role(0, LoadSwitchRole::Precharge);
  device->set_channel_role(2, LoadSwitchRole::Precharge);
  ASSERT_TRUE(device->init());
  EXPECT_EQ(device->channel_role(0), LoadSwitchRole::Precharge);
  EXPECT_EQ(device->channel_role(2), LoadSwitchRole::Disabled);
  EXPECT_EQ(get_event_pointer(EVENT_LOAD_SWITCH_ROLE_CONFLICT)->state, EVENT_STATE_ACTIVE);
  EXPECT_EQ(get_event_pointer(EVENT_LOAD_SWITCH_ROLE_CONFLICT)->data, 2);
}

TEST_F(Vn9dTest, Vn9dOutputMapsSwitchedOutputSemantics) {
  script_init_responses(PC3_VN9D5D20FN);
  device->set_channel_role(1, LoadSwitchRole::NegativeContactor);
  device->request_duty(1, 250);
  ASSERT_TRUE(device->init());
  device->tick();
  Vn9dOutput output(*device, 1);
  EXPECT_TRUE(output.init("Contactors"));
  output.set(true);
  EXPECT_TRUE(output.level());
  EXPECT_EQ(device->status().channels[1].duty, kLoadSwitchDutyMax);
  pwm_contactor_control = true;
  output.set_hold();
  EXPECT_EQ(device->status().channels[1].duty, 250);
  output.set(false);
  EXPECT_FALSE(output.level());
  EXPECT_FALSE(output.fault());
}

TEST_F(Vn9dTest, VccUndervoltageDecodedFromOutsrAndClears) {
  script_init_responses(PC3_VN9D5D20FN);
  ASSERT_TRUE(device->init());

  SPIClass::rx_queue.push_back(make_response(0, 0));  // SOCR feed
  for (int i = 0; i < 5; i++) {
    SPIClass::rx_queue.push_back(make_response(0, 0));  // ADC0-3, ADC9
  }
  SPIClass::rx_queue.push_back(make_response(0, (uint16_t)(1u << VN9DX_OUTSR_VCCUV_BIT)));  // OUTSR0
  device->tick();
  EXPECT_TRUE(device->status().vcc_undervoltage);

  device->tick();  // all OUTSR read clean via the parity-valid default
  EXPECT_FALSE(device->status().vcc_undervoltage);
}

TEST_F(Vn9dTest, ManualRequestSurfacesPendingUntilTickApplies) {
  script_init_responses(PC3_VN9D5D20FN);
  device->set_channel_role(0, LoadSwitchRole::Manual);
  ASSERT_TRUE(device->init());
  EXPECT_FALSE(device->status().channels[0].pending);

  device->request_manual(0, true);
  // The request is marshalled to the core-loop tick, so the UI must be able to
  // say "asked for on, not there yet" rather than claim it succeeded.
  EXPECT_TRUE(device->status().channels[0].pending);
  EXPECT_TRUE(device->status().channels[0].pending_on);
  EXPECT_FALSE(device->status().channels[0].on);

  device->tick();
  EXPECT_FALSE(device->status().channels[0].pending);
  EXPECT_TRUE(device->status().channels[0].on);
}
