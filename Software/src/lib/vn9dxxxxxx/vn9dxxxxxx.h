/*
 * ST VN9Dxxxxxx multichannel high-side driver (VN9D5D20FN, VN9D30Q100F).
 *
 * Register protocol, watchdog handling and conversion maths ported from
 * Wired Square's Zephyr ws-vn9dxxxxxx driver
 * (SPDX-License-Identifier: Apache-2.0), reshaped onto Arduino SPI/LEDC.
 */

#ifndef _VN9DXXXXXX_H_
#define _VN9DXXXXXX_H_

#include <SPI.h>
#include <soc/gpio_num.h>
#include <stdint.h>

#include "../../devboard/hal/LoadSwitch.h"
#include "../../devboard/hal/SwitchedOutput.h"

// SPI opcodes (frame byte 0, bits 7:6).
inline constexpr uint8_t VN9DX_OP_WRITE = 0x00;
inline constexpr uint8_t VN9DX_OP_READ = 0x40;
inline constexpr uint8_t VN9DX_OP_READ_ROM = 0xC0;

// Global status byte bits.
inline constexpr uint8_t VN9DX_GSB_SPIE_BIT = 5;
inline constexpr uint8_t VN9DX_GSB_FS_BIT = 0;

inline constexpr uint8_t VN9DX_PARITY_BIT = 0;
inline constexpr uint8_t VN9DX_WDTB_BIT = 1;

// RAM register addresses.
inline constexpr uint8_t VN9DX_RAM_REG_OUTCTRCR0 = 0x00;
inline constexpr uint8_t VN9DX_RAM_REG_OUTCTRCR5 = 0x05;
inline constexpr uint8_t VN9DX_RAM_REG_OUTCFGR0 = 0x08;
inline constexpr uint8_t VN9DX_RAM_REG_CHLOFFTCR0 = 0x10;
inline constexpr uint8_t VN9DX_RAM_REG_CHLOFFTCR1 = 0x11;
inline constexpr uint8_t VN9DX_RAM_REG_SOCR = 0x13;
inline constexpr uint8_t VN9DX_RAM_REG_CTRL = 0x14;
inline constexpr uint8_t VN9DX_RAM_REG_OUTSR0 = 0x20;
inline constexpr uint8_t VN9DX_RAM_REG_ADC0SR = 0x28;
inline constexpr uint8_t VN9DX_RAM_REG_ADC9SR = 0x31;

// ROM addresses and product codes.
inline constexpr uint8_t VN9DX_ROM_REG_PC3 = 0x04;
inline constexpr uint8_t PC3_VN9D5D20FN = 0x61;
inline constexpr uint8_t PC3_VN9D30Q100F = 0x63;

// Register fields.
inline constexpr uint8_t VN9DX_DUTYCR0_BIT = 4;
inline constexpr uint8_t VN9DX_PWMFCY0_BIT = 4;
inline constexpr uint16_t VN9DX_PWMFCY_MASK = 0x3u << VN9DX_PWMFCY0_BIT;
inline constexpr uint16_t VN9DX_CHPHA_MASK = 0x1F00;
inline constexpr uint16_t VN9DX_SLOPECR_MASK = 0xC000;
inline constexpr uint8_t VN9DX_SOCR0_BIT = 8;
inline constexpr uint16_t VN9DX_CTRL_EN = 1u << 11;
inline constexpr uint16_t VN9DX_CTRL_UNLOCK = 1u << 14;
inline constexpr uint8_t VN9DX_OUTSR_VCCUV_BIT = 4;
inline constexpr uint8_t VN9DX_OUTSR_CHLOFFSR_BIT = 8;
inline constexpr uint8_t VN9DX_OUTSR_OLPUSR_BIT = 9;
inline constexpr uint8_t VN9DX_OUTSR_STKFLTR_BIT = 10;
inline constexpr uint8_t VN9DX_OUTSR_CHFBSR_BIT = 12;
inline constexpr uint8_t VN9DX_ADC_VALUE_SHIFT = 4;
inline constexpr uint16_t VN9DX_ADC_VALUE_MASK = 0x3FF;

// PWMFCY encoding is 00:÷1024, 01:÷2048, 10:÷4096, 11:÷512; indexed here
// by the LSDIV<n> setting order ÷512/1024/2048/4096.
inline constexpr uint8_t kVn9dDivisorCodeToPwmfcy[kLoadSwitchDivisorCodes] = {3, 0, 1, 2};

// Bulb-mode current sense gain (ADCOUT/IOUT) in 1/A — DS13579 Tables 57-58.
inline constexpr uint32_t VN9DX_K_BULB_CH01 = 36;
inline constexpr uint32_t VN9DX_K_BULB_CH23 = 89;

inline constexpr uint32_t kVn9dSpiHz = 5000000;
// 400 kHz 50% PWM engine clock: 80 MHz LEDC source / 400 kHz = 200 counts,
// so 7 bits is the highest clean resolution; duty 64 = 50%.
inline constexpr uint32_t kVn9dPwmClockHz = 400000;
inline constexpr uint8_t kVn9dPwmClockResolutionBits = 7;
inline constexpr uint32_t kVn9dPwmClockDuty = 64;

// Fixed-point frame-temperature constants (Zephyr driver): 401.8 °C
// intercept in 0.1 °C, 1.009 slope in Q16.
inline constexpr int32_t kVn9dTempInterceptDeciC = 4018;
inline constexpr uint32_t kVn9dTempSlopeQ16 = 66127;
inline constexpr uint32_t kVn9dMilliampsPerAmp = 1000;

// Odd parity over frame bits 23..1; the result lands in data bit 0.
int vn9dx_calculate_parity(uint32_t frame);

struct Vn9dPins {
  gpio_num_t sck;
  gpio_num_t miso;  // chip SDO
  gpio_num_t mosi;  // chip SDI
  gpio_num_t cs;
};

class Vn9d : public LoadSwitch {
 public:
  Vn9d(uint8_t spi_bus, Vn9dPins pins, gpio_num_t pwm_clk_pin, uint8_t ledc_channel)
      : spi_bus_(spi_bus), pins_(pins), pwm_clk_pin_(pwm_clk_pin), ledc_channel_(ledc_channel) {}

  // Full bring-up: fail-safe exit, first watchdog feed, device detect,
  // latch-off configuration, divisor programming. On failure raises
  // EVENT_LOAD_SWITCH_INIT_FAILURE (data = failing register address) and
  // leaves the device in fail-safe with all outputs open.
  bool init();

  void set_channel_config(uint8_t channel, LoadSwitchRole role, uint16_t duty, uint8_t divisor_code) override;
  void tick() override;
  const LoadSwitchStatus& status() override { return status_; }
  uint32_t pwm_clock_hz() const override { return kVn9dPwmClockHz; }
  void request_manual(uint8_t channel, bool on) override;
  void request_duty(uint8_t channel, uint16_t duty) override;
  void request_divisor(uint8_t channel, uint8_t divisor_code) override;

  // Contactor-role channel control. Core-loop task only — every VN9D SPI
  // access shares that one context.
  void engage(uint8_t channel);
  void disengage(uint8_t channel);
  void hold(uint8_t channel);

  LoadSwitchRole channel_role(uint8_t channel) const { return config_[channel].role; }

  // Exposed for host tests.
  bool transceive(uint8_t opcode, uint8_t address, uint16_t data, uint8_t* status_byte, uint16_t* data_out);

 private:
  struct ChannelConfig {
    LoadSwitchRole role = LoadSwitchRole::Disabled;
    uint16_t duty = kLoadSwitchDutyMax;
    uint8_t divisor_code = 0;
  };
  enum class ChannelState : uint8_t { Off, PullIn, Steady };

  bool fail_init(uint8_t address);
  bool write_register(uint8_t address, uint16_t data);
  bool read_register(uint8_t address, uint16_t* data_out);
  bool protected_write(uint8_t address, uint16_t data);
  bool set_channel_duty(uint8_t channel, uint16_t duty);
  bool apply_divisor(uint8_t channel, uint8_t divisor_code);
  void apply_pending_requests();
  void resolve_duplicate_roles();

  SPIClass* spi_ = nullptr;
  uint8_t spi_bus_;
  Vn9dPins pins_;
  gpio_num_t pwm_clk_pin_;
  uint8_t ledc_channel_;
  bool last_wdt_bit_ = false;
  uint8_t enabled_bitmap_ = 0;
  uint16_t outcfgr_[kLoadSwitchMaxChannels] = {};
  ChannelConfig config_[kLoadSwitchMaxChannels];
  ChannelState state_[kLoadSwitchMaxChannels] = {};
  LoadSwitchStatus status_ = {};

  // Single-writer request flags (webserver task) consumed by the tick.
  volatile bool pending_manual_[kLoadSwitchMaxChannels] = {};
  volatile bool pending_manual_on_[kLoadSwitchMaxChannels] = {};
  volatile bool pending_duty_[kLoadSwitchMaxChannels] = {};
  volatile uint16_t pending_duty_value_[kLoadSwitchMaxChannels] = {};
  volatile bool pending_divisor_[kLoadSwitchMaxChannels] = {};
  volatile uint8_t pending_divisor_value_[kLoadSwitchMaxChannels] = {};
};

class Vn9dOutput : public SwitchedOutput {
 public:
  Vn9dOutput(Vn9d& device, uint8_t channel) : device_(device), channel_(channel) {}

  bool init(const char* /*owner*/) override { return device_.status().device_ok; }
  void set(bool on) override {
    if (on) {
      device_.engage(channel_);
    } else {
      device_.disengage(channel_);
    }
  }
  void set_hold() override { device_.hold(channel_); }
  bool fault() override {
    const LoadSwitchChannelStatus& channel = device_.status().channels[channel_];
    return channel.fault || channel.latched_off;
  }
  bool level() override { return device_.status().channels[channel_].on; }

 private:
  Vn9d& device_;
  uint8_t channel_;
};

#endif
