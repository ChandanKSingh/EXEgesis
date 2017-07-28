// Copyright 2016 Google Inc.
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

#include "exegesis/x86/cleanup_instruction_set_properties.h"

#include "exegesis/base/cleanup_instruction_set_test_utils.h"
#include "gtest/gtest.h"

namespace exegesis {
namespace x86 {
namespace {

TEST(AddMissingCpuFlagsTest, AddsMissing) {
  constexpr char kInstructionSetProto[] = R"(
      instructions {
        vendor_syntax {
          mnemonic: 'CLFLUSH'
        }
      }
      instructions {
        vendor_syntax {
          mnemonic: 'INS'
          operands {
            name: 'm8'
          }
        }
        raw_encoding_specification: '6C'
      })";
  constexpr char kExpectedInstructionSetProto[] = R"(
      instructions {
        vendor_syntax {
          mnemonic: 'CLFLUSH'
        }
        feature_name: 'CLFSH'
      }
      instructions {
        vendor_syntax {
          mnemonic: 'INS'
          operands {
            name: 'm8'
          }
        }
        raw_encoding_specification: '6C'
      })";
  TestTransform(AddMissingCpuFlags, kInstructionSetProto,
                kExpectedInstructionSetProto);
}

TEST(AddRestrictedModesTest, AddsProtectionModes) {
  constexpr char kInstructionSetProto[] = R"(
      instructions {
        vendor_syntax {
          mnemonic: 'HLT'
        }
        protection_mode: -1
      }
      instructions {
        vendor_syntax {
          mnemonic: 'MOV'
          operands {
            name: 'r64'
          }
          operands {
            name: 'imm64'
          }
        }
      })";
  constexpr char kExpectedInstructionSetProto[] = R"(
      instructions {
        vendor_syntax {
          mnemonic: 'HLT'
        }
        protection_mode: 0
      }
      instructions {
        protection_mode: -1
        vendor_syntax {
          mnemonic: 'MOV'
          operands {
            name: 'r64'
          }
          operands {
            name: 'imm64'
          }
        }
      })";
  TestTransform(AddProtectionModes, kInstructionSetProto,
                kExpectedInstructionSetProto);
}

}  // namespace
}  // namespace x86
}  // namespace exegesis
