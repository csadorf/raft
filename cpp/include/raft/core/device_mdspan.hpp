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

#pragma once

#include <cstdint>
#include <raft/core/host_device_accessor.hpp>
#include <raft/core/mdspan.hpp>
#include <raft/core/memory_type.hpp>

namespace raft {

template <typename AccessorPolicy>
using device_accessor = host_device_accessor<AccessorPolicy, memory_type::device>;

template <typename AccessorPolicy>
using managed_accessor = host_device_accessor<AccessorPolicy, memory_type::managed>;

/**
 * @brief std::experimental::mdspan with device tag to avoid accessing incorrect memory location.
 */
template <typename ElementType,
          typename Extents,
          typename LayoutPolicy   = layout_c_contiguous,
          typename AccessorPolicy = std::experimental::default_accessor<ElementType>>
using device_mdspan = mdspan<ElementType, Extents, LayoutPolicy, device_accessor<AccessorPolicy>>;

template <typename ElementType,
          typename Extents,
          typename LayoutPolicy   = layout_c_contiguous,
          typename AccessorPolicy = std::experimental::default_accessor<ElementType>>
using managed_mdspan = mdspan<ElementType, Extents, LayoutPolicy, managed_accessor<AccessorPolicy>>;

namespace detail {
template <typename T, bool B>
struct is_device_mdspan : std::false_type {
};
template <typename T>
struct is_device_mdspan<T, true> : std::bool_constant<T::accessor_type::is_device_accessible> {
};

/**
 * @\brief Boolean to determine if template type T is either raft::device_mdspan or a derived type
 */
template <typename T>
using is_device_mdspan_t = is_device_mdspan<T, is_mdspan_v<T>>;

template <typename T>
using is_input_device_mdspan_t = is_device_mdspan<T, is_input_mdspan_v<T>>;

template <typename T>
using is_output_device_mdspan_t = is_device_mdspan<T, is_output_mdspan_v<T>>;

template <typename T, bool B>
struct is_managed_mdspan : std::false_type {
};
template <typename T>
struct is_managed_mdspan<T, true> : std::bool_constant<T::accessor_type::is_managed_accessible> {
};

/**
 * @\brief Boolean to determine if template type T is either raft::managed_mdspan or a derived type
 */
template <typename T>
using is_managed_mdspan_t = is_managed_mdspan<T, is_mdspan_v<T>>;

template <typename T>
using is_input_managed_mdspan_t = is_managed_mdspan<T, is_input_mdspan_v<T>>;

template <typename T>
using is_output_managed_mdspan_t = is_managed_mdspan<T, is_output_mdspan_v<T>>;

}  // end namespace detail

/**
 * @\brief Boolean to determine if variadic template types Tn are either raft::device_mdspan or a
 * derived type
 */
template <typename... Tn>
inline constexpr bool is_device_mdspan_v = std::conjunction_v<detail::is_device_mdspan_t<Tn>...>;

template <typename... Tn>
inline constexpr bool is_input_device_mdspan_v =
  std::conjunction_v<detail::is_input_device_mdspan_t<Tn>...>;

template <typename... Tn>
inline constexpr bool is_output_device_mdspan_v =
  std::conjunction_v<detail::is_output_device_mdspan_t<Tn>...>;

template <typename... Tn>
using enable_if_device_mdspan = std::enable_if_t<is_device_mdspan_v<Tn...>>;

template <typename... Tn>
using enable_if_input_device_mdspan = std::enable_if_t<is_input_device_mdspan_v<Tn...>>;

template <typename... Tn>
using enable_if_output_device_mdspan = std::enable_if_t<is_output_device_mdspan_v<Tn...>>;

/**
 * @\brief Boolean to determine if variadic template types Tn are either raft::managed_mdspan or a
 * derived type
 */
template <typename... Tn>
inline constexpr bool is_managed_mdspan_v = std::conjunction_v<detail::is_managed_mdspan_t<Tn>...>;

template <typename... Tn>
inline constexpr bool is_input_managed_mdspan_v =
  std::conjunction_v<detail::is_input_managed_mdspan_t<Tn>...>;

template <typename... Tn>
inline constexpr bool is_output_managed_mdspan_v =
  std::conjunction_v<detail::is_output_managed_mdspan_t<Tn>...>;

template <typename... Tn>
using enable_if_managed_mdspan = std::enable_if_t<is_managed_mdspan_v<Tn...>>;

template <typename... Tn>
using enable_if_input_managed_mdspan = std::enable_if_t<is_input_managed_mdspan_v<Tn...>>;

template <typename... Tn>
using enable_if_output_managed_mdspan = std::enable_if_t<is_output_managed_mdspan_v<Tn...>>;

/**
 * @brief Shorthand for 0-dim host mdspan (scalar).
 * @tparam ElementType the data type of the scalar element
 * @tparam IndexType the index type of the extents
 */
template <typename ElementType, typename IndexType = std::uint32_t>
using device_scalar_view = device_mdspan<ElementType, scalar_extent<IndexType>>;

/**
 * @brief Shorthand for 1-dim device mdspan.
 * @tparam ElementType the data type of the vector elements
 * @tparam IndexType the index type of the extents
 * @tparam LayoutPolicy policy for strides and layout ordering
 */
template <typename ElementType,
          typename IndexType    = std::uint32_t,
          typename LayoutPolicy = layout_c_contiguous>
using device_vector_view = device_mdspan<ElementType, vector_extent<IndexType>, LayoutPolicy>;

/**
 * @brief Shorthand for c-contiguous device matrix view.
 * @tparam ElementType the data type of the matrix elements
 * @tparam IndexType the index type of the extents
 * @tparam LayoutPolicy policy for strides and layout ordering
 */
template <typename ElementType,
          typename IndexType    = std::uint32_t,
          typename LayoutPolicy = layout_c_contiguous>
using device_matrix_view = device_mdspan<ElementType, matrix_extent<IndexType>, LayoutPolicy>;

/**
 * @brief Shorthand for 128 byte aligned device matrix view.
 * @tparam ElementType the data type of the matrix elements
 * @tparam IndexType the index type of the extents
 * @tparam LayoutPolicy must be of type layout_{left/right}_padded
 */
template <typename ElementType,
          typename IndexType    = std::uint32_t,
          typename LayoutPolicy = layout_right_padded<ElementType>,
          typename              = enable_if_layout_padded<ElementType, LayoutPolicy>>
using device_aligned_matrix_view =
  device_mdspan<ElementType,
                matrix_extent<IndexType>,
                LayoutPolicy,
                std::experimental::aligned_accessor<ElementType, detail::alignment::value>>;

/**
 * @brief Create a 2-dim 128 byte aligned mdspan instance for device pointer. It's
 *        expected that the given layout policy match the layout of the underlying
 *        pointer.
 * @tparam ElementType the data type of the matrix elements
 * @tparam LayoutPolicy must be of type layout_{left/right}_padded
 * @tparam IndexType the index type of the extents
 * @param[in] ptr on device to wrap
 * @param[in] n_rows number of rows in pointer
 * @param[in] n_cols number of columns in pointer
 */
template <typename ElementType,
          typename IndexType    = std::uint32_t,
          typename LayoutPolicy = layout_right_padded<ElementType>>
auto make_device_aligned_matrix_view(ElementType* ptr, IndexType n_rows, IndexType n_cols)
{
  using data_handle_type =
    typename std::experimental::aligned_accessor<ElementType,
                                                 detail::alignment::value>::data_handle_type;
  static_assert(std::is_same<LayoutPolicy, layout_left_padded<ElementType>>::value ||
                std::is_same<LayoutPolicy, layout_right_padded<ElementType>>::value);
  assert(ptr == alignTo(ptr, detail::alignment::value));

  data_handle_type aligned_pointer = ptr;

  matrix_extent<IndexType> extents{n_rows, n_cols};
  return device_aligned_matrix_view<ElementType, IndexType, LayoutPolicy>{aligned_pointer, extents};
}

/**
 * @brief Create a raft::managed_mdspan
 * @tparam ElementType the data type of the matrix elements
 * @tparam IndexType the index type of the extents
 * @tparam LayoutPolicy policy for strides and layout ordering
 * @param ptr Pointer to the data
 * @param exts dimensionality of the array (series of integers)
 * @return raft::managed_mdspan
 */
template <typename ElementType,
          typename IndexType    = std::uint32_t,
          typename LayoutPolicy = layout_c_contiguous,
          size_t... Extents>
auto make_managed_mdspan(ElementType* ptr, extents<IndexType, Extents...> exts)
{
  return make_mdspan<ElementType, IndexType, LayoutPolicy, true, true>(ptr, exts);
}

/**
 * @brief Create a 0-dim (scalar) mdspan instance for device value.
 *
 * @tparam ElementType the data type of the matrix elements
 * @tparam IndexType the index type of the extents
 * @param[in] ptr on device to wrap
 */
template <typename ElementType, typename IndexType = std::uint32_t>
auto make_device_scalar_view(ElementType* ptr)
{
  scalar_extent<IndexType> extents;
  return device_scalar_view<ElementType, IndexType>{ptr, extents};
}

/**
 * @brief Create a 2-dim c-contiguous mdspan instance for device pointer. It's
 *        expected that the given layout policy match the layout of the underlying
 *        pointer.
 * @tparam ElementType the data type of the matrix elements
 * @tparam LayoutPolicy policy for strides and layout ordering
 * @tparam IndexType the index type of the extents
 * @param[in] ptr on device to wrap
 * @param[in] n_rows number of rows in pointer
 * @param[in] n_cols number of columns in pointer
 */
template <typename ElementType,
          typename IndexType    = std::uint32_t,
          typename LayoutPolicy = layout_c_contiguous>
auto make_device_matrix_view(ElementType* ptr, IndexType n_rows, IndexType n_cols)
{
  matrix_extent<IndexType> extents{n_rows, n_cols};
  return device_matrix_view<ElementType, IndexType, LayoutPolicy>{ptr, extents};
}

/**
 * @brief Create a 1-dim mdspan instance for device pointer.
 * @tparam ElementType the data type of the vector elements
 * @tparam IndexType the index type of the extents
 * @tparam LayoutPolicy policy for strides and layout ordering
 * @param[in] ptr on device to wrap
 * @param[in] n number of elements in pointer
 * @return raft::device_vector_view
 */
template <typename ElementType, typename IndexType, typename LayoutPolicy = layout_c_contiguous>
auto make_device_vector_view(ElementType* ptr, IndexType n)
{
  return device_vector_view<ElementType, IndexType, LayoutPolicy>{ptr, n};
}

/**
 * @brief Create a 1-dim mdspan instance for device pointer.
 * @tparam ElementType the data type of the vector elements
 * @tparam IndexType the index type of the extents
 * @tparam LayoutPolicy policy for strides and layout ordering
 * @param[in] ptr on device to wrap
 * @param[in] mapping The layout mapping to use for this vector
 * @return raft::device_vector_view
 */
template <typename ElementType, typename IndexType, typename LayoutPolicy = layout_c_contiguous>
auto make_device_vector_view(
  ElementType* ptr,
  const typename LayoutPolicy::template mapping<vector_extent<IndexType>>& mapping)
{
  return device_vector_view<ElementType, IndexType, LayoutPolicy>{ptr, mapping};
}

/**
 * @brief Create a layout_stride mapping from extents and strides
 * @param[in] extents the dimensionality of the layout
 * @param[in] strides the strides between elements in the layout
 * @return raft::layout_stride::mapping<Extents>
 */
template <typename Extents, typename Strides>
auto make_strided_layout(Extents extents, Strides strides)
{
  return layout_stride::mapping<Extents>{extents, strides};
}

/**
 * @brief Construct a strided vector layout mapping
 *
 * Usage example:
 * @code{.cpp}
 *  #include <raft/core/device_mdspan.hpp>
 *
 *  int n_elements = 10;
 *  int stride = 10;
 *  auto vector = raft::make_device_vector_view(vector_ptr,
 * raft::make_vector_strided_layout(n_elements, stride));
 * @endcode
 *
 * @tparam IndexType the index type of the extents
 * @param[in] n the number of elements in the vector
 * @param[in] stride the stride between elements in the vector
 */
template <typename IndexType>
auto make_vector_strided_layout(IndexType n, IndexType stride)
{
  return make_strided_layout(vector_extent<IndexType>{n}, std::array<IndexType, 1>{stride});
}
}  // end namespace raft
