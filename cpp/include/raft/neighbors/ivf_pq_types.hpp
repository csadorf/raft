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

#include "ann_types.hpp"

#include <raft/core/device_mdarray.hpp>
#include <raft/core/error.hpp>
#include <raft/distance/distance_types.hpp>
#include <raft/util/integer_utils.hpp>

#include <type_traits>

namespace raft::neighbors::ivf_pq {

/** A type for specifying how PQ codebooks are created. */
enum class codebook_gen {  // NOLINT
  PER_SUBSPACE = 0,        // NOLINT
  PER_CLUSTER  = 1,        // NOLINT
};

struct index_params : ann::index_params {
  /**
   * The number of inverted lists (clusters)
   *
   * Hint: the number of vectors per cluster (`n_rows/n_lists`) should be approximately 1,000 to
   * 10,000.
   */
  uint32_t n_lists = 1024;
  /** The number of iterations searching for kmeans centers (index building). */
  uint32_t kmeans_n_iters = 20;
  /** The fraction of data to use during iterative kmeans building. */
  double kmeans_trainset_fraction = 0.5;
  /**
   * The bit length of the vector element after compression by PQ.
   *
   * Possible values: [4, 5, 6, 7, 8].
   *
   * Hint: the smaller the 'pq_bits', the smaller the index size and the better the search
   * performance, but the lower the recall.
   */
  uint32_t pq_bits = 8;
  /**
   * The dimensionality of the vector after compression by PQ. When zero, an optimal value is
   * selected using a heuristic.
   *
   * NB: `pq_dim * pq_bits` must be a multiple of 8.
   *
   * Hint: a smaller 'pq_dim' results in a smaller index size and better search performance, but
   * lower recall. If 'pq_bits' is 8, 'pq_dim' can be set to any number, but multiple of 8 are
   * desirable for good performance. If 'pq_bits' is not 8, 'pq_dim' should be a multiple of 8.
   * For good performance, it is desirable that 'pq_dim' is a multiple of 32. Ideally, 'pq_dim'
   * should be also a divisor of the dataset dim.
   */
  uint32_t pq_dim = 0;
  /** How PQ codebooks are created. */
  codebook_gen codebook_kind = codebook_gen::PER_SUBSPACE;
  /**
   * Apply a random rotation matrix on the input data and queries even if `dim % pq_dim == 0`.
   *
   * Note: if `dim` is not multiple of `pq_dim`, a random rotation is always applied to the input
   * data and queries to transform the working space from `dim` to `rot_dim`, which may be slightly
   * larger than the original space and and is a multiple of `pq_dim` (`rot_dim % pq_dim == 0`).
   * However, this transform is not necessary when `dim` is multiple of `pq_dim`
   *   (`dim == rot_dim`, hence no need in adding "extra" data columns / features).
   *
   * By default, if `dim == rot_dim`, the rotation transform is initialized with the identity
   * matrix. When `force_random_rotation == true`, a random orthogonal transform matrix is generated
   * regardless of the values of `dim` and `pq_dim`.
   */
  bool force_random_rotation = false;
};

struct search_params : ann::search_params {
  /** The number of clusters to search. */
  uint32_t n_probes = 20;
  /**
   * Data type of look up table to be created dynamically at search time.
   *
   * Possible values: [CUDA_R_32F, CUDA_R_16F, CUDA_R_8U]
   *
   * The use of low-precision types reduces the amount of shared memory required at search time, so
   * fast shared memory kernels can be used even for datasets with large dimansionality. Note that
   * the recall is slightly degraded when low-precision type is selected.
   */
  cudaDataType_t lut_dtype = CUDA_R_32F;
  /**
   * Storage data type for distance/similarity computed at search time.
   *
   * Possible values: [CUDA_R_16F, CUDA_R_32F]
   *
   * If the performance limiter at search time is device memory access, selecting FP16 will improve
   * performance slightly.
   */
  cudaDataType_t internal_distance_dtype = CUDA_R_32F;
  /**
   * Thread block size of the distance calculation kernel at search time.
   * When zero, an optimal block size is selected using a heuristic.
   *
   * Possible values: [0, 256, 512, 1024]
   */
  uint32_t preferred_thread_block_size = 0;
};

static_assert(std::is_aggregate_v<index_params>);
static_assert(std::is_aggregate_v<search_params>);

/**
 * @brief IVF-PQ index.
 *
 * In the IVF-PQ index, a database vector y is approximated with two level quantization:
 *
 * y = Q_1(y) + Q_2(y - Q_1(y))
 *
 * The first level quantizer (Q_1), maps the vector y to the nearest cluster center. The number of
 * clusters is n_lists.
 *
 * The second quantizer encodes the residual, and it is defined as a product quantizer [1].
 *
 * A product quantizer encodes a `dim` dimensional vector with a `pq_dim` dimensional vector.
 * First we split the input vector into `pq_dim` subvectors (denoted by u), where each u vector
 * contains `pq_len` distinct components of y
 *
 * y_1, y_2, ... y_{pq_len}, y_{pq_len+1}, ... y_{2*pq_len}, ... y_{dim-pq_len+1} ... y_{dim}
 *  \___________________/     \____________________________/      \______________________/
 *         u_1                         u_2                          u_{pq_dim}
 *
 * Then each subvector encoded with a separate quantizer q_i, end the results are concatenated
 *
 * Q_2(y) = q_1(u_1),q_2(u_2),...,q_{pq_dim}(u_pq_dim})
 *
 * Each quantizer q_i outputs a code with pq_bit bits. The second level quantizers are also defined
 * by k-means clustering in the corresponding sub-space: the reproduction values are the centroids,
 * and the set of reproduction values is the codebook.
 *
 * When the data dimensionality `dim` is not multiple of `pq_dim`, the feature space is transformed
 * using a random orthogonal matrix to have `rot_dim = pq_dim * pq_len` dimensions
 * (`rot_dim >= dim`).
 *
 * The second-level quantizers are trained either for each subspace or for each cluster:
 *   (a) codebook_gen::PER_SUBSPACE:
 *         creates `pq_dim` second-level quantizers - one for each slice of the data along features;
 *   (b) codebook_gen::PER_CLUSTER:
 *         creates `n_lists` second-level quantizers - one for each first-level cluster.
 * In either case, the centroids are again found using k-means clustering interpreting the data as
 * having pq_len dimensions.
 *
 * [1] Product quantization for nearest neighbor search Herve Jegou, Matthijs Douze, Cordelia Schmid
 *
 * @tparam IdxT type of the indices in the source dataset
 *
 */
template <typename IdxT>
struct index : ann::index {
  static_assert(!raft::is_narrowing_v<uint32_t, IdxT>,
                "IdxT must be able to represent all values of uint32_t");

 public:
  /** Total length of the index. */
  [[nodiscard]] constexpr inline auto size() const noexcept -> IdxT { return indices_.extent(0); }
  /** Dimensionality of the input data. */
  [[nodiscard]] constexpr inline auto dim() const noexcept -> uint32_t { return dim_; }
  /**
   * Dimensionality of the cluster centers:
   * input data dim extended with vector norms and padded to 8 elems.
   */
  [[nodiscard]] constexpr inline auto dim_ext() const noexcept -> uint32_t
  {
    return raft::round_up_safe(dim() + 1, 8u);
  }
  /**
   * Dimensionality of the data after transforming it for PQ processing
   * (rotated and augmented to be muplitple of `pq_dim`).
   */
  [[nodiscard]] constexpr inline auto rot_dim() const noexcept -> uint32_t
  {
    return pq_len() * pq_dim();
  }
  /** The bit length of an encoded vector element after compression by PQ. */
  [[nodiscard]] constexpr inline auto pq_bits() const noexcept -> uint32_t { return pq_bits_; }
  /** The dimensionality of an encoded vector after compression by PQ. */
  [[nodiscard]] constexpr inline auto pq_dim() const noexcept -> uint32_t { return pq_dim_; }
  /** Dimensionality of a subspaces, i.e. the number of vector components mapped to a subspace */
  [[nodiscard]] constexpr inline auto pq_len() const noexcept -> uint32_t
  {
    return raft::div_rounding_up_unsafe(dim(), pq_dim());
  }
  /** The number of vectors in a PQ codebook (`1 << pq_bits`). */
  [[nodiscard]] constexpr inline auto pq_book_size() const noexcept -> uint32_t
  {
    return 1 << pq_bits();
  }
  /** Distance metric used for clustering. */
  [[nodiscard]] constexpr inline auto metric() const noexcept -> raft::distance::DistanceType
  {
    return metric_;
  }
  /** How PQ codebooks are created. */
  [[nodiscard]] constexpr inline auto codebook_kind() const noexcept -> codebook_gen
  {
    return codebook_kind_;
  }
  /** Number of clusters/inverted lists (first level quantization). */
  [[nodiscard]] constexpr inline auto n_lists() const noexcept -> uint32_t { return n_lists_; }
  /** Number of non-empty clusters/inverted lists. */
  [[nodiscard]] constexpr inline auto n_nonempty_lists() const noexcept -> uint32_t
  {
    return n_nonempty_lists_;
  }

  // Don't allow copying the index for performance reasons (try avoiding copying data)
  index(const index&) = delete;
  index(index&&)      = default;
  auto operator=(const index&) -> index& = delete;
  auto operator=(index&&) -> index& = default;
  ~index()                          = default;

  /** Construct an empty index. It needs to be trained and then populated. */
  index(const handle_t& handle,
        raft::distance::DistanceType metric,
        codebook_gen codebook_kind,
        uint32_t n_lists,
        uint32_t dim,
        uint32_t pq_bits          = 8,
        uint32_t pq_dim           = 0,
        uint32_t n_nonempty_lists = 0)
    : ann::index(),
      metric_(metric),
      codebook_kind_(codebook_kind),
      n_lists_(n_lists),
      dim_(dim),
      pq_bits_(pq_bits),
      pq_dim_(pq_dim == 0 ? calculate_pq_dim(dim) : pq_dim),
      n_nonempty_lists_(n_nonempty_lists),
      pq_centers_{make_device_mdarray<float>(handle, make_pq_centers_extents())},
      pq_dataset_{make_device_mdarray<uint8_t>(
        handle, make_extents<IdxT>(0, this->pq_dim() * this->pq_bits() / 8))},
      indices_{make_device_mdarray<IdxT>(handle, make_extents<IdxT>(0))},
      rotation_matrix_{
        make_device_mdarray<float>(handle, make_extents<uint32_t>(this->rot_dim(), this->dim()))},
      list_offsets_{make_device_mdarray<IdxT>(handle, make_extents<uint32_t>(this->n_lists() + 1))},
      centers_{make_device_mdarray<float>(
        handle, make_extents<uint32_t>(this->n_lists(), this->dim_ext()))},
      centers_rot_{make_device_mdarray<float>(
        handle, make_extents<uint32_t>(this->n_lists(), this->rot_dim()))}
  {
    check_consistency();
  }

  /** Construct an empty index. It needs to be trained and then populated. */
  index(const handle_t& handle,
        const index_params& params,
        uint32_t dim,
        uint32_t n_nonempty_lists = 0)
    : index(handle,
            params.metric,
            params.codebook_kind,
            params.n_lists,
            dim,
            params.pq_bits,
            params.pq_dim,
            n_nonempty_lists)
  {
  }

  /**
   * Replace the content of the index with new uninitialized mdarrays to hold the indicated amount
   * of data.
   */
  void allocate(const handle_t& handle, IdxT index_size)
  {
    pq_dataset_ =
      make_device_mdarray<uint8_t>(handle, make_extents<IdxT>(index_size, pq_dataset_.extent(1)));
    indices_ = make_device_mdarray<IdxT>(handle, make_extents<IdxT>(index_size));
    check_consistency();
  }

  /**
   * PQ cluster centers
   *
   *   - codebook_gen::PER_SUBSPACE: [pq_dim , pq_book_size, pq_len]
   *   - codebook_gen::PER_CLUSTER:  [n_lists, pq_book_size, pq_len]
   */
  inline auto pq_centers() noexcept -> device_mdspan<float, extent_3d<uint32_t>, row_major>
  {
    return pq_centers_.view();
  }
  [[nodiscard]] inline auto pq_centers() const noexcept
    -> device_mdspan<const float, extent_3d<uint32_t>, row_major>
  {
    return pq_centers_.view();
  }

  /** PQ-encoded data [size, pq_dim * pq_bits / 8]. */
  inline auto pq_dataset() noexcept -> device_mdspan<uint8_t, extent_2d<IdxT>, row_major>
  {
    return pq_dataset_.view();
  }
  [[nodiscard]] inline auto pq_dataset() const noexcept
    -> device_mdspan<const uint8_t, extent_2d<IdxT>, row_major>
  {
    return pq_dataset_.view();
  }

  /** Inverted list indices: ids of items in the source data [size] */
  inline auto indices() noexcept -> device_mdspan<IdxT, extent_1d<IdxT>, row_major>
  {
    return indices_.view();
  }
  [[nodiscard]] inline auto indices() const noexcept
    -> device_mdspan<const IdxT, extent_1d<IdxT>, row_major>
  {
    return indices_.view();
  }

  /** The transform matrix (original space -> rotated padded space) [rot_dim, dim] */
  inline auto rotation_matrix() noexcept -> device_mdspan<float, extent_2d<uint32_t>, row_major>
  {
    return rotation_matrix_.view();
  }
  [[nodiscard]] inline auto rotation_matrix() const noexcept
    -> device_mdspan<const float, extent_2d<uint32_t>, row_major>
  {
    return rotation_matrix_.view();
  }

  /**
   * Offsets into the lists [n_lists + 1].
   * The last value contains the total length of the index.
   */
  inline auto list_offsets() noexcept -> device_mdspan<IdxT, extent_1d<uint32_t>, row_major>
  {
    return list_offsets_.view();
  }
  [[nodiscard]] inline auto list_offsets() const noexcept
    -> device_mdspan<const IdxT, extent_1d<uint32_t>, row_major>
  {
    return list_offsets_.view();
  }

  /** Cluster centers corresponding to the lists in the original space [n_lists, dim_ext] */
  inline auto centers() noexcept -> device_mdspan<float, extent_2d<uint32_t>, row_major>
  {
    return centers_.view();
  }
  [[nodiscard]] inline auto centers() const noexcept
    -> device_mdspan<const float, extent_2d<uint32_t>, row_major>
  {
    return centers_.view();
  }

  /** Cluster centers corresponding to the lists in the rotated space [n_lists, rot_dim] */
  inline auto centers_rot() noexcept -> device_mdspan<float, extent_2d<uint32_t>, row_major>
  {
    return centers_rot_.view();
  }
  [[nodiscard]] inline auto centers_rot() const noexcept
    -> device_mdspan<const float, extent_2d<uint32_t>, row_major>
  {
    return centers_rot_.view();
  }

 private:
  raft::distance::DistanceType metric_;
  codebook_gen codebook_kind_;
  uint32_t n_lists_;
  uint32_t dim_;
  uint32_t pq_bits_;
  uint32_t pq_dim_;
  uint32_t n_nonempty_lists_;

  device_mdarray<float, extent_3d<uint32_t>, row_major> pq_centers_;
  device_mdarray<uint8_t, extent_2d<IdxT>, row_major> pq_dataset_;
  device_mdarray<IdxT, extent_1d<IdxT>, row_major> indices_;
  device_mdarray<float, extent_2d<uint32_t>, row_major> rotation_matrix_;
  device_mdarray<IdxT, extent_1d<uint32_t>, row_major> list_offsets_;
  device_mdarray<float, extent_2d<uint32_t>, row_major> centers_;
  device_mdarray<float, extent_2d<uint32_t>, row_major> centers_rot_;

  /** Throw an error if the index content is inconsistent. */
  void check_consistency()
  {
    RAFT_EXPECTS(pq_bits() >= 4 && pq_bits() <= 8,
                 "`pq_bits` must be within closed range [4,8], but got %u.",
                 pq_bits());
    RAFT_EXPECTS((pq_bits() * pq_dim()) % 8 == 0,
                 "`pq_bits * pq_dim` must be a multiple of 8, but got %u * %u = %u.",
                 pq_bits(),
                 pq_dim(),
                 pq_bits() * pq_dim());
  }

  auto make_pq_centers_extents() -> extent_3d<uint32_t>
  {
    switch (codebook_kind()) {
      case codebook_gen::PER_SUBSPACE:
        return make_extents<uint32_t>(pq_dim(), pq_book_size(), pq_len());
      case codebook_gen::PER_CLUSTER:
        return make_extents<uint32_t>(n_lists(), pq_book_size(), pq_len());
      default: RAFT_FAIL("Unreachable code");
    }
  }

  static inline auto calculate_pq_dim(uint32_t dim) -> uint32_t
  {
    // If the dimensionality is large enough, we can reduce it to improve performance
    if (dim >= 128) { dim /= 2; }
    // Round it down to 32 to improve performance.
    uint32_t r = raft::round_down_safe<uint32_t>(dim, 32);
    if (r > 0) return r;
    // If the dimensionality is really low, round it to the closest power-of-two
    r = 1;
    while ((r << 1) <= dim) {
      r = r << 1;
    }
    return r;
  }
};

}  // namespace raft::neighbors::ivf_pq
