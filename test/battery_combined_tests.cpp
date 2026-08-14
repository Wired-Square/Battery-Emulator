#include <gtest/gtest.h>

#include "../Software/src/datalayer/battery_combined.h"
#include "../Software/src/datalayer/datalayer.h"

// update_reported_values() composes datalayer.battery.combined — the inverter-facing
// view — from the per-pack data. These tests pin the fold semantics inherited
// from update_calculated_values(): sums for reported capacities/current,
// primary-mirror for everything else, SOC-extremes override from joined slots.
namespace {

constexpr bool kOnlyPrimary[kMaxBatterySlots] = {true, false, false};
constexpr bool kDualPack[kMaxBatterySlots] = {true, true, false};

class BatteryAggregateTest : public testing::Test {
 protected:
  void SetUp() override {
    datalayer = DataLayer();
    // No NSDMI on these — give every test a deterministic base.
    for (uint8_t slot = 0; slot < kMaxBatterySlots; slot++) {
      auto& status = datalayer.battery_slot(slot).status;
      status.real_soc = 0;
      status.reported_soc = 0;
      status.temperature_max_dC = 0;
      status.temperature_min_dC = 0;
      status.CAN_error_counter = 0;
    }
    datalayer.battery.combined.status.real_soc = 0;
    datalayer.battery.combined.status.reported_soc = 0;
    datalayer.battery.combined.status.temperature_max_dC = 0;
    datalayer.battery.combined.status.temperature_min_dC = 0;
    datalayer.battery.combined.status.CAN_error_counter = 0;
  }

  void TearDown() override { datalayer = DataLayer(); }
};

TEST_F(BatteryAggregateTest, ScalesPrimarySocIntoWindow) {
  auto& pack = datalayer.battery.pack[0];
  datalayer.battery.settings.soc_scaling_active = true;
  datalayer.battery.settings.min_percentage = 2000;
  datalayer.battery.settings.max_percentage = 8000;
  pack.status.real_soc = 5000;
  pack.info.total_capacity_Wh = 30000;

  update_reported_values(kOnlyPrimary);

  EXPECT_EQ(datalayer.battery.combined.status.reported_soc, 5000);
  EXPECT_EQ(datalayer.battery.combined.info.reported_total_capacity_Wh, 18000u);
  EXPECT_EQ(datalayer.battery.combined.status.reported_remaining_capacity_Wh, 9000u);
  // The pack keeps its own scaled values — the fold only lands in the combined view.
  EXPECT_EQ(datalayer.battery.pack[0].status.reported_soc, 5000);
  EXPECT_EQ(datalayer.battery.pack[0].info.reported_total_capacity_Wh, 18000u);
}

TEST_F(BatteryAggregateTest, DualPackFoldsCapacitiesIntoAggregateOnly) {
  auto& primary = datalayer.battery.pack[0];
  datalayer.battery.settings.soc_scaling_active = true;
  datalayer.battery.settings.min_percentage = 2000;
  datalayer.battery.settings.max_percentage = 8000;
  primary.status.real_soc = 5000;
  primary.info.total_capacity_Wh = 30000;
  datalayer.battery.pack[1].info.total_capacity_Wh = 30000;
  datalayer.battery.pack[1].status.real_soc = 5000;
  datalayer.battery.pack[1].status.current_dA = -3;

  update_reported_values(kDualPack);

  EXPECT_EQ(datalayer.battery.combined.info.reported_total_capacity_Wh, 36000u);
  EXPECT_EQ(datalayer.battery.combined.status.reported_remaining_capacity_Wh, 18000u);
  EXPECT_EQ(datalayer.battery.pack[0].info.reported_total_capacity_Wh, 18000u);
  EXPECT_EQ(datalayer.battery.pack[1].info.reported_total_capacity_Wh, 18000u);
  EXPECT_EQ(datalayer.battery.pack[1].status.reported_remaining_capacity_Wh, 9000u);
  // Secondary active power: -3 dA at the default 370.0 V.
  EXPECT_EQ(datalayer.battery.pack[1].status.active_power_W, -111);
}

TEST_F(BatteryAggregateTest, EmptySlotExcludedFromSums) {
  auto& primary = datalayer.battery.pack[0];
  datalayer.battery.settings.soc_scaling_active = true;
  datalayer.battery.settings.min_percentage = 2000;
  datalayer.battery.settings.max_percentage = 8000;
  primary.status.real_soc = 5000;
  primary.info.total_capacity_Wh = 30000;
  // The capacity setting seeds every slot's live value; an enabled-but-absent
  // battery must still not inflate the sums.
  datalayer.battery.pack[1].info.total_capacity_Wh = 30000;

  update_reported_values(kOnlyPrimary);

  EXPECT_EQ(datalayer.battery.combined.info.reported_total_capacity_Wh, 18000u);
  EXPECT_EQ(datalayer.battery.combined.status.reported_remaining_capacity_Wh, 9000u);
}

TEST_F(BatteryAggregateTest, CurrentSumsAcrossAllSlots) {
  datalayer.battery.settings.soc_scaling_active = false;
  datalayer.battery.pack[0].status.real_soc = 4000;
  datalayer.battery.pack[0].status.current_dA = 15;
  datalayer.battery.pack[1].status.current_dA = -3;

  // The current sum spans every slot regardless of the populated mask —
  // unpopulated slots carry 0.
  update_reported_values(kOnlyPrimary);

  EXPECT_EQ(datalayer.battery.combined.status.reported_current_dA, 12);
  // Primary active power: 15 dA at the default 370.0 V, mirrored into the view.
  EXPECT_EQ(datalayer.battery.pack[0].status.active_power_W, 555);
  EXPECT_EQ(datalayer.battery.combined.status.active_power_W, 555);
}

TEST_F(BatteryAggregateTest, SocExtremesOverrideFromJoinedSlot) {
  datalayer.battery.settings.soc_scaling_active = false;
  datalayer.battery.pack[0].status.real_soc = 4200;
  datalayer.battery.pack[1].status.real_soc = 9950;
  datalayer.system.status.battery2_allowed_contactor_closing = true;

  update_reported_values(kDualPack);

  EXPECT_EQ(datalayer.battery.combined.status.reported_soc, 9950);
  // The per-slot mirror keeps the slot's own value.
  EXPECT_EQ(datalayer.battery.pack[1].status.reported_soc, 9950);
  EXPECT_EQ(datalayer.battery.pack[0].status.reported_soc, 4200);
}

TEST_F(BatteryAggregateTest, SocExtremesOverrideRequiresContactorPermission) {
  datalayer.battery.settings.soc_scaling_active = false;
  datalayer.battery.pack[0].status.real_soc = 4200;
  datalayer.battery.pack[1].status.real_soc = 9950;
  datalayer.system.status.battery2_allowed_contactor_closing = false;

  update_reported_values(kDualPack);

  EXPECT_EQ(datalayer.battery.combined.status.reported_soc, 4200);
}

TEST_F(BatteryAggregateTest, NoScalingPathReportsRawSums) {
  auto& primary = datalayer.battery.pack[0];
  datalayer.battery.settings.soc_scaling_active = false;
  primary.status.real_soc = 4200;
  primary.status.remaining_capacity_Wh = 12000;
  primary.info.total_capacity_Wh = 30000;
  datalayer.battery.pack[1].status.remaining_capacity_Wh = 5000;
  datalayer.battery.pack[1].info.total_capacity_Wh = 20000;

  update_reported_values(kDualPack);

  EXPECT_EQ(datalayer.battery.combined.status.reported_soc, 4200);
  EXPECT_EQ(datalayer.battery.combined.status.reported_remaining_capacity_Wh, 17000u);
  EXPECT_EQ(datalayer.battery.combined.info.reported_total_capacity_Wh, 50000u);
  EXPECT_EQ(datalayer.battery.pack[0].status.reported_remaining_capacity_Wh, 12000u);
  EXPECT_EQ(datalayer.battery.pack[0].info.reported_total_capacity_Wh, 30000u);
}

TEST_F(BatteryAggregateTest, ScalingFallbackWhenCapacityUnknown) {
  auto& primary = datalayer.battery.pack[0];
  datalayer.battery.settings.soc_scaling_active = true;
  datalayer.battery.settings.min_percentage = 2000;
  datalayer.battery.settings.max_percentage = 8000;
  primary.status.real_soc = 5000;
  primary.info.total_capacity_Wh = 0;
  primary.status.remaining_capacity_Wh = 7500;

  update_reported_values(kOnlyPrimary);

  EXPECT_EQ(datalayer.battery.combined.status.reported_soc, 5000);
  EXPECT_EQ(datalayer.battery.combined.info.reported_total_capacity_Wh, 0u);
  EXPECT_EQ(datalayer.battery.combined.status.reported_remaining_capacity_Wh, 7500u);
}

TEST_F(BatteryAggregateTest, UserLimitCapsChargeCurrentAndSetsFlag) {
  auto& primary = datalayer.battery.pack[0];
  primary.status.real_soc = 5000;
  primary.status.max_charge_power_W = 50000;    // 1351 dA at 370.0 V
  primary.status.max_discharge_power_W = 3700;  // 100 dA at 370.0 V

  update_reported_values(kOnlyPrimary);

  EXPECT_EQ(datalayer.battery.combined.status.max_charge_current_dA, 300);  // default user cap
  EXPECT_TRUE(datalayer.battery.settings.user_settings_limit_charge);
  EXPECT_EQ(datalayer.battery.combined.status.max_discharge_current_dA, 100);
  EXPECT_FALSE(datalayer.battery.settings.user_settings_limit_discharge);
}

TEST_F(BatteryAggregateTest, RemoteLimitTakesPrecedenceOverUserCap) {
  auto& primary = datalayer.battery.pack[0];
  primary.status.real_soc = 5000;
  primary.status.max_charge_power_W = 50000;
  datalayer.battery.settings.remote_settings_limit_charge = true;
  datalayer.battery.settings.max_remote_set_charge_dA = 150;

  update_reported_values(kOnlyPrimary);

  EXPECT_EQ(datalayer.battery.combined.status.max_charge_current_dA, 150);
  EXPECT_FALSE(datalayer.battery.settings.user_settings_limit_charge);
}

TEST_F(BatteryAggregateTest, ConversionFallsBackToDesignVoltage) {
  auto& primary = datalayer.battery.pack[0];
  primary.status.real_soc = 5000;
  primary.status.voltage_dV = 0;
  primary.info.max_design_voltage_dV = 4000;
  primary.status.max_charge_power_W = 40000;  // 1000 dA at 400.0 V
  datalayer.battery.settings.max_user_set_charge_dA = 3000;

  update_reported_values(kOnlyPrimary);

  EXPECT_EQ(datalayer.battery.combined.status.max_charge_current_dA, 1000);
}

TEST_F(BatteryAggregateTest, InvalidVoltageKeepsPreviousCurrents) {
  auto& primary = datalayer.battery.pack[0];
  primary.status.real_soc = 5000;
  primary.status.max_charge_power_W = 50000;
  update_reported_values(kOnlyPrimary);
  ASSERT_EQ(datalayer.battery.combined.status.max_charge_current_dA, 300);

  primary.status.voltage_dV = 0;
  primary.info.max_design_voltage_dV = 0;
  update_reported_values(kOnlyPrimary);

  EXPECT_EQ(datalayer.battery.combined.status.max_charge_current_dA, 300);
}

TEST_F(BatteryAggregateTest, LimitingFactorFlagsWhenIdle) {
  auto& primary = datalayer.battery.pack[0];
  primary.status.real_soc = 5000;
  primary.status.current_dA = 0;
  primary.status.max_discharge_power_W = 3700;  // discharge allowed, unused
  primary.status.max_charge_power_W = 0;        // charge blocked

  update_reported_values(kOnlyPrimary);

  EXPECT_TRUE(datalayer.battery.settings.inverter_limits_discharge);
  EXPECT_FALSE(datalayer.battery.settings.inverter_limits_charge);
}

// Parallel packs share current unevenly, so the safe system limit is bounded
// by the weakest joined pack: min(sum, joined_count * weakest).
TEST_F(BatteryAggregateTest, PowerLimitsFoldToWeakestPack) {
  datalayer.battery.pack[0].status.max_charge_power_W = 5000;
  datalayer.battery.pack[0].status.max_discharge_power_W = 4000;
  datalayer.battery.pack[1].status.max_charge_power_W = 3000;
  datalayer.battery.pack[1].status.max_discharge_power_W = 6000;
  datalayer.system.status.battery2_allowed_contactor_closing = true;

  update_reported_values(kDualPack);

  EXPECT_EQ(datalayer.battery.combined.status.max_charge_power_W, 6000u);
  EXPECT_EQ(datalayer.battery.combined.status.max_discharge_power_W, 8000u);
  // Derived currents come from the folded limits (6000 W at 370.0 V).
  EXPECT_EQ(datalayer.battery.combined.status.max_charge_current_dA, 162);
}

TEST_F(BatteryAggregateTest, ZeroedPackBlocksSystemPowerLimit) {
  datalayer.battery.pack[0].status.max_charge_power_W = 5000;
  datalayer.battery.pack[0].status.max_discharge_power_W = 4000;
  datalayer.battery.pack[1].status.max_charge_power_W = 0;
  datalayer.battery.pack[1].status.max_discharge_power_W = 6000;
  datalayer.system.status.battery2_allowed_contactor_closing = true;

  update_reported_values(kDualPack);

  EXPECT_EQ(datalayer.battery.combined.status.max_charge_power_W, 0u);
  EXPECT_EQ(datalayer.battery.combined.status.max_discharge_power_W, 8000u);
}

TEST_F(BatteryAggregateTest, DisconnectedPackExcludedFromLimitFold) {
  datalayer.battery.pack[0].status.max_charge_power_W = 5000;
  datalayer.battery.pack[1].status.max_charge_power_W = 0;
  datalayer.system.status.battery2_allowed_contactor_closing = false;

  update_reported_values(kDualPack);

  EXPECT_EQ(datalayer.battery.combined.status.max_charge_power_W, 5000u);
}

TEST_F(BatteryAggregateTest, ActivePowerSumsAcrossJoinedPacks) {
  datalayer.battery.pack[0].status.real_soc = 5000;
  datalayer.battery.pack[0].status.current_dA = 15;   // 555 W at 370.0 V
  datalayer.battery.pack[1].status.current_dA = -3;   // -111 W at 370.0 V
  datalayer.system.status.battery2_allowed_contactor_closing = true;

  update_reported_values(kDualPack);

  EXPECT_EQ(datalayer.battery.combined.status.active_power_W, 444);
}

TEST_F(BatteryAggregateTest, CellAndTempExtremesFoldAcrossJoinedPacks) {
  auto& primary = datalayer.battery.pack[0];
  primary.status.cell_max_voltage_mV = 4100;
  primary.status.cell_min_voltage_mV = 3900;
  primary.status.temperature_max_dC = 200;
  primary.status.temperature_min_dC = 100;
  auto& secondary = datalayer.battery.pack[1];
  secondary.status.cell_max_voltage_mV = 4200;
  secondary.status.cell_min_voltage_mV = 3800;
  secondary.status.temperature_max_dC = 350;
  secondary.status.temperature_min_dC = 50;
  datalayer.system.status.battery2_allowed_contactor_closing = true;

  update_reported_values(kDualPack);

  EXPECT_EQ(datalayer.battery.combined.status.cell_max_voltage_mV, 4200);
  EXPECT_EQ(datalayer.battery.combined.status.cell_min_voltage_mV, 3800);
  EXPECT_EQ(datalayer.battery.combined.status.temperature_max_dC, 350);
  EXPECT_EQ(datalayer.battery.combined.status.temperature_min_dC, 50);
}

TEST_F(BatteryAggregateTest, DatalessPackDoesNotPolluteExtremes) {
  auto& primary = datalayer.battery.pack[0];
  primary.status.cell_max_voltage_mV = 4100;
  primary.status.cell_min_voltage_mV = 3900;
  primary.status.temperature_max_dC = 200;
  primary.status.temperature_min_dC = 100;
  auto& secondary = datalayer.battery.pack[1];
  secondary.status.voltage_dV = 0;  // no data from this pack yet
  secondary.status.cell_max_voltage_mV = 0;
  secondary.status.cell_min_voltage_mV = 0;
  datalayer.system.status.battery2_allowed_contactor_closing = true;

  update_reported_values(kDualPack);

  EXPECT_EQ(datalayer.battery.combined.status.cell_max_voltage_mV, 4100);
  EXPECT_EQ(datalayer.battery.combined.status.cell_min_voltage_mV, 3900);
  EXPECT_EQ(datalayer.battery.combined.status.temperature_max_dC, 200);
  EXPECT_EQ(datalayer.battery.combined.status.temperature_min_dC, 100);
}

TEST_F(BatteryAggregateTest, ScaledCapacityUsesEachPacksOwnSoc) {
  auto& primary = datalayer.battery.pack[0];
  datalayer.battery.settings.soc_scaling_active = true;
  datalayer.battery.settings.min_percentage = 2000;
  datalayer.battery.settings.max_percentage = 8000;
  primary.status.real_soc = 5000;
  primary.info.total_capacity_Wh = 30000;
  auto& secondary = datalayer.battery.pack[1];
  secondary.status.real_soc = 8000;  // at the window top: scaled 100%
  secondary.info.total_capacity_Wh = 60000;

  update_reported_values(kDualPack);

  EXPECT_EQ(datalayer.battery.pack[1].info.reported_total_capacity_Wh, 36000u);
  EXPECT_EQ(datalayer.battery.pack[1].status.reported_remaining_capacity_Wh, 36000u);
  EXPECT_EQ(datalayer.battery.combined.info.reported_total_capacity_Wh, 54000u);
  EXPECT_EQ(datalayer.battery.combined.status.reported_remaining_capacity_Wh, 45000u);
}

TEST_F(BatteryAggregateTest, LimitFlagsClassifyOnSummedCurrent) {
  auto& primary = datalayer.battery.pack[0];
  primary.status.real_soc = 5000;
  primary.status.current_dA = 0;
  primary.status.max_discharge_power_W = 3700;  // 100 dA at 370.0 V
  datalayer.battery.pack[1].status.current_dA = -200;
  datalayer.system.status.battery2_allowed_contactor_closing = true;

  update_reported_values(kDualPack);

  // System discharges 200 dA against a 100 dA limit: the battery, not the
  // inverter, is the limiting factor.
  EXPECT_FALSE(datalayer.battery.settings.inverter_limits_discharge);
}

TEST_F(BatteryAggregateTest, SocExtremeOverrideUsesScaledDomain) {
  auto& primary = datalayer.battery.pack[0];
  datalayer.battery.settings.soc_scaling_active = true;
  datalayer.battery.settings.min_percentage = 2000;
  datalayer.battery.settings.max_percentage = 8000;
  primary.status.real_soc = 5000;
  primary.info.total_capacity_Wh = 30000;
  datalayer.battery.pack[1].status.real_soc = 1500;  // below the window: scaled 0
  datalayer.battery.pack[1].info.total_capacity_Wh = 30000;
  datalayer.system.status.battery2_allowed_contactor_closing = true;

  update_reported_values(kDualPack);

  EXPECT_EQ(datalayer.battery.combined.status.reported_soc, 0);
}

TEST_F(BatteryAggregateTest, RealSocIsCapacityWeightedAcrossJoinedPacks) {
  datalayer.battery.pack[0].status.real_soc = 8000;
  datalayer.battery.pack[0].info.total_capacity_Wh = 30000;
  datalayer.battery.pack[0].status.soh_pptt = 9800;
  datalayer.battery.pack[1].status.real_soc = 2000;
  datalayer.battery.pack[1].info.total_capacity_Wh = 60000;
  datalayer.battery.pack[1].status.soh_pptt = 9500;
  datalayer.system.status.battery2_allowed_contactor_closing = true;

  update_reported_values(kDualPack);

  // (8000×30000 + 2000×60000) / 90000
  EXPECT_EQ(datalayer.battery.combined.status.real_soc, 4000);
  EXPECT_EQ(datalayer.battery.combined.status.soh_pptt, 9500);
  // Per-pack values are untouched by the fold.
  EXPECT_EQ(datalayer.battery.pack[0].status.real_soc, 8000);
  EXPECT_EQ(datalayer.battery.pack[1].status.soh_pptt, 9500);
}

TEST_F(BatteryAggregateTest, UnjoinedPackExcludedFromSocAndSohFold) {
  datalayer.battery.pack[0].status.real_soc = 8000;
  datalayer.battery.pack[0].info.total_capacity_Wh = 30000;
  datalayer.battery.pack[0].status.soh_pptt = 9800;
  datalayer.battery.pack[1].status.real_soc = 2000;
  datalayer.battery.pack[1].info.total_capacity_Wh = 60000;
  datalayer.battery.pack[1].status.soh_pptt = 9500;
  datalayer.system.status.battery2_allowed_contactor_closing = false;

  update_reported_values(kDualPack);

  EXPECT_EQ(datalayer.battery.combined.status.real_soc, 8000);
  EXPECT_EQ(datalayer.battery.combined.status.soh_pptt, 9800);
}

TEST_F(BatteryAggregateTest, ZeroCapacityKeepsPrimarySocMirror) {
  datalayer.battery.pack[0].status.real_soc = 5000;
  datalayer.battery.pack[0].info.total_capacity_Wh = 0;
  datalayer.battery.pack[1].status.real_soc = 2000;
  datalayer.battery.pack[1].info.total_capacity_Wh = 0;
  datalayer.system.status.battery2_allowed_contactor_closing = true;

  update_reported_values(kDualPack);

  EXPECT_EQ(datalayer.battery.combined.status.real_soc, 5000);
}

TEST_F(BatteryAggregateTest, CellArraysConcatenateGroupedByPack) {
  auto& primary = datalayer.battery.pack[0];
  primary.info.number_of_cells = 2;
  primary.status.cell_voltages_mV[0] = 4000;
  primary.status.cell_voltages_mV[1] = 4010;
  primary.status.cell_balancing_status[0] = true;
  auto& secondary = datalayer.battery.pack[1];
  secondary.info.number_of_cells = 3;
  secondary.status.cell_max_voltage_mV = 3920;
  secondary.status.cell_min_voltage_mV = 3900;
  secondary.status.cell_voltages_mV[0] = 3900;
  secondary.status.cell_voltages_mV[1] = 3910;
  secondary.status.cell_voltages_mV[2] = 3920;
  secondary.status.cell_balancing_status[2] = true;
  datalayer.system.status.battery2_allowed_contactor_closing = true;

  update_reported_values(kDualPack);

  auto& agg = datalayer.battery.combined;
  EXPECT_EQ(agg.info.number_of_cells, 5);
  EXPECT_EQ(agg.status.cell_voltages_mV[0], 4000);
  EXPECT_EQ(agg.status.cell_voltages_mV[1], 4010);
  EXPECT_EQ(agg.status.cell_voltages_mV[2], 3900);
  EXPECT_EQ(agg.status.cell_voltages_mV[3], 3910);
  EXPECT_EQ(agg.status.cell_voltages_mV[4], 3920);
  EXPECT_TRUE(agg.status.cell_balancing_status[0]);
  EXPECT_FALSE(agg.status.cell_balancing_status[1]);
  EXPECT_FALSE(agg.status.cell_balancing_status[2]);
  EXPECT_TRUE(agg.status.cell_balancing_status[4]);
  // Per-pack arrays keep their own contents.
  EXPECT_EQ(datalayer.battery.pack[1].info.number_of_cells, 3);
  EXPECT_EQ(datalayer.battery.pack[1].status.cell_voltages_mV[0], 3900);
}

TEST_F(BatteryAggregateTest, CellArrayFoldTruncatesAtCapacity) {
  auto& primary = datalayer.battery.pack[0];
  primary.info.number_of_cells = 100;
  for (uint16_t i = 0; i < 100; i++) {
    primary.status.cell_voltages_mV[i] = 3000 + i;
  }
  auto& secondary = datalayer.battery.pack[1];
  secondary.info.number_of_cells = 150;
  secondary.status.cell_max_voltage_mV = 4000;
  for (uint16_t i = 0; i < 150; i++) {
    secondary.status.cell_voltages_mV[i] = 4000 + i;
  }
  datalayer.system.status.battery2_allowed_contactor_closing = true;

  update_reported_values(kDualPack);

  auto& agg = datalayer.battery.combined;
  EXPECT_EQ(agg.info.number_of_cells, MAX_AMOUNT_CELLS);
  EXPECT_EQ(agg.status.cell_voltages_mV[99], 3099);
  EXPECT_EQ(agg.status.cell_voltages_mV[100], 4000);
  // Only the first 92 of the secondary's 150 cells fit.
  EXPECT_EQ(agg.status.cell_voltages_mV[MAX_AMOUNT_CELLS - 1], 4091);
}

TEST_F(BatteryAggregateTest, DatalessPackCellsNotAppended) {
  auto& primary = datalayer.battery.pack[0];
  primary.info.number_of_cells = 2;
  primary.status.cell_voltages_mV[0] = 4000;
  primary.status.cell_voltages_mV[1] = 4010;
  auto& secondary = datalayer.battery.pack[1];
  secondary.info.number_of_cells = 3;  // configured, but no cell data received yet
  secondary.status.cell_max_voltage_mV = 0;
  datalayer.system.status.battery2_allowed_contactor_closing = true;

  update_reported_values(kDualPack);

  EXPECT_EQ(datalayer.battery.combined.info.number_of_cells, 2);
}

TEST_F(BatteryAggregateTest, SocExtremeOverrideFullPackInScaledDomain) {
  auto& primary = datalayer.battery.pack[0];
  datalayer.battery.settings.soc_scaling_active = true;
  datalayer.battery.settings.min_percentage = 2000;
  datalayer.battery.settings.max_percentage = 8000;
  primary.status.real_soc = 5000;
  primary.info.total_capacity_Wh = 30000;
  datalayer.battery.pack[1].status.real_soc = 8500;  // above the window: scaled 100%
  datalayer.battery.pack[1].info.total_capacity_Wh = 30000;
  datalayer.system.status.battery2_allowed_contactor_closing = true;

  update_reported_values(kDualPack);

  EXPECT_EQ(datalayer.battery.combined.status.reported_soc, 10000);
}

}  // namespace
