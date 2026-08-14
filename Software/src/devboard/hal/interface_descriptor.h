#ifndef _INTERFACE_DESCRIPTOR_H_
#define _INTERFACE_DESCRIPTOR_H_

#include <stddef.h>
#include <stdint.h>

#include "../utils/types.h"

class CanBus;
class Rs485Port;

// Per-board interface tables: identity is the table index; config persists
// {type, index} packed.

// Persisted inside the packed interface config: append-only, never reorder.
// Types classify hardware/stack, never instances.
enum class InterfaceType : uint8_t {
  CanNative = 0,
  CanMcp2515 = 1,
  CanMcp2517fd = 2,
  Rs485 = 3,
  Modbus = 4,
};

struct InterfaceDescriptor {
  InterfaceType type;
  // nullptr = default label for the type.
  const char* name;
  // Pre-descriptor NVM value, consumed only by the one-time config
  // migration (find_by_legacy). Descriptors with no legacy slot use
  // comm_interface::Highest, which the migration guard never reaches.
  comm_interface legacy_id;
  // Routing binding; nullptr = not a CAN interface.
  CanBus* can_bus;
  // Routing binding; nullptr = not an RS-485 interface. Modbus rows alias
  // their sibling Rs485 row's port.
  Rs485Port* rs485_port = nullptr;
};

struct InterfaceList {
  const InterfaceDescriptor* data;
  size_t count;
};

inline constexpr uint32_t INTERFACE_INDEX_MASK = 0xFF;
inline constexpr uint32_t INTERFACE_TYPE_SHIFT = 8;
inline constexpr uint32_t INTERFACE_TYPE_MASK = 0x7F;
// Bit 15 keeps packed values disjoint from legacy comm_interface integers,
// making the one-time migration idempotent.
inline constexpr uint32_t INTERFACE_CONFIG_MARKER = 0x8000;
inline constexpr uint32_t INTERFACE_SCHEMA_VERSION = 2;

template <size_t N>
constexpr InterfaceList make_interface_list(const InterfaceDescriptor (&arr)[N]) {
  static_assert(N <= INTERFACE_INDEX_MASK + 1, "descriptor index must fit the packed index field");
  return {arr, N};
}

// Visits each distinct non-null binding once, in table order; stops early
// when fn returns false.
template <typename T, typename Fn>
void for_each_unique_binding(InterfaceList list, T* InterfaceDescriptor::* member, Fn&& fn) {
  for (size_t i = 0; i < list.count; i++) {
    T* bound = list.data[i].*member;
    if (bound == nullptr) {
      continue;
    }
    bool seen = false;
    for (size_t j = 0; j < i; j++) {
      if (list.data[j].*member == bound) {
        seen = true;
        break;
      }
    }
    if (!seen && !fn(bound)) {
      return;
    }
  }
}

// No default case: -Wswitch flags every new type here.
constexpr const char* default_name_for_type(InterfaceType type) {
  switch (type) {
    case InterfaceType::CanNative:
      return "CAN (Native)";
    case InterfaceType::CanMcp2515:
      return "CAN (MCP2515 add-on)";
    case InterfaceType::CanMcp2517fd:
      return "CAN FD (MCP2518 add-on)";
    case InterfaceType::Rs485:
      return "RS485";
    case InterfaceType::Modbus:
      return "Modbus";
  }
  return "";
}

constexpr const char* descriptor_name(const InterfaceDescriptor& desc) {
  return desc.name ? desc.name : default_name_for_type(desc.type);
}

constexpr const InterfaceDescriptor* find_by_legacy(InterfaceList list, comm_interface legacy) {
  for (size_t i = 0; i < list.count; i++) {
    if (list.data[i].legacy_id == legacy) {
      return &list.data[i];
    }
  }
  return nullptr;
}

constexpr const InterfaceDescriptor* find_by_type(InterfaceList list, InterfaceType type) {
  for (size_t i = 0; i < list.count; i++) {
    if (list.data[i].type == type) {
      return &list.data[i];
    }
  }
  return nullptr;
}

constexpr bool has_type(InterfaceList list, InterfaceType type) {
  for (size_t i = 0; i < list.count; i++) {
    if (list.data[i].type == type) {
      return true;
    }
  }
  return false;
}

enum class BusRequirement : uint8_t {
  None = 0,
  Can,
  CanFd,
  Rs485,
};

constexpr bool interface_satisfies(BusRequirement req, InterfaceType type) {
  switch (req) {
    case BusRequirement::None:
      return true;
    case BusRequirement::Can:
      return type == InterfaceType::CanNative || type == InterfaceType::CanMcp2515 ||
             type == InterfaceType::CanMcp2517fd;
    case BusRequirement::CanFd:
      return type == InterfaceType::CanMcp2517fd;
    case BusRequirement::Rs485:
      return type == InterfaceType::Rs485 || type == InterfaceType::Modbus;
  }
  return false;
}

constexpr const InterfaceDescriptor* find_satisfying(InterfaceList list, BusRequirement req) {
  for (size_t i = 0; i < list.count; i++) {
    if (interface_satisfies(req, list.data[i].type)) {
      return &list.data[i];
    }
  }
  return nullptr;
}

constexpr uint32_t pack_interface_config(InterfaceType type, size_t index) {
  return INTERFACE_CONFIG_MARKER | (static_cast<uint32_t>(type) << INTERFACE_TYPE_SHIFT) |
         (static_cast<uint32_t>(index) & INTERFACE_INDEX_MASK);
}

// A stored type mismatch means the board table changed shape under the config.
constexpr const InterfaceDescriptor* resolve_interface_config(InterfaceList list, uint32_t packed) {
  constexpr uint32_t valid_bits =
      INTERFACE_CONFIG_MARKER | (INTERFACE_TYPE_MASK << INTERFACE_TYPE_SHIFT) | INTERFACE_INDEX_MASK;
  if ((packed & ~valid_bits) != 0 || (packed & INTERFACE_CONFIG_MARKER) == 0) {
    return nullptr;
  }
  size_t index = packed & INTERFACE_INDEX_MASK;
  auto type = static_cast<InterfaceType>((packed >> INTERFACE_TYPE_SHIFT) & INTERFACE_TYPE_MASK);
  if (index >= list.count || list.data[index].type != type) {
    return nullptr;
  }
  return &list.data[index];
}

// Modbus is a protocol carried on an RS-485 port, not a selectable
// interface; the role's protocol type picks the stack.
constexpr bool descriptor_selectable(const InterfaceDescriptor& desc) {
  return desc.type != InterfaceType::Modbus;
}

// Schema version 2: a stored Modbus selection becomes the Rs485 row bound
// to the same port. Anything else passes through unchanged.
constexpr uint32_t consolidate_modbus_config(InterfaceList list, uint32_t packed) {
  const InterfaceDescriptor* desc = resolve_interface_config(list, packed);
  if (desc == nullptr || desc->type != InterfaceType::Modbus || desc->rs485_port == nullptr) {
    return packed;
  }
  for (size_t i = 0; i < list.count; i++) {
    if (list.data[i].type == InterfaceType::Rs485 && list.data[i].rs485_port == desc->rs485_port) {
      return pack_interface_config(InterfaceType::Rs485, i);
    }
  }
  return packed;
}

// The CanNative descriptor (statically asserted per board) doubles as the
// unset-key default, matching the legacy default.
constexpr uint32_t default_interface_config(InterfaceList list) {
  const InterfaceDescriptor* native = find_by_type(list, InterfaceType::CanNative);
  return native ? pack_interface_config(native->type, static_cast<size_t>(native - list.data))
                : pack_interface_config(InterfaceType::CanNative, 0);
}

// Unmappable or out-of-range legacy values fall back to the default,
// matching the old readIf fallback.
constexpr uint32_t migrate_interface_config(InterfaceList list, uint32_t legacy_value) {
  if ((legacy_value & INTERFACE_CONFIG_MARKER) != 0) {
    return legacy_value;
  }
  if (legacy_value == 0 || legacy_value >= static_cast<uint32_t>(comm_interface::Highest)) {
    return default_interface_config(list);
  }
  const InterfaceDescriptor* desc = find_by_legacy(list, static_cast<comm_interface>(legacy_value));
  return desc ? pack_interface_config(desc->type, static_cast<size_t>(desc - list.data))
              : default_interface_config(list);
}

#endif
