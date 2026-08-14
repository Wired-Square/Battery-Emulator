#ifndef SYSTEM_SETTINGS_H_
#define SYSTEM_SETTINGS_H_
#include <stdint.h>
/** TASKS
 * Higher number equals higher priority. Max 25 per core
 * 
 * Parameter: TASK_CORE_PRIO
 * Description:
 * Defines the priority of core functionality (CAN, Modbus, etc)
 * 
 * Parameter: TASK_CONNECTIVITY_PRIO
 * Description:
 * Defines the priority of various wireless functionality (TCP, MQTT, etc)
 * 
 * Parameter: TASK_MODBUS_PRIO
 * Description:
 * Defines the priority of MODBUS handling
 *
 * Parameter: TASK_ACAN2515_PRIORITY
 * Description:
 * Defines the priority of ACAN2515 CAN handling
 *  
 * Parameter: TASK_ACAN2515_PRIORITY
 * Description:
 * Defines the priority of ACAN2517FD CAN-FD handling
*/
#define TASK_CORE_PRIO 4
#define TASK_CONNECTIVITY_PRIO 3
#define TASK_MQTT_PRIO 2
#define TASK_MODBUS_PRIO 8
#define TASK_ACAN2515_PRIORITY 10
#define TASK_ACAN2517FD_PRIORITY 10

/** MAX AMOUNT OF CELLS
 * 
 * Parameter: MAX_AMOUNT_CELLS
 * Description:
 * Basically the length of the array used to hold individual cell voltages
*/
#define MAX_AMOUNT_CELLS 192

/** BATTERY SLOTS
 *
 * Parameter: kMaxBatterySlots
 * Description:
 * Number of battery slots the firmware can address (primary + extras).
 * Slot state is allocated unconditionally, so every slot costs RAM
 * whether or not the user enables it.
*/
inline constexpr uint8_t kMaxBatterySlots = 3;

// AsyncTCP should only need a 4k stack (default is 16k)
#define CONFIG_ASYNC_TCP_STACK_SIZE 4096

#endif
