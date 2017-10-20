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

#include "exegesis/x86/cleanup_instruction_set_utils.h"

#include <string>

#include "exegesis/proto/x86/encoding_specification.pb.h"
#include "glog/logging.h"
#include "strings/str_cat.h"

namespace exegesis {
namespace x86 {
namespace {

// The operand size override prefix used by the 16-bit versions of some
// instructions.
const char kOperandSizeOverridePrefix[] = "66 ";

}  // namespace

// Adds the operand size override prefix to the binary encoding specification of
// the given instruction proto. If the instruction already has the prefix, it is
// not added and a warning is printed to the log.
void AddOperandSizeOverrideToInstructionProto(InstructionProto* instruction) {
  CHECK(instruction != nullptr);
  if (instruction->has_x86_encoding_specification()) {
    // If the x86 encoding specification was not parsed yet, ignore it; it would
    // be overwritten later anyway.
    EncodingSpecification* const encoding_specification =
        instruction->mutable_x86_encoding_specification();
    encoding_specification->mutable_legacy_prefixes()
        ->set_has_mandatory_operand_size_override_prefix(true);
  }
  const std::string& raw_encoding_specification =
      instruction->raw_encoding_specification();
  if (raw_encoding_specification.find(kOperandSizeOverridePrefix) ==
      std::string::npos) {
    instruction->set_raw_encoding_specification(
        StrCat(kOperandSizeOverridePrefix, raw_encoding_specification));
  } else {
    LOG(WARNING)
        << "The instruction already has an operand size override prefix: "
        << instruction->raw_encoding_specification();
  }
}

}  // namespace x86
}  // namespace exegesis
