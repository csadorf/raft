/*
 * Copyright (c) 2018-2022, NVIDIA CORPORATION.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "../test_utils.h"
#include "reduce.cuh"
#include <gtest/gtest.h>
#include <raft/linalg/coalesced_reduction.cuh>
#include <raft/random/rng.cuh>
#include <raft/util/cuda_utils.cuh>
#include <raft/util/cudart_utils.hpp>

namespace raft {
namespace linalg {

template <typename T>
struct coalescedReductionInputs {
  T tolerance;
  int rows, cols;
  unsigned long long int seed;
};

template <typename T>
::std::ostream& operator<<(::std::ostream& os, const coalescedReductionInputs<T>& dims)
{
  return os;
}

// Or else, we get the following compilation error
// for an extended __device__ lambda cannot have private or protected access
// within its class
template <typename T>
void coalescedReductionLaunch(
  const raft::handle_t& handle, T* dots, const T* data, int cols, int rows, bool inplace = false)
{
  auto dots_view = raft::make_device_vector_view(dots, rows);
  auto data_view = raft::make_device_matrix_view(data, rows, cols);
  coalesced_reduction(
    handle, data_view, dots_view, (T)0, inplace, [] __device__(T in, int i) { return in * in; });
}

template <typename T>
class coalescedReductionTest : public ::testing::TestWithParam<coalescedReductionInputs<T>> {
 public:
  coalescedReductionTest()
    : params(::testing::TestWithParam<coalescedReductionInputs<T>>::GetParam()),
      stream(handle.get_stream()),
      data(params.rows * params.cols, stream),
      dots_exp(params.rows * params.cols, stream),
      dots_act(params.rows * params.cols, stream)
  {
  }

 protected:
  void SetUp() override
  {
    raft::random::RngState r(params.seed);
    int rows = params.rows, cols = params.cols;
    int len = rows * cols;
    uniform(handle, r, data.data(), len, T(-1.0), T(1.0));
    naiveCoalescedReduction(dots_exp.data(), data.data(), cols, rows, stream);

    // Perform reduction with default inplace = false first
    coalescedReductionLaunch(handle, dots_act.data(), data.data(), cols, rows);
    // Add to result with inplace = true next
    coalescedReductionLaunch(handle, dots_act.data(), data.data(), cols, rows, true);

    handle.sync_stream(stream);
  }

 protected:
  raft::handle_t handle;
  cudaStream_t stream;

  coalescedReductionInputs<T> params;
  rmm::device_uvector<T> data;
  rmm::device_uvector<T> dots_exp;
  rmm::device_uvector<T> dots_act;
};

const std::vector<coalescedReductionInputs<float>> inputsf = {{0.000002f, 1024, 32, 1234ULL},
                                                              {0.000002f, 1024, 64, 1234ULL},
                                                              {0.000002f, 1024, 128, 1234ULL},
                                                              {0.000002f, 1024, 256, 1234ULL}};

const std::vector<coalescedReductionInputs<double>> inputsd = {{0.000000001, 1024, 32, 1234ULL},
                                                               {0.000000001, 1024, 64, 1234ULL},
                                                               {0.000000001, 1024, 128, 1234ULL},
                                                               {0.000000001, 1024, 256, 1234ULL}};

typedef coalescedReductionTest<float> coalescedReductionTestF;
TEST_P(coalescedReductionTestF, Result)
{
  ASSERT_TRUE(raft::devArrMatch(dots_exp.data(),
                                dots_act.data(),
                                params.rows,
                                raft::CompareApprox<float>(params.tolerance),
                                stream));
}

typedef coalescedReductionTest<double> coalescedReductionTestD;
TEST_P(coalescedReductionTestD, Result)
{
  ASSERT_TRUE(raft::devArrMatch(dots_exp.data(),
                                dots_act.data(),
                                params.rows,
                                raft::CompareApprox<double>(params.tolerance),
                                stream));
}

INSTANTIATE_TEST_CASE_P(coalescedReductionTests,
                        coalescedReductionTestF,
                        ::testing::ValuesIn(inputsf));

INSTANTIATE_TEST_CASE_P(coalescedReductionTests,
                        coalescedReductionTestD,
                        ::testing::ValuesIn(inputsd));

}  // end namespace linalg
}  // end namespace raft
