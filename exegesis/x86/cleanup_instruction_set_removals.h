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

// Contains instruction set transforms that remove instructions that are not
// used in the instruction database.

#ifndef EXEGESIS_X86_CLEANUP_INSTRUCTION_SET_REMOVALS_H_
#define EXEGESIS_X86_CLEANUP_INSTRUCTION_SET_REMOVALS_H_

#include "exegesis/proto/instructions.pb.h"
#include "util/task/status.h"

namespace exegesis {
namespace x86 {

using ::exegesis::util::Status;

// Finds duplicate entries in the instruction set. Two entries are considered
// duplicate, if they produce exactly the same binary encoding. This is a weak
// definition of equality, because two protos with different binary encodings
// may still be equivalent e.g. through default values, but we assume that such
// cases are so unlikely in our data set that we can safely ignore them.
Status RemoveDuplicateInstructions(InstructionSetProto* instruction_set);

// Removes all instructions that use the pseudo-prefix "9B" (wait for pending
// FPU exceptions). The byte "9B" actually is a stand-alone instruction, and
// the disassembler treats it as such.
// TODO(ondrasej): We need to verify how the instruction is treated by the CPU,
// e.g. if it is fused into a single micro-operation, or if the CPU does some
// kind of synchronization to prevent other exceptions from happening.
Status RemoveInstructionsWaitingForFpuSync(
    InstructionSetProto* instruction_set);

// Removes instructions that are not encodable in the 64-bit x86-64 mode.
Status RemoveNonEncodableInstructions(InstructionSetProto* instruction_set);

// Removes all instructions that use the prefixes "F2" and "F3" in the binary
// encoding and that use the REP/REPNE prefix in the assembly code. These are
// instructions that we already represent in the form without the prefix, and
// we do not need the special case.
// TODO(ondrasej): We should keep the information that these instructions can
// have the REP/REPNE prefix, ideally in a separate field of the instruction
// proto.
Status RemoveRepAndRepneInstructions(InstructionSetProto* instruction_set);

// Removes instructions that are a special case of another instructions. All of
// these are special cases of instructions that encode one of their operands in
// the opcode using the "+i" encoding. More specifically, they are instructions
// that perform a certain operation on ST(0) and ST(1), but we also have another
// instruction that uses the same mnemonic, performs the same operation on ST(0)
// and ST(i), and encodes to the same sequence of bytes when used with ST(1).
Status RemoveSpecialCaseInstructions(InstructionSetProto* instruction_set);

// Removes the UD0 and UD1 instructions introduced in the December 2016 version
// of the SDM. These instructions are not "real" instructions - they raise the
// "undefined opcode" exception, by using an undefined opcode.
Status RemoveUndefinedInstructions(InstructionSetProto* instruction_set);

// Removes instructions whose encoding specification has the token "REX" (not
// "REX.W") and checks that there is an equaivalent definition without the REX
// prefix. We suspect that this REX prefix is there only to signal that the
// instruction may use the REX prefix to access the extended registers added by
// the 64-bit instruction set.
Status RemoveDuplicateInstructionsWithRexPrefix(
    InstructionSetProto* instruction_set);

// As of the October 2017 version of the SDM, there are two entries for the
// instruction REX.W + 8C /r (MOV from segment register to register/memory).
// Since we're aiming at the 64-bit mode, we remove the 16/32/64-bit version.
Status RemoveDuplicateMovFromSReg(InstructionSetProto* instruction_set);

// Some of the instructions like "FXCH" has multiple variants that cover each
// other, for example encoding for "FXCH st(0), st(i)" is "D9 C8+i" which swaps
// contents of ST(0) with ST(i), but there is also one version with "D9 C9",
// which is "FXCH st(0), st(1)" and implicitly included in the previous case. We
// delete those of second type.
Status RemoveX87InstructionsWithGeneralVersions(
    InstructionSetProto* instruction_set);

}  // namespace x86
}  // namespace exegesis

#endif  // EXEGESIS_X86_CLEANUP_INSTRUCTION_SET_REMOVALS_H_
