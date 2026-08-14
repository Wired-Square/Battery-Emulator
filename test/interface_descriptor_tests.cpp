#include <gtest/gtest.h>

#include <vector>

#include "../Software/src/communication/rs485/Rs485Port.h"
#include "../Software/src/devboard/hal/interface_descriptor.h"

namespace {

// Synthetic board: two same-type FD interfaces (the "_2" case) + native CAN.
constexpr InterfaceDescriptor kTestTable[] = {
    {InterfaceType::CanNative, nullptr, comm_interface::CanNative, nullptr},
    {InterfaceType::CanMcp2517fd, "FD One", comm_interface::CanFdAddonMcp2518, nullptr},
    {InterfaceType::CanMcp2517fd, "FD Two", comm_interface::CanFdAddonMcp2518_2, nullptr},
};
constexpr InterfaceList kTestList = make_interface_list(kTestTable);

static_assert(find_by_legacy(kTestList, comm_interface::CanNative) != nullptr,
              "constexpr find must work on constexpr tables");

}  // namespace

namespace {
Rs485Port test_serial_port{Serial2, UART_NUM_2, {GPIO_NUM_22, GPIO_NUM_21, GPIO_NUM_NC}};

constexpr InterfaceDescriptor kSerialTable[] = {
    {InterfaceType::Modbus, nullptr, comm_interface::Modbus, nullptr, &test_serial_port},
    {InterfaceType::Rs485, nullptr, comm_interface::RS485, nullptr, &test_serial_port},
    {InterfaceType::CanNative, nullptr, comm_interface::CanNative, nullptr},
};
}  // namespace

TEST(InterfaceDescriptorTest, DefaultNamesPerType) {
  EXPECT_STREQ(default_name_for_type(InterfaceType::CanNative), "CAN (Native)");
  EXPECT_STREQ(default_name_for_type(InterfaceType::CanMcp2515), "CAN (MCP2515 add-on)");
  EXPECT_STREQ(default_name_for_type(InterfaceType::CanMcp2517fd), "CAN FD (MCP2518 add-on)");
  EXPECT_STREQ(default_name_for_type(InterfaceType::Rs485), "RS485");
  EXPECT_STREQ(default_name_for_type(InterfaceType::Modbus), "Modbus");
}

TEST(InterfaceDescriptorTest, DescriptorNameFallsBackToTypeDefault) {
  EXPECT_STREQ(descriptor_name(kTestTable[0]), "CAN (Native)");
  EXPECT_STREQ(descriptor_name(kTestTable[1]), "FD One");
  EXPECT_STREQ(descriptor_name(kTestTable[2]), "FD Two");
}

TEST(InterfaceDescriptorTest, PackResolveRoundTrip) {
  for (size_t i = 0; i < kTestList.count; i++) {
    uint32_t packed = pack_interface_config(kTestList.data[i].type, i);
    EXPECT_EQ(resolve_interface_config(kTestList, packed), &kTestList.data[i]);
  }
}

TEST(InterfaceDescriptorTest, ResolveRejectsOutOfRangeIndex) {
  EXPECT_EQ(resolve_interface_config(kTestList, pack_interface_config(InterfaceType::CanNative, 3)), nullptr);
}

TEST(InterfaceDescriptorTest, ResolveRejectsTypeMismatch) {
  // Index 1 is CanMcp2517fd; a stored CanNative there is a shape change.
  EXPECT_EQ(resolve_interface_config(kTestList, pack_interface_config(InterfaceType::CanNative, 1)), nullptr);
}

TEST(InterfaceDescriptorTest, DefaultIsTheNativeCanDescriptor) {
  EXPECT_EQ(default_interface_config(kTestList), pack_interface_config(InterfaceType::CanNative, 0));
}

TEST(InterfaceDescriptorTest, MigrateMapsLegacyValuesToIndexes) {
  EXPECT_EQ(migrate_interface_config(kTestList, (uint32_t)comm_interface::CanNative),
            pack_interface_config(InterfaceType::CanNative, 0));
  EXPECT_EQ(migrate_interface_config(kTestList, (uint32_t)comm_interface::CanFdAddonMcp2518),
            pack_interface_config(InterfaceType::CanMcp2517fd, 1));
  EXPECT_EQ(migrate_interface_config(kTestList, (uint32_t)comm_interface::CanFdAddonMcp2518_2),
            pack_interface_config(InterfaceType::CanMcp2517fd, 2));
}

TEST(InterfaceDescriptorTest, MigrateFallsBackToDefault) {
  // Legacy value not in this board's table.
  EXPECT_EQ(migrate_interface_config(kTestList, (uint32_t)comm_interface::Modbus), default_interface_config(kTestList));
  EXPECT_EQ(migrate_interface_config(kTestList, 0), default_interface_config(kTestList));
  EXPECT_EQ(migrate_interface_config(kTestList, 99), default_interface_config(kTestList));
}

TEST(InterfaceDescriptorTest, ResolveRejectsUnmarkedAndOversizedValues) {
  // Legacy RS485 (2) numerically equals an unmarked packed {CanNative, 2}.
  EXPECT_EQ(resolve_interface_config(kTestList, 2), nullptr);
  EXPECT_EQ(resolve_interface_config(kTestList, 65538), nullptr);
}

TEST(InterfaceDescriptorTest, MigrationIsIdempotent) {
  for (size_t i = 0; i < kTestList.count; i++) {
    uint32_t packed = pack_interface_config(kTestList.data[i].type, i);
    EXPECT_EQ(migrate_interface_config(kTestList, packed), packed);
  }
}

TEST(InterfaceDescriptorTest, FindByTypeReturnsFirstOfType) {
  EXPECT_EQ(find_by_type(kTestList, InterfaceType::CanNative), &kTestTable[0]);
  EXPECT_EQ(find_by_type(kTestList, InterfaceType::CanMcp2517fd), &kTestTable[1]);
  EXPECT_EQ(find_by_type(kTestList, InterfaceType::Rs485), nullptr);
}

TEST(InterfaceDescriptorTest, ModbusRowsAreNotSelectable) {
  InterfaceList list = make_interface_list(kSerialTable);
  EXPECT_FALSE(descriptor_selectable(list.data[0]));
  EXPECT_TRUE(descriptor_selectable(list.data[1]));
  EXPECT_TRUE(descriptor_selectable(list.data[2]));
}

TEST(InterfaceDescriptorTest, ConsolidateMapsModbusToSamePortRs485Row) {
  InterfaceList list = make_interface_list(kSerialTable);
  uint32_t modbus = pack_interface_config(InterfaceType::Modbus, 0);
  EXPECT_EQ(consolidate_modbus_config(list, modbus), pack_interface_config(InterfaceType::Rs485, 1));
}

TEST(InterfaceDescriptorTest, ConsolidatePassesThroughNonModbus) {
  InterfaceList list = make_interface_list(kSerialTable);
  uint32_t rs485 = pack_interface_config(InterfaceType::Rs485, 1);
  uint32_t native = pack_interface_config(InterfaceType::CanNative, 2);
  EXPECT_EQ(consolidate_modbus_config(list, rs485), rs485);
  EXPECT_EQ(consolidate_modbus_config(list, native), native);
  EXPECT_EQ(consolidate_modbus_config(list, 0xDEAD0000), 0xDEAD0000u);
}

TEST(InterfaceDescriptorTest, LegacyModbusMigratesThenConsolidates) {
  InterfaceList list = make_interface_list(kSerialTable);
  uint32_t migrated = migrate_interface_config(list, (uint32_t)comm_interface::Modbus);
  EXPECT_EQ(migrated, pack_interface_config(InterfaceType::Modbus, 0));
  EXPECT_EQ(consolidate_modbus_config(list, migrated), pack_interface_config(InterfaceType::Rs485, 1));
}

namespace {
Rs485Port iter_port_a{Serial2, UART_NUM_2, {GPIO_NUM_22, GPIO_NUM_21, GPIO_NUM_NC}};
Rs485Port iter_port_b{Serial2, UART_NUM_2, {GPIO_NUM_17, GPIO_NUM_16, GPIO_NUM_NC}};

// Modbus aliases port A (dedupe); port B is a second distinct port.
constexpr InterfaceDescriptor kIterTable[] = {
    {InterfaceType::CanNative, nullptr, comm_interface::CanNative, nullptr},
    {InterfaceType::Rs485, nullptr, comm_interface::RS485, nullptr, &iter_port_a},
    {InterfaceType::Modbus, nullptr, comm_interface::Modbus, nullptr, &iter_port_a},
    {InterfaceType::Rs485, "RS485 (2)", comm_interface::Highest, nullptr, &iter_port_b},
};
constexpr InterfaceList kIterList = make_interface_list(kIterTable);
}  // namespace

TEST(ForEachUniqueBinding, VisitsEachDistinctBindingOnceInTableOrder) {
  std::vector<Rs485Port*> visited;
  for_each_unique_binding(kIterList, &InterfaceDescriptor::rs485_port, [&](Rs485Port* port) {
    visited.push_back(port);
    return true;
  });
  ASSERT_EQ(visited.size(), 2u);
  EXPECT_EQ(visited[0], &iter_port_a);
  EXPECT_EQ(visited[1], &iter_port_b);
}

TEST(ForEachUniqueBinding, StopsWhenCallbackReturnsFalse) {
  size_t visits = 0;
  for_each_unique_binding(kIterList, &InterfaceDescriptor::rs485_port, [&](Rs485Port*) {
    visits++;
    return false;
  });
  EXPECT_EQ(visits, 1u);
}

TEST(ForEachUniqueBinding, SkipsTablesWithNoBindings) {
  size_t visits = 0;
  for_each_unique_binding(kIterList, &InterfaceDescriptor::can_bus, [&](CanBus*) {
    visits++;
    return true;
  });
  EXPECT_EQ(visits, 0u);
}
