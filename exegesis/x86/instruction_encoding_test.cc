// Copyright 2018 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "exegesis/x86/instruction_encoding.h"

#include <cstdint>
#include <initializer_list>
#include <memory>

#include "absl/strings/str_cat.h"
#include "exegesis/proto/x86/encoding_specification.pb.h"
#include "exegesis/testing/test_util.h"
#include "exegesis/util/proto_util.h"
#include "exegesis/x86/encoding_specification.h"
#include "exegesis/x86/instruction_encoding_test_utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/google/protobuf/text_format.h"
#include "util/task/status.h"
#include "util/task/statusor.h"

namespace exegesis {
namespace x86 {
namespace {

using ::exegesis::testing::EqualsProto;
using ::exegesis::testing::StatusIs;
using ::exegesis::util::Status;
using ::exegesis::util::StatusOr;
using ::exegesis::util::error::Code;
using ::exegesis::util::error::FAILED_PRECONDITION;
using ::exegesis::util::error::INVALID_ARGUMENT;
using ::exegesis::util::error::OK;
using ::testing::UnorderedPointwise;

// Pull the values of the enums defined in the VexPrefixEncodingSpecification
// proto to the main namespace to avoid repeating the long class name
// everywhere.
const auto kVexWIsZero = VexPrefixEncodingSpecification::VEX_W_IS_ZERO;
const auto kVexWIsOne = VexPrefixEncodingSpecification::VEX_W_IS_ONE;
const auto kVexWIsIgnored = VexPrefixEncodingSpecification::VEX_W_IS_IGNORED;

TEST(GetModRmModBitsTest, Test) {
  EXPECT_EQ(GetModRmModBits(0xD8), 3);
  EXPECT_EQ(GetModRmModBits(0xC9), 3);
  EXPECT_EQ(GetModRmModBits(0x80), 2);
  EXPECT_EQ(GetModRmModBits(0xBF), 2);
  EXPECT_EQ(GetModRmModBits(0x77), 1);
  EXPECT_EQ(GetModRmModBits(0x4A), 1);
  EXPECT_EQ(GetModRmModBits(0x33), 0);
  EXPECT_EQ(GetModRmModBits(0x1F), 0);
}

TEST(GetModRmRegBitsTest, Test) {
  EXPECT_EQ(GetModRmRegBits(0xD8), 3);
  EXPECT_EQ(GetModRmRegBits(0xC9), 1);
  EXPECT_EQ(GetModRmRegBits(0x80), 0);
  EXPECT_EQ(GetModRmRegBits(0xBF), 7);
  EXPECT_EQ(GetModRmRegBits(0x77), 6);
  EXPECT_EQ(GetModRmRegBits(0x4A), 1);
  EXPECT_EQ(GetModRmRegBits(0x33), 6);
  EXPECT_EQ(GetModRmRegBits(0x1F), 3);
}

TEST(GetModRmRmBitsTest, Test) {
  EXPECT_EQ(GetModRmRmBits(0xD8), 0);
  EXPECT_EQ(GetModRmRmBits(0xC9), 1);
  EXPECT_EQ(GetModRmRmBits(0x80), 0);
  EXPECT_EQ(GetModRmRmBits(0xBF), 7);
  EXPECT_EQ(GetModRmRmBits(0x77), 7);
  EXPECT_EQ(GetModRmRmBits(0x4A), 2);
  EXPECT_EQ(GetModRmRmBits(0x33), 3);
  EXPECT_EQ(GetModRmRmBits(0x1F), 7);
}

TEST(NumModRmDisplacementBytesTest, Test) {
  constexpr struct {
    const char* modrm;
    const char* sib;
    int expected_num_bytes;
  } kInputs[] = {
      {"addressing_mode: DIRECT register_operand: 1 rm_operand: 2", "", 0},
      {R"(addressing_mode: INDIRECT_WITH_8_BIT_DISPLACEMENT register_operand: 2
          rm_operand: 3)",
       "", 1},
      {R"(addressing_mode: INDIRECT_WITH_8_BIT_DISPLACEMENT register_operand: 2
          rm_operand: 4)",
       "base: 5 index: 3 scale: 1", 1},
      {"addressing_mode: INDIRECT register_operand: 2 rm_operand: 4",
       "base: 5 index: 3 scale: 1", 4},
      {"addressing_mode: INDIRECT register_operand: 2 rm_operand: 5", "", 4},
  };
  for (const auto& input : kInputs) {
    SCOPED_TRACE(absl::StrCat("ModR/M: ", input.modrm, "\nSIB: ", input.sib));
    ModRm modrm;
    Sib sib;
    EXPECT_TRUE(
        google::protobuf::TextFormat::ParseFromString(input.modrm, &modrm));
    EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(input.sib, &sib));
    const int num_bytes = NumModRmDisplacementBytes(modrm, sib);
    EXPECT_EQ(input.expected_num_bytes, num_bytes);
  }
}

TEST(ModRmRequiresSibTest, Test) {
  constexpr struct {
    const char* modrm;
    bool expected_requires_sib;
  } kInputs[] = {
      {"addressing_mode: DIRECT register_operand: 1 rm_operand: 2", false},
      {"addressing_mode: DIRECT register_operand: 2 rm_operand: 4", false},
      {"addressing_mode: INDIRECT register_operand: 0 rm_operand: 0", false},
      {R"(addressing_mode: INDIRECT_WITH_8_BIT_DISPLACEMENT register_operand: 0
          rm_operand: 0)",
       false},
      {R"(addressing_mode: INDIRECT_WITH_32_BIT_DISPLACEMENT register_operand: 0
          rm_operand: 0)",
       false},
      {"addressing_mode: INDIRECT register_operand: 0 rm_operand: 4", true},
      {R"(addressing_mode: INDIRECT_WITH_8_BIT_DISPLACEMENT register_operand: 0
          rm_operand: 4)",
       true},
      {R"(addressing_mode: INDIRECT_WITH_32_BIT_DISPLACEMENT register_operand: 0
          rm_operand: 4)",
       true},
  };
  for (const auto& input : kInputs) {
    SCOPED_TRACE(input.modrm);
    ModRm modrm;
    EXPECT_TRUE(
        google::protobuf::TextFormat::ParseFromString(input.modrm, &modrm));
    const bool requires_sib = ModRmRequiresSib(modrm);
    EXPECT_EQ(input.expected_requires_sib, requires_sib);
  }
}

TEST(ValidateMandatoryPrefixBitsTest, TestVexAndEvex) {
  constexpr struct {
    VexEncoding::MandatoryPrefix mandatory_prefix_in_specification;
    VexEncoding::MandatoryPrefix mandatory_prefix_in_prefix;
    Code expected_status_code;
  } kInputs[] = {
      {VexEncoding::NO_MANDATORY_PREFIX, VexEncoding::NO_MANDATORY_PREFIX, OK},
      {VexEncoding::NO_MANDATORY_PREFIX, VexEncoding::MANDATORY_PREFIX_REPE,
       INVALID_ARGUMENT}};
  for (const auto& input : kInputs) {
    SCOPED_TRACE(absl::StrCat(
        "{",
        VexEncoding::MandatoryPrefix_Name(
            input.mandatory_prefix_in_specification),
        ", ",
        VexEncoding::MandatoryPrefix_Name(input.mandatory_prefix_in_prefix),
        ", ", input.expected_status_code, "}"));
    VexPrefixEncodingSpecification specification;
    specification.set_mandatory_prefix(input.mandatory_prefix_in_specification);
    VexPrefix vex_prefix;
    vex_prefix.set_mandatory_prefix(input.mandatory_prefix_in_prefix);
    EXPECT_THAT(ValidateMandatoryPrefixBits(specification, vex_prefix),
                StatusIs(input.expected_status_code));
    EvexPrefix evex_prefix;
    evex_prefix.set_mandatory_prefix(input.mandatory_prefix_in_prefix);
    EXPECT_THAT(ValidateMandatoryPrefixBits(specification, evex_prefix),
                StatusIs(input.expected_status_code));
  }
}

TEST(ValidateMapSelectBits, TestVexAndEvex) {
  constexpr struct {
    VexEncoding::MapSelect map_select_in_specification;
    VexEncoding::MapSelect map_select_in_prefix;
    Code expected_status_code;
  } kInputs[] = {{VexEncoding::MAP_SELECT_0F, VexEncoding::MAP_SELECT_0F, OK},
                 {VexEncoding::MAP_SELECT_0F3A, VexEncoding::MAP_SELECT_0F38,
                  INVALID_ARGUMENT},
                 {VexEncoding::UNDEFINED_OPERAND_MAP,
                  VexEncoding::MAP_SELECT_0F, INVALID_ARGUMENT},
                 {VexEncoding::UNDEFINED_OPERAND_MAP,
                  VexEncoding::UNDEFINED_OPERAND_MAP, INVALID_ARGUMENT}};
  for (const auto& input : kInputs) {
    SCOPED_TRACE(absl::StrCat(
        "{", VexEncoding::MapSelect_Name(input.map_select_in_specification),
        ", ", VexEncoding::MapSelect_Name(input.map_select_in_prefix), ", ",
        input.expected_status_code, "}"));
    VexPrefixEncodingSpecification specification;
    specification.set_map_select(input.map_select_in_specification);
    VexPrefix vex_prefix;
    vex_prefix.set_map_select(input.map_select_in_prefix);
    EXPECT_THAT(ValidateMapSelectBits(specification, vex_prefix),
                StatusIs(input.expected_status_code));
    EvexPrefix evex_prefix;
    evex_prefix.set_map_select(input.map_select_in_prefix);
    EXPECT_THAT(ValidateMapSelectBits(specification, evex_prefix),
                StatusIs(input.expected_status_code));
  }
}

TEST(ValidateVectorSizeBitsTest, Test) {
  constexpr struct {
    VexVectorSize vector_size;
    uint32_t vector_length_bits;
    VexPrefixType prefix_type;
    Code expected_status_code;
  } kInputs[] = {{VEX_VECTOR_SIZE_128_BIT, kEvexPrefixVectorLength128BitsOrZero,
                  VEX_PREFIX, OK},
                 {VEX_VECTOR_SIZE_BIT_IS_ZERO,
                  kEvexPrefixVectorLength128BitsOrZero, VEX_PREFIX, OK},
                 {VEX_VECTOR_SIZE_IS_IGNORED,
                  kEvexPrefixVectorLength128BitsOrZero, VEX_PREFIX, OK},
                 {VEX_VECTOR_SIZE_IS_IGNORED, kEvexPrefixVectorLength512Bits,
                  VEX_PREFIX, INVALID_ARGUMENT},
                 {VEX_VECTOR_SIZE_512_BIT, kEvexPrefixVectorLength512Bits,
                  VEX_PREFIX, FAILED_PRECONDITION},
                 {VEX_VECTOR_SIZE_512_BIT, kEvexPrefixVectorLength512Bits,
                  EVEX_PREFIX, OK},
                 {VEX_VECTOR_SIZE_256_BIT, kEvexPrefixVectorLength512Bits,
                  VEX_PREFIX, INVALID_ARGUMENT}};
  for (const auto& input : kInputs) {
    SCOPED_TRACE(absl::StrCat("{", VexVectorSize_Name(input.vector_size), ", ",
                              input.vector_length_bits, ", ",
                              VexPrefixType_Name(input.prefix_type), "}"));
    const Status validation_status = ValidateVectorSizeBits(
        input.vector_size, input.vector_length_bits, input.prefix_type);
    EXPECT_THAT(validation_status, StatusIs(input.expected_status_code));
  }
}

TEST(ValidateVexWBitTest, Test) {
  constexpr struct {
    VexPrefixEncodingSpecification::VexWUsage vex_w_usage;
    bool vex_w_bit;
    Code expected_status_code;
  } kInputs[] = {{kVexWIsOne, true, OK},
                 {kVexWIsOne, false, INVALID_ARGUMENT},
                 {kVexWIsZero, true, INVALID_ARGUMENT},
                 {kVexWIsZero, false, OK},
                 {kVexWIsIgnored, true, OK},
                 {kVexWIsIgnored, false, OK}};
  for (const auto& input : kInputs) {
    SCOPED_TRACE(absl::StrCat(
        "{", VexPrefixEncodingSpecification::VexWUsage_Name(input.vex_w_usage),
        ", ", input.vex_w_bit, ", ", input.expected_status_code));
    const Status validation_status =
        ValidateVexWBit(input.vex_w_usage, input.vex_w_bit);
    EXPECT_THAT(validation_status, StatusIs(input.expected_status_code));
  }
}

TEST(GetUsedEvexBInterpretationTest, Test) {
  constexpr struct {
    const char* vex_prefix_specification;
    const char* decoded_instruction;
    EvexBInterpretation expected_interpretation;
  } kInputs[] = {
      {"prefix_type: VEX_PREFIX", "modrm { addressing_mode: DIRECT }",
       UNDEFINED_EVEX_B_INTERPRETATION},
      {"prefix_type: EVEX_PREFIX", "modrm { addressing_mode: DIRECT }",
       UNDEFINED_EVEX_B_INTERPRETATION},
      {"prefix_type: EVEX_PREFIX", "modrm { addressing_mode: INDIRECT }",
       UNDEFINED_EVEX_B_INTERPRETATION},
      {"prefix_type: EVEX_PREFIX "
       "evex_b_interpretations: EVEX_B_ENABLES_32_BIT_BROADCAST ",
       "modrm { addressing_mode: DIRECT }", UNDEFINED_EVEX_B_INTERPRETATION},
      {"prefix_type: EVEX_PREFIX "
       "evex_b_interpretations: EVEX_B_ENABLES_32_BIT_BROADCAST ",
       "modrm { addressing_mode: INDIRECT }", EVEX_B_ENABLES_32_BIT_BROADCAST},
      {"prefix_type: EVEX_PREFIX "
       "evex_b_interpretations: EVEX_B_ENABLES_64_BIT_BROADCAST ",
       "modrm { addressing_mode: INDIRECT_WITH_32_BIT_DISPLACEMENT }",
       EVEX_B_ENABLES_64_BIT_BROADCAST},
      {"prefix_type: EVEX_PREFIX "
       "evex_b_interpretations: EVEX_B_ENABLES_STATIC_ROUNDING_CONTROL ",
       "modrm { addressing_mode: INDIRECT }",
       EVEX_B_ENABLES_STATIC_ROUNDING_CONTROL},
      {"prefix_type: EVEX_PREFIX "
       "evex_b_interpretations: EVEX_B_ENABLES_STATIC_ROUNDING_CONTROL ",
       "modrm { addressing_mode: DIRECT }",
       EVEX_B_ENABLES_STATIC_ROUNDING_CONTROL},
      {"prefix_type: EVEX_PREFIX "
       "evex_b_interpretations: EVEX_B_ENABLES_STATIC_ROUNDING_CONTROL "
       "evex_b_interpretations: EVEX_B_ENABLES_64_BIT_BROADCAST ",
       "modrm { addressing_mode: INDIRECT }", EVEX_B_ENABLES_64_BIT_BROADCAST},
      {"prefix_type: EVEX_PREFIX "
       "evex_b_interpretations: EVEX_B_ENABLES_STATIC_ROUNDING_CONTROL "
       "evex_b_interpretations: EVEX_B_ENABLES_64_BIT_BROADCAST ",
       "modrm { addressing_mode: DIRECT }",
       EVEX_B_ENABLES_STATIC_ROUNDING_CONTROL}};
  for (const auto& input : kInputs) {
    SCOPED_TRACE(
        absl::StrCat("Specification:\n", input.vex_prefix_specification));
    SCOPED_TRACE(
        absl::StrCat("Instruction data:\n", input.decoded_instruction));
    VexPrefixEncodingSpecification vex_prefix_specification;
    EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(
        input.vex_prefix_specification, &vex_prefix_specification));
    DecodedInstruction instruction;
    EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(
        input.decoded_instruction, &instruction));
    EXPECT_EQ(GetUsedEvexBInterpretation(vex_prefix_specification, instruction),
              input.expected_interpretation);
  }
}

TEST(ValidateEvexBBitTest, Test) {
  constexpr struct {
    const char* vex_prefix_specification;
    const char* decoded_instruction;
    Code expected_status_code;
  } kInputs[] = {
      {"prefix_type: VEX_PREFIX", "evex_prefix { broadcast_or_control: true }",
       FAILED_PRECONDITION},
      {"prefix_type: VEX_PREFIX", "evex_prefix { broadcast_or_control: true }",
       FAILED_PRECONDITION},
      {"prefix_type: EVEX_PREFIX", "evex_prefix {}", OK},
      {"prefix_type: EVEX_PREFIX", "evex_prefix { broadcast_or_control: true }",
       INVALID_ARGUMENT},
      {"prefix_type: EVEX_PREFIX "
       "evex_b_interpretations: EVEX_B_ENABLES_32_BIT_BROADCAST ",
       "evex_prefix {}", OK},
      {"prefix_type: EVEX_PREFIX "
       "evex_b_interpretations: EVEX_B_ENABLES_STATIC_ROUNDING_CONTROL ",
       "evex_prefix { broadcast_or_control: true } ", OK},
      {"prefix_type: EVEX_PREFIX "
       "evex_b_interpretations: EVEX_B_ENABLES_32_BIT_BROADCAST ",
       "evex_prefix { broadcast_or_control: true } ", INVALID_ARGUMENT},
      {"prefix_type: EVEX_PREFIX "
       "evex_b_interpretations: EVEX_B_ENABLES_32_BIT_BROADCAST ",
       "evex_prefix { broadcast_or_control: true } "
       "modrm { addressing_mode: INDIRECT }",
       OK},
      {"prefix_type: EVEX_PREFIX "
       "evex_b_interpretations: EVEX_B_ENABLES_32_BIT_BROADCAST "
       "evex_b_interpretations: EVEX_B_ENABLES_SUPPRESS_ALL_EXCEPTIONS ",
       "evex_prefix { broadcast_or_control: true } "
       "modrm { addressing_mode: INDIRECT } ",
       OK},
      {"prefix_type: EVEX_PREFIX "
       "evex_b_interpretations: EVEX_B_ENABLES_32_BIT_BROADCAST "
       "evex_b_interpretations: EVEX_B_ENABLES_SUPPRESS_ALL_EXCEPTIONS ",
       "evex_prefix { broadcast_or_control: true } "
       "modrm { addressing_mode: DIRECT } ",
       OK}};
  for (const auto& input : kInputs) {
    SCOPED_TRACE(
        absl::StrCat("Specification:\n", input.vex_prefix_specification));
    SCOPED_TRACE(
        absl::StrCat("Instruction data:\n", input.decoded_instruction));
    VexPrefixEncodingSpecification specification;
    ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
        input.vex_prefix_specification, &specification));
    DecodedInstruction instruction;
    ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
        input.decoded_instruction, &instruction));
    EXPECT_THAT(ValidateEvexBBit(specification, instruction),
                StatusIs(input.expected_status_code));
  }
}

TEST(ValidateEvexOpmaskTest, Test) {
  constexpr struct {
    const char* vex_prefix_specification;
    const char* decoded_instruction;
    Code expected_status_code;
  } kInputs[] = {
      {"prefix_type: VEX_PREFIX", "vex_prefix {}", FAILED_PRECONDITION},
      {"prefix_type: VEX_PREFIX", "evex_prefix { opmask_register: 1 }",
       FAILED_PRECONDITION},
      {"prefix_type: EVEX_PREFIX", "evex_prefix {}", OK},
      {"prefix_type: EVEX_PREFIX opmask_usage: EVEX_OPMASK_IS_NOT_USED",
       "evex_prefix { opmask_register: 1 }", INVALID_ARGUMENT},
      {"prefix_type: EVEX_PREFIX opmask_usage: EVEX_OPMASK_IS_NOT_USED",
       "evex_prefix { opmask_register: 0 z: true }", INVALID_ARGUMENT},
      {"prefix_type: EVEX_PREFIX opmask_usage: EVEX_OPMASK_IS_OPTIONAL "
       "masking_operation: EVEX_MASKING_MERGING_ONLY",
       "evex_prefix { opmask_register: 1 z: false }", OK},
      {"prefix_type: EVEX_PREFIX opmask_usage: EVEX_OPMASK_IS_REQUIRED "
       "masking_operation: EVEX_MASKING_MERGING_ONLY",
       "evex_prefix { opmask_register: 1 z: false }", OK},
      {"prefix_type: EVEX_PREFIX opmask_usage: EVEX_OPMASK_IS_REQUIRED "
       "masking_operation: EVEX_MASKING_MERGING_ONLY",
       "evex_prefix { opmask_register: 0 z: false }", INVALID_ARGUMENT},
      {"prefix_type: EVEX_PREFIX opmask_usage: EVEX_OPMASK_IS_OPTIONAL "
       "masking_operation: EVEX_MASKING_MERGING_ONLY",
       "evex_prefix { opmask_register: 1 z: true }", INVALID_ARGUMENT},
      {"prefix_type: EVEX_PREFIX opmask_usage: EVEX_OPMASK_IS_REQUIRED "
       "masking_operation: EVEX_MASKING_MERGING_AND_ZEROING",
       "evex_prefix { opmask_register: 0 z: true }", INVALID_ARGUMENT},
      {"prefix_type: EVEX_PREFIX opmask_usage: EVEX_OPMASK_IS_OPTIONAL "
       "masking_operation: EVEX_MASKING_MERGING_AND_ZEROING",
       "evex_prefix { opmask_register: 0 z: true }", OK}};
  for (const auto& input : kInputs) {
    SCOPED_TRACE(
        absl::StrCat("Specification:\n", input.vex_prefix_specification));
    SCOPED_TRACE(
        absl::StrCat("Instruction data:\n", input.decoded_instruction));
    VexPrefixEncodingSpecification specification;
    ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
        input.vex_prefix_specification, &specification));
    DecodedInstruction decoded_instruction;
    ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
        input.decoded_instruction, &decoded_instruction));
    EXPECT_THAT(ValidateEvexOpmask(specification, decoded_instruction),
                StatusIs(input.expected_status_code));
  }
}

TEST(ValidateVexRegisterOperandBitsTest, Test) {
  constexpr struct {
    VexPrefixType prefix_type;
    VexOperandUsage operand_usage;
    uint32_t operand_bits;
    Code expected_status_code;
  } kInputs[] = {
      {VEX_PREFIX, NO_VEX_OPERAND_USAGE, 0, OK},
      {EVEX_PREFIX, NO_VEX_OPERAND_USAGE, 0, OK},
      {VEX_PREFIX, NO_VEX_OPERAND_USAGE, 1, INVALID_ARGUMENT},
      {EVEX_PREFIX, NO_VEX_OPERAND_USAGE, 15, INVALID_ARGUMENT},
      {VEX_PREFIX, NO_VEX_OPERAND_USAGE, 15, OK},
      {EVEX_PREFIX, NO_VEX_OPERAND_USAGE, 31, OK},
      {VEX_PREFIX, VEX_OPERAND_IS_DESTINATION_REGISTER, 22, INVALID_ARGUMENT},
      {EVEX_PREFIX, VEX_OPERAND_IS_DESTINATION_REGISTER, 22, OK},
      {VEX_PREFIX, VEX_OPERAND_IS_FIRST_SOURCE_REGISTER, 0, OK},
      {VEX_PREFIX, VEX_OPERAND_IS_FIRST_SOURCE_REGISTER, 1, OK},
      {VEX_PREFIX, VEX_OPERAND_IS_FIRST_SOURCE_REGISTER, 15, OK},
      {VEX_PREFIX, VEX_OPERAND_IS_SECOND_SOURCE_REGISTER, 4, OK},
      {VEX_PREFIX, VEX_OPERAND_IS_DESTINATION_REGISTER, 4, OK}};
  for (const auto& input : kInputs) {
    SCOPED_TRACE(absl::StrCat("{", VexOperandUsage_Name(input.operand_usage),
                              ", ", input.operand_bits, ", ",
                              input.expected_status_code, "}"));
    VexPrefixEncodingSpecification specification;
    specification.set_prefix_type(input.prefix_type);
    specification.set_vex_operand_usage(input.operand_usage);
    EXPECT_THAT(
        ValidateVexRegisterOperandBits(specification, input.operand_bits),
        StatusIs(input.expected_status_code));
  }
}

TEST(ModRmUsageMatchesSpecification, FullModRm) {
  constexpr struct {
    const char* specification;
    const char* decoded_instruction;
    const char* instruction_format;
    bool expected_match;
  } kInputs[] = {
      {"modrm_usage: FULL_MODRM",
       "modrm { addressing_mode: INDIRECT_WITH_8_BIT_DISPLACEMENT }",
       R"(operands {
            addressing_mode: DIRECT_ADDRESSING
            encoding: MODRM_REG_ENCODING
          }
          operands {
            addressing_mode: INDIRECT_ADDRESSING
            encoding: MODRM_RM_ENCODING
          })",
       true},
      {"modrm_usage: FULL_MODRM",
       "modrm { addressing_mode: INDIRECT_WITH_8_BIT_DISPLACEMENT }",
       R"(operands {
            addressing_mode: DIRECT_ADDRESSING
            encoding: MODRM_REG_ENCODING
          }
          operands {
            addressing_mode: DIRECT_ADDRESSING
            encoding: MODRM_RM_ENCODING
          })",
       false},
      {"modrm_usage: FULL_MODRM", "modrm { addressing_mode: DIRECT }",
       R"(operands {
            addressing_mode: DIRECT_ADDRESSING
            encoding: MODRM_REG_ENCODING
          }
          operands {
            addressing_mode: DIRECT_ADDRESSING
            encoding: MODRM_RM_ENCODING
          })",
       true},
      {"modrm_usage: FULL_MODRM", "modrm { addressing_mode: DIRECT }",
       R"(operands {
            addressing_mode: DIRECT_ADDRESSING
            encoding: MODRM_REG_ENCODING
          }
          operands {
            addressing_mode: INDIRECT_ADDRESSING
            encoding: MODRM_RM_ENCODING
          })",
       false},
      {"modrm_usage: FULL_MODRM",
       "modrm { addressing_mode: INDIRECT_WITH_32_BIT_DISPLACEMENT }",
       R"(operands {
            addressing_mode: DIRECT_ADDRESSING
            encoding: MODRM_REG_ENCODING
          }
          operands {
            addressing_mode: INDIRECT_ADDRESSING
            encoding: MODRM_RM_ENCODING
          })",
       true},
      {"modrm_usage: FULL_MODRM",
       "modrm { addressing_mode: INDIRECT_WITH_32_BIT_DISPLACEMENT }",
       R"(operands {
            addressing_mode: DIRECT_ADDRESSING
            encoding: MODRM_REG_ENCODING
          }
          operands {
            addressing_mode: DIRECT_ADDRESSING
            encoding: MODRM_RM_ENCODING
          })",
       false},
  };
  for (const auto& input : kInputs) {
    SCOPED_TRACE(input.specification);
    EncodingSpecification specification =
        ParseProtoFromStringOrDie<EncodingSpecification>(input.specification);
    DecodedInstruction decoded_instruction =
        ParseProtoFromStringOrDie<DecodedInstruction>(
            input.decoded_instruction);
    InstructionFormat instruction_format =
        ParseProtoFromStringOrDie<InstructionFormat>(input.instruction_format);
    EXPECT_EQ(ModRmUsageMatchesSpecification(specification, decoded_instruction,
                                             instruction_format),
              input.expected_match);
  }
}

TEST(ConvertToInstructionOperandAddressingMode, NoSib) {
  constexpr struct {
    const char* decoded_instruction;
    InstructionOperand::AddressingMode expected;
  } kInputs[] = {
      {"modrm { addressing_mode: INDIRECT }",
       InstructionOperand::INDIRECT_ADDRESSING},
      {"modrm { addressing_mode: INDIRECT_WITH_8_BIT_DISPLACEMENT }",
       InstructionOperand::INDIRECT_ADDRESSING_WITH_BASE_AND_DISPLACEMENT},
      {"modrm { addressing_mode: INDIRECT_WITH_32_BIT_DISPLACEMENT }",
       InstructionOperand::INDIRECT_ADDRESSING_WITH_BASE_AND_DISPLACEMENT},
      {"modrm { addressing_mode: INDIRECT rm_operand: 5 }",
       InstructionOperand::INDIRECT_ADDRESSING_WITH_INSTRUCTION_POINTER},
      {"modrm { addressing_mode: DIRECT }",
       InstructionOperand::DIRECT_ADDRESSING},
  };
  for (const auto& input : kInputs) {
    SCOPED_TRACE(input.decoded_instruction);
    const DecodedInstruction decoded_instruction =
        ParseProtoFromStringOrDie<DecodedInstruction>(
            input.decoded_instruction);

    EXPECT_EQ(ConvertToInstructionOperandAddressingMode(decoded_instruction),
              input.expected);
  }
}

TEST(ConvertToInstructionOperandAddressingMode, SibIndirectWithDisplacement) {
  constexpr struct {
    const char* decoded_instruction;
    InstructionOperand::AddressingMode expected;
  } kInputs[] = {
      {R"(modrm {
            addressing_mode: INDIRECT_WITH_8_BIT_DISPLACEMENT
            rm_operand: 4
          }
          sib { index: 4 })",
       InstructionOperand::INDIRECT_ADDRESSING_WITH_BASE_AND_DISPLACEMENT},
      {R"(modrm {
            addressing_mode: INDIRECT_WITH_8_BIT_DISPLACEMENT
            rm_operand: 4
          }
          sib {})",
       InstructionOperand::
           INDIRECT_ADDRESSING_WITH_BASE_DISPLACEMENT_AND_INDEX},
      {R"(modrm {
            addressing_mode: INDIRECT_WITH_32_BIT_DISPLACEMENT
            rm_operand: 4
          }
          sib { index: 4 })",
       InstructionOperand::INDIRECT_ADDRESSING_WITH_BASE_AND_DISPLACEMENT},
      {R"(modrm {
            addressing_mode: INDIRECT_WITH_32_BIT_DISPLACEMENT
            rm_operand: 4
          }
          sib {})",
       InstructionOperand::
           INDIRECT_ADDRESSING_WITH_BASE_DISPLACEMENT_AND_INDEX},
  };
  for (const auto& input : kInputs) {
    SCOPED_TRACE(input.decoded_instruction);
    const DecodedInstruction decoded_instruction =
        ParseProtoFromStringOrDie<DecodedInstruction>(
            input.decoded_instruction);

    EXPECT_EQ(ConvertToInstructionOperandAddressingMode(decoded_instruction),
              input.expected);
  }
}

TEST(ConvertToInstructionOperandAddressingMode, SibIndirect) {
  constexpr struct {
    const char* decoded_instruction;
    InstructionOperand::AddressingMode expected;
  } kInputs[] = {
      {"modrm { addressing_mode: INDIRECT rm_operand: 4}"
       "sib { index: 4 base: 5 }",
       InstructionOperand::INDIRECT_ADDRESSING_WITH_DISPLACEMENT},
      {"modrm { addressing_mode: INDIRECT rm_operand: 4} sib { index: 4 }",
       InstructionOperand::INDIRECT_ADDRESSING_WITH_BASE},
      {"modrm { addressing_mode: INDIRECT rm_operand: 4} sib { base: 5 }",
       InstructionOperand::INDIRECT_ADDRESSING_WITH_INDEX_AND_DISPLACEMENT},
      {"modrm { addressing_mode: INDIRECT rm_operand: 4} sib { }",
       InstructionOperand::INDIRECT_ADDRESSING_WITH_BASE_AND_INDEX},
  };
  for (const auto& input : kInputs) {
    SCOPED_TRACE(input.decoded_instruction);
    const DecodedInstruction decoded_instruction =
        ParseProtoFromStringOrDie<DecodedInstruction>(
            input.decoded_instruction);

    EXPECT_EQ(ConvertToInstructionOperandAddressingMode(decoded_instruction),
              input.expected);
  }
}

TEST(AddressingModeMatchesSpecification, NoSibIndirect) {
  constexpr struct {
    const char* decoded_instruction;
    const char* specification;
    const char* instruction_format;
    bool expected_match;
  } kInputs[] = {
      {"modrm { addressing_mode: INDIRECT }", "modrm_usage: FULL_MODRM",
       R"(operands {
            addressing_mode: INDIRECT_ADDRESSING
            encoding: MODRM_RM_ENCODING
          })",
       true},
      {"modrm { addressing_mode: INDIRECT_WITH_8_BIT_DISPLACEMENT }",
       "modrm_usage: OPCODE_EXTENSION_IN_MODRM",
       R"(operands {
            addressing_mode: INDIRECT_ADDRESSING
            encoding: MODRM_RM_ENCODING
          })",
       true},
      {"modrm { addressing_mode: INDIRECT_WITH_32_BIT_DISPLACEMENT }",
       "modrm_usage: NO_MODRM_USAGE",
       R"(operands {
            addressing_mode: DIRECT_ADDRESSING
            encoding: MODRM_RM_ENCODING
          })",
       true},
      {"modrm { addressing_mode: INDIRECT_WITH_32_BIT_DISPLACEMENT }",
       "modrm_usage: FULL_MODRM",
       R"(operands {
            addressing_mode: INDIRECT_ADDRESSING_WITH_BASE_AND_DISPLACEMENT
            encoding: MODRM_RM_ENCODING
          })",
       true},
      {"modrm { addressing_mode: INDIRECT rm_operand: 5 }",
       "modrm_usage: FULL_MODRM",
       R"(operands {
            addressing_mode: INDIRECT_ADDRESSING_WITH_INSTRUCTION_POINTER
            encoding: MODRM_RM_ENCODING
          })",
       true},
      {"modrm { addressing_mode: DIRECT }", "modrm_usage: FULL_MODRM",
       R"(operands {
            addressing_mode: INDIRECT_ADDRESSING
            encoding: MODRM_RM_ENCODING
          })",
       false},
      {"modrm { addressing_mode: INDIRECT_WITH_32_BIT_DISPLACEMENT }",
       "modrm_usage: FULL_MODRM",
       R"(operands {
            addressing_mode: INDIRECT_ADDRESSING_WITH_BASE
            encoding: MODRM_RM_ENCODING
          })",
       false},
  };
  for (const auto& input : kInputs) {
    SCOPED_TRACE(input.specification);
    const EncodingSpecification specification =
        ParseProtoFromStringOrDie<EncodingSpecification>(input.specification);
    const DecodedInstruction decoded_instruction =
        ParseProtoFromStringOrDie<DecodedInstruction>(
            input.decoded_instruction);
    const InstructionFormat instruction_format =
        ParseProtoFromStringOrDie<InstructionFormat>(input.instruction_format);

    EXPECT_EQ(ModRmUsageMatchesSpecification(specification, decoded_instruction,
                                             instruction_format),
              input.expected_match);
  }
}

TEST(AddressingModeMatchesSpecification, SibIndirect) {
  constexpr struct {
    const char* decoded_instruction;
    const char* specification;
    const char* instruction_format;
    bool expected_match;
  } kInputs[] = {
      {"sib { index: 0x04 base: 0x05 } modrm { rm_operand: 4 addressing_mode: "
       "INDIRECT }",
       "modrm_usage: FULL_MODRM",
       R"(operands {
            addressing_mode: INDIRECT_ADDRESSING_WITH_DISPLACEMENT
            encoding: MODRM_RM_ENCODING
          })",
       true},
      {"sib { index: 0x04 base: 0x02 } modrm { rm_operand: 4 addressing_mode: "
       "INDIRECT }",
       "modrm_usage: FULL_MODRM",
       R"(operands {
            addressing_mode: INDIRECT_ADDRESSING_WITH_BASE
            encoding: MODRM_RM_ENCODING
          })",
       true},
      {"sib { index: 0x04 base: 0x02 } modrm { rm_operand: 4 addressing_mode: "
       "INDIRECT }",
       "modrm_usage: FULL_MODRM",
       R"(operands {
            addressing_mode: INDIRECT_ADDRESSING_WITH_DISPLACEMENT
            encoding: MODRM_RM_ENCODING
          })",
       false},
      {"sib { index: 0x02 base: 0x05 } modrm { rm_operand: 4 addressing_mode: "
       "INDIRECT }",
       "modrm_usage: FULL_MODRM",
       R"(operands {
            addressing_mode: INDIRECT_ADDRESSING_WITH_INDEX_AND_DISPLACEMENT
            encoding: MODRM_RM_ENCODING
          })",
       true},
      {"sib { index: 0x02 base: 0x04 } modrm { rm_operand: 4 addressing_mode: "
       "INDIRECT }",
       "modrm_usage: FULL_MODRM",
       R"(operands {
            addressing_mode: INDIRECT_ADDRESSING_WITH_BASE_AND_INDEX
            encoding: MODRM_RM_ENCODING
          })",
       true},
      {"sib { index: 0x02 base: 0x04 } modrm { rm_operand: 4 addressing_mode: "
       "INDIRECT }",
       "modrm_usage: FULL_MODRM",
       R"(operands {
            addressing_mode: INDIRECT_ADDRESSING_WITH_INDEX_AND_DISPLACEMENT
            encoding: MODRM_RM_ENCODING
          })",
       false},
  };
  for (const auto& input : kInputs) {
    SCOPED_TRACE(input.decoded_instruction);
    const EncodingSpecification specification =
        ParseProtoFromStringOrDie<EncodingSpecification>(input.specification);
    const DecodedInstruction decoded_instruction =
        ParseProtoFromStringOrDie<DecodedInstruction>(
            input.decoded_instruction);
    const InstructionFormat instruction_format =
        ParseProtoFromStringOrDie<InstructionFormat>(input.instruction_format);

    EXPECT_EQ(ModRmUsageMatchesSpecification(specification, decoded_instruction,
                                             instruction_format),
              input.expected_match);
  }
}

TEST(AddressingModeMatchesSpecification, SibIndirectDWithDisplacement) {
  constexpr struct {
    const char* decoded_instruction;
    const char* specification;
    const char* instruction_format;
    bool expected_match;
  } kInputs[] = {
      {R"(sib { index: 0x04 }
          modrm {
            rm_operand: 4
            addressing_mode: INDIRECT_WITH_8_BIT_DISPLACEMENT
          })",
       "modrm_usage: FULL_MODRM",
       R"(operands {
            addressing_mode: INDIRECT_ADDRESSING_WITH_BASE_AND_DISPLACEMENT
            encoding: MODRM_RM_ENCODING
          })",
       true},
      {R"(sib { index: 0x04 }
          modrm {
            rm_operand: 4
            addressing_mode: INDIRECT_WITH_32_BIT_DISPLACEMENT
          })",
       "modrm_usage: FULL_MODRM",
       R"(operands {
            addressing_mode: INDIRECT_ADDRESSING_WITH_BASE_AND_DISPLACEMENT
            encoding: MODRM_RM_ENCODING
          })",
       true},
      {R"(sib { index: 0x02 }
          modrm {
            rm_operand: 4
            addressing_mode: INDIRECT_WITH_32_BIT_DISPLACEMENT
          })",
       "modrm_usage: FULL_MODRM",
       R"(operands {
            addressing_mode: INDIRECT_ADDRESSING_WITH_BASE_DISPLACEMENT_AND_INDEX
            encoding: MODRM_RM_ENCODING
          })",
       true},
      {R"(sib { index: 0x02 }
          modrm {
            rm_operand: 4
            addressing_mode: INDIRECT_WITH_8_BIT_DISPLACEMENT
          })",
       "modrm_usage: FULL_MODRM",
       R"(operands {
            addressing_mode: INDIRECT_ADDRESSING_WITH_BASE_DISPLACEMENT_AND_INDEX
            encoding: MODRM_RM_ENCODING
          })",
       true},
      {R"(sib { index: 0x02 }
          modrm {
            rm_operand: 4
            addressing_mode: INDIRECT_WITH_32_BIT_DISPLACEMENT
          })",
       "modrm_usage: FULL_MODRM",
       R"(operands {
            addressing_mode: INDIRECT_ADDRESSING_WITH_BASE_AND_INDEX
            encoding: MODRM_RM_ENCODING
          })",
       false},
  };
  for (const auto& input : kInputs) {
    SCOPED_TRACE(input.decoded_instruction);
    const EncodingSpecification specification =
        ParseProtoFromStringOrDie<EncodingSpecification>(input.specification);
    const DecodedInstruction decoded_instruction =
        ParseProtoFromStringOrDie<DecodedInstruction>(
            input.decoded_instruction);
    const InstructionFormat instruction_format =
        ParseProtoFromStringOrDie<InstructionFormat>(input.instruction_format);

    EXPECT_EQ(ModRmUsageMatchesSpecification(specification, decoded_instruction,
                                             instruction_format),
              input.expected_match);
  }
}

TEST(BaseDecodedInstructionTest, Test) {
  constexpr struct {
    const char* specification;
    const char* expected_encoding;
  } kInputs[] = {
      {R"(legacy_prefixes {} opcode: 0xff
          modrm_usage: OPCODE_EXTENSION_IN_MODRM
          modrm_opcode_extension: 2)",
       R"(legacy_prefixes {} opcode: 0xff
          modrm { register_operand: 2 })"},
      {R"(legacy_prefixes { has_mandatory_repne_prefix: true }
          opcode: 0x0f58 modrm_usage: FULL_MODRM)",
       R"(legacy_prefixes { lock_or_rep: REPNE_PREFIX } opcode: 0x0f58
          modrm {})"},
      {R"(legacy_prefixes {} opcode: 0xd5
          immediate_value_bytes: 1)",
       "legacy_prefixes {} opcode: 0xd5 "},
      {R"(vex_prefix { vector_size: VEX_VECTOR_SIZE_128_BIT
                       map_select: MAP_SELECT_0F }
          opcode: 0x0f77)",
       R"(vex_prefix { not_r: true not_x: true
                       not_b: true map_select: MAP_SELECT_0F
                       w: false use_256_bit_vector_length: false }
          opcode: 0x0f77)"},
      {R"(vex_prefix {
            vex_operand_usage: VEX_OPERAND_IS_SECOND_SOURCE_REGISTER
            vector_size: VEX_VECTOR_SIZE_128_BIT
            mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
            map_select: MAP_SELECT_0F38 vex_w_usage: VEX_W_IS_ONE }
          opcode: 0x0f3899 modrm_usage: FULL_MODRM)",
       R"(vex_prefix { not_b: true not_r: true
                       not_x: true map_select: MAP_SELECT_0F38
                       mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
                       w: true }
          opcode: 0x0f3899 modrm {})"},
      {R"(vex_prefix {
            vex_operand_usage: VEX_OPERAND_IS_DESTINATION_REGISTER
            vector_size: VEX_VECTOR_SIZE_128_BIT
            mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
            map_select: MAP_SELECT_0F
            vex_w_usage: VEX_W_IS_IGNORED }
          opcode: 0x0f72
          modrm_usage: OPCODE_EXTENSION_IN_MODRM
          modrm_opcode_extension: 6
          immediate_value_bytes: 1)",
       R"(vex_prefix { not_b: true not_r: true
                       not_x: true map_select: MAP_SELECT_0F
                       mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
          }
          opcode: 0x0f72 modrm { register_operand: 6 })"},
      {R"(opcode: 0x0f38f3
            modrm_usage: OPCODE_EXTENSION_IN_MODRM
            modrm_opcode_extension: 2
            vex_prefix {
              prefix_type: VEX_PREFIX
              vex_operand_usage: VEX_OPERAND_IS_DESTINATION_REGISTER
              vector_size: VEX_VECTOR_SIZE_BIT_IS_ZERO
              map_select: MAP_SELECT_0F38
              vex_w_usage: VEX_W_IS_ONE
            })",
       R"(vex_prefix { not_b: true not_r: true not_x: true
                       w: true map_select: MAP_SELECT_0F38
          }
          opcode: 0x0f38f3
          modrm { register_operand: 2 }
      )"},
      {R"(opcode: 0x0f38f3
            modrm_usage: OPCODE_EXTENSION_IN_MODRM
            modrm_opcode_extension: 3
            vex_prefix {
              prefix_type: VEX_PREFIX
              vex_operand_usage: VEX_OPERAND_IS_DESTINATION_REGISTER
              vector_size: VEX_VECTOR_SIZE_BIT_IS_ZERO
              map_select: MAP_SELECT_0F38
              vex_w_usage: VEX_W_IS_ONE
            })",
       R"(vex_prefix { not_b: true not_r: true not_x: true
                       w: true map_select: MAP_SELECT_0F38
          }
          opcode: 0x0f38f3
          modrm { register_operand: 3 }
      )"},
      {R"(vex_prefix { vector_size: VEX_VECTOR_SIZE_256_BIT
                       map_select: MAP_SELECT_0F }
          opcode: 0x0f77)",
       R"(vex_prefix { not_b: true not_r: true
                       not_x: true map_select: MAP_SELECT_0F
                       use_256_bit_vector_length: true }
          opcode: 0x0f77)"}};
  for (const auto& input : kInputs) {
    SCOPED_TRACE(input.specification);
    EncodingSpecification specification;
    EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(
        input.specification, &specification));
    const DecodedInstruction instruction_data =
        BaseDecodedInstruction(specification);
    EXPECT_THAT(instruction_data, EqualsProto(input.expected_encoding));
  }
}

void TestGenerateEncodingExamples(
    const std::string& instruction_proto,
    const std::vector<std::string>& expected_examples) {
  SCOPED_TRACE(instruction_proto);
  InstructionProto instruction;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(instruction_proto,
                                                            &instruction));
  const std::vector<DecodedInstruction> examples =
      GenerateEncodingExamples(instruction);
  EXPECT_THAT(examples, UnorderedPointwise(EqualsProto(), expected_examples));
}

TEST(GenerateEncodingExamplesTest, NoOperands) {
  TestGenerateEncodingExamples(
      R"(vendor_syntax { mnemonic: 'CLC' }
         encoding_scheme: 'NP'
         raw_encoding_specification: 'F8')",
      {"legacy_prefixes {} opcode: 0xf8 "});
}

TEST(GenerateEncodingExamplesTest, OperandInOpcode) {
  TestGenerateEncodingExamples(
      R"(vendor_syntax {
           mnemonic: 'BSWAP'
           operands { name: 'r32'
                      encoding: OPCODE_ENCODING
                      addressing_mode: DIRECT_ADDRESSING
                      value_size_bits: 32 }}
         encoding_scheme: 'O'
         raw_encoding_specification: '0F C8+rd')",
      {"legacy_prefixes {} opcode: 0x0fc8",
       "legacy_prefixes { rex { b: true }} opcode: 0x0fc8"});
  TestGenerateEncodingExamples(
      R"(vendor_syntax {
           mnemonic: 'BSWAP' operands { }
           operands { name: 'r64' encoding: OPCODE_ENCODING
                      addressing_mode: DIRECT_ADDRESSING
                      value_size_bits: 64 }}
         encoding_scheme: 'O'
         raw_encoding_specification: 'REX.W + 0F C8+rd')",
      // Note that there is no version of the instruction with rex.b set to
      // true, because the instruction already requires the REX prefix because
      // of rex.w.
      {"legacy_prefixes { rex { w: true }} opcode: 0x0fc8"});
}

TEST(GenerateEncodingExamplesTest, ImplicitOperands) {
  TestGenerateEncodingExamples(
      R"(vendor_syntax {
           mnemonic: 'LODS'
           operands { name: 'EAX' addressing_mode: DIRECT_ADDRESSING
                      encoding: IMPLICIT_ENCODING value_size_bits: 32 }
           operands { name: 'DWORD PTR [RSI]'
                      addressing_mode: INDIRECT_ADDRESSING_BY_RSI
                      encoding: IMPLICIT_ENCODING value_size_bits: 32 }}
         encoding_scheme: 'NP'
         raw_encoding_specification: 'AD')",
      {"legacy_prefixes {} opcode: 0xad"});
}

TEST(GenerateEncodingExamplesTest, ImmediateValue) {}

TEST(GenerateEncodingExamplesTest, CodeOffset) {
  TestGenerateEncodingExamples(
      R"(vendor_syntax { mnemonic: 'JS'
                         operands { name: 'rel32'
                                    addressing_mode: NO_ADDRESSING
                                    encoding: IMMEDIATE_VALUE_ENCODING }}
         encoding_scheme: 'D'
         raw_encoding_specification: '0F 88 cd')",
      {R"(legacy_prefixes {} opcode: 0x0f88 code_offset: '\xc0\xc0\xc0\xc0')"});
  TestGenerateEncodingExamples(
      R"(vendor_syntax { mnemonic: 'JS'
                         operands { name: 'rel8'
                                    addressing_mode: NO_ADDRESSING
                                    encoding: IMMEDIATE_VALUE_ENCODING }}
         encoding_scheme: 'D'
         raw_encoding_specification: '78 cb')",
      {R"(legacy_prefixes {} opcode: 0x78 code_offset: '\xc0')"});
}

TEST(GenerateEncodingExamplesTest, DirectAddressingInModRmRm) {
  TestGenerateEncodingExamples(
      R"(vendor_syntax {
           mnemonic: 'RDFSBASE' operands { }
           operands { name: 'r64'
                      addressing_mode: DIRECT_ADDRESSING
                      encoding: MODRM_RM_ENCODING
                      value_size_bits: 64 }}
         feature_name: 'FSGSBASE'
         legacy_instruction: false
         encoding_scheme: 'M'
         raw_encoding_specification: 'REX.W + F3 0F AE /0')",
      {R"(legacy_prefixes { rex { w: true } lock_or_rep: REP_PREFIX }
          opcode: 0x0fae
          modrm { addressing_mode: DIRECT rm_operand: 3 })"});
}

TEST(GenerateEncodingExamplesTest, IndirectAddressingInModRm) {
  TestGenerateEncodingExamples(
      R"(vendor_syntax {
           mnemonic: 'LDMXCSR' operands { }
           operands { name: 'm32' addressing_mode: INDIRECT_ADDRESSING
                      encoding: MODRM_RM_ENCODING value_size_bits: 32 }}
         feature_name: 'SSE'
         encoding_scheme: 'M'
         raw_encoding_specification: '0F AE /2')",
      {// Indirect addressing with 8-bit displacement, with and without SIB.
       R"(legacy_prefixes {}
          opcode: 0x0fae
          modrm { addressing_mode: INDIRECT_WITH_8_BIT_DISPLACEMENT
                  register_operand: 2 rm_operand: 3
                  address_displacement: 127 })",
       R"(legacy_prefixes {}
          opcode: 0x0fae
          modrm { addressing_mode: INDIRECT_WITH_8_BIT_DISPLACEMENT
                  register_operand: 2 rm_operand: 4 address_displacement: 127 }
          sib { scale: 2 index: 1 base: 4 })",
       // Indirect addressing with 32-bit displacement.
       R"(legacy_prefixes {}
          opcode: 0x0fae
          modrm { addressing_mode: INDIRECT_WITH_32_BIT_DISPLACEMENT
                  register_operand: 2 rm_operand: 3
                  address_displacement: 305419896 })",
       R"(legacy_prefixes {}
          opcode: 0x0fae
          modrm { addressing_mode: INDIRECT_WITH_32_BIT_DISPLACEMENT
                  register_operand: 2 rm_operand: 4
                  address_displacement: 305419896 }
          sib { scale: 2 index: 1 base: 4 })",
       // Indirect addressing with no displacement and just a base register.
       R"(legacy_prefixes {}
          opcode: 0x0fae
          modrm { addressing_mode: INDIRECT register_operand: 2 rm_operand: 3 })",
       // Indirect addressing with RIP-relative addressing using a fixed
       // displacement.
       R"(legacy_prefixes {}
          opcode: 0x0fae
          modrm { addressing_mode: INDIRECT register_operand: 2 rm_operand: 5
                  address_displacement: 305419896 })",
       // Indirect addressing with ModR/M and SIB and no specialities.
       R"(legacy_prefixes {}
          opcode: 0x0fae
          modrm { addressing_mode: INDIRECT register_operand: 2 rm_operand: 4 }
          sib { scale: 2 index: 1 base: 4 })",
       // Indirect addressing with ModR/M, SIB and a 32-bit displacement
       // (obtained through SIB).
       R"(legacy_prefixes {}
          opcode: 0x0fae
          modrm { addressing_mode: INDIRECT register_operand: 2 rm_operand: 4
                  address_displacement: 305419896 }
          sib { scale: 2 index: 1 base: 5 })",
       // All of the above, but with rex.b set to one.
       R"(legacy_prefixes { rex { b: true }}
          opcode: 0x0fae
          modrm { addressing_mode: INDIRECT_WITH_8_BIT_DISPLACEMENT
                  register_operand: 2 rm_operand: 3
                  address_displacement: 127 })",
       R"(legacy_prefixes { rex { b: true }}
          opcode: 0x0fae
          modrm { addressing_mode: INDIRECT_WITH_8_BIT_DISPLACEMENT
                  register_operand: 2 rm_operand: 4 address_displacement: 127 }
          sib { scale: 2 index: 1 base: 4 })",
       R"(legacy_prefixes { rex { b: true }}
          opcode: 0x0fae
          modrm { addressing_mode: INDIRECT_WITH_32_BIT_DISPLACEMENT
                  register_operand: 2 rm_operand: 3
                  address_displacement: 305419896 })",
       R"(legacy_prefixes { rex { b: true }}
          opcode: 0x0fae
          modrm { addressing_mode: INDIRECT_WITH_32_BIT_DISPLACEMENT
                  register_operand: 2 rm_operand: 4
                  address_displacement: 305419896 }
          sib { scale: 2 index: 1 base: 4 })",
       R"(legacy_prefixes { rex { b: true }}
          opcode: 0x0fae
          modrm { addressing_mode: INDIRECT register_operand: 2 rm_operand: 3 })",
       R"(legacy_prefixes { rex { b: true }}
          opcode: 0x0fae
          modrm { addressing_mode: INDIRECT register_operand: 2 rm_operand: 5
                  address_displacement: 305419896 })",
       R"(legacy_prefixes { rex { b: true }}
          opcode: 0x0fae
          modrm { addressing_mode: INDIRECT register_operand: 2 rm_operand: 4 }
          sib { scale: 2 index: 1 base: 4 })",
       R"(legacy_prefixes { rex { b: true }}
          opcode: 0x0fae
          modrm { addressing_mode: INDIRECT register_operand: 2 rm_operand: 4
                  address_displacement: 305419896 }
          sib { scale: 2 index: 1 base: 5 })"});
}

TEST(GenerateEncodingExamplesTest, FullModRm) {
  TestGenerateEncodingExamples(
      R"(vendor_syntax {
           mnemonic: 'ADD'
           operands { name: 'r/m8'
                      addressing_mode: ANY_ADDRESSING_WITH_FLEXIBLE_REGISTERS
                      encoding: MODRM_RM_ENCODING
                      value_size_bits: 8 }
           operands { name: 'imm8' addressing_mode: NO_ADDRESSING
                      encoding: IMMEDIATE_VALUE_ENCODING
                      value_size_bits: 8 }}
         encoding_scheme: 'MI'
         raw_encoding_specification: '80 /0 ib')",
      {// Direct addressing.
       R"(legacy_prefixes {} opcode: 0x80 immediate_value: '\xab'
          modrm { addressing_mode: DIRECT register_operand: 0 rm_operand: 3 })",
       // Indirect addressing with 8-bit displacement.
       R"(legacy_prefixes {} opcode: 0x80 immediate_value: '\xab'
          modrm { addressing_mode: INDIRECT_WITH_8_BIT_DISPLACEMENT
                  register_operand: 0 rm_operand: 3 address_displacement: 127 })",
       R"(legacy_prefixes {} opcode: 0x80 immediate_value: '\xab'
          modrm { addressing_mode: INDIRECT_WITH_8_BIT_DISPLACEMENT
                  register_operand: 0 rm_operand: 4 address_displacement: 127 }
          sib { scale: 2 index: 1 base: 4 })",
       // Indirect addressing with 32-bit displacement.
       R"(legacy_prefixes {} opcode: 0x80 immediate_value: '\xab'
          modrm { addressing_mode: INDIRECT_WITH_32_BIT_DISPLACEMENT
                  register_operand: 0 rm_operand: 3
                  address_displacement: 305419896 })",
       R"(legacy_prefixes {} opcode: 0x80 immediate_value: '\xab'
          modrm { addressing_mode: INDIRECT_WITH_32_BIT_DISPLACEMENT
                  register_operand: 0 rm_operand: 4
                  address_displacement: 305419896 }
          sib { scale: 2 index: 1 base: 4 })",
       // Indirect addressing with no displacement and just a base register.
       R"(legacy_prefixes {} opcode: 0x80 immediate_value: '\xab'
          modrm { addressing_mode: INDIRECT
                  register_operand: 0
                  rm_operand: 3 })",
       // Indirect addressing with RIP-relative addressing using a fixed
       // displacement.
       R"(legacy_prefixes {} opcode: 0x80 immediate_value: '\xab'
          modrm { addressing_mode: INDIRECT register_operand: 0 rm_operand: 5
                  address_displacement: 305419896 })",
       // Indirect addressing with ModR/M and SIB and no specialities.
       R"(legacy_prefixes {} opcode: 0x80 immediate_value: '\xab'
          modrm { addressing_mode: INDIRECT register_operand: 0 rm_operand: 4 }
          sib { scale: 2 index: 1 base: 4 })",
       // Indirect addressing with ModR/M, SIB and a 32-bit displacement
       // (obtained through SIB).
       R"(legacy_prefixes {} opcode: 0x80 immediate_value: '\xab'
          modrm { addressing_mode: INDIRECT register_operand: 0 rm_operand: 4
                  address_displacement: 305419896 }
          sib { scale: 2 index: 1 base: 5 })",
       // All of the above, but with rex.b set to one.
       R"(legacy_prefixes { rex { b: true }} opcode: 0x80
          immediate_value: '\xab'
          modrm { addressing_mode: DIRECT register_operand: 0 rm_operand: 3 })",
       R"(legacy_prefixes { rex { b: true }} opcode: 0x80
          immediate_value: '\xab'
          modrm { addressing_mode: INDIRECT_WITH_8_BIT_DISPLACEMENT
                  register_operand: 0
                  rm_operand: 3
                  address_displacement: 127 })",
       R"(legacy_prefixes { rex { b: true }} opcode: 0x80
          immediate_value: '\xab'
          modrm { addressing_mode: INDIRECT_WITH_8_BIT_DISPLACEMENT
                  register_operand: 0 rm_operand: 4 address_displacement: 127 }
          sib { scale: 2 index: 1 base: 4 })",
       R"(legacy_prefixes { rex { b: true }} opcode: 0x80
          immediate_value: '\xab'
          modrm { addressing_mode: INDIRECT_WITH_32_BIT_DISPLACEMENT
                  register_operand: 0 rm_operand: 3
                  address_displacement: 305419896 })",
       R"(legacy_prefixes { rex { b: true }} opcode: 0x80
          immediate_value: '\xab'
          modrm { addressing_mode: INDIRECT_WITH_32_BIT_DISPLACEMENT
                  register_operand: 0 rm_operand: 4
                  address_displacement: 305419896 }
          sib { scale: 2 index: 1 base: 4 })",
       R"(legacy_prefixes { rex { b: true }} opcode: 0x80
          immediate_value: '\xab'
          modrm { addressing_mode: INDIRECT
                  register_operand: 0
                  rm_operand: 3 })",
       R"(legacy_prefixes { rex { b: true }} opcode: 0x80
          immediate_value: '\xab'
          modrm { addressing_mode: INDIRECT register_operand: 0 rm_operand: 5
                  address_displacement: 305419896 })",
       R"(legacy_prefixes { rex { b: true }} opcode: 0x80
          immediate_value: '\xab'
          modrm { addressing_mode: INDIRECT register_operand: 0 rm_operand: 4 }
          sib { scale: 2 index: 1 base: 4 })",
       R"(legacy_prefixes { rex { b: true }} opcode: 0x80
          immediate_value: '\xab'
          modrm { addressing_mode: INDIRECT register_operand: 0 rm_operand: 4
                  address_displacement: 305419896 }
          sib { scale: 2 index: 1 base: 5 })"});
}

TEST(GenerateEncodingExamplesTest, ThreeByteVex) {
  TestGenerateEncodingExamples(
      R"(vendor_syntax {
           mnemonic: 'ANDN'
           operands { name: 'r32a' addressing_mode: DIRECT_ADDRESSING
                      encoding: MODRM_REG_ENCODING value_size_bits: 32 }
           operands { name: 'r32b' addressing_mode: DIRECT_ADDRESSING
                      encoding: VEX_V_ENCODING value_size_bits: 32 }
           operands { name: 'r/m32'
                      addressing_mode: ANY_ADDRESSING_WITH_FLEXIBLE_REGISTERS
                      encoding: MODRM_RM_ENCODING value_size_bits: 32 }}
         feature_name: 'BMI1'
         encoding_scheme: 'RVM'
         raw_encoding_specification: 'VEX.NDS.LZ.0F38.W0 F2 /r')",
      {
          // Direct addressing.
          R"(vex_prefix { not_b: true not_r: true
                          not_x: true map_select: MAP_SELECT_0F38
                          inverted_register_operand: 7 }
             opcode: 0x0f38f2
             modrm { addressing_mode: DIRECT
                     register_operand: 6
                     rm_operand: 3 })",
          // Indirect addressing with 8-bit displacement.
          R"(vex_prefix { not_b: true not_r: true
                          not_x: true map_select: MAP_SELECT_0F38
                          inverted_register_operand: 7 }
             opcode: 0x0f38f2
             modrm { addressing_mode: INDIRECT_WITH_8_BIT_DISPLACEMENT
                     register_operand: 6 rm_operand: 3
                     address_displacement: 127 })",
          R"(vex_prefix { not_b: true not_r: true
                          not_x: true map_select: MAP_SELECT_0F38
                          inverted_register_operand: 7 }
             opcode: 0x0f38f2
             modrm { addressing_mode: INDIRECT_WITH_8_BIT_DISPLACEMENT
                     register_operand: 6 rm_operand: 4
                     address_displacement: 127 }
             sib { scale: 2 index: 1 base: 4 })",
          // Indirect addressing with 32-bit displacement.
          R"(vex_prefix { not_b: true not_r: true
                          not_x: true map_select: MAP_SELECT_0F38
                          inverted_register_operand: 7 }
             opcode: 0x0f38f2
             modrm { addressing_mode: INDIRECT_WITH_32_BIT_DISPLACEMENT
                     register_operand: 6 rm_operand: 3
                     address_displacement: 305419896 })",
          R"(vex_prefix { not_b: true not_r: true
                          not_x: true map_select: MAP_SELECT_0F38
                          inverted_register_operand: 7 }
             opcode: 0x0f38f2
             modrm { addressing_mode: INDIRECT_WITH_32_BIT_DISPLACEMENT
                     register_operand: 6 rm_operand: 4
                     address_displacement: 305419896 }
             sib { scale: 2 index: 1 base: 4 })",
          // Indirect addressing with no displacement and just a base register.
          R"(vex_prefix { not_b: true not_r: true
                          not_x: true map_select: MAP_SELECT_0F38
                          inverted_register_operand: 7 }
             opcode: 0x0f38f2
             modrm { register_operand: 6 rm_operand: 3 })",
          // Indirect addressing with RIP-relative addressing using a fixed
          // displacement.
          R"(vex_prefix { not_b: true not_r: true
                          not_x: true map_select: MAP_SELECT_0F38
                          inverted_register_operand: 7 }
             opcode: 0x0f38f2
             modrm { register_operand: 6 rm_operand: 5
                     address_displacement: 305419896 })",
          // Indirect addressing with ModR/M and SIB and no specialities.
          R"(vex_prefix { not_b: true not_r: true
                          not_x: true map_select: MAP_SELECT_0F38
                          inverted_register_operand: 7 }
             opcode: 0x0f38f2
             modrm { register_operand: 6 rm_operand: 4 }
             sib { scale: 2 index: 1 base: 4 })",
          // Indirect addressing with ModR/M, SIB and a 32-bit displacement
          // (obtained through SIB).
          R"(vex_prefix { not_b: true not_r: true
                          not_x: true map_select: MAP_SELECT_0F38
                          inverted_register_operand: 7 }
             opcode: 0x0f38f2
             modrm { register_operand: 6 rm_operand: 4
                     address_displacement: 305419896 }
             sib { scale: 2 index: 1 base: 5 })"
          // There are no variations of the instruction with vex.not_b
          // set to zero, because the instruction has vex.map_select == 0F38,
          // and this already forces the three-byte VEX prefix.
      });
}

TEST(GenerateEncodingExamplesTest, TwoByteVex) {
  TestGenerateEncodingExamples(
      R"(vendor_syntax {
           mnemonic: 'VADDPD' operands { }
           operands { name: 'ymm1' addressing_mode: DIRECT_ADDRESSING
                      encoding: MODRM_REG_ENCODING
                      value_size_bits: 256 }
           operands { name: 'ymm2' addressing_mode: DIRECT_ADDRESSING
                      encoding: VEX_V_ENCODING
                      value_size_bits: 256 }
           operands { name: 'ymm3/m256'
                      addressing_mode: ANY_ADDRESSING_WITH_FLEXIBLE_REGISTERS
                      encoding: MODRM_RM_ENCODING
                      value_size_bits: 256 }}
         feature_name: 'AVX'
         encoding_scheme: 'RVM'
         raw_encoding_specification: 'VEX.NDS.256.66.0F.WIG 58 /r')",
      {R"(vex_prefix {
            mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
            not_b: true not_r: true not_x: true
            map_select: MAP_SELECT_0F inverted_register_operand: 7
            use_256_bit_vector_length: true }
          opcode: 3928
          modrm { addressing_mode: DIRECT register_operand: 6 rm_operand: 3 })",
       R"(vex_prefix {
            mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
            not_b: true not_r: true not_x: true
            inverted_register_operand: 7 map_select: MAP_SELECT_0F
            use_256_bit_vector_length: true }
          opcode: 3928
          modrm { addressing_mode: INDIRECT_WITH_8_BIT_DISPLACEMENT
                  register_operand: 6 rm_operand: 3 address_displacement: 127 })",
       R"(vex_prefix {
            mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
            not_b: true not_r: true not_x: true
            inverted_register_operand: 7 map_select: MAP_SELECT_0F
            use_256_bit_vector_length: true }
          opcode: 3928
          modrm { addressing_mode: INDIRECT_WITH_8_BIT_DISPLACEMENT
                  register_operand: 6 rm_operand: 4 address_displacement: 127 }
          sib { scale: 2 index: 1 base: 4 })",
       R"(vex_prefix {
            mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
            not_b: true not_r: true not_x: true
            inverted_register_operand: 7 map_select: MAP_SELECT_0F
            use_256_bit_vector_length: true }
          opcode: 3928
          modrm { addressing_mode: INDIRECT_WITH_32_BIT_DISPLACEMENT
                  register_operand: 6 rm_operand: 3
                  address_displacement: 305419896 })",
       R"(vex_prefix {
            mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
            not_b: true not_r: true not_x: true
            inverted_register_operand: 7 map_select: MAP_SELECT_0F
            use_256_bit_vector_length: true }
          opcode: 3928
          modrm { addressing_mode: INDIRECT_WITH_32_BIT_DISPLACEMENT
                  register_operand: 6 rm_operand: 4
                  address_displacement: 305419896 }
          sib { scale: 2 index: 1 base: 4 })",
       R"(vex_prefix {
            mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
            not_b: true not_r: true not_x: true
            inverted_register_operand: 7 map_select: MAP_SELECT_0F
            use_256_bit_vector_length: true }
          opcode: 3928
          modrm { register_operand: 6 rm_operand: 3 })",
       R"(vex_prefix {
            mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
            not_b: true not_r: true not_x: true
            inverted_register_operand: 7 map_select: MAP_SELECT_0F
            use_256_bit_vector_length: true }
          opcode: 3928
          modrm { register_operand: 6 rm_operand: 5
                  address_displacement: 305419896 })",
       R"(vex_prefix {
            mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
            not_b: true not_r: true not_x: true
            inverted_register_operand: 7 map_select: MAP_SELECT_0F
            use_256_bit_vector_length: true }
          opcode: 3928
          modrm { register_operand: 6 rm_operand: 4 }
          sib { scale: 2 index: 1 base: 4 })",
       R"(vex_prefix {
            mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
            not_b: true not_r: true not_x: true
            inverted_register_operand: 7 map_select: MAP_SELECT_0F
            use_256_bit_vector_length: true }
          opcode: 3928
          modrm { register_operand: 6 rm_operand: 4
                  address_displacement: 305419896 }
          sib { scale: 2 index: 1 base: 5 })",
       R"(vex_prefix {
            mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
            not_r: true not_x: true
            inverted_register_operand: 7 map_select: MAP_SELECT_0F
            use_256_bit_vector_length: true }
          opcode: 3928
          modrm { addressing_mode: DIRECT register_operand: 6 rm_operand: 3 })",
       R"(vex_prefix {
            mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
            not_r: true not_x: true
            inverted_register_operand: 7 map_select: MAP_SELECT_0F
            use_256_bit_vector_length: true }
          opcode: 3928
          modrm { addressing_mode: INDIRECT_WITH_8_BIT_DISPLACEMENT
                  register_operand: 6 rm_operand: 3
                  address_displacement: 127 })",
       R"(vex_prefix {
            mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
            not_r: true not_x: true
            inverted_register_operand: 7 map_select: MAP_SELECT_0F
            use_256_bit_vector_length: true }
          opcode: 3928
          modrm { addressing_mode: INDIRECT_WITH_8_BIT_DISPLACEMENT
                  register_operand: 6 rm_operand: 4 address_displacement: 127 }
          sib { scale: 2 index: 1 base: 4 })",
       R"(vex_prefix {
            mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
            not_r: true not_x: true
            inverted_register_operand: 7 map_select: MAP_SELECT_0F
            use_256_bit_vector_length: true }
          opcode: 3928
          modrm { addressing_mode: INDIRECT_WITH_32_BIT_DISPLACEMENT
                  register_operand: 6 rm_operand: 3
                  address_displacement: 305419896 })",
       R"(vex_prefix {
            mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
            not_r: true not_x: true
            inverted_register_operand: 7 map_select: MAP_SELECT_0F
            use_256_bit_vector_length: true }
          opcode: 3928
          modrm { addressing_mode: INDIRECT_WITH_32_BIT_DISPLACEMENT
                  register_operand: 6 rm_operand: 4
                  address_displacement: 305419896 }
          sib { scale: 2 index: 1 base: 4 })",
       R"(vex_prefix {
            mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
            not_r: true not_x: true
            inverted_register_operand: 7 map_select: MAP_SELECT_0F
            use_256_bit_vector_length: true }
          opcode: 3928
          modrm { register_operand: 6 rm_operand: 3 })",
       R"(vex_prefix {
            mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
            not_r: true not_x: true
            inverted_register_operand: 7 map_select: MAP_SELECT_0F
            use_256_bit_vector_length: true }
          opcode: 3928
          modrm { register_operand: 6 rm_operand: 5
                  address_displacement: 305419896 })",
       R"(vex_prefix {
            mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
            not_r: true not_x: true
            inverted_register_operand: 7 map_select: MAP_SELECT_0F
            use_256_bit_vector_length: true }
          opcode: 3928
          modrm { register_operand: 6 rm_operand: 4 }
          sib { scale: 2 index: 1 base: 4 })",
       R"(vex_prefix {
            mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
            not_r: true not_x: true
            inverted_register_operand: 7 map_select: MAP_SELECT_0F
            use_256_bit_vector_length: true }
          opcode: 3928
          modrm { register_operand: 6 rm_operand: 4
                  address_displacement: 305419896 }
          sib { scale: 2 index: 1 base: 5 })"});
}

class PrefixesAndOpcodeMatchSpecificationTest : public ::testing::Test {
 protected:
  // Tests matching 'instruction' with the given encoding specification. Assumes
  // that the specification is in the format used in the Intel SDM.
  void TestMatch(const std::string& specification,
                 const std::string& instruction, bool expected_is_match);
  // Tests matching 'instruction' with the given encoding specification. Assumes
  // that the specification is EncodingSpecification in the text format. This
  // needed to test matches that depends on the interpretation of the EVEX.b
  // prefix, because that can't be parsed from the encoding specification in the
  // Intel SDM format.
  void TestMatchWithSpecificationProto(const std::string& specification,
                                       const std::string& instruction,
                                       bool expected_is_match);
};

void PrefixesAndOpcodeMatchSpecificationTest::TestMatch(
    const std::string& specification, const std::string& instruction,
    bool expected_is_match) {
  SCOPED_TRACE(absl::StrCat("Specification: ", specification,
                            "\nInstruction: ", instruction));
  DecodedInstruction instruction_proto;
  ASSERT_TRUE(::google::protobuf::TextFormat::ParseFromString(
      instruction, &instruction_proto));
  StatusOr<EncodingSpecification> specification_or_status =
      ParseEncodingSpecification(specification);
  ASSERT_OK(specification_or_status.status());

  const bool is_match = PrefixesAndOpcodeMatchSpecification(
      specification_or_status.ValueOrDie(), instruction_proto);
  EXPECT_EQ(is_match, expected_is_match);
}

void PrefixesAndOpcodeMatchSpecificationTest::TestMatchWithSpecificationProto(
    const std::string& specification, const std::string& instruction,
    bool expected_is_match) {
  SCOPED_TRACE(absl::StrCat("Specification:\n", specification,
                            "\nInstruction: ", instruction));
  DecodedInstruction instruction_proto;
  ASSERT_TRUE(::google::protobuf::TextFormat::ParseFromString(
      instruction, &instruction_proto));
  EncodingSpecification specification_proto;
  ASSERT_TRUE(::google::protobuf::TextFormat::ParseFromString(
      specification, &specification_proto));

  const bool is_match = PrefixesAndOpcodeMatchSpecification(specification_proto,
                                                            instruction_proto);
  EXPECT_EQ(is_match, expected_is_match);
}

TEST_F(PrefixesAndOpcodeMatchSpecificationTest, SimpleInstruction) {
  TestMatch("0F A2", "opcode: 0x0fa2", true);
  TestMatch("0F A2", "legacy_prefixes{} opcode: 0x0fa2", true);
  TestMatch("0F A2", "opcode: 0xa2", false);
  TestMatch("0F A2",
            "legacy_prefixes { operand_size_override: OPERAND_SIZE_OVERRIDE }",
            false);
  TestMatch("0F A2", "vex_prefix {} opcode: 0x0fa2", false);
}

TEST_F(PrefixesAndOpcodeMatchSpecificationTest, RexPrefix) {
  TestMatch("REX.W + 35 id", "legacy_prefixes { rex { w: true } } opcode: 0x35",
            true);
  TestMatch("REX.W + 35 id", "legacy_prefixes {} opcode: 0x35", false);
}

TEST_F(PrefixesAndOpcodeMatchSpecificationTest, InstructionWithOperands) {
  TestMatch("11 /r", "opcode: 0x11", true);
  TestMatch("11 /r", "opcode: 0x11 modrm { rm_operand: 3 }", true);
  TestMatch("11 /r",
            R"(legacy_prefixes { operand_size_override: OPERAND_SIZE_OVERRIDE }
               opcode: 0x11 modrm { rm_operand: 3 })",
            false);
}

TEST_F(PrefixesAndOpcodeMatchSpecificationTest,
       InstructionWithOperandsEncodedInOpcode) {
  TestMatch("B8+ rd id", R"(opcode: 0xB9 immediate_value: "xV4\022")", true);

  // Returns false when there is no operand encoded in the opcode.
  TestMatch("10 /r", "opcode: 0x11", false);
}

TEST_F(PrefixesAndOpcodeMatchSpecificationTest, OperandSizeOverride) {
  TestMatch("66 11 /r", "opcode: 0x11", false);
  TestMatch("66 11 /r",
            R"(legacy_prefixes { operand_size_override: OPERAND_SIZE_OVERRIDE }
               opcode: 0x11)",
            true);
}

TEST_F(PrefixesAndOpcodeMatchSpecificationTest, AddressSizeOverride) {
  TestMatch("67 11 /r", "opcode: 0x11", false);
  TestMatch("67 11 /r",
            R"(legacy_prefixes { address_size_override: ADDRESS_SIZE_OVERRIDE }
               opcode: 0x11)",
            true);
}

TEST_F(PrefixesAndOpcodeMatchSpecificationTest, RepRepnPrefix) {
  // 0xF2 is REPNE prefix
  TestMatch("F2 11 /r", "opcode: 0x11", false);
  TestMatch("F2 11 /r",
            R"(legacy_prefixes { lock_or_rep: REP_PREFIX }
               opcode: 0x11)",
            false);
  TestMatch("F2 11 /r",
            R"(legacy_prefixes { lock_or_rep: REPNE_PREFIX }
               opcode: 0x11)",
            true);

  // 0xF3 is REPE prefix
  TestMatch("F3 11 /r", "opcode: 0x11", false);
  TestMatch("F3 11 /r",
            R"(legacy_prefixes { lock_or_rep: REP_PREFIX }
               opcode: 0x11)",
            true);
  TestMatch("F3 11 /r",
            R"(legacy_prefixes { lock_or_rep: REPNE_PREFIX }
               opcode: 0x11)",
            false);
}

TEST_F(PrefixesAndOpcodeMatchSpecificationTest, VexPrefix) {
  TestMatch(
      "VEX.NDS.LZ.0F38.W1 F2 /r",
      "vex_prefix { map_select: MAP_SELECT_0F38 w: true } opcode: 0x0f38f2",
      true);
  TestMatch("VEX.NDS.LZ.0F38.W1 F2 /r",
            "vex_prefix { map_select: MAP_SELECT_0F w: true } opcode: 0x0f38f2",
            false);
  TestMatch("VEX.NDS.LZ.0F38.W1 F2 /r",
            "vex_prefix { map_select: MAP_SELECT_0F38 } opcode: 0x0f38f2 ",
            false);
  TestMatch("VEX.NDS.LZ.0F38.W1 F2 /r",
            "legacy_prefixes { rex { w: true }} opcode: 0x0f38f2 ", false);
}

TEST_F(PrefixesAndOpcodeMatchSpecificationTest, VexPrefix128Bit) {
  TestMatch("VEX.128.66.0F3A.WIG DF /r ib",
            R"(vex_prefix {
                 mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
                 map_select: MAP_SELECT_0F3A
                 w: true }
               opcode: 0x0f3adf)",
            true);
  TestMatch("VEX.128.66.0F3A.WIG DF /r ib",
            R"(vex_prefix {
                 mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
                 map_select: MAP_SELECT_0F3A }
               opcode: 0x0f3adf)",
            true);
  TestMatch(
      "VEX.128.66.0F3A.WIG DF /r ib",
      "vex_prefix { map_select: MAP_SELECT_0F3A w: true } opcode: 0x0f3adf ",
      false);
  TestMatch("VEX.128.66.0F3A.WIG DF /r ib",
            R"(vex_prefix {
                 map_select: MAP_SELECT_0F3A
                 mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
                 w: true
                 use_256_bit_vector_length: true }
               opcode: 0x0f3adf)",
            false);
}

TEST_F(PrefixesAndOpcodeMatchSpecificationTest, VexPrefix256Bit) {
  TestMatch("VEX.256.66.0F38.W0 19 /r",
            R"(vex_prefix {
                 mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
                 map_select: MAP_SELECT_0F38
                 use_256_bit_vector_length: true }
               opcode: 0x0f3819)",
            true);
  TestMatch("VEX.256.66.0F38.W0 19 /r",
            R"(vex_prefix {
                 mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
                 map_select: MAP_SELECT_0F38 }
               opcode: 0x0f3819)",
            false);
}

TEST_F(PrefixesAndOpcodeMatchSpecificationTest, EvexPrefix) {
  TestMatch("EVEX.NDS.128.66.0F.W1 58 /r",
            R"(evex_prefix {
                 mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
                 map_select: MAP_SELECT_0F
                 w: true }
               opcode: 0x0f58)",
            true);
  TestMatch("EVEX.NDS.128.66.0F.W1 58 /r",
            R"(evex_prefix {
                 mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
                 map_select: MAP_SELECT_0F
                 w: false }
               opcode: 0x0f58)",
            false);
  TestMatch("EVEX.NDS.128.66.0F.W1 58 /r",
            R"(evex_prefix {
                 mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
                 map_select: MAP_SELECT_0F
                 vector_length_or_rounding: 1
                 w: true }
               opcode: 0x0f58)",
            false);
  TestMatch("EVEX.NDS.128.66.0F.W1 58 /r",
            R"(vex_prefix {
                 mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
                 map_select: MAP_SELECT_0F
                 use_256_bit_vector_length: false
                 w: true }
               opcode: 0x0f58)",
            false);
  TestMatch("EVEX.NDS.128.66.0F.W1 58 /r",
            R"(evex_prefix {
                 mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
                 map_select: MAP_SELECT_0F38
                 w: true }
               opcode: 0x0f58)",
            false);
  TestMatch("EVEX.NDS.128.66.0F.W1 58 /r",
            R"(evex_prefix {
                 mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
                 map_select: MAP_SELECT_0F
                 w: true }
               opcode: 0x0f68)",
            false);

  // Check that an instruction using the EVEX prefix does not match one of the
  // older encoding schemes.
  TestMatch("0F 58 /r",
            R"(evex_prefix {
                 mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
                 map_select: MAP_SELECT_0F
                 w: true }
               opcode: 0x0f58)",
            false);
  TestMatch("VEX.NDS.128.66.0F.WIG FE /r",
            R"(evex_prefix {
                 mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
                 map_select: MAP_SELECT_0F
                 w: true
                 vector_length_or_rounding: 0 }
               opcode: 0x0f58)",
            false);
}

TEST_F(PrefixesAndOpcodeMatchSpecificationTest, EvexPrefix256Bit) {
  TestMatch("EVEX.NDS.256.0F.W0 58 /r",
            R"(evex_prefix {
                 mandatory_prefix: NO_MANDATORY_PREFIX
                 map_select: MAP_SELECT_0F
                 w: false
                 vector_length_or_rounding: 1 }
               opcode: 0x0f58)",
            true);
  TestMatch("EVEX.NDS.256.0F.W0 58 /r",
            R"(evex_prefix {
                 mandatory_prefix: NO_MANDATORY_PREFIX
                 map_select: MAP_SELECT_0F
                 w: true
                 vector_length_or_rounding: 1 }
               opcode: 0x0f58)",
            false);
  TestMatch("EVEX.NDS.256.0F.W0 58 /r",
            R"(evex_prefix {
                 mandatory_prefix: NO_MANDATORY_PREFIX
                 map_select: MAP_SELECT_0F
                 w: false
                 vector_length_or_rounding: 2 }
               opcode: 0x0f58)",
            false);
}

TEST_F(PrefixesAndOpcodeMatchSpecificationTest, EvexPrefix512Bit) {
  TestMatch("EVEX.NDS.512.66.0F.W1 58 /r",
            R"(evex_prefix {
                 mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
                 map_select: MAP_SELECT_0F
                 w: true
                 vector_length_or_rounding: 2 }
               opcode: 0x0f58)",
            true);
  TestMatch("EVEX.NDS.512.66.0F.W1 58 /r",
            R"(evex_prefix {
                 mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
                 map_select: MAP_SELECT_0F
                 w: true
                 vector_length_or_rounding: 3 }
               opcode: 0x0f58)",
            false);
}

// Tests matching on instructions where the EVEX.b bit overrides the vector
// length; checks that the encoded instruction matches the 512-bit specification
// only when the EVEX.b bit is set to one.
TEST_F(PrefixesAndOpcodeMatchSpecificationTest, EvexBStaticRounding) {
  constexpr char kSpecification[] = R"(
      opcode: 0x0f7a
      modrm_usage: FULL_MODRM
      vex_prefix {
        prefix_type: EVEX_PREFIX
        vector_size: VEX_VECTOR_SIZE_512_BIT
        mandatory_prefix: MANDATORY_PREFIX_REPNE
        map_select: MAP_SELECT_0F
        vex_w_usage: VEX_W_IS_ONE
        evex_b_interpretations: EVEX_B_ENABLES_64_BIT_BROADCAST
        evex_b_interpretations: EVEX_B_ENABLES_STATIC_ROUNDING_CONTROL})";
  constexpr char kInstructionWithStaticRounding[] = R"(
      evex_prefix {
        mandatory_prefix: MANDATORY_PREFIX_REPNE
        map_select: MAP_SELECT_0F
        w: true
        broadcast_or_control: true
        vector_length_or_rounding: 1 }
      opcode: 0x0f7a)";
  constexpr char kInstructionWithoutStaticRounding[] = R"(
      evex_prefix {
        mandatory_prefix: MANDATORY_PREFIX_REPNE
        map_select: MAP_SELECT_0F
        w: true
        vector_length_or_rounding: 1 }
      opcode: 0x0f7a)";
  TestMatchWithSpecificationProto(kSpecification,
                                  kInstructionWithStaticRounding, true);
  TestMatchWithSpecificationProto(kSpecification,
                                  kInstructionWithoutStaticRounding, false);
}

TEST_F(PrefixesAndOpcodeMatchSpecificationTest, EvexBSuppressAllExceptions) {
  constexpr char kSpecification[] = R"(
      opcode: 0x0fc2
      modrm_usage: FULL_MODRM
      vex_prefix {
        prefix_type: EVEX_PREFIX
        vex_operand_usage: VEX_OPERAND_IS_FIRST_SOURCE_REGISTER
        vector_size: VEX_VECTOR_SIZE_512_BIT
        mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
        map_select: MAP_SELECT_0F
        vex_w_usage: VEX_W_IS_ONE
        evex_b_interpretations: EVEX_B_ENABLES_64_BIT_BROADCAST
        evex_b_interpretations: EVEX_B_ENABLES_SUPPRESS_ALL_EXCEPTIONS })";
  constexpr char kInstructionWithSae[] = R"(
      evex_prefix {
        mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
        map_select: MAP_SELECT_0F
        w: true
        broadcast_or_control: true
        vector_length_or_rounding: 2 }
      opcode: 0x0fc2)";
  constexpr char kInstructionWithoutSae[] = R"(
      evex_prefix {
        mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
        map_select: MAP_SELECT_0F
        w: true
        vector_length_or_rounding: 2 }
      opcode: 0x0fc2)";
  constexpr char kInstructionWithSaeAndDifferentVectorLength[] = R"(
      evex_prefix {
        mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
        map_select: MAP_SELECT_0F
        w: true
        broadcast_or_control: true
        vector_length_or_rounding: 1 }
      opcode: 0x0fc2)";
  TestMatchWithSpecificationProto(kSpecification, kInstructionWithSae, true);
  TestMatchWithSpecificationProto(kSpecification, kInstructionWithoutSae, true);
  TestMatchWithSpecificationProto(
      kSpecification, kInstructionWithSaeAndDifferentVectorLength, false);
}

TEST_F(PrefixesAndOpcodeMatchSpecificationTest, EvexBVectorLengthIsIgnored) {
  constexpr char kSpecification[] = R"(
      opcode: 0x0f11
      modrm_usage: FULL_MODRM
      vex_prefix { prefix_type: EVEX_PREFIX
                   vex_operand_usage: VEX_OPERAND_IS_FIRST_SOURCE_REGISTER
                   mandatory_prefix: MANDATORY_PREFIX_REPNE
                   map_select: MAP_SELECT_0F
                   vex_w_usage: VEX_W_IS_ONE
                   opmask_usage: EVEX_OPMASK_IS_OPTIONAL
                   masking_operation: EVEX_MASKING_MERGING_AND_ZEROING })";
  constexpr const char* const kInstructions[] = {
      R"(evex_prefix { mandatory_prefix: MANDATORY_PREFIX_REPNE
                    map_select: MAP_SELECT_0F w: true }
         opcode: 0x0f11)",
      R"(evex_prefix { mandatory_prefix: MANDATORY_PREFIX_REPNE
                       map_select: MAP_SELECT_0F w: true
                       vector_length_or_rounding: 1 }
         opcode: 0x0f11)",
      R"(evex_prefix { mandatory_prefix: MANDATORY_PREFIX_REPNE
                       map_select: MAP_SELECT_0F w: true
                       vector_length_or_rounding: 2 }
         opcode: 0x0f11)",
      R"(evex_prefix { mandatory_prefix: MANDATORY_PREFIX_REPNE
                       map_select: MAP_SELECT_0F w: true
                       vector_length_or_rounding: 3 }
         opcode: 0x0f11)"};
  for (const char* instruction : kInstructions) {
    TestMatchWithSpecificationProto(kSpecification, instruction, true);
  }
}

class GetRegisterIndexTest : public ::testing::Test {
 protected:
  struct GetRegisterIndexTextInput {
    const char* register_name;
    RegisterIndex expected_register_index;
  };

  void TestGetRegisterIndex(
      const std::initializer_list<GetRegisterIndexTextInput>& test_inputs);
};

void GetRegisterIndexTest::TestGetRegisterIndex(
    const std::initializer_list<GetRegisterIndexTextInput>& test_inputs) {
  for (const auto& test_input : test_inputs) {
    SCOPED_TRACE(absl::StrCat("register_name = ", test_input.register_name));
    const RegisterIndex register_index =
        GetRegisterIndex(test_input.register_name);
    EXPECT_EQ(register_index, test_input.expected_register_index);
  }
}

TEST_F(GetRegisterIndexTest, GetNamedRegisters) {
  TestGetRegisterIndex({{"al", RegisterIndex(0)},
                        {"es", RegisterIndex(0)},
                        {"RAX", RegisterIndex(0)},
                        {"esi", RegisterIndex(6)},
                        {"cr0", RegisterIndex(0)},
                        {"cr1", kInvalidRegisterIndex},
                        {"dr0", RegisterIndex(0)},
                        {"cr8", RegisterIndex(8)},
                        {"dr8", kInvalidRegisterIndex},
                        {"foo", kInvalidRegisterIndex}});
}

TEST_F(GetRegisterIndexTest, GetNumberedRegisters) {
  TestGetRegisterIndex({{"xmm0", RegisterIndex(0)},
                        {"st1", RegisterIndex(1)},
                        {"ymm14", RegisterIndex(14)},
                        {"zmm6", RegisterIndex(6)},
                        {"st7", RegisterIndex(7)},
                        {"st9", kInvalidRegisterIndex},
                        {"r14", RegisterIndex(14)},
                        {"zmm30", RegisterIndex(30)},
                        {"zmm32", kInvalidRegisterIndex},
                        {"ymm17", kInvalidRegisterIndex},
                        {"xmm16", kInvalidRegisterIndex}});
}

class SetOperandTestBase : public ::testing::Test {
 protected:
  // A legacy instruction used in the tests (ADC r/m32, r32).
  static constexpr char kLegacyInstructionEncodingSpecification[] = "11 /r";
  static constexpr char kLegacyInstructionFormat[] = R"(
      mnemonic: 'ADC'
        operands { name: 'r/m32'
                   addressing_mode: ANY_ADDRESSING_WITH_FLEXIBLE_REGISTERS
                   encoding: MODRM_RM_ENCODING value_size_bits: 32 }
        operands { name: 'r32' addressing_mode: DIRECT_ADDRESSING
                   encoding: MODRM_REG_ENCODING value_size_bits: 32 })";
  static constexpr int kLegacyInstructionRegOperand = 1;
  static constexpr int kLegacyInstructionRmOperand = 0;

  // A VEX instruction used in the tests (VPBLENDVB xmm1, xmm2, xmm3/m128, xmm4
  static constexpr char kVexInstructionEncodingSpecification[] =
      "VEX.NDS.128.66.0F3A.W0 4C /r /is4";
  static constexpr char kVexInstructionFormat[] = R"(
      mnemonic: 'VPBLENDVB'
        operands { name: 'xmm1' addressing_mode: DIRECT_ADDRESSING
                   encoding: MODRM_REG_ENCODING value_size_bits: 128 }
        operands { name: 'xmm2' addressing_mode: DIRECT_ADDRESSING
                   encoding: VEX_V_ENCODING value_size_bits: 128 }
        operands { name: 'xmm3/m128'
                   addressing_mode: ANY_ADDRESSING_WITH_FLEXIBLE_REGISTERS
                   encoding: MODRM_RM_ENCODING value_size_bits: 128 }
        operands { name: 'xmm4' addressing_mode: DIRECT_ADDRESSING
                   encoding: VEX_SUFFIX_ENCODING value_size_bits: 128 })";
  static constexpr int kVexInstructionRegOperand = 0;
  static constexpr int kVexInstructionRmOperand = 2;
  static constexpr int kVexInstructionVexVOperand = 1;
  static constexpr int kVexInstructionVexSuffixOperand = 3;

  // An instruction that does not use the ModR/M byte, and thus can't accept a
  // memory operand.
  static constexpr char kLegacyInstructionNoMemoryOperandFormat[] = R"(
      mnemonic: 'BSWAP'
      operands { name: 'r32' addressing_mode: DIRECT_ADDRESSING
                 encoding: OPCODE_ENCODING value_size_bits: 32 })";
};

class SetOperandToRegisterTest : public SetOperandTestBase {
 protected:
  void TestSetOperand(const std::string& instruction_format_proto,
                      const std::string& encoding_specification,
                      const std::string& instruction_proto,
                      int operand_position, RegisterIndex register_index,
                      const std::string& expected_instruction_proto,
                      const std::string& expected_disassembly);

  void TestSetOperandError(const std::string& instruction_format_proto,
                           int operand_position, RegisterIndex register_index);
};

constexpr char SetOperandTestBase::kLegacyInstructionEncodingSpecification[];
constexpr char SetOperandTestBase::kLegacyInstructionFormat[];
constexpr int SetOperandTestBase::kLegacyInstructionRegOperand;
constexpr int SetOperandTestBase::kLegacyInstructionRmOperand;

constexpr char SetOperandTestBase::kVexInstructionEncodingSpecification[];
constexpr char SetOperandTestBase::kVexInstructionFormat[];
constexpr int SetOperandTestBase::kVexInstructionRegOperand;
constexpr int SetOperandTestBase::kVexInstructionRmOperand;
constexpr int SetOperandTestBase::kVexInstructionVexVOperand;
constexpr int SetOperandTestBase::kVexInstructionVexSuffixOperand;

constexpr char SetOperandTestBase::kLegacyInstructionNoMemoryOperandFormat[];

void SetOperandToRegisterTest::TestSetOperand(
    const std::string& instruction_format_proto,
    const std::string& encoding_specification,
    const std::string& instruction_proto, int operand_position,
    RegisterIndex register_index, const std::string& expected_instruction_proto,
    const std::string& expected_disassembly) {
  InstructionFormat instruction_format;
  DecodedInstruction instruction;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      instruction_format_proto, &instruction_format));
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(instruction_proto,
                                                            &instruction));
  EXPECT_OK(SetOperandToRegister(instruction_format, operand_position,
                                 register_index, &instruction));
  EXPECT_THAT(instruction, EqualsProto(expected_instruction_proto));
  EXPECT_THAT(instruction,
              DisassemblesTo(encoding_specification, expected_disassembly));
}

void SetOperandToRegisterTest::TestSetOperandError(
    const std::string& instruction_format_proto, int operand_position,
    RegisterIndex register_index) {
  InstructionFormat instruction_format;
  DecodedInstruction instruction;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      instruction_format_proto, &instruction_format));
  const Status set_operand_status = SetOperandToRegister(
      instruction_format, operand_position, register_index, &instruction);
  EXPECT_THAT(set_operand_status, StatusIs(INVALID_ARGUMENT));
}

TEST_F(SetOperandToRegisterTest, InvalidOperandPosition) {
  TestSetOperandError(kLegacyInstructionFormat, -1, RegisterIndex(3));
  TestSetOperandError(kLegacyInstructionFormat, 3, RegisterIndex(3));
}

TEST_F(SetOperandToRegisterTest, InvalidRegisterIndex) {
  TestSetOperandError(kLegacyInstructionFormat, kLegacyInstructionRegOperand,
                      kInvalidRegisterIndex);
  TestSetOperandError(kLegacyInstructionFormat, kLegacyInstructionRegOperand,
                      RegisterIndex(12345));
}

TEST_F(SetOperandToRegisterTest, OperandInOpcode) {
  // Test that the function sets the operand correctly if possible.
  TestSetOperand(
      R"(mnemonic: 'BSWAP'
         operands { name: 'r32' addressing_mode: DIRECT_ADDRESSING
                    encoding: OPCODE_ENCODING value_size_bits: 32 })",
      "0F C8+rd", "opcode: 0x0fc8", 0, RegisterIndex(3), "opcode: 0x0fcb",
      "BSWAP EBX");
  // Test that the function returns an error for non-legacy operands.
  TestSetOperandError(
      R"(mnemonic: 'BSWAP'
         operands { name: 'r32' addressing_mode: DIRECT_ADDRESSING
                    encoding: OPCODE_ENCODING value_size_bits: 32 })",
      0, RegisterIndex(13));
}

TEST_F(SetOperandToRegisterTest, OperandInModRmRegLegacy) {
  TestSetOperand(
      kLegacyInstructionFormat, kLegacyInstructionEncodingSpecification,
      "opcode: 0x11", kLegacyInstructionRegOperand, RegisterIndex(3),
      "legacy_prefixes { rex {}} opcode: 0x11 modrm { register_operand: 3 }",
      "ADC DWORD PTR [RAX], EBX");
  TestSetOperand(kLegacyInstructionFormat,
                 kLegacyInstructionEncodingSpecification, "opcode: 0x11",
                 kLegacyInstructionRegOperand, RegisterIndex(12),
                 R"(legacy_prefixes { rex { r: true }}
                    opcode: 0x11
                    modrm { register_operand: 4 })",
                 "ADC DWORD PTR [RAX], R12D");
}

TEST_F(SetOperandToRegisterTest, OperandInModRmRegVex) {
  TestSetOperand(kVexInstructionFormat, kVexInstructionEncodingSpecification,
                 R"(vex_prefix {
                      map_select: MAP_SELECT_0F3A
                      mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE }
                    opcode: 0x0f3a4c)",
                 kVexInstructionRegOperand, RegisterIndex(5),
                 R"(vex_prefix {
                      not_r: true map_select: MAP_SELECT_0F3A
                      mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE }
                    opcode: 0x0f3a4c
                    modrm { register_operand: 5 })",
                 "VPBLENDVB XMM5, XMM15, XMMWORD PTR [R8], XMM0");
  TestSetOperand(kVexInstructionFormat, kVexInstructionEncodingSpecification,
                 R"(vex_prefix {
                      map_select: MAP_SELECT_0F3A
                      mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE }
                    opcode: 0x0f3a4c)",
                 kVexInstructionRegOperand, RegisterIndex(15),
                 R"(vex_prefix {
                      not_r: false map_select: MAP_SELECT_0F3A
                      mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE }
                    opcode: 0x0f3a4c
                    modrm { register_operand: 7 })",
                 "VPBLENDVB XMM15, XMM15, XMMWORD PTR [R8], XMM0");
}

TEST_F(SetOperandToRegisterTest, OperandInModRmRmLegacy) {
  TestSetOperand(kLegacyInstructionFormat,
                 kLegacyInstructionEncodingSpecification, "opcode: 0x11",
                 kLegacyInstructionRmOperand, RegisterIndex(3),
                 R"(legacy_prefixes { rex {}}
                    opcode: 0x11
                    modrm { addressing_mode: DIRECT rm_operand: 3 })",
                 "ADC EBX, EAX");
  TestSetOperand(kLegacyInstructionFormat,
                 kLegacyInstructionEncodingSpecification, "opcode: 0x11",
                 kLegacyInstructionRmOperand, RegisterIndex(10),
                 R"(legacy_prefixes { rex { b: true }}
                    opcode: 0x11
                    modrm { addressing_mode: DIRECT rm_operand: 2 })",
                 "ADC R10D, EAX");
  TestSetOperand(kLegacyInstructionFormat,
                 kLegacyInstructionEncodingSpecification,
                 "legacy_prefixes { rex { w: true }} opcode: 0x11",
                 kLegacyInstructionRmOperand, RegisterIndex(10),
                 R"(legacy_prefixes { rex { b: true w: true }}
                    opcode: 0x11
                    modrm { addressing_mode: DIRECT rm_operand: 2 })",
                 "ADC R10, RAX");
  TestSetOperand(kLegacyInstructionFormat,
                 kLegacyInstructionEncodingSpecification,
                 R"(legacy_prefixes { rex { w: true }} opcode: 0x11
                    sib { base: 3 })",
                 kLegacyInstructionRmOperand, RegisterIndex(10),
                 R"(legacy_prefixes { rex { b: true w: true }}
                    opcode: 0x11
                    modrm { addressing_mode: DIRECT rm_operand: 2 })",
                 "ADC R10, RAX");
}

TEST_F(SetOperandToRegisterTest, OperandInModRmRmVex) {
  TestSetOperand(kVexInstructionFormat, kVexInstructionEncodingSpecification,
                 R"(vex_prefix {
                      map_select: MAP_SELECT_0F3A
                      mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE }
                    opcode: 0x0f3a4c)",
                 kVexInstructionRmOperand, RegisterIndex(1),
                 R"(vex_prefix {
                      map_select: MAP_SELECT_0F3A
                      mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
                      not_b: true }
                    opcode: 0x0f3a4c
                    modrm { addressing_mode: DIRECT rm_operand: 1 })",
                 "VPBLENDVB XMM8, XMM15, XMM1, XMM0");
  TestSetOperand(kVexInstructionFormat, kVexInstructionEncodingSpecification,
                 R"(vex_prefix {
                      map_select: MAP_SELECT_0F3A
                      mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE }
                    opcode: 0x0f3a4c)",
                 kVexInstructionRmOperand, RegisterIndex(11),
                 R"(vex_prefix {
                      map_select: MAP_SELECT_0F3A
                      mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
                      not_b: false }
                    opcode: 0x0f3a4c
                    modrm { addressing_mode: DIRECT rm_operand: 3 })",
                 "VPBLENDVB XMM8, XMM15, XMM11, XMM0");
}

TEST_F(SetOperandToRegisterTest, OperandInVexV) {
  TestSetOperand(kVexInstructionFormat, kVexInstructionEncodingSpecification,
                 R"(vex_prefix {
                      map_select: MAP_SELECT_0F3A
                      mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE }
                    opcode: 0x0f3a4c modrm { addressing_mode: DIRECT })",
                 kVexInstructionVexVOperand, RegisterIndex(12),
                 R"(vex_prefix {
                      map_select: MAP_SELECT_0F3A
                      mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
                      inverted_register_operand: 3 }
                    opcode: 0x0f3a4c modrm { addressing_mode: DIRECT })",
                 "VPBLENDVB XMM8, XMM12, XMM8, XMM0");
}

TEST_F(SetOperandToRegisterTest, OperandInVexSuffix) {
  TestSetOperand(kVexInstructionFormat, kVexInstructionEncodingSpecification,
                 R"(vex_prefix {
                      map_select: MAP_SELECT_0F3A
                      mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE }
                    opcode: 0x0f3a4c modrm { addressing_mode: DIRECT })",
                 kVexInstructionVexSuffixOperand, RegisterIndex(7),
                 R"(vex_prefix {
                      map_select: MAP_SELECT_0F3A
                      mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
                      vex_suffix_value: 0x70 }
                    opcode: 0x0f3a4c modrm { addressing_mode: DIRECT })",
                 "VPBLENDVB XMM8, XMM15, XMM8, XMM7");
}

class SetOperandToMemoryAbsoluteTest : public SetOperandTestBase {
 protected:
  void TestSetOperand(const std::string& instruction_format_proto,
                      const std::string& encoding_specification,
                      const std::string& instruction_proto,
                      uint32_t absolute_address,
                      const std::string& expected_instruction_proto,
                      const std::string& expected_disassembly);
  void TestSetOperandError(const std::string& instruction_format_proto);
};

void SetOperandToMemoryAbsoluteTest::TestSetOperand(
    const std::string& instruction_format_proto,
    const std::string& encoding_specification,
    const std::string& instruction_proto, uint32_t absolute_address,
    const std::string& expected_instruction_proto,
    const std::string& expected_disassembly) {
  InstructionFormat instruction_format;
  DecodedInstruction instruction;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      instruction_format_proto, &instruction_format));
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(instruction_proto,
                                                            &instruction));
  EXPECT_OK(SetOperandToMemoryAbsolute(instruction_format, absolute_address,
                                       &instruction));
  EXPECT_THAT(instruction, EqualsProto(expected_instruction_proto));
  EXPECT_THAT(instruction,
              DisassemblesTo(encoding_specification, expected_disassembly));
}

void SetOperandToMemoryAbsoluteTest::TestSetOperandError(
    const std::string& instruction_format_proto) {
  // The only way SetOperandToMemoryAbsolute can fail is if the instruction
  // does not have any memory operands.
  InstructionFormat instruction_format;
  DecodedInstruction instruction;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      instruction_format_proto, &instruction_format));
  const Status set_operand_status =
      SetOperandToMemoryAbsolute(instruction_format, 0x123456, &instruction);
  EXPECT_THAT(set_operand_status, StatusIs(INVALID_ARGUMENT));
}

TEST_F(SetOperandToMemoryAbsoluteTest, LegacyInstruction) {
  TestSetOperand(kLegacyInstructionFormat,
                 kLegacyInstructionEncodingSpecification, "", 0x12345678,
                 R"(legacy_prefixes { rex {}}
                    modrm { addressing_mode: INDIRECT rm_operand: 4
                            address_displacement: 0x12345678 }
                    sib { index: 4 base: 5 })",
                 "ADC DWORD PTR [0x12345678], EAX");
}

TEST_F(SetOperandToMemoryAbsoluteTest, NoMemoryOperand) {
  TestSetOperandError(
      R"(mnemonic: 'BSWAP'
         operands { name: 'r32' addressing_mode: DIRECT_ADDRESSING
                    encoding: OPCODE_ENCODING value_size_bits: 32 })");
}

class SetOperandToMemoryBaseTest : public SetOperandTestBase {
 protected:
  void TestSetOperand(const std::string& instruction_format_proto,
                      const std::string& encoding_specification,
                      const std::string& instruction_proto,
                      RegisterIndex register_index,
                      const std::string& expected_instruction_proto,
                      const std::string& expected_disassembly);

  void TestSetOperandError(const std::string& instruction_format_proto,
                           RegisterIndex register_index);
};

void SetOperandToMemoryBaseTest::TestSetOperand(
    const std::string& instruction_format_proto,
    const std::string& encoding_specification,
    const std::string& instruction_proto, RegisterIndex register_index,
    const std::string& expected_instruction_proto,
    const std::string& expected_disassembly) {
  InstructionFormat instruction_format;
  DecodedInstruction instruction;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      instruction_format_proto, &instruction_format));
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(instruction_proto,
                                                            &instruction));
  EXPECT_OK(
      SetOperandToMemoryBase(instruction_format, register_index, &instruction));
  EXPECT_THAT(instruction, EqualsProto(expected_instruction_proto));
  EXPECT_THAT(instruction,
              DisassemblesTo(encoding_specification, expected_disassembly));
}

void SetOperandToMemoryBaseTest::TestSetOperandError(
    const std::string& instruction_format_proto, RegisterIndex register_index) {
  InstructionFormat instruction_format;
  DecodedInstruction instruction;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      instruction_format_proto, &instruction_format));
  const Status set_operand_status =
      SetOperandToMemoryBase(instruction_format, register_index, &instruction);
  EXPECT_THAT(set_operand_status, StatusIs(INVALID_ARGUMENT));
}

TEST_F(SetOperandToMemoryBaseTest, LegacyInstruction) {
  TestSetOperand(kLegacyInstructionFormat,
                 kLegacyInstructionEncodingSpecification, "opcode: 0x11",
                 RegisterIndex(7),
                 R"(legacy_prefixes { rex {}}
         opcode: 0x11 modrm { addressing_mode: INDIRECT rm_operand: 7 })",
                 "ADC DWORD PTR [RDI], EAX");
  // In addition to setting the operand, this test verifies that the function
  // removes the SIB byte that is not needed with this addressing mode.
  TestSetOperand(kLegacyInstructionFormat,
                 kLegacyInstructionEncodingSpecification,
                 R"(opcode: 0x11
                    modrm { addressing_mode: INDIRECT register_operand: 3
                            rm_operand: 4 }
                    sib { base: 4 index: 4 })",
                 RegisterIndex(7),
                 R"(legacy_prefixes { rex {}}
         opcode: 0x11
         modrm { addressing_mode: INDIRECT register_operand: 3 rm_operand: 7 })",
                 "ADC DWORD PTR [RDI], EBX");
}

TEST_F(SetOperandToMemoryBaseTest, LegacyInstructionWithExtendedBit) {
  TestSetOperand(kLegacyInstructionFormat,
                 kLegacyInstructionEncodingSpecification, "opcode: 0x11",
                 RegisterIndex(15),
                 R"(legacy_prefixes { rex { b: true }}
                    opcode: 0x11 modrm { addressing_mode: INDIRECT
                                         rm_operand: 7 })",
                 "ADC DWORD PTR [R15], EAX");
}

TEST_F(SetOperandToMemoryBaseTest, VexInstruction) {
  TestSetOperand(kVexInstructionFormat, kVexInstructionEncodingSpecification,
                 R"(vex_prefix {
                      mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
                      map_select: MAP_SELECT_0F3A }
                    opcode: 0x0f3a4c)",
                 RegisterIndex(6),
                 R"(vex_prefix {
                      mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
                      map_select: MAP_SELECT_0F3A
                      not_b: true }
                    opcode: 0x0f3a4c
                    modrm { addressing_mode: INDIRECT rm_operand: 6 })",
                 "VPBLENDVB XMM8, XMM15, XMMWORD PTR [RSI], XMM0");
}

TEST_F(SetOperandToMemoryBaseTest, VexInstructionWithExtendedBit) {
  TestSetOperand(kVexInstructionFormat, kVexInstructionEncodingSpecification,
                 R"(vex_prefix {
                      mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
                      map_select: MAP_SELECT_0F3A })",
                 RegisterIndex(11),
                 R"(vex_prefix {
                      mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
                      map_select: MAP_SELECT_0F3A }
                    modrm { addressing_mode: INDIRECT rm_operand: 3 })",
                 "VPBLENDVB XMM8, XMM15, XMMWORD PTR [R11], XMM0");
}

TEST_F(SetOperandToMemoryBaseTest, NoMemoryOperand) {
  // An attempt to encode BSWAP DWORD PTR [RAX + 123]. The operand of BSWAP is
  // encoded in the opcode, so the instruction can accept only register
  // operands.
  TestSetOperandError(
      R"(mnemonic: 'BSWAP'
         operands { name: 'r/m32' addressing_mode: DIRECT_ADDRESSING
                    encoding: OPCODE_ENCODING value_size_bits: 32 })",
      RegisterIndex(0));
}

TEST_F(SetOperandToMemoryBaseTest, UnencodableOperand) {
  // The register indices used in the test can't be encoded using only the
  // ModR/M byte.
  TestSetOperandError(kLegacyInstructionFormat, RegisterIndex(4));
  TestSetOperandError(kLegacyInstructionFormat, RegisterIndex(12));
}

TEST_F(SetOperandToMemoryBaseTest, InvalidOperandIndex) {
  // The register index used in the test is not valid.
  TestSetOperandError(kLegacyInstructionFormat, RegisterIndex(-2));
}

class SetOperandToMemoryBaseSibTest : public SetOperandTestBase {
 protected:
  void TestSetOperand(const std::string& instruction_format_proto,
                      const std::string& encoding_specification,
                      const std::string& instruction_proto,
                      RegisterIndex base_register,
                      const std::string& expected_instruction_proto,
                      const std::string& expected_disassembly);
  void TestSetOperandError(const std::string& instruction_format_proto,
                           RegisterIndex base_register);
};

void SetOperandToMemoryBaseSibTest::TestSetOperand(
    const std::string& instruction_format_proto,
    const std::string& encoding_specification,
    const std::string& instruction_proto, RegisterIndex base_register,
    const std::string& expected_instruction_proto,
    const std::string& expected_disassembly) {
  InstructionFormat instruction_format;
  DecodedInstruction instruction;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      instruction_format_proto, &instruction_format));
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(instruction_proto,
                                                            &instruction));
  EXPECT_OK(SetOperandToMemoryBaseSib(instruction_format, base_register,
                                      &instruction));
  EXPECT_THAT(instruction, EqualsProto(expected_instruction_proto));
  EXPECT_THAT(instruction,
              DisassemblesTo(encoding_specification, expected_disassembly));
}

void SetOperandToMemoryBaseSibTest::TestSetOperandError(
    const std::string& instruction_format_proto, RegisterIndex base_register) {
  InstructionFormat instruction_format;
  DecodedInstruction instruction;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      instruction_format_proto, &instruction_format));
  const Status set_operand_status = SetOperandToMemoryBaseSib(
      instruction_format, base_register, &instruction);
  EXPECT_THAT(set_operand_status, StatusIs(INVALID_ARGUMENT));
}

TEST_F(SetOperandToMemoryBaseSibTest, LegacyInstruction) {
  TestSetOperand(kLegacyInstructionFormat,
                 kLegacyInstructionEncodingSpecification,
                 "modrm { register_operand: 2 }", RegisterIndex(7),
                 R"(legacy_prefixes { rex {}}
                    modrm { addressing_mode: INDIRECT register_operand: 2
                            rm_operand: 4 }
                    sib { base: 7 index: 4 })",
                 "ADC DWORD PTR [RDI], EDX");
  // Tests that it is possible to encode ADC DWORD PTR [RSP], EAX. This
  // instruction is not encodable with just the ModR/M byte, because the
  // register index of RSP is used as an escape value.
  TestSetOperand(kLegacyInstructionFormat,
                 kLegacyInstructionEncodingSpecification,
                 "legacy_prefixes { rex { b: true }} ", RegisterIndex(4),
                 R"(legacy_prefixes { rex {}}
                    modrm { addressing_mode: INDIRECT rm_operand: 4 }
                    sib { base: 4 index: 4 })",
                 "ADC DWORD PTR [RSP], EAX");
  TestSetOperand(kLegacyInstructionFormat,
                 kLegacyInstructionEncodingSpecification, "", RegisterIndex(12),
                 R"(legacy_prefixes { rex { b: true }}
                    modrm { addressing_mode: INDIRECT rm_operand: 4 }
                    sib { base: 4 index: 4 })",
                 "ADC DWORD PTR [R12], EAX");
}

TEST_F(SetOperandToMemoryBaseSibTest, VexInstruction) {
  TestSetOperand(
      kVexInstructionFormat, kVexInstructionEncodingSpecification,
      R"(vex_prefix { mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
                      map_select: MAP_SELECT_0F3A })",
      RegisterIndex(6),
      R"(vex_prefix { mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
                      map_select: MAP_SELECT_0F3A not_b: true not_x: true }
         modrm { addressing_mode: INDIRECT rm_operand: 4 }
         sib { base: 6 index: 4 })",
      "VPBLENDVB XMM8, XMM15, XMMWORD PTR [RSI], XMM0");
  TestSetOperand(
      kVexInstructionFormat, kVexInstructionEncodingSpecification,
      R"(vex_prefix { mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
                      map_select: MAP_SELECT_0F3A })",
      RegisterIndex(14),
      R"(vex_prefix { mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
                      map_select: MAP_SELECT_0F3A not_x: true }
         modrm { addressing_mode: INDIRECT rm_operand: 4 }
         sib { base: 6 index: 4 })",
      "VPBLENDVB XMM8, XMM15, XMMWORD PTR [R14], XMM0");
}

TEST_F(SetOperandToMemoryBaseSibTest, InvalidOperand) {
  // Register indices 5 and 13 serve as esacape values for addressing by an
  // absolute address encoded as an immediate value after the instruction.
  TestSetOperandError(kLegacyInstructionFormat, RegisterIndex(5));
  TestSetOperandError(kLegacyInstructionFormat, RegisterIndex(13));
}

TEST_F(SetOperandToMemoryBaseSibTest, InvalidRegisterIndex) {
  TestSetOperandError(kLegacyInstructionFormat, RegisterIndex(-1));
  TestSetOperandError(kLegacyInstructionFormat, RegisterIndex(16));
}

TEST_F(SetOperandToMemoryBaseSibTest, NoMemoryOperand) {
  // BSWAP encodes the operand in the opcode; there is no way to encode a memory
  // operand.
  TestSetOperandError(
      R"(mnemonic: 'BSWAP'
         operands { name: 'r/m32' addressing_mode: DIRECT_ADDRESSING
                    encoding: OPCODE_ENCODING value_size_bits: 32 })",
      RegisterIndex(0));
}

class SetOperandToMemoryRelativeToRipTest : public SetOperandTestBase {
 protected:
  void TestSetOperand(const std::string& instruction_format_proto,
                      const std::string& encoding_specification,
                      const std::string& instruction_proto,
                      int32_t displacement,
                      const std::string& expected_instruction_proto,
                      const std::string& expected_disassembly);
  void TestSetOperandError(const std::string& instruction_format_proto);
};

void SetOperandToMemoryRelativeToRipTest::TestSetOperand(
    const std::string& instruction_format_proto,
    const std::string& encoding_specification,
    const std::string& instruction_proto, int32_t displacement,
    const std::string& expected_instruction_proto,
    const std::string& expected_disassembly) {
  InstructionFormat instruction_format;
  DecodedInstruction instruction;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      instruction_format_proto, &instruction_format));
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(instruction_proto,
                                                            &instruction));
  EXPECT_OK(SetOperandToMemoryRelativeToRip(instruction_format, displacement,
                                            &instruction));
  EXPECT_THAT(instruction, EqualsProto(expected_instruction_proto));
  EXPECT_THAT(instruction,
              DisassemblesTo(encoding_specification, expected_disassembly));
}

void SetOperandToMemoryRelativeToRipTest::TestSetOperandError(
    const std::string& instruction_format_proto) {
  InstructionFormat instruction_format;
  DecodedInstruction instruction;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      instruction_format_proto, &instruction_format));
  const Status set_operand_status =
      SetOperandToMemoryRelativeToRip(instruction_format, 12345, &instruction);
  EXPECT_THAT(set_operand_status, StatusIs(INVALID_ARGUMENT));
}

TEST_F(SetOperandToMemoryRelativeToRipTest, LegacyInstruction) {
  TestSetOperand(kLegacyInstructionFormat,
                 kLegacyInstructionEncodingSpecification,
                 "modrm { register_operand: 2 }", 0x12345,
                 R"(legacy_prefixes { rex {}}
         modrm { addressing_mode: INDIRECT register_operand: 2 rm_operand: 5
                 address_displacement: 0x12345 })",
                 "ADC DWORD PTR [RIP + 0x12345], EDX");
  TestSetOperand(kLegacyInstructionFormat,
                 kLegacyInstructionEncodingSpecification,
                 "modrm { register_operand: 2 }", -45,
                 R"(legacy_prefixes { rex {}}
         modrm { addressing_mode: INDIRECT register_operand: 2 rm_operand: 5
                 address_displacement: 0xffffffd3 })",
                 "ADC DWORD PTR [RIP - 0x2D], EDX");
}

TEST_F(SetOperandToMemoryRelativeToRipTest, NoMemoryOperand) {
  TestSetOperandError(kLegacyInstructionNoMemoryOperandFormat);
}

// Similar to SetOperandToMemoryBaseAndDisplacement, the testing functions are
// the same for both the 8-bit and 32-bit versions, so they are templatized.
// On the other hand, the actual instructions data used in the tests are
// different for 8 bits and 32 bits, so we keep them separated.
template <typename DisplType>
class SetOperandToMemoryBaseAndDisplacementTest : public SetOperandTestBase {
 protected:
  // We need to create an alias for DisplType inside the class, otherwise the
  // compiler would not match the overrides of
  // SetOperandToMemoryBaseAndDisplacement.
  using DisplacementType = DisplType;

  void TestSetOperand(const std::string& instruction_format_proto,
                      const std::string& encoding_specification,
                      const std::string& instruction_proto,
                      RegisterIndex base_register,
                      DisplacementType displacement,
                      const std::string& expected_instruction_proto,
                      const std::string& expected_disassembly);
  void TestSetOperandError(const std::string& instruction_format_proto,
                           RegisterIndex register_index);

  // Calls the appropriate SetOperandToMemoryBaseAndDisplacement function.
  virtual Status SetOperandToMemoryBaseAndDisplacement(
      const InstructionFormat& instruction_format, RegisterIndex base_register,
      DisplacementType displacement, DecodedInstruction* instruction) = 0;
};

template <typename DisplacementType>
void SetOperandToMemoryBaseAndDisplacementTest<DisplacementType>::
    TestSetOperand(const std::string& instruction_format_proto,
                   const std::string& encoding_specification,
                   const std::string& instruction_proto,
                   RegisterIndex base_register, DisplacementType displacement,
                   const std::string& expected_instruction_proto,
                   const std::string& expected_disassembly) {
  InstructionFormat instruction_format;
  DecodedInstruction instruction;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      instruction_format_proto, &instruction_format));
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(instruction_proto,
                                                            &instruction));
  EXPECT_OK(SetOperandToMemoryBaseAndDisplacement(
      instruction_format, base_register, displacement, &instruction));
  EXPECT_THAT(instruction, EqualsProto(expected_instruction_proto));
  EXPECT_THAT(instruction,
              DisassemblesTo(encoding_specification, expected_disassembly));
}

template <typename DisplacementType>
void SetOperandToMemoryBaseAndDisplacementTest<DisplacementType>::
    TestSetOperandError(const std::string& instruction_format_proto,
                        RegisterIndex register_index) {
  InstructionFormat instruction_format;
  DecodedInstruction instruction;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      instruction_format_proto, &instruction_format));
  const Status set_operand_status = SetOperandToMemoryBaseAndDisplacement(
      instruction_format, register_index, 123, &instruction);
  EXPECT_THAT(set_operand_status, StatusIs(INVALID_ARGUMENT));
}

class SetOperandToMemoryBaseAnd8BitDisplacementTest
    : public SetOperandToMemoryBaseAndDisplacementTest<int8_t> {
 protected:
  Status SetOperandToMemoryBaseAndDisplacement(
      const InstructionFormat& instruction_format, RegisterIndex base_register,
      DisplacementType displacement, DecodedInstruction* instruction) override {
    return SetOperandToMemoryBaseAnd8BitDisplacement(
        instruction_format, base_register, displacement, instruction);
  }
};

TEST_F(SetOperandToMemoryBaseAnd8BitDisplacementTest, LegacyInstruction) {
  TestSetOperand(kLegacyInstructionFormat,
                 kLegacyInstructionEncodingSpecification, "", RegisterIndex(3),
                 12,
                 R"(legacy_prefixes { rex {}}
         modrm { addressing_mode: INDIRECT_WITH_8_BIT_DISPLACEMENT rm_operand: 3
                 address_displacement: 12 })",
                 "ADC DWORD PTR [RBX + 0xC], EAX");
  // Tests that the function replaces all previous contents of the ModR/M and
  // SIB bytes.
  TestSetOperand(
      kLegacyInstructionFormat, kLegacyInstructionEncodingSpecification,
      R"(modrm { addressing_mode: INDIRECT register_operand: 3 rm_operand: 4 }
         sib { base: 4 index: 4 })",
      RegisterIndex(3), 0x12,
      R"(legacy_prefixes { rex {}}
         modrm { addressing_mode: INDIRECT_WITH_8_BIT_DISPLACEMENT
                 register_operand: 3 rm_operand: 3 address_displacement: 0x12 })",
      "ADC DWORD PTR [RBX + 0x12], EBX");
  // Tests that the negative 8-bit displacement is converted to the unsiged/bit
  // representation correctly (no sign extension beyond the 8 bits).
  TestSetOperand(kLegacyInstructionFormat,
                 kLegacyInstructionEncodingSpecification, "", RegisterIndex(3),
                 -15,
                 R"(legacy_prefixes { rex {}}
         modrm { addressing_mode: INDIRECT_WITH_8_BIT_DISPLACEMENT rm_operand: 3
                 address_displacement: 241 })",
                 "ADC DWORD PTR [RBX - 0xF], EAX");
}

TEST_F(SetOperandToMemoryBaseAnd8BitDisplacementTest,
       LegacyInstructionWithExtendedBit) {
  TestSetOperand(kLegacyInstructionFormat,
                 kLegacyInstructionEncodingSpecification, "", RegisterIndex(15),
                 0x7f,
                 R"(legacy_prefixes { rex { b: true }}
         modrm { addressing_mode: INDIRECT_WITH_8_BIT_DISPLACEMENT rm_operand: 7
                 address_displacement: 0x7f })",
                 "ADC DWORD PTR [R15 + 0x7F], EAX");
  TestSetOperand(kLegacyInstructionFormat,
                 kLegacyInstructionEncodingSpecification, "", RegisterIndex(15),
                 -127,
                 R"(legacy_prefixes { rex { b: true }}
         modrm { addressing_mode: INDIRECT_WITH_8_BIT_DISPLACEMENT rm_operand: 7
                 address_displacement: 129 })",
                 "ADC DWORD PTR [R15 - 0x7F], EAX");
}

TEST_F(SetOperandToMemoryBaseAnd8BitDisplacementTest, VexInstruction) {
  TestSetOperand(kVexInstructionFormat, kVexInstructionEncodingSpecification,
                 R"(vex_prefix {
           mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
           map_select: MAP_SELECT_0F3A })",
                 RegisterIndex(6), 0x6f,
                 R"(vex_prefix {
           mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
           map_select: MAP_SELECT_0F3A
           not_b: true }
         modrm { addressing_mode: INDIRECT_WITH_8_BIT_DISPLACEMENT rm_operand: 6
                 address_displacement: 0x6f })",
                 "VPBLENDVB XMM8, XMM15, XMMWORD PTR [RSI + 0x6F], XMM0");
}

TEST_F(SetOperandToMemoryBaseAnd8BitDisplacementTest,
       VexInstructionWithExtendedBit) {
  TestSetOperand(kVexInstructionFormat, kVexInstructionEncodingSpecification,
                 R"(vex_prefix {
           mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
           map_select: MAP_SELECT_0F3A })",
                 RegisterIndex(11), -4,
                 R"(vex_prefix {
           mandatory_prefix: MANDATORY_PREFIX_OPERAND_SIZE_OVERRIDE
           map_select: MAP_SELECT_0F3A }
         modrm { addressing_mode: INDIRECT_WITH_8_BIT_DISPLACEMENT rm_operand: 3
                 address_displacement: 252 })",
                 "VPBLENDVB XMM8, XMM15, XMMWORD PTR [R11 - 0x4], XMM0");
}

TEST_F(SetOperandToMemoryBaseAnd8BitDisplacementTest, NoMemoryOperand) {
  // Note that the operand of BSWAP is encoded in the opcode, so the instruction
  // can accept only register operands.
  TestSetOperandError(
      R"(mnemonic: 'BSWAP'
         operands { name: 'r32' addressing_mode: DIRECT_ADDRESSING
                    encoding: OPCODE_ENCODING value_size_bits: 32 })",
      RegisterIndex(0));
}

TEST_F(SetOperandToMemoryBaseAnd8BitDisplacementTest, UnencodableOperand) {
  // The register indices used in the test are not encodable with the ModR/M
  // byte.
  TestSetOperandError(kLegacyInstructionFormat, RegisterIndex(4));
  TestSetOperandError(kLegacyInstructionFormat, RegisterIndex(12));
}

TEST_F(SetOperandToMemoryBaseAnd8BitDisplacementTest, InvalidOperandIndex) {
  // The register indices used in the test are not valid.
  TestSetOperandError(kLegacyInstructionFormat, RegisterIndex(-2));
  TestSetOperandError(kLegacyInstructionFormat, RegisterIndex(22));
}

// Since most of the code is shared with the 8-bit version, we only test that
// the 32-bit displacement is treated properly by the function - the 32-bit
// values are not truncated and the signed-to-unsigned conversion is correct.
class SetOperandToMemoryBaseAnd32BitDisplacementTest
    : public SetOperandToMemoryBaseAndDisplacementTest<int32_t> {
 protected:
  Status SetOperandToMemoryBaseAndDisplacement(
      const InstructionFormat& instruction_format, RegisterIndex base_register,
      DisplacementType displacement, DecodedInstruction* instruction) override {
    return SetOperandToMemoryBaseAnd32BitDisplacement(
        instruction_format, base_register, displacement, instruction);
  }
};

TEST_F(SetOperandToMemoryBaseAnd32BitDisplacementTest, LegacyInstruction) {
  TestSetOperand(kLegacyInstructionFormat,
                 kLegacyInstructionEncodingSpecification, "", RegisterIndex(3),
                 0x12345678,
                 R"(legacy_prefixes { rex {}}
                    modrm { addressing_mode: INDIRECT_WITH_32_BIT_DISPLACEMENT
                    rm_operand: 3
                            address_displacement: 0x12345678 })",
                 "ADC DWORD PTR [RBX + 0x12345678], EAX");
  // Tests that the function replaces all previous contents of the ModR/M and
  // SIB bytes.
  TestSetOperand(
      kLegacyInstructionFormat, kLegacyInstructionEncodingSpecification,
      R"(modrm { addressing_mode: INDIRECT register_operand: 3 rm_operand: 4 }
         sib { base: 4 index: 4 })",
      RegisterIndex(3), 0x12345678,
      R"(legacy_prefixes { rex {}}
         modrm { addressing_mode: INDIRECT_WITH_32_BIT_DISPLACEMENT
                 register_operand: 3 rm_operand: 3
                 address_displacement: 0x12345678 })",
      "ADC DWORD PTR [RBX + 0x12345678], EBX");
  // Tests that the negative 8-bit displacement is converted to the unsiged/bit
  // representation correctly (no sign extension beyond the 8 bits).
  TestSetOperand(kLegacyInstructionFormat,
                 kLegacyInstructionEncodingSpecification, "", RegisterIndex(3),
                 -0x9b,
                 R"(legacy_prefixes { rex {}}
                    modrm { addressing_mode: INDIRECT_WITH_32_BIT_DISPLACEMENT
                            rm_operand: 3 address_displacement: 4294967141 })",
                 "ADC DWORD PTR [RBX - 0x9b], EAX");
}

}  // namespace
}  // namespace x86
}  // namespace exegesis
