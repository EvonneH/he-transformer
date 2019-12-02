//*****************************************************************************
// Copyright 2018-2019 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//*****************************************************************************

#include <random>

#include "ENCRYPTO_utils/crypto/crypto.h"
#include "ENCRYPTO_utils/parse_options.h"
#include "aby/aby_util.hpp"
#include "aby/kernel/maxpool_aby.hpp"
#include "abycore/aby/abyparty.h"
#include "abycore/circuit/booleancircuits.h"
#include "abycore/circuit/share.h"
#include "abycore/sharing/sharing.h"
#include "gtest/gtest.h"

namespace ngraph::runtime::aby {

auto test_maxpool_circuit = [](size_t num_vals, size_t coeff_modulus) {
  e_sharing sharing = S_BOOL;
  uint32_t bitlen = 64;

  NGRAPH_INFO << "coeff_modulus " << coeff_modulus;
  NGRAPH_INFO << "num_vals " << num_vals;
  std::vector<uint64_t> zeros(num_vals, 0);

  std::vector<uint64_t> x(num_vals);
  std::vector<uint64_t> xs(num_vals);
  std::vector<uint64_t> xc(num_vals);

  std::random_device rd;
  std::mt19937 gen(0);  // rd());
  std::uniform_int_distribution<uint64_t> dis(0, coeff_modulus - 1);
  uint64_t r{dis(gen)};
  for (int i = 0; i < static_cast<int>(num_vals); ++i) {
    x[i] = i;

    xc[i] = dis(gen);
    xs[i] = (x[i] % coeff_modulus + coeff_modulus) - xc[i];
    xs[i] = xs[i] % coeff_modulus;
    // maxpool circuit expects transformation (-q/2, q/2) => (0,q) by adding q

    EXPECT_EQ((xs[i] + xc[i]) % coeff_modulus, x[i] % coeff_modulus);
  }
  uint64_t exp_output =
      (*std::max_element(xs.begin(), xs.end()) + r) % coeff_modulus;

  // Server function
  auto server_fun = [&]() {
    NGRAPH_INFO << "server function";
    auto server = std::make_unique<ABYParty>(
        SERVER, "0.0.0.0", 30001, get_sec_lvl(128), 64, 1, MT_OT, 100000);

    std::vector<Sharing*>& sharings = server->GetSharings();
    BooleanCircuit& circ = dynamic_cast<BooleanCircuit&>(
        *sharings[sharing]->GetCircuitBuildRoutine());

    std::this_thread::sleep_for(std::chrono::seconds(1));

    maxpool_aby(circ, num_vals, xs, zeros, r, bitlen, coeff_modulus);
    server->ExecCircuit();
    server->Reset();
  };

  // Client function
  auto client_fun = [&]() {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    NGRAPH_INFO << "client function";
    auto client = std::make_unique<ABYParty>(
        CLIENT, "localhost", 30001, get_sec_lvl(128), 64, 1, MT_OT, 100000);

    std::vector<Sharing*>& sharings = client->GetSharings();
    BooleanCircuit& circ = dynamic_cast<BooleanCircuit&>(
        *sharings[sharing]->GetCircuitBuildRoutine());

    share* maxpool_out =
        maxpool_aby(circ, num_vals, zeros, xc, 0, bitlen, coeff_modulus);

    client->ExecCircuit();

    uint32_t out_bitlen_maxpool, out_num_aby_vals;
    uint64_t* out_vals_maxpool;

    maxpool_out->get_clear_value_vec(&out_vals_maxpool, &out_bitlen_maxpool,
                                     &out_num_aby_vals);

    EXPECT_EQ(out_num_aby_vals, 1);

    for (size_t i = 0; i < out_num_aby_vals; ++i) {
      if (out_vals_maxpool[i] != exp_output) {
        NGRAPH_INFO << "Not same at index " << i;
        NGRAPH_INFO << "x";
        for (const auto& elem : x) {
          NGRAPH_INFO << elem;
        }
        NGRAPH_INFO << "xs";
        for (const auto& elem : xs) {
          NGRAPH_INFO << elem;
        }
        NGRAPH_INFO << "xc";
        for (const auto& elem : xc) {
          NGRAPH_INFO << elem;
        }
        NGRAPH_INFO << "\tr " << r;
        NGRAPH_INFO << "\texp_output[i] " << exp_output;
        NGRAPH_INFO << "\toutput " << out_vals_maxpool[i];
      }
      EXPECT_EQ(out_vals_maxpool[i], exp_output);
    }

    client->Reset();
  };
  std::thread server_thread(server_fun);
  client_fun();
  server_thread.join();
};

TEST(aby, maxpool_circuit_10_q8) { test_maxpool_circuit(10, 8); }

TEST(aby, maxpool_circuit_100_q8) { test_maxpool_circuit(100, 8); }

TEST(aby, maxpool_circuit_10_q9) { test_maxpool_circuit(10, 9); }

TEST(aby, maxpool_circuit_100_q9) { test_maxpool_circuit(100, 9); }

}  // namespace ngraph::runtime::aby
