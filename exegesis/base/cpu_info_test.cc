// Copyright 2017 Google Inc.
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

#include "exegesis/base/cpu_info.h"

#include <initializer_list>

#include "exegesis/base/cpuid_x86.h"
#include "exegesis/proto/cpuid.pb.h"
#include "exegesis/proto/microarchitecture.pb.h"
#include "exegesis/proto/x86/cpuid.pb.h"
#include "exegesis/util/proto_util.h"
#include "glog/logging.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "strings/string_view.h"
#include "strings/string_view_utils.h"
#include "util/task/status.h"

namespace exegesis {
namespace {

using ::exegesis::x86::CpuIdDump;
using ::testing::UnorderedElementsAreArray;
using ::exegesis::util::StatusOr;

TEST(CpuInfoTest, SupportsFeature) {
  const CpuInfo cpu_info(ParseProtoFromStringOrDie<CpuInfoProto>(R"(
      model_id: "doesnotexist"
      feature_names: "ADX"
      feature_names: "SSE"
      feature_names: "LZCNT"
  )"));
  EXPECT_TRUE(cpu_info.SupportsFeature("ADX"));
  EXPECT_TRUE(cpu_info.SupportsFeature("SSE"));
  EXPECT_TRUE(cpu_info.SupportsFeature("LZCNT"));
  EXPECT_FALSE(cpu_info.SupportsFeature("AVX"));

  EXPECT_TRUE(cpu_info.SupportsFeature("ADX || AVX"));

  EXPECT_FALSE(cpu_info.SupportsFeature("ADX && AVX"));
}

}  // namespace
}  // namespace exegesis
