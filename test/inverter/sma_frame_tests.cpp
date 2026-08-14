#include <gtest/gtest.h>

#include <array>

#include "../../Software/src/datalayer/datalayer.h"
#include "../../Software/src/devboard/hal/hal.h"
#include "../../Software/src/devboard/utils/events.h"
#include "../../Software/src/inverter/CanInverterProtocol.h"
#include "../../Software/src/inverter/INVERTERS.h"
#include "../emul/can_recorder.h"

// Characterization goldens: literal wire bytes captured from the pre-fold SMA drivers.
// They lock byte-for-byte behaviour across the shared-logic hoist; never derive them anew.

namespace {

const CAN_frame* frame_by_id(uint32_t id) {
  const CAN_frame* found = nullptr;
  for (const auto& f : recorded_frames()) {
    if (f.ID == id) {
      found = &f;
    }
  }
  return found;
}

void expect_frame(uint32_t id, const std::array<uint8_t, 8>& want) {
  const CAN_frame* f = frame_by_id(id);
  ASSERT_NE(f, nullptr) << "frame 0x" << std::hex << id << " not emitted";
  for (int i = 0; i < 8; i++) {
    EXPECT_EQ(f->data.u8[i], want[i]) << "0x" << std::hex << id << " byte " << std::dec << i;
  }
}

// reported_current_dA and current_dA differ so SBS's distinct current field locks independently.
void set_normal_inputs() {
  datalayer.battery.combined.info.max_design_voltage_dV = 4000;
  datalayer.battery.combined.info.min_design_voltage_dV = 3000;
  datalayer.battery.combined.status.max_discharge_current_dA = 500;
  datalayer.battery.combined.status.max_charge_current_dA = 125;
  datalayer.battery.combined.status.reported_soc = 5000;
  datalayer.battery.combined.status.soh_pptt = 9900;
  datalayer.battery.combined.status.reported_remaining_capacity_Wh = 10000;
  datalayer.battery.combined.status.voltage_dV = 3600;
  datalayer.battery.combined.status.reported_current_dA = 150;
  datalayer.battery.combined.status.current_dA = -150;
  datalayer.battery.combined.status.temperature_max_dC = 250;
  datalayer.battery.combined.status.temperature_min_dC = 100;
  datalayer.battery.combined.status.cell_min_voltage_mV = 3300;
  datalayer.battery.combined.status.cell_max_voltage_mV = 3400;
  datalayer.battery.combined.status.total_charged_battery_Wh = 4552;
  datalayer.battery.combined.status.total_discharged_battery_Wh = 3828;
  datalayer.system.status.system_status = ACTIVE;
  datalayer.system.status.battery_allows_contactor_closing = true;
  datalayer.system.status.inverter_allows_contactor_closing = true;
  datalayer.system.status.contactors_engaged = 1;
}

class SmaFrameTest : public testing::Test {
 protected:
  void SetUp() override {
    init_hal();
    reset_recorded_frames();
    saved_inverter_ = inverter;
    saved_type_ = user_selected_inverter_protocol;
    inverter = nullptr;  // setup_inverter early-returns when non-null; isolate each test
  }
  void TearDown() override {
    if (inverter) {
      delete inverter;
    }
    inverter = saved_inverter_;
    user_selected_inverter_protocol = saved_type_;
  }

  // SmaInverterBase::setup() drops inverter_allows_contactor_closing, so datalayer
  // inputs must be applied AFTER this or the transmit paths stay gated off.
  CanInverterProtocol* setup_protocol(InverterProtocolType type) {
    user_selected_inverter_protocol = type;
    if (!setup_inverter()) {
      return nullptr;
    }
    return static_cast<CanInverterProtocol*>(inverter);
  }

 private:
  InverterProtocol* saved_inverter_ = nullptr;
  InverterProtocolType saved_type_{};
};

TEST_F(SmaFrameTest, BydH_Normal) {
  CanInverterProtocol* sma = setup_protocol(InverterProtocolType::SmaBydH);
  ASSERT_NE(sma, nullptr);
  set_normal_inputs();
  sma->update_values();
  sma->transmit_can(100);

  expect_frame(0x358, {0x0F, 0xA0, 0x0B, 0xB8, 0x01, 0xF4, 0x00, 0x7D});
  expect_frame(0x3D8, {0x13, 0x88, 0x26, 0xAC, 0x00, 0xC8, 0xF9, 0x00});
  expect_frame(0x4D8, {0x0E, 0x10, 0x00, 0x96, 0x00, 0xAF, 0x03, 0x08});
  expect_frame(0x518, {0x00, 0xFA, 0x00, 0x64, 0x0E, 0x10, 0x84, 0x88});
  expect_frame(0x458, {0x00, 0x00, 0x11, 0xC8, 0x00, 0x00, 0x0E, 0xF4});
  expect_frame(0x158, {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA});
}

TEST_F(SmaFrameTest, BydHvs_Normal) {
  CanInverterProtocol* sma = setup_protocol(InverterProtocolType::SmaBydHvs);
  ASSERT_NE(sma, nullptr);
  set_normal_inputs();
  sma->update_values();
  // HVS only queues its batch on a pairing request, then drains one frame per 250 ms tick.
  CAN_frame rx{};
  rx.ID = 0x5E7;
  sma->map_can_frame_to_variable(rx);
  for (int i = 1; i <= 12; i++) {
    sma->transmit_can(250 * i);
  }

  // HVS has no 0x158; 0x3D8[6,7] are its retained header init 0x00,0xFA.
  expect_frame(0x358, {0x0F, 0xA0, 0x0B, 0xB8, 0x01, 0xF4, 0x00, 0x7D});
  expect_frame(0x3D8, {0x13, 0x88, 0x26, 0xAC, 0x00, 0xC8, 0x00, 0xFA});
  expect_frame(0x4D8, {0x0E, 0x10, 0x00, 0x96, 0x00, 0xAF, 0x03, 0x08});
  expect_frame(0x518, {0x00, 0xFA, 0x00, 0x64, 0x0E, 0x10, 0x84, 0x88});
  expect_frame(0x458, {0x00, 0x00, 0x11, 0xC8, 0x00, 0x00, 0x0E, 0xF4});
}

TEST_F(SmaFrameTest, SbsByd_Normal) {
  CanInverterProtocol* sma = setup_protocol(InverterProtocolType::SmaSBSByd);
  ASSERT_NE(sma, nullptr);
  set_normal_inputs();
  sma->update_values();
  sma->transmit_can(100);

  // All SMA variants map the folded reported_current_dA (150 -> 0x00,0x96) into 0x4D8[2,3];
  // the fixture's differing current_dA (-150) would show as 0xFF,0x6A if a variant regressed
  // to the pack-0 mirror.
  expect_frame(0x358, {0x0F, 0xA0, 0x0B, 0xB8, 0x01, 0xF4, 0x00, 0x7D});
  expect_frame(0x3D8, {0x13, 0x88, 0x26, 0xAC, 0x00, 0xC8, 0xF9, 0x00});
  expect_frame(0x4D8, {0x0E, 0x10, 0x00, 0x96, 0x00, 0xAF, 0x03, 0x08});
  expect_frame(0x518, {0x00, 0xFA, 0x00, 0x64, 0x0E, 0x10, 0x84, 0x88});
  expect_frame(0x458, {0x00, 0x00, 0x11, 0xC8, 0x00, 0x00, 0x0E, 0xF4});
  expect_frame(0x158, {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA});
}

TEST_F(SmaFrameTest, BydH_Fault_And_ContactorDenied) {
  CanInverterProtocol* sma = setup_protocol(InverterProtocolType::SmaBydH);
  ASSERT_NE(sma, nullptr);
  set_normal_inputs();
  datalayer.system.status.system_status = FAULT;                     // 0x4D8[6] -> STOP_STATE
  datalayer.system.status.battery_allows_contactor_closing = false;  // 0x158[2] -> 0x6A
  sma->update_values();
  sma->transmit_can(100);

  const CAN_frame* f4d8 = frame_by_id(0x4D8);
  ASSERT_NE(f4d8, nullptr);
  EXPECT_EQ(f4d8->data.u8[6], 0x02);
  const CAN_frame* f158 = frame_by_id(0x158);
  ASSERT_NE(f158, nullptr);
  EXPECT_EQ(f158->data.u8[2], 0x6A);
}

// AH-remaining has a div0 guard: voltage_dV <= 10 keeps the previously computed value.
TEST_F(SmaFrameTest, BydH_AhRemainingRetainedWhenVoltageLow) {
  CanInverterProtocol* sma = setup_protocol(InverterProtocolType::SmaBydH);
  ASSERT_NE(sma, nullptr);
  set_normal_inputs();
  sma->update_values();  // voltage 3600 -> AH computed (0x00C8)
  datalayer.battery.combined.status.voltage_dV = 5;
  reset_recorded_frames();
  sma->update_values();
  sma->transmit_can(100);

  const CAN_frame* f3d8 = frame_by_id(0x3D8);
  ASSERT_NE(f3d8, nullptr);
  EXPECT_EQ(f3d8->data.u8[4], 0x00);
  EXPECT_EQ(f3d8->data.u8[5], 0xC8);  // retained, not zeroed
}

// check_enable_line watchdog: counter increments then compares > 1200, so the event
// first fires on the 1201st starved update_values() call.
TEST_F(SmaFrameTest, BydH_EnableLineWatchdog) {
  CanInverterProtocol* sma = setup_protocol(InverterProtocolType::SmaBydH);
  ASSERT_NE(sma, nullptr);
  set_normal_inputs();
  datalayer.system.status.inverter_allows_contactor_closing = false;  // starved
  reset_all_events();
  for (int i = 0; i < 1200; i++) {
    sma->update_values();
  }
  const EVENTS_STRUCT_TYPE* e = get_event_pointer(EVENT_NO_ENABLE_DETECTED);
  ASSERT_NE(e, nullptr);
  EXPECT_EQ(e->state, EVENT_STATE_INACTIVE);  // not yet fired at 1200
  sma->update_values();
  e = get_event_pointer(EVENT_NO_ENABLE_DETECTED);
  ASSERT_NE(e, nullptr);
  EXPECT_NE(e->state, EVENT_STATE_INACTIVE);
}

}  // namespace
