#ifndef BATTERIES_H
#define BATTERIES_H
#include <stdint.h>
#include "BatterySlotContext.h"
#include "Shunt.h"

#include "Battery.h"
#include "Shunt.h"

#include "BMW-I3-BATTERY.h"
#include "BMW-IX-BATTERY.h"
#include "BMW-PHEV-BATTERY.h"
#include "BMW-SBOX.h"
#include "BOLT-AMPERA-BATTERY.h"
#include "BYD-ATTO-3-BATTERY.h"
#include "CELLPOWER-BMS.h"
#include "CHADEMO-BATTERY.h"
#include "CHADEMO-CT.h"
#include "CHADEMO-SHUNTS.h"
#include "CHARGEBYTE-CCS.h"
#include "CMFA-EV-BATTERY.h"
#include "CMP-SMART-CAR-BATTERY.h"
#include "DALY-BMS.h"
#include "ECMP-BATTERY.h"
#include "ENNOID-BMS.h"
#include "FORD-MACH-E-BATTERY.h"
#include "FOXESS-BATTERY.h"
#include "GEELY-GEOMETRY-C-BATTERY.h"
#include "GEELY-SEA-BATTERY.h"
#include "GROWATT-HV-ARK-BATTERY.h"
#include "HYUNDAI-IONIQ-28-BATTERY.h"
#include "IMIEV-CZERO-ION-BATTERY.h"
#include "JAGUAR-IPACE-BATTERY.h"
#include "KIA-64FD-BATTERY.h"
#include "KIA-E-GMP-BATTERY.h"
#include "KIA-HYUNDAI-64-BATTERY.h"
#include "KIA-HYUNDAI-HYBRID-BATTERY.h"
#include "MEB-BATTERY.h"
#include "MG-5-BATTERY.h"
#include "MG-GEN1-BATTERY.h"
#include "NISSAN-LEAF-BATTERY.h"
#include "ORION-BMS.h"
#include "PYLON-BATTERY.h"
#include "RANGE-ROVER-PHEV-BATTERY.h"
#include "RELION-LV-BATTERY.h"
#include "RENAULT-KANGOO-BATTERY.h"
#include "RENAULT-TWIZY.h"
#include "RENAULT-ZOE-GEN1-BATTERY.h"
#include "RENAULT-ZOE-GEN2-BATTERY.h"
#include "RIVIAN-BATTERY.h"
#include "RJXZS-BMS.h"
#include "SAMSUNG-SDI-LV-BATTERY.h"
#include "SANTA-FE-PHEV-BATTERY.h"
#include "SIMPBMS-BATTERY.h"
#include "SONO-BATTERY.h"
#include "STELLANTIS-SMALL-WIDE-4x4.h"
#include "TESLA-BATTERY.h"
#include "TESLA-LEGACY-BATTERY.h"
#include "TEST-FAKE-BATTERY.h"
#include "THINK-BATTERY.h"
#include "THUNDERSTRUCK-BMS.h"
#include "VOLVO-SPA-BATTERY.h"
#include "VOLVO-SPA-HYBRID-BATTERY.h"

#include "battery_slots.h"

// Number of user-enabled battery slots (highest enabled slot + 1). An enabled
// slot can still hold a null battery when the selected type does not support
// it, so consumers must null-check batteries[slot].
uint8_t active_battery_slots();

void setup_shunt();

void setup_battery(void);
Battery* create_battery(BatteryType type, const BatterySlotContext& ctx);

bool board_supports_battery_type(BatteryType type);
bool interface_supports_battery_type(BatteryType type, const InterfaceDescriptor* interface);

// Same per-slot globals setup_battery() binds when constructing drivers.
BatterySlotContext battery_slot_context(uint8_t slot);

extern uint16_t user_selected_max_pack_voltage_dV;
extern uint16_t user_selected_min_pack_voltage_dV;
extern uint16_t user_selected_max_cell_voltage_mV;
extern uint16_t user_selected_min_cell_voltage_mV;
extern bool user_selected_use_estimated_SOC;
extern bool user_selected_use_estimated_charge_limits;
extern bool user_selected_LEAF_interlock_mandatory;
extern bool user_selected_tesla_digital_HVIL;
inline constexpr uint16_t kTeslaGtwCountryDefault = 17477;
inline constexpr bool kTeslaGtwRightHandDriveDefault = true;
inline constexpr uint16_t kTeslaGtwMapRegionDefault = 2;
inline constexpr uint16_t kTeslaGtwChassisTypeDefault = 2;
inline constexpr uint16_t kTeslaGtwPackEnergyDefault = 1;
extern uint16_t user_selected_tesla_GTW_country;
extern bool user_selected_tesla_GTW_rightHandDrive;
extern uint16_t user_selected_tesla_GTW_mapRegion;
extern uint16_t user_selected_tesla_GTW_chassisType;
extern uint16_t user_selected_tesla_GTW_packEnergy;
extern uint16_t user_selected_pylon_baudrate;

/* User-selected DALY BMS settings */
extern int user_selected_daly_power_per_percent;
extern int user_selected_daly_power_per_dV;
extern int user_selected_daly_power_per_dV_start;
extern int user_selected_daly_power_per_degree_C;
extern int user_selected_daly_power_at_0_degree_C;

#endif
