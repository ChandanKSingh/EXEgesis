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

#include "cpu_instructions/base/cpu_type.h"

#include "gtest/gtest.h"

namespace cpu_instructions {
namespace {

void CheckCPU(const CpuModel& cpu_type, int num_port_masks,
              const string& load_store_address_generation_ports,
              const string& store_address_generation_ports,
              const string& store_data_ports) {
  const auto& uarch = cpu_type.microarchitecture();
  EXPECT_EQ(uarch.port_masks().size(), num_port_masks);
  const PortMask* mask = uarch.load_store_address_generation();
  ASSERT_NE(mask, nullptr);
  EXPECT_EQ(mask->ToString(), load_store_address_generation_ports);
  mask = uarch.store_address_generation();
  ASSERT_NE(mask, nullptr);
  EXPECT_EQ(mask->ToString(), store_address_generation_ports);
  mask = uarch.store_data();
  ASSERT_NE(mask, nullptr);
  EXPECT_EQ(mask->ToString(), store_data_ports);
  EXPECT_FALSE(uarch.IsProtectedMode(-1));
  EXPECT_TRUE(uarch.IsProtectedMode(0));
  EXPECT_FALSE(uarch.IsProtectedMode(3));
  EXPECT_FALSE(uarch.IsProtectedMode(15));
}

TEST(CpuModelTest, SkylakeParses) {
  CheckCPU(CpuModel::Skylake(), 12, "P23", "P237", "P4");
}

TEST(CpuModelTest, BroadwellParses) {
  CheckCPU(CpuModel::Broadwell(), 12, "P23", "P237", "P4");
}

TEST(CpuModelTest, HaswellParses) {
  CheckCPU(CpuModel::Haswell(), 12, "P23", "P237", "P4");
}

TEST(CpuModelTest, IvyBridgeParses) {
  CheckCPU(CpuModel::IvyBridge(), 6, "P23", "P23", "P4");
}

TEST(CpuModelTest, SandyBridgeParses) {
  CheckCPU(CpuModel::SandyBridge(), 6, "P23", "P23", "P4");
}

TEST(CpuModelTest, WestmereParses) {
  CheckCPU(CpuModel::Westmere(), 7, "P2", "P3", "P4");
}

TEST(CpuModelTest, NehalemParses) {
  CheckCPU(CpuModel::Nehalem(), 7, "P2", "P3", "P4");
}

}  // namespace
}  // namespace cpu_instructions
