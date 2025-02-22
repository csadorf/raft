/*
 * Copyright (c) 2022, NVIDIA CORPORATION.
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
#ifndef __COALESCED_REDUCTION_H
#define __COALESCED_REDUCTION_H

#pragma once

#include "detail/coalesced_reduction.cuh"

#include <raft/core/device_mdspan.hpp>
#include <raft/core/handle.hpp>

namespace raft {
namespace linalg {

/**
 * @brief Compute reduction of the input matrix along the leading dimension
 *
 * @tparam InType the data type of the input
 * @tparam OutType the data type of the output (as well as the data type for
 *  which reduction is performed)
 * @tparam IdxType data type of the indices of the array
 * @tparam MainLambda Unary lambda applied while acculumation (eg: L1 or L2 norm)
 * It must be a 'callable' supporting the following input and output:
 * <pre>OutType (*MainLambda)(InType, IdxType);</pre>
 * @tparam ReduceLambda Binary lambda applied for reduction (eg: addition(+) for L2 norm)
 * It must be a 'callable' supporting the following input and output:
 * <pre>OutType (*ReduceLambda)(OutType);</pre>
 * @tparam FinalLambda the final lambda applied before STG (eg: Sqrt for L2 norm)
 * It must be a 'callable' supporting the following input and output:
 * <pre>OutType (*FinalLambda)(OutType);</pre>
 * @param dots the output reduction vector
 * @param data the input matrix
 * @param D leading dimension of data
 * @param N second dimension data
 * @param init initial value to use for the reduction
 * @param main_op elementwise operation to apply before reduction
 * @param reduce_op binary reduction operation
 * @param final_op elementwise operation to apply before storing results
 * @param inplace reduction result added inplace or overwrites old values?
 * @param stream cuda stream where to launch work
 */
template <typename InType,
          typename OutType      = InType,
          typename IdxType      = int,
          typename MainLambda   = raft::Nop<InType, IdxType>,
          typename ReduceLambda = raft::Sum<OutType>,
          typename FinalLambda  = raft::Nop<OutType>>
void coalescedReduction(OutType* dots,
                        const InType* data,
                        IdxType D,
                        IdxType N,
                        OutType init,
                        cudaStream_t stream,
                        bool inplace           = false,
                        MainLambda main_op     = raft::Nop<InType, IdxType>(),
                        ReduceLambda reduce_op = raft::Sum<OutType>(),
                        FinalLambda final_op   = raft::Nop<OutType>())
{
  detail::coalescedReduction<InType, OutType, IdxType>(
    dots, data, D, N, init, stream, inplace, main_op, reduce_op, final_op);
}

/**
 * @defgroup coalesced_reduction Coalesced Memory Access Reductions
 * For reducing along rows for col-major and along columns for row-major
 * @{
 */

/**
 * @brief Compute reduction of the input matrix along the leading dimension
 *        This API is to be used when the desired reduction is along the dimension
 *        of the memory layout. For example, a row-major matrix will be reduced
 *        along the columns whereas a column-major matrix will be reduced along
 *        the rows.
 *
 * @tparam InValueType the input data-type of underlying raft::matrix_view
 * @tparam LayoutPolicy The layout of Input/Output (row or col major)
 * @tparam OutValueType the output data-type of underlying raft::matrix_view and reduction
 * @tparam IndexType Integer type used to for addressing
 * @tparam MainLambda Unary lambda applied while acculumation (eg: L1 or L2 norm)
 * It must be a 'callable' supporting the following input and output:
 * <pre>OutType (*MainLambda)(InType, IdxType);</pre>
 * @tparam ReduceLambda Binary lambda applied for reduction (eg: addition(+) for L2 norm)
 * It must be a 'callable' supporting the following input and output:
 * <pre>OutType (*ReduceLambda)(OutType);</pre>
 * @tparam FinalLambda the final lambda applied before STG (eg: Sqrt for L2 norm)
 * It must be a 'callable' supporting the following input and output:
 * <pre>OutType (*FinalLambda)(OutType);</pre>
 * @param handle raft::handle_t
 * @param[in] data Input of type raft::device_matrix_view
 * @param[out] dots Output of type raft::device_matrix_view
 * @param[in] init initial value to use for the reduction
 * @param[in] inplace reduction result added inplace or overwrites old values?
 * @param[in] main_op fused elementwise operation to apply before reduction
 * @param[in] reduce_op fused binary reduction operation
 * @param[in] final_op fused elementwise operation to apply before storing results
 */
template <typename InValueType,
          typename LayoutPolicy,
          typename OutValueType,
          typename IndexType,
          typename MainLambda   = raft::Nop<InValueType>,
          typename ReduceLambda = raft::Sum<OutValueType>,
          typename FinalLambda  = raft::Nop<OutValueType>>
void coalesced_reduction(const raft::handle_t& handle,
                         raft::device_matrix_view<const InValueType, IndexType, LayoutPolicy> data,
                         raft::device_vector_view<OutValueType, IndexType> dots,
                         OutValueType init,
                         bool inplace           = false,
                         MainLambda main_op     = raft::Nop<InValueType>(),
                         ReduceLambda reduce_op = raft::Sum<OutValueType>(),
                         FinalLambda final_op   = raft::Nop<OutValueType>())
{
  if constexpr (std::is_same_v<LayoutPolicy, raft::row_major>) {
    RAFT_EXPECTS(static_cast<IndexType>(dots.size()) == data.extent(0),
                 "Output should be equal to number of rows in Input");

    coalescedReduction(dots.data_handle(),
                       data.data_handle(),
                       data.extent(1),
                       data.extent(0),
                       init,
                       handle.get_stream(),
                       inplace,
                       main_op,
                       reduce_op,
                       final_op);
  } else if constexpr (std::is_same_v<LayoutPolicy, raft::col_major>) {
    RAFT_EXPECTS(static_cast<IndexType>(dots.size()) == data.extent(1),
                 "Output should be equal to number of columns in Input");

    coalescedReduction(dots.data_handle(),
                       data.data_handle(),
                       data.extent(0),
                       data.extent(1),
                       init,
                       handle.get_stream(),
                       inplace,
                       main_op,
                       reduce_op,
                       final_op);
  }
}

/** @} */  // end of group coalesced_reduction

};  // end namespace linalg
};  // end namespace raft

#endif