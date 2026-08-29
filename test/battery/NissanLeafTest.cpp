#include <gtest/gtest.h>

#include <cstring>
#include <initializer_list>

#include "../../Software/src/battery/BATTERIES.h"
#include "../../Software/src/battery/NISSAN-LEAF-BATTERY.h"
#include "../../Software/src/datalayer/datalayer.h"
#include "../../Software/src/devboard/hal/hal.h"
#include "../../Software/src/devboard/webserver/advanced_api.h"
#include "../../Software/src/devboard/webserver/json_response_writer.h"

static String advanced_json() {
  return render_json(write_advanced);
}
#include "../../Software/src/lib/bblanchon-ArduinoJson/ArduinoJson.h"

#include "Arduino.h"
#include "../advanced_status_recorder.h"

namespace {

// Builds a frame on the given ID from up to 8 raw bytes.
CAN_frame leaf_frame(uint32_t id, std::initializer_list<uint8_t> bytes) {
  CAN_frame frame = {};
  frame.DLC = 8;
  frame.ID = id;
  uint8_t i = 0;
  for (uint8_t b : bytes) {
    if (i >= 8) {
      break;
    }
    frame.data.u8[i++] = b;
  }
  return frame;
}

CAN_frame leaf_7bb_frame(std::initializer_list<uint8_t> bytes) {
  return leaf_frame(0x7BB, bytes);
}

// The datalayer is a global shared by every test, so put the DTC block back to its power-on state.
void reset_dtc_state() {
  datalayer.battery.pack[0].dtc = DATALAYER_BATTERY_DTC_TYPE{};
}

// Drives a battery up to the point where it is waiting for a DTC reply on 0x7BB. update_values() is
// what consumes the web request and puts 19 02 0E on the wire.
NissanLeafBattery* battery_awaiting_dtc_reply() {
  reset_dtc_state();
  set_millis64(50000);  // Non-zero, so a completed read is distinguishable from "never read"
  auto battery = new NissanLeafBattery(battery_slot_context(0));
  battery->setup();
  batteries[0] = battery;
  EXPECT_TRUE(run_advanced_command("readDTC", 0));
  battery->transmit_can(50000);  // Channel is idle, so the request goes out immediately
  return battery;
}

}  // namespace

TEST(NissanLeafTests, ShouldReportVoltage) {
  init_hal();
  auto battery = new NissanLeafBattery(battery_slot_context(0));
  battery->setup();

  int expected_dV = 440;

  int divided = expected_dV / 5;

  CAN_frame frame = {.ID = 0x1DB, .data = {.u8 = {0, 0, (uint8_t)(divided >> 2), (uint8_t)((divided & 0xC0) << 6)}}};

  frame.data.u8[7] = battery->calculate_crc(frame);
  battery->handle_incoming_can_frame(frame);
  battery->update_values();

  EXPECT_EQ(datalayer.battery.pack[0].status.voltage_dV, expected_dV);
}

// resetDTC is asserted throughout because a non-empty declared list suppresses
// the legacy registry outright, so dropping a command fails silently.
TEST(NissanLeafTests, ResetSohIsWithdrawnOnceZe1IsDetected) {
  init_hal();
  auto leaf = new NissanLeafBattery(battery_slot_context(0));
  leaf->setup();
  batteries[0] = leaf;

  JsonDocument doc;
  auto offers = [&doc](const char* id) {
    EXPECT_FALSE(deserializeJson(doc, advanced_json().c_str()));
    for (JsonObject command : doc["batteries"][0]["commands"].as<JsonArray>()) {
      if (std::strcmp(command["id"] | "", id) == 0) {
        return true;
      }
    }
    return false;
  };

  EXPECT_TRUE(offers("resetSOH"));
  EXPECT_TRUE(run_advanced_command("resetSOH", 0));
  EXPECT_TRUE(offers("resetDTC"));
  EXPECT_TRUE(run_advanced_command("resetDTC", 0));
  EXPECT_TRUE(offers("readDTC"));
  EXPECT_TRUE(run_advanced_command("readDTC", 0));

  CAN_frame ze1 = {.ID = 0x380};
  leaf->handle_incoming_can_frame(ze1);

  EXPECT_FALSE(offers("resetSOH"));
  EXPECT_FALSE(run_advanced_command("resetSOH", 0));
  EXPECT_TRUE(offers("resetDTC"));
  EXPECT_TRUE(run_advanced_command("resetDTC", 0));
  EXPECT_TRUE(offers("readDTC"));

  batteries[0] = nullptr;
  delete leaf;
}

// A single stored code fits in one ISO-TP frame: 59 02 <mask> then one 4-byte record.
TEST(NissanLeafDtcTests, ShouldParseSingleFrameReply) {
  init_hal();
  auto battery = battery_awaiting_dtc_reply();

  battery->handle_incoming_can_frame(leaf_7bb_frame({0x07, 0x59, 0x02, 0x4E, 0xD0, 0x00, 0x00, 0x4E}));

  EXPECT_FALSE(datalayer.battery.pack[0].dtc.dtc_read_failed);
  ASSERT_EQ(datalayer.battery.pack[0].dtc.dtc_count, 1);
  EXPECT_EQ(datalayer.battery.pack[0].dtc.dtc_codes[0], 0xD00000u);  // Renders as U1000
  EXPECT_EQ(datalayer.battery.pack[0].dtc.dtc_status[0], 0x4E);
  EXPECT_NE(datalayer.battery.pack[0].dtc.dtc_last_read_millis, 0u);

  batteries[0] = nullptr;
  delete battery;
}

// Real LBC capture holding four codes: U1000, P33D7, P33D9 and P33DD. The announced ISO-TP length of
// 0x013 (19 bytes) is what stops the trailing FF padding being parsed as a fifth code.
TEST(NissanLeafDtcTests, ShouldParseMultiFrameReply) {
  init_hal();
  auto battery = battery_awaiting_dtc_reply();

  battery->handle_incoming_can_frame(leaf_7bb_frame({0x10, 0x13, 0x59, 0x02, 0x4E, 0xD0, 0x00, 0x00}));
  battery->handle_incoming_can_frame(leaf_7bb_frame({0x21, 0x4E, 0x33, 0xD7, 0x00, 0x4E, 0x33, 0xD9}));
  battery->handle_incoming_can_frame(leaf_7bb_frame({0x22, 0x00, 0x4E, 0x33, 0xDD, 0x00, 0x4E, 0xFF}));

  EXPECT_FALSE(datalayer.battery.pack[0].dtc.dtc_read_failed);
  ASSERT_EQ(datalayer.battery.pack[0].dtc.dtc_count, 4);
  EXPECT_EQ(datalayer.battery.pack[0].dtc.dtc_codes[0], 0xD00000u);  // U1000
  EXPECT_EQ(datalayer.battery.pack[0].dtc.dtc_codes[1], 0x33D700u);  // P33D7
  EXPECT_EQ(datalayer.battery.pack[0].dtc.dtc_codes[2], 0x33D900u);  // P33D9
  EXPECT_EQ(datalayer.battery.pack[0].dtc.dtc_codes[3], 0x33DD00u);  // P33DD
  for (int i = 0; i < 4; i++) {
    EXPECT_EQ(datalayer.battery.pack[0].dtc.dtc_status[i], 0x4E);
  }

  batteries[0] = nullptr;
  delete battery;
}

// A healthy pack answers with the 3-byte header and nothing else, padded with FF. That is a
// successful read of zero codes, not a failed read.
TEST(NissanLeafDtcTests, ShouldReportNoDtcsStored) {
  init_hal();
  auto battery = battery_awaiting_dtc_reply();

  battery->handle_incoming_can_frame(leaf_7bb_frame({0x03, 0x59, 0x02, 0x4E, 0xFF, 0xFF, 0xFF, 0xFF}));

  EXPECT_FALSE(datalayer.battery.pack[0].dtc.dtc_read_failed);
  EXPECT_EQ(datalayer.battery.pack[0].dtc.dtc_count, 0);
  EXPECT_NE(datalayer.battery.pack[0].dtc.dtc_last_read_millis, 0u);

  batteries[0] = nullptr;
  delete battery;
}

// The LBC acknowledges ClearDiagnosticInformation with a single-frame 54. Until that arrives the
// previously read list has to stay put, because an unconfirmed erase proves nothing.
TEST(NissanLeafDtcTests, ShouldClearStoredDtcsOnlyOnAcknowledgement) {
  init_hal();
  reset_dtc_state();
  set_millis64(50000);
  auto battery = new NissanLeafBattery(battery_slot_context(0));
  battery->setup();
  batteries[0] = battery;

  datalayer.battery.pack[0].dtc.dtc_count = 1;
  datalayer.battery.pack[0].dtc.dtc_codes[0] = 0x33D700;
  datalayer.battery.pack[0].dtc.dtc_last_read_millis = 50000;

  EXPECT_TRUE(run_advanced_command("resetDTC", 0));
  battery->transmit_can(50000);  // Sends 14 FF FF FF, but must not wipe anything yet

  EXPECT_EQ(datalayer.battery.pack[0].dtc.dtc_count, 1);

  battery->handle_incoming_can_frame(leaf_7bb_frame({0x01, 0x54, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}));

  EXPECT_EQ(datalayer.battery.pack[0].dtc.dtc_count, 0);
  EXPECT_FALSE(datalayer.battery.pack[0].dtc.dtc_read_failed);
  // Back to "not read yet": the erase says nothing about what the LBC reports next.
  EXPECT_EQ(datalayer.battery.pack[0].dtc.dtc_last_read_millis, 0u);

  batteries[0] = nullptr;
  delete battery;
}

// An erase the LBC never acknowledges must leave the stored list alone rather than falsely
// reporting success.
TEST(NissanLeafDtcTests, ShouldKeepStoredDtcsWhenEraseIsNotAcknowledged) {
  init_hal();
  reset_dtc_state();
  set_millis64(50000);
  auto battery = new NissanLeafBattery(battery_slot_context(0));
  battery->setup();
  batteries[0] = battery;

  datalayer.battery.pack[0].dtc.dtc_count = 1;
  datalayer.battery.pack[0].dtc.dtc_codes[0] = 0x33D700;
  datalayer.battery.pack[0].dtc.dtc_last_read_millis = 50000;

  EXPECT_TRUE(run_advanced_command("resetDTC", 0));
  battery->transmit_can(50000);

  set_millis64(50000 + 2500);
  battery->transmit_can(50000 + 2500);

  EXPECT_EQ(datalayer.battery.pack[0].dtc.dtc_count, 1);
  EXPECT_EQ(datalayer.battery.pack[0].dtc.dtc_last_read_millis, 50000u);

  batteries[0] = nullptr;
  delete battery;
}

// 7F 19 xx means the LBC refused the request outright.
TEST(NissanLeafDtcTests, ShouldFlagFailureOnNegativeResponse) {
  init_hal();
  auto battery = battery_awaiting_dtc_reply();

  battery->handle_incoming_can_frame(leaf_7bb_frame({0x03, 0x7F, 0x19, 0x12, 0x00, 0x00, 0x00, 0x00}));

  EXPECT_TRUE(datalayer.battery.pack[0].dtc.dtc_read_failed);
  EXPECT_EQ(datalayer.battery.pack[0].dtc.dtc_count, 0);

  batteries[0] = nullptr;
  delete battery;
}

// If nothing ever answers, the read has to give up so the page stops showing it as pending.
TEST(NissanLeafDtcTests, ShouldTimeOutWhenLbcNeverReplies) {
  init_hal();
  auto battery = battery_awaiting_dtc_reply();

  // Each unanswered attempt is retried; only after the retries are exhausted is it a failure.
  unsigned long t = 50000;
  for (int attempt = 0; attempt < 4; attempt++) {
    t += 2500;
    set_millis64(t);
    battery->transmit_can(t);  // times out this attempt, re-arms if retries remain
    battery->transmit_can(t);  // sends the retry
  }

  EXPECT_TRUE(datalayer.battery.pack[0].dtc.dtc_read_failed);

  batteries[0] = nullptr;
  delete battery;
}

// Regression for the collision seen on real hardware: pressing Read DTC while a group poll transfer
// was still in flight put 19 02 0E on the bus 1 ms after a flow control frame, and the LBC dropped
// it without answering. The request must instead wait for the channel to go quiet.
TEST(NissanLeafDtcTests, ShouldNotSendRequestWhileGroupTransferIsInFlight) {
  init_hal();
  reset_dtc_state();
  set_millis64(60000);
  auto battery = new NissanLeafBattery(battery_slot_context(0));
  battery->setup();
  batteries[0] = battery;

  // LBC is mid-transfer: first frame of a group 0x90 reply has just arrived.
  battery->handle_incoming_can_frame(leaf_7bb_frame({0x10, 0x0A, 0x61, 0x90, 0x47, 0x41, 0x51, 0x31}));

  EXPECT_TRUE(run_advanced_command("readDTC", 0));
  battery->transmit_can(60000);  // Channel busy, so nothing should go out yet

  // A DTC reply arriving now would mean the request had been sent. Feed one and check it is ignored.
  battery->handle_incoming_can_frame(leaf_7bb_frame({0x07, 0x59, 0x02, 0x4E, 0xD0, 0x00, 0x00, 0x4E}));
  EXPECT_EQ(datalayer.battery.pack[0].dtc.dtc_count, 0);

  // Once the channel has been quiet for longer than the idle threshold, the request goes out.
  set_millis64(60000 + 200);
  battery->transmit_can(60000 + 200);
  battery->handle_incoming_can_frame(leaf_7bb_frame({0x07, 0x59, 0x02, 0x4E, 0xD0, 0x00, 0x00, 0x4E}));

  ASSERT_EQ(datalayer.battery.pack[0].dtc.dtc_count, 1);
  EXPECT_EQ(datalayer.battery.pack[0].dtc.dtc_codes[0], 0xD00000u);

  batteries[0] = nullptr;
  delete battery;
}

// A request must not go out in the window between an earlier request being sent and its first
// response frame arriving. The channel looks quiet there, but the LBC is still working on the
// previous transaction and will drop whatever lands next.
TEST(NissanLeafDtcTests, ShouldNotSendRequestWhileEarlierRequestIsUnanswered) {
  init_hal();
  reset_dtc_state();
  set_millis64(70000);
  auto battery = new NissanLeafBattery(battery_slot_context(0));
  battery->setup();
  batteries[0] = battery;

  // Periodic polling only runs once 0x5BC has marked the battery as alive.
  battery->handle_incoming_can_frame(leaf_frame(0x5BC, {0x43, 0xC0, 0xB4, 0x8C, 0xC8, 0x02, 0x5F, 0xFF}));

  // Polling is held off for the first few 10 s cycles after startup, so tick past that until a
  // group request actually goes out. The last tick leaves it outstanding with no reply yet.
  unsigned long t = 70000;
  for (int tick = 0; tick < 5; tick++) {
    battery->transmit_can(t);
    t += 10001;
  }
  battery->transmit_can(t);  // This one puts a group request on the bus

  set_millis64(t + 500);
  EXPECT_TRUE(run_advanced_command("readDTC", 0));
  battery->transmit_can(t + 500);  // Channel is quiet, but that request is still unanswered

  // If the DTC request had gone out, this reply would be accepted.
  battery->handle_incoming_can_frame(leaf_7bb_frame({0x07, 0x59, 0x02, 0x4E, 0xD0, 0x00, 0x00, 0x4E}));
  EXPECT_EQ(datalayer.battery.pack[0].dtc.dtc_count, 0);

  batteries[0] = nullptr;
  delete battery;
}

// The periodic group polling answers on 0x7BB too, and its first frame carries 0x02 in the byte the
// group handler reads as a group number. The 0x61 service byte is what keeps the two apart, so a
// group reply arriving mid-readout must not be swallowed by the DTC reassembler.
TEST(NissanLeafDtcTests, ShouldNotConsumeGroupReplyAsDtc) {
  init_hal();
  auto battery = battery_awaiting_dtc_reply();

  battery->handle_incoming_can_frame(leaf_7bb_frame({0x10, 0x35, 0x61, 0x01, 0xFF, 0xFF, 0xFC, 0x18}));

  EXPECT_EQ(datalayer.battery.pack[0].dtc.dtc_count, 0);
  EXPECT_FALSE(datalayer.battery.pack[0].dtc.dtc_read_failed);

  // The genuine DTC reply that follows still parses.
  battery->handle_incoming_can_frame(leaf_7bb_frame({0x07, 0x59, 0x02, 0x4E, 0xD0, 0x00, 0x00, 0x4E}));

  ASSERT_EQ(datalayer.battery.pack[0].dtc.dtc_count, 1);
  EXPECT_EQ(datalayer.battery.pack[0].dtc.dtc_codes[0], 0xD00000u);

  batteries[0] = nullptr;
  delete battery;
}

// Reproduces a reply larger than our storage: the LBC announced 599 bytes (149 codes) when asked
// with an over-wide status mask. The first 32 codes must be kept, and every remaining frame must
// still be acknowledged, because falling silent mid-transfer leaves the LBC waiting on a flow
// control that never arrives and blocks the next request.
TEST(NissanLeafDtcTests, ShouldDrainReplyLargerThanStorage) {
  init_hal();
  auto battery = battery_awaiting_dtc_reply();

  const uint16_t announced = 599;
  battery->handle_incoming_can_frame(leaf_frame(
      0x7BB, {(uint8_t)(0x10 | (announced >> 8)), (uint8_t)(announced & 0xFF), 0x59, 0x02, 0x4E, 0x0A, 0x1F, 0x00}));

  // 6 payload bytes arrived in the first frame; feed consecutive frames until all 599 are sent.
  uint16_t sent = 6;
  uint8_t seq = 1;
  bool checked_midway = false;
  while (sent < announced) {
    CAN_frame cf = leaf_frame(0x7BB, {(uint8_t)(0x20 | (seq & 0x0F))});
    for (uint8_t i = 1; i < 8; i++) {
      cf.data.u8[i] = (sent < announced) ? 0x40 : 0xFF;
      sent++;
    }
    battery->handle_incoming_can_frame(cf);
    seq++;

    // Once our storage is full there is still far more to come. The readout must stay open and keep
    // acknowledging, not declare itself finished the moment the buffer fills.
    if (!checked_midway && sent >= 3 + 4 * DATALAYER_BATTERY_DTC_TYPE::MAX_DTC_COUNT) {
      EXPECT_EQ(datalayer.battery.pack[0].dtc.dtc_count, 0) << "readout ended early instead of draining";
      checked_midway = true;
    }
  }
  EXPECT_TRUE(checked_midway);

  // Completed rather than timed out, and filled to capacity without overrunning it.
  EXPECT_FALSE(datalayer.battery.pack[0].dtc.dtc_read_failed);
  EXPECT_EQ(datalayer.battery.pack[0].dtc.dtc_count, DATALAYER_BATTERY_DTC_TYPE::MAX_DTC_COUNT);
  EXPECT_EQ(datalayer.battery.pack[0].dtc.dtc_codes[0], 0x0A1F00u);  // P0A1F, first code in the real capture

  // The full count is kept even though only the first 32 are stored, so the page can say the list
  // is truncated. 599 bytes less the 3 byte header is 149 codes.
  EXPECT_EQ(datalayer.battery.pack[0].dtc.dtc_reported_count, 149);

  RecordingWriter recorder;
  write_dtc_section(recorder, *battery, datalayer.battery.pack[0].dtc, DtcCodeStyle::kShortFailureType);
  const RecordingWriter::Section& section = recorder.sections.at(0);
  bool found_truncation = false;
  for (const auto& f : section.fields) {
    if (f.value.find("32 codes shown of 149 reported") != std::string::npos) {
      found_truncation = true;
    }
  }
  EXPECT_TRUE(found_truncation);

  batteries[0] = nullptr;
  delete battery;
}

// When everything fits, the page must not clutter the line with a redundant "of N reported".
TEST(NissanLeafDtcTests, ShouldNotClaimTruncationWhenEverythingFits) {
  init_hal();
  auto battery = battery_awaiting_dtc_reply();

  battery->handle_incoming_can_frame(leaf_7bb_frame({0x07, 0x59, 0x02, 0x4E, 0xD0, 0x00, 0x00, 0x4E}));

  ASSERT_EQ(datalayer.battery.pack[0].dtc.dtc_count, 1);
  EXPECT_EQ(datalayer.battery.pack[0].dtc.dtc_reported_count, 1);

  RecordingWriter recorder;
  write_dtc_section(recorder, *battery, datalayer.battery.pack[0].dtc, DtcCodeStyle::kShortFailureType);
  const RecordingWriter::Section& section = recorder.sections.at(0);
  for (const auto& f : section.fields) {
    EXPECT_EQ(std::string(f.value.c_str()).find("reported"), std::string::npos);
  }

  batteries[0] = nullptr;
  delete battery;
}

// nissan_leaf_dtc.json is keyed by the 5-character short form, so that is what has to end up in the
// display string produced by the shared short-failure-type formatter.
TEST(NissanLeafDtcTests, ShouldRenderShortNissanCodeAsLookupKey) {
  init_hal();
  reset_dtc_state();
  set_millis64(50000);
  auto battery = new NissanLeafBattery(battery_slot_context(0));

  datalayer.battery.pack[0].dtc.dtc_count = 2;
  datalayer.battery.pack[0].dtc.dtc_codes[0] = 0x33D700;  // P33D7, failure type byte 00
  datalayer.battery.pack[0].dtc.dtc_status[0] = 0x4E;
  datalayer.battery.pack[0].dtc.dtc_codes[1] = 0xD0002F;  // U1000, failure type byte 2F
  datalayer.battery.pack[0].dtc.dtc_status[1] = 0x09;
  datalayer.battery.pack[0].dtc.dtc_last_read_millis = 50000;

  RecordingWriter recorder;
  write_dtc_section(recorder, *battery, datalayer.battery.pack[0].dtc, DtcCodeStyle::kShortFailureType);
  const RecordingWriter::Section& section = recorder.sections.at(0);
  const RecordingWriter::Field& table = section.fields.back();
  ASSERT_TRUE(table.is_table);
  ASSERT_EQ(table.rows.size(), 2u);
  EXPECT_EQ(table.rows[0][0], "P33D7");
  EXPECT_EQ(table.rows[1][0], "U1000-2F");

  // The catalogue is keyed on the short form, so the failure type must stay out of the key.
  EXPECT_EQ(table.catalogue, "nissan_leaf_dtc.json");
  EXPECT_EQ(table.row_keys[0], "P33D7");
  EXPECT_EQ(table.row_keys[1], "U1000");

  delete battery;
}

// The three read states each have to be distinguishable on the page.
TEST(NissanLeafDtcTests, ShouldRenderReadStateWhenNoTableIsShown) {
  init_hal();
  reset_dtc_state();  // Never read
  auto battery = new NissanLeafBattery(battery_slot_context(0));
  {
    RecordingWriter recorder;
  write_dtc_section(recorder, *battery, datalayer.battery.pack[0].dtc, DtcCodeStyle::kShortFailureType);
  const RecordingWriter::Section& section = recorder.sections.at(0);
    ASSERT_EQ(section.fields.size(), 1u);
    EXPECT_NE(std::string(section.fields[0].value.c_str()).find("Not read yet"), std::string::npos);
  }

  reset_dtc_state();
  datalayer.battery.pack[0].dtc.dtc_last_read_millis = 50000;
  datalayer.battery.pack[0].dtc.dtc_read_failed = true;
  {
    RecordingWriter recorder;
  write_dtc_section(recorder, *battery, datalayer.battery.pack[0].dtc, DtcCodeStyle::kShortFailureType);
  const RecordingWriter::Section& section = recorder.sections.at(0);
    ASSERT_EQ(section.fields.size(), 1u);
    EXPECT_NE(std::string(section.fields[0].value.c_str()).find("failed"), std::string::npos);
  }

  reset_dtc_state();
  datalayer.battery.pack[0].dtc.dtc_last_read_millis = 50000;
  {
    RecordingWriter recorder;
  write_dtc_section(recorder, *battery, datalayer.battery.pack[0].dtc, DtcCodeStyle::kShortFailureType);
  const RecordingWriter::Section& section = recorder.sections.at(0);
    ASSERT_EQ(section.fields.size(), 1u);
    EXPECT_NE(std::string(section.fields[0].value.c_str()).find("No DTCs present"), std::string::npos);
  }

  delete battery;
}
