/*
 * Ported from Wired Square's Zephyr ws-vn9dxxxxxx driver
 * (SPDX-License-Identifier: Apache-2.0) — see vn9dxxxxxx.h.
 */

#include "vn9dxxxxxx.h"

#include <Arduino.h>

#include "../../communication/contactorcontrol/comm_contactorcontrol.h"
#include "../../devboard/hal/hal.h"
#include "../../devboard/utils/events.h"
#include "../../devboard/utils/logging.h"

int vn9dx_calculate_parity(uint32_t frame) {
  frame &= 0xFFFFFE;

  frame ^= frame >> 1;
  frame ^= frame >> 2;
  frame ^= frame >> 4;
  frame ^= frame >> 8;
  frame ^= frame >> 16;

  return (frame & 1) ^ 1;
}

bool Vn9d::transceive(uint8_t opcode, uint8_t address, uint16_t data, uint8_t* status_byte, uint16_t* data_out) {
  if (spi_ == nullptr) {
    return false;
  }

  // WDTB lives only in SOCR and OUTCTRCR0-5 — bit 1 means PWMSYNC/VDSMASK/
  // reserved elsewhere. These writes double as the watchdog feed.
  if (opcode == VN9DX_OP_WRITE && (address == VN9DX_RAM_REG_SOCR || address <= VN9DX_RAM_REG_OUTCTRCR5)) {
    if (last_wdt_bit_) {
      data |= (uint16_t)(1u << VN9DX_WDTB_BIT);
    } else {
      data &= (uint16_t)~(1u << VN9DX_WDTB_BIT);
    }
    last_wdt_bit_ = !last_wdt_bit_;
  }

  uint8_t header = opcode | address;
  int parity = vn9dx_calculate_parity(((uint32_t)header << 16) | data);
  data = (uint16_t)((data & ~(1u << VN9DX_PARITY_BIT)) | parity);

  uint8_t tx[3] = {header, (uint8_t)(data >> 8), (uint8_t)data};
  uint8_t rx[3] = {0};

  spi_->beginTransaction(SPISettings(kVn9dSpiHz, MSBFIRST, SPI_MODE0));
  digitalWrite(pins_.cs, LOW);
  spi_->transferBytes(tx, rx, sizeof(tx));
  digitalWrite(pins_.cs, HIGH);
  spi_->endTransaction();

  uint8_t gsb = rx[0];
  uint16_t response = (uint16_t)((rx[1] << 8) | rx[2]);
  status_.gsb = gsb;

  if (opcode != VN9DX_OP_READ_ROM &&
      vn9dx_calculate_parity(((uint32_t)gsb << 16) | response) != (int)(response & (1u << VN9DX_PARITY_BIT))) {
    DEBUG_PRINTF("VN9D response parity error %02X:%04X\n", gsb, response);
    return false;
  }

  if (gsb & (1u << VN9DX_GSB_SPIE_BIT)) {
    DEBUG_PRINTF("VN9D reports SPI error (GSB=0x%02X)\n", gsb);
  }

  if (status_byte != nullptr) {
    *status_byte = gsb;
  }
  if (data_out != nullptr) {
    *data_out = response;
  }
  return true;
}

bool Vn9d::write_register(uint8_t address, uint16_t data) {
  return transceive(VN9DX_OP_WRITE, address, data, nullptr, nullptr);
}

bool Vn9d::read_register(uint8_t address, uint16_t* data_out) {
  return transceive(VN9DX_OP_READ, address, 0, nullptr, data_out);
}

// Protected registers need an UNLOCK frame immediately before the write.
bool Vn9d::protected_write(uint8_t address, uint16_t data) {
  if (!write_register(VN9DX_RAM_REG_CTRL, VN9DX_CTRL_UNLOCK | VN9DX_CTRL_EN)) {
    return false;
  }
  return write_register(address, data);
}

bool Vn9d::fail_init(uint8_t address) {
  status_.device_ok = false;
  set_event(EVENT_LOAD_SWITCH_INIT_FAILURE, address);
  return false;
}

bool Vn9d::init() {
  if (!esp32hal->alloc_pins("Load switch", pins_.sck, pins_.miso, pins_.mosi, pins_.cs, pwm_clk_pin_)) {
    return fail_init(0);
  }

  pinMode(pins_.cs, OUTPUT);
  digitalWrite(pins_.cs, HIGH);
  spi_ = new SPIClass(spi_bus_);
  spi_->begin(pins_.sck, pins_.miso, pins_.mosi);

  // Continuous 50% clock for the chip's PWM engine.
  ledcAttachChannel(pwm_clk_pin_, kVn9dPwmClockHz, kVn9dPwmClockResolutionBits, ledc_channel_);
  ledcWrite(pwm_clk_pin_, kVn9dPwmClockDuty);

  // Dummy read clears RSTB and wakes the SPI interface before the
  // fail-safe exit sequence.
  uint16_t ctrl = 0;
  if (!read_register(VN9DX_RAM_REG_CTRL, &ctrl)) {
    return fail_init(VN9DX_RAM_REG_CTRL);
  }

  // Exit fail-safe — DS13579 §2.2.4: CTRL UNLOCK=1,EN=0 then CTRL EN=1.
  if (!write_register(VN9DX_RAM_REG_CTRL, VN9DX_CTRL_UNLOCK) || !write_register(VN9DX_RAM_REG_CTRL, VN9DX_CTRL_EN)) {
    return fail_init(VN9DX_RAM_REG_CTRL);
  }

  // The device's WDTB is 0 after reset, so the first feed sends WDTB=1.
  last_wdt_bit_ = true;
  if (!write_register(VN9DX_RAM_REG_SOCR, 0)) {
    return fail_init(VN9DX_RAM_REG_SOCR);
  }

  uint16_t pc3 = 0;
  if (!transceive(VN9DX_OP_READ_ROM, VN9DX_ROM_REG_PC3, 0, nullptr, &pc3)) {
    return fail_init(VN9DX_ROM_REG_PC3);
  }
  switch (pc3 >> 8) {
    case PC3_VN9D5D20FN:
      status_.channel_count = 4;
      break;
    case PC3_VN9D30Q100F:
      status_.channel_count = 6;
      break;
    default:
      DEBUG_PRINTF("Unknown VN9D device type 0x%04X\n", pc3);
      return fail_init(VN9DX_ROM_REG_PC3);
  }

  // Latch off on power limitation instead of auto-restart, consistent with
  // the app-level fault latch.
  if (!write_register(VN9DX_RAM_REG_CHLOFFTCR0, 0) || !write_register(VN9DX_RAM_REG_CHLOFFTCR1, 0)) {
    return fail_init(VN9DX_RAM_REG_CHLOFFTCR0);
  }

  resolve_duplicate_roles();

  for (uint8_t ch = 0; ch < status_.channel_count; ch++) {
    if (!read_register(VN9DX_RAM_REG_OUTCFGR0 + ch, &outcfgr_[ch])) {
      return fail_init(VN9DX_RAM_REG_OUTCFGR0 + ch);
    }
    uint16_t new_val = outcfgr_[ch];
    new_val &= (uint16_t)~(VN9DX_SLOPECR_MASK | VN9DX_CHPHA_MASK | VN9DX_PWMFCY_MASK);
    new_val |= (uint16_t)(kVn9dDivisorCodeToPwmfcy[config_[ch].divisor_code] << VN9DX_PWMFCY0_BIT);
    if (new_val != outcfgr_[ch]) {
      outcfgr_[ch] = new_val;
      if (!protected_write(VN9DX_RAM_REG_OUTCFGR0 + ch, outcfgr_[ch])) {
        return fail_init(VN9DX_RAM_REG_OUTCFGR0 + ch);
      }
    }
    status_.channels[ch].role = config_[ch].role;
  }

  status_.device_ok = true;
  return true;
}

void Vn9d::set_channel_role(uint8_t channel, LoadSwitchRole role) {
  if (channel >= kLoadSwitchMaxChannels) {
    return;
  }
  config_[channel].role = role < LoadSwitchRole::Highest ? role : LoadSwitchRole::Disabled;
  status_.channels[channel].role = config_[channel].role;
}

// First channel with a contactor/BMS role wins; later duplicates run as
// Disabled.
void Vn9d::resolve_duplicate_roles() {
  bool seen[(size_t)LoadSwitchRole::Highest] = {};
  for (uint8_t ch = 0; ch < kLoadSwitchConfigChannels; ch++) {
    LoadSwitchRole role = config_[ch].role;
    if (role == LoadSwitchRole::Disabled || role == LoadSwitchRole::Manual) {
      continue;
    }
    if (seen[(size_t)role]) {
      config_[ch].role = LoadSwitchRole::Disabled;
      status_.channels[ch].role = LoadSwitchRole::Disabled;
      set_event(EVENT_LOAD_SWITCH_ROLE_CONFLICT, ch);
      continue;
    }
    seen[(size_t)role] = true;
  }
}

bool Vn9d::set_channel_duty(uint8_t channel, uint16_t duty) {
  if (!status_.device_ok || channel >= status_.channel_count) {
    return false;
  }
  if (duty > kLoadSwitchDutyMax) {
    duty = kLoadSwitchDutyMax;
  }
  if (!write_register((uint8_t)(VN9DX_RAM_REG_OUTCTRCR0 + channel), (uint16_t)(duty << VN9DX_DUTYCR0_BIT))) {
    return false;
  }
  if (duty > 0) {
    enabled_bitmap_ |= (uint8_t)(1u << channel);
  } else {
    enabled_bitmap_ &= (uint8_t)~(1u << channel);
  }
  if (!write_register(VN9DX_RAM_REG_SOCR, (uint16_t)(enabled_bitmap_ << VN9DX_SOCR0_BIT))) {
    return false;
  }
  status_.channels[channel].duty = duty;
  status_.channels[channel].on = duty > 0;
  return true;
}

bool Vn9d::apply_divisor(uint8_t channel, uint8_t divisor_code) {
  uint16_t new_val = outcfgr_[channel];
  new_val &= (uint16_t)~VN9DX_PWMFCY_MASK;
  new_val |= (uint16_t)(kVn9dDivisorCodeToPwmfcy[divisor_code] << VN9DX_PWMFCY0_BIT);
  if (new_val == outcfgr_[channel]) {
    return true;
  }
  outcfgr_[channel] = new_val;
  return protected_write((uint8_t)(VN9DX_RAM_REG_OUTCFGR0 + channel), new_val);
}

void Vn9d::engage(uint8_t channel) {
  if (set_channel_duty(channel, PWM_ON_DUTY)) {
    state_[channel] = ChannelState::PullIn;
  }
}

void Vn9d::disengage(uint8_t channel) {
  if (set_channel_duty(channel, PWM_OFF_DUTY)) {
    state_[channel] = ChannelState::Off;
  }
}

void Vn9d::hold(uint8_t channel) {
  // Matches the GPIO path: without pwm_contactor_control, hold re-asserts
  // full duty instead of economising.
  if (!pwm_contactor_control) {
    engage(channel);
    return;
  }
  if (set_channel_duty(channel, config_[channel].duty)) {
    state_[channel] = ChannelState::Steady;
  }
}

void Vn9d::request_manual(uint8_t channel, bool on) {
  if (channel >= kLoadSwitchMaxChannels) {
    return;
  }
  // Ordered so a reader that sees pending always sees the value with it.
  status_.channels[channel].pending_on = on;
  pending_manual_on_[channel] = on;
  status_.channels[channel].pending = true;
  pending_manual_[channel] = true;
}

void Vn9d::request_duty(uint8_t channel, uint16_t duty) {
  if (channel >= kLoadSwitchMaxChannels) {
    return;
  }
  pending_duty_value_[channel] = duty;
  pending_duty_[channel] = true;
}

void Vn9d::request_divisor(uint8_t channel, uint8_t divisor_code) {
  if (channel >= kLoadSwitchMaxChannels) {
    return;
  }
  pending_divisor_value_[channel] = divisor_code;
  pending_divisor_[channel] = true;
}

void Vn9d::apply_pending_requests() {
  for (uint8_t ch = 0; ch < status_.channel_count; ch++) {
    if (pending_duty_[ch]) {
      pending_duty_[ch] = false;
      uint16_t duty = pending_duty_value_[ch];
      config_[ch].duty = duty <= kLoadSwitchDutyMax ? duty : kLoadSwitchDutyMax;
      if (state_[ch] == ChannelState::Steady) {
        set_channel_duty(ch, config_[ch].duty);
      }
    }
    if (pending_divisor_[ch]) {
      pending_divisor_[ch] = false;
      uint8_t code = pending_divisor_value_[ch];
      config_[ch].divisor_code = code < kLoadSwitchDivisorCodes ? code : 0;
      apply_divisor(ch, config_[ch].divisor_code);
    }
    if (pending_manual_[ch]) {
      pending_manual_[ch] = false;
      status_.channels[ch].pending = false;
      if (config_[ch].role != LoadSwitchRole::Manual) {
        continue;
      }
      if (pending_manual_on_[ch]) {
        if (set_channel_duty(ch, config_[ch].duty)) {
          state_[ch] = ChannelState::Steady;
        }
      } else {
        disengage(ch);
      }
    }
  }
}

void Vn9d::tick() {
  if (!status_.device_ok) {
    return;
  }

  // Watchdog feed and output refresh in one SOCR write.
  uint8_t gsb = 0;
  if (!transceive(VN9DX_OP_WRITE, VN9DX_RAM_REG_SOCR, (uint16_t)(enabled_bitmap_ << VN9DX_SOCR0_BIT), &gsb, nullptr)) {
    return;
  }

  // A watchdog lapse (core-loop stall past ~70 ms) drops the chip to
  // fail-safe; re-exit and let the next SOCR write re-assert the outputs.
  if (gsb & (1u << VN9DX_GSB_FS_BIT)) {
    write_register(VN9DX_RAM_REG_CTRL, VN9DX_CTRL_UNLOCK);
    write_register(VN9DX_RAM_REG_CTRL, VN9DX_CTRL_EN);
  }

  apply_pending_requests();

  for (uint8_t ch = 0; ch < status_.channel_count; ch++) {
    uint16_t adc = 0;
    if (read_register((uint8_t)(VN9DX_RAM_REG_ADC0SR + ch), &adc)) {
      uint32_t raw = (adc >> VN9DX_ADC_VALUE_SHIFT) & VN9DX_ADC_VALUE_MASK;
      uint32_t k_factor = (ch < 2) ? VN9DX_K_BULB_CH01 : VN9DX_K_BULB_CH23;
      status_.channels[ch].current_mA =
          (enabled_bitmap_ & (1u << ch)) ? (raw * kVn9dMilliampsPerAmp) / k_factor : 0;
    }
  }

  uint16_t temp_adc = 0;
  if (read_register(VN9DX_RAM_REG_ADC9SR, &temp_adc)) {
    uint32_t raw = (temp_adc >> VN9DX_ADC_VALUE_SHIFT) & VN9DX_ADC_VALUE_MASK;
    status_.frame_temperature_dC =
        (int16_t)(kVn9dTempInterceptDeciC - (int32_t)(((kVn9dTempSlopeQ16 * raw) >> 16) * 10));
  }

  bool vcc_undervoltage = false;
  for (uint8_t ch = 0; ch < status_.channel_count; ch++) {
    uint16_t outsr = 0;
    if (read_register((uint8_t)(VN9DX_RAM_REG_OUTSR0 + ch), &outsr)) {
      status_.channels[ch].fault = (outsr & (1u << VN9DX_OUTSR_CHFBSR_BIT)) != 0;
      status_.channels[ch].latched_off = (outsr & (1u << VN9DX_OUTSR_CHLOFFSR_BIT)) != 0;
      status_.channels[ch].open_load =
          (outsr & ((1u << VN9DX_OUTSR_STKFLTR_BIT) | (1u << VN9DX_OUTSR_OLPUSR_BIT))) != 0;
      vcc_undervoltage |= (outsr & (1u << VN9DX_OUTSR_VCCUV_BIT)) != 0;
    }
  }
  status_.vcc_undervoltage = vcc_undervoltage;
}
