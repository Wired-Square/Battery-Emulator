cmake_minimum_required(VERSION 3.28)
enable_testing()

# set the project name
project(UnitTests)

# Enable ExternalProject CMake module
include(ExternalProject)
include(GoogleTest)

# specify the C++ standard
# At least 17 is required by Gtest
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Download and install GoogleTest
ExternalProject_Add(
    gtest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
    PREFIX ${CMAKE_CURRENT_BINARY_DIR}/gtest
    # Disable install step
    INSTALL_COMMAND ""
)

# Get GTest source and binary directories from CMake project
ExternalProject_Get_Property(gtest source_dir binary_dir)

# Create a libgtest target to be used as a dependency by test programs
add_library(libgtest IMPORTED STATIC GLOBAL)
add_dependencies(libgtest gtest)
add_definitions(-DUNIT_TEST)

# Set libgtest properties
if(WIN32)
    set_target_properties(libgtest PROPERTIES
        "IMPORTED_LOCATION" "${binary_dir}/lib/Debug/gtest.lib"
        "IMPORTED_LINK_INTERFACE_LIBRARIES" "${CMAKE_THREAD_LIBS_INIT}")
else()
    set_target_properties(libgtest PROPERTIES
        "IMPORTED_LOCATION" "${binary_dir}/lib/libgtest.a"
        "IMPORTED_LINK_INTERFACE_LIBRARIES" "${CMAKE_THREAD_LIBS_INIT}")
endif(WIN32)

# Create a libgmock target to be used as a dependency by test programs
add_library(libgmock IMPORTED STATIC GLOBAL)
add_dependencies(libgmock gtest)

# Set libgmock properties
if(WIN32)
    set_target_properties(libgmock PROPERTIES
        "IMPORTED_LOCATION" "${binary_dir}/lib/Debug/gmock.lib"
        "IMPORTED_LINK_INTERFACE_LIBRARIES" "${CMAKE_THREAD_LIBS_INIT}")
else()
    set_target_properties(libgmock PROPERTIES
        "IMPORTED_LOCATION" "${binary_dir}/lib/libgmock.a"
        "IMPORTED_LINK_INTERFACE_LIBRARIES" "${CMAKE_THREAD_LIBS_INIT}")
endif(WIN32)

# I couldn't make it work with INTERFACE_INCLUDE_DIRECTORIES
include_directories("${source_dir}/googletest/include"
                    "${source_dir}/googlemock/include")

include_directories(emul)

# For eModBus
add_compile_definitions(ESP32 HW_LILYGO COMMON_IMAGE BOARD_HAS_LOAD_SWITCH)

# add the executable
add_executable(tests
    tests.cpp
    emul_preferences_tests.cpp
    settings_api_tests.cpp
    web_ui_selection_tests.cpp
    emul/web_shells.cpp
    webserver/logging_api_tests.cpp
    webserver/events_api_tests.cpp
    webserver/canreplay_api_tests.cpp
    webserver/cellmonitor_api_tests.cpp
    webserver/advanced_api_tests.cpp
    voltage_sync_tests.cpp
    battery_slot_tests.cpp
    battery_combined_tests.cpp
    battery_slot_context_tests.cpp
    battery_ctor_tests.cpp
    inverter_registry_tests.cpp
    inverter/sma_frame_tests.cpp
    charger_registry_tests.cpp
    shunt_setup_tests.cpp
    offgrid_downgrade_tests.cpp
    bms_reset_tests.cpp
    safety_balancing_tests.cpp
    safety_pack_tests.cpp
    inverter_alive_tests.cpp
    millis_wrap_tests.cpp
    estop_graceful_open_tests.cpp
    contactor_sequence_tests.cpp
    precharge_control_tests.cpp
    nvm_migration_tests.cpp
    hal_interface_tests.cpp
    interface_descriptor_tests.cpp
    can_bus_tests.cpp
    rs485_port_tests.cpp
    tca9538_tests.cpp
    wfl_board_tests.cpp
    gpio_output_tests.cpp
    gpio_options_tests.cpp
    pwm_tone_tests.cpp
    switched_output_board_tests.cpp
    lilygo2can_board_tests.cpp
    becom_board_tests.cpp
    waveshare_board_tests.cpp
    vn9dxxxxxx_tests.cpp
    battery/FordMachETest.cpp
    battery_alive_tests.cpp
    battery/NissanLeafTest.cpp
    battery/BydAtto3Test.cpp
    battery/foxess_tests.cpp
    battery/still_alive_tests.cpp
    battery/TeslaTest.cpp
    battery/TestFakeTest.cpp
    battery/VolvoSPATest.cpp
    battery/UdsCanBatteryTest.cpp
    can_log_based/canlog_safety_tests.cpp
    utils/utils.cpp
    ../Software/src/devboard/safety/parallel_safety.cpp
    ../Software/src/communication/can/CanBus.cpp
    ../Software/src/lib/tca9538/tca9538.cpp
    ../Software/src/lib/vn9dxxxxxx/vn9dxxxxxx.cpp
    ../Software/src/communication/contactorcontrol/comm_contactorcontrol.cpp
    ../Software/src/communication/precharge_control/precharge_control.cpp
    ../Software/src/communication/rs485/comm_rs485.cpp
    ../Software/src/communication/rs485/Rs485Port.cpp
    ../Software/src/devboard/safety/safety.cpp
    ../Software/src/devboard/hal/hal.cpp
    ../Software/src/devboard/hal/gpio_options.cpp
    ../Software/src/devboard/webserver/settings_api.cpp
    ../Software/src/devboard/webserver/web_ui_selection.cpp
    ../Software/src/devboard/webserver/logging_api.cpp
    ../Software/src/devboard/webserver/events_api.cpp
    ../Software/src/devboard/webserver/canreplay_api.cpp
    ../Software/src/devboard/webserver/cellmonitor_api.cpp
    ../Software/src/devboard/webserver/advanced_api.cpp
    ../Software/src/devboard/webserver/web_json.cpp
    ../Software/src/devboard/hal/GpioOutput.cpp
    ../Software/src/devboard/hal/PwmTone.cpp
    ../Software/src/devboard/utils/types.cpp
    ../Software/src/devboard/utils/events.cpp
    ../Software/src/devboard/utils/time_format.cpp
    ../Software/src/devboard/utils/common_functions.cpp
    ../Software/src/datalayer/battery_combined.cpp
    ../Software/src/devboard/utils/led_handler.cpp
    ../Software/src/lib/adafruit-Adafruit_NeoPixel/Adafruit_NeoPixel.cpp
    emul/neopixel_show.cpp
    ../Software/src/datalayer/datalayer.cpp
    ../Software/src/datalayer/datalayer_extended.cpp
    ../Software/src/lib/uds_isotp/isotp.cpp
    ../Software/src/lib/eModbus-eModbus/ModbusMessage.cpp
    ../Software/src/lib/eModbus-eModbus/ModbusServer.cpp
    ../Software/src/lib/eModbus-eModbus/ModbusServerRTU.cpp
    ../Software/src/lib/eModbus-eModbus/ModbusTypeDefs.cpp
    ../Software/src/lib/eModbus-eModbus/RTUutils.cpp
    ../Software/src/battery/BATTERIES.cpp
    ../Software/src/battery/Battery.cpp
    ../Software/src/battery/BMW-I3-BATTERY.cpp
    ../Software/src/battery/BMW-IX-BATTERY.cpp
    ../Software/src/battery/BMW-PHEV-BATTERY.cpp
    ../Software/src/battery/BMW-SBOX.cpp
    ../Software/src/battery/BOLT-AMPERA-BATTERY.cpp
    ../Software/src/battery/BYD-ATTO-3-BATTERY.cpp
    ../Software/src/battery/CanBattery.cpp
    ../Software/src/battery/CELLPOWER-BMS.cpp
    ../Software/src/battery/CHARGEBYTE-CCS.cpp
    ../Software/src/battery/CHADEMO-BATTERY.cpp
    ../Software/src/battery/CHADEMO-CT.cpp
    ../Software/src/battery/CHADEMO-SHUNTS.cpp
    ../Software/src/battery/CMFA-EV-BATTERY.cpp
    ../Software/src/battery/CMP-SMART-CAR-BATTERY.cpp
    ../Software/src/battery/DALY-BMS.cpp
    ../Software/src/battery/ECMP-BATTERY.cpp
    ../Software/src/battery/ENNOID-BMS.cpp
    ../Software/src/battery/FORD-MACH-E-BATTERY.cpp
    ../Software/src/battery/FOXESS-BATTERY.cpp
    ../Software/src/battery/GEELY-GEOMETRY-C-BATTERY.cpp
    ../Software/src/battery/GROWATT-HV-ARK-BATTERY.cpp
    ../Software/src/battery/HYUNDAI-IONIQ-28-BATTERY.cpp
    ../Software/src/battery/IMIEV-CZERO-ION-BATTERY.cpp
    ../Software/src/battery/JAGUAR-IPACE-BATTERY.cpp
    ../Software/src/battery/KIA-E-GMP-BATTERY.cpp
    ../Software/src/battery/KIA-64FD-BATTERY.cpp
    ../Software/src/battery/KIA-HYUNDAI-64-BATTERY.cpp
    ../Software/src/battery/KIA-HYUNDAI-HYBRID-BATTERY.cpp
    ../Software/src/battery/MEB-BATTERY.cpp
    ../Software/src/battery/MG-5-BATTERY.cpp
    ../Software/src/battery/MG-GEN1-BATTERY.cpp
    ../Software/src/battery/NISSAN-LEAF-BATTERY.cpp
    ../Software/src/battery/ORION-BMS.cpp
    ../Software/src/battery/PYLON-BATTERY.cpp
    ../Software/src/battery/RANGE-ROVER-PHEV-BATTERY.cpp
    ../Software/src/battery/RELION-LV-BATTERY.cpp
    ../Software/src/battery/RENAULT-KANGOO-BATTERY.cpp
    ../Software/src/battery/RENAULT-TWIZY.cpp
    ../Software/src/battery/RENAULT-ZOE-GEN1-BATTERY.cpp
    ../Software/src/battery/RENAULT-ZOE-GEN2-BATTERY.cpp
    ../Software/src/battery/RIVIAN-BATTERY.cpp
    ../Software/src/battery/RJXZS-BMS.cpp
    ../Software/src/battery/SAMSUNG-SDI-LV-BATTERY.cpp
    ../Software/src/battery/SANTA-FE-PHEV-BATTERY.cpp
    ../Software/src/battery/Shunts.cpp
    ../Software/src/battery/SIMPBMS-BATTERY.cpp
    ../Software/src/battery/SONO-BATTERY.cpp
    ../Software/src/battery/STELLANTIS-SMALL-WIDE-4x4.cpp
    ../Software/src/battery/TESLA-BATTERY.cpp
    ../Software/src/battery/TESLA-LEGACY-BATTERY.cpp
    ../Software/src/battery/TEST-FAKE-BATTERY.cpp
    ../Software/src/battery/THINK-BATTERY.cpp
    ../Software/src/battery/THUNDERSTRUCK-BMS.cpp
    ../Software/src/battery/GEELY-SEA-BATTERY.cpp
    ../Software/src/battery/UdsCanBattery.cpp
    ../Software/src/battery/VOLVO-SPA-BATTERY.cpp
    ../Software/src/battery/VOLVO-SPA-HYBRID-BATTERY.cpp
    ../Software/src/inverter/AFORE-CAN.cpp
    ../Software/src/inverter/BYD-CAN.cpp
    ../Software/src/inverter/BYD-MODBUS.cpp
    ../Software/src/inverter/FERROAMP-CAN.cpp
    ../Software/src/inverter/FOXESS-CAN.cpp
    ../Software/src/inverter/GROWATT-HV-CAN.cpp
    ../Software/src/inverter/GROWATT-LV-CAN.cpp
    ../Software/src/inverter/GROWATT-WIT-CAN.cpp
    ../Software/src/inverter/INVERTERS.cpp
    ../Software/src/inverter/KOSTAL-RS485.cpp
    ../Software/src/inverter/ModbusInverterProtocol.cpp
    ../Software/src/inverter/PYLON-CAN.cpp
    ../Software/src/inverter/PYLON-LV-RS485.cpp
    ../Software/src/inverter/PYLON-LV-CAN.cpp
    ../Software/src/inverter/SCHNEIDER-CAN.cpp
    ../Software/src/inverter/SMA-BYD-H-CAN.cpp
    ../Software/src/inverter/SMA-BYD-HVS-CAN.cpp
    ../Software/src/inverter/SMA-SBS-BYD-CAN.cpp
    ../Software/src/inverter/SMA-LV-CAN.cpp
    ../Software/src/inverter/SmaInverterBase.cpp
    ../Software/src/inverter/SOFAR-CAN.cpp
    ../Software/src/inverter/SOL-ARK-LV-CAN.cpp
    ../Software/src/inverter/SOLAX-CAN.cpp
    ../Software/src/inverter/SOLXPOW-CAN.cpp
    ../Software/src/inverter/SUNGROW-CAN.cpp
    ../Software/src/inverter/VCU-CAN.cpp
    ../Software/src/charger/CHARGERS.cpp
    ../Software/src/charger/CHEVY-VOLT-CHARGER.cpp
    ../Software/src/charger/NISSAN-LEAF-CHARGER.cpp
    emul/can.cpp
    emul/time.cpp
    emul/serial.cpp
    emul/wifi.cpp
    emul/Arduino.cpp
    emul/Wire.cpp
    emul/SPI.cpp
    emul/freertos/FreeRTOS.cpp
    emul/driver/i2c_master.cpp
    )

set_source_files_properties(lilygo2can_board_tests.cpp PROPERTIES COMPILE_DEFINITIONS "HW_LILYGO2CAN")
set_source_files_properties(switched_output_board_tests.cpp PROPERTIES COMPILE_DEFINITIONS "HW_STARK")
set_source_files_properties(becom_board_tests.cpp PROPERTIES COMPILE_DEFINITIONS "HW_BECOM")
set_source_files_properties(waveshare_board_tests.cpp PROPERTIES COMPILE_DEFINITIONS "HW_WAVESHARE")

target_link_libraries(tests
    libgtest
    libgmock
)

# Define the path for tests
target_compile_definitions(tests PRIVATE 
    TEST_CAN_LOG_DIR="${CMAKE_SOURCE_DIR}/can_log_based/can_logs"
)

gtest_discover_tests(tests)
