/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef KAGOME_SCALE_TIE_HPP
#define KAGOME_SCALE_TIE_HPP

#include <tuple>

#define SCALE_TIE(N)                                                    \
  static constexpr size_t scale_tie = N;                                \
  template <typename T>                                                 \
  bool operator==(const T &r) const {                                   \
    using ThisT = std::decay_t<decltype(*this)>;                        \
    using ExtT = std::decay_t<T>;                                       \
    static_assert(                                                      \
        std::is_same_v<ExtT, ThisT> || std::is_base_of_v<ThisT, ExtT>); \
    return ::scale::as_tie(*this, [&](auto l) {                         \
      return ::scale::as_tie(r, [&](auto r) { return l == r; });        \
    });                                                                 \
  }                                                                     \
  template <typename T>                                                 \
  bool operator!=(const T &r) const {                                   \
    return !operator==(r);                                              \
  }

namespace scale {
  class ScaleEncoderStream;
  class ScaleDecoderStream;

  // generated by housekeeping/scale_tie.py
  template <typename T,
            typename F,
            size_t N = std::remove_reference_t<T>::scale_tie>
  auto as_tie(T &&v, F &&f) {
    if constexpr (N == 1) {
      auto &[v0] = v;
      return std::forward<F>(f)(std::tie(v0));
    } else if constexpr (N == 2) {
      auto &[v0, v1] = v;
      return std::forward<F>(f)(std::tie(v0, v1));
    } else if constexpr (N == 3) {
      auto &[v0, v1, v2] = v;
      return std::forward<F>(f)(std::tie(v0, v1, v2));
    } else if constexpr (N == 4) {
      auto &[v0, v1, v2, v3] = v;
      return std::forward<F>(f)(std::tie(v0, v1, v2, v3));
    } else if constexpr (N == 5) {
      auto &[v0, v1, v2, v3, v4] = v;
      return std::forward<F>(f)(std::tie(v0, v1, v2, v3, v4));
    } else if constexpr (N == 6) {
      auto &[v0, v1, v2, v3, v4, v5] = v;
      return std::forward<F>(f)(std::tie(v0, v1, v2, v3, v4, v5));
    } else if constexpr (N == 7) {
      auto &[v0, v1, v2, v3, v4, v5, v6] = v;
      return std::forward<F>(f)(std::tie(v0, v1, v2, v3, v4, v5, v6));
    } else if constexpr (N == 8) {
      auto &[v0, v1, v2, v3, v4, v5, v6, v7] = v;
      return std::forward<F>(f)(std::tie(v0, v1, v2, v3, v4, v5, v6, v7));
    } else if constexpr (N == 9) {
      auto &[v0, v1, v2, v3, v4, v5, v6, v7, v8] = v;
      return std::forward<F>(f)(std::tie(v0, v1, v2, v3, v4, v5, v6, v7, v8));
    } else if constexpr (N == 10) {
      auto &[v0, v1, v2, v3, v4, v5, v6, v7, v8, v9] = v;
      return std::forward<F>(f)(
          std::tie(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9));
    } else {
      // generate code for bigger tuples
      static_assert(1 <= N && N <= 10);
    }
  }

  template <typename T, size_t = T::scale_tie>
  ScaleEncoderStream &operator<<(ScaleEncoderStream &s, const T &v) {
    as_tie(v, [&](auto v) {
      std::apply([&](const auto &...v) { (..., (s << v)); }, v);
    });
    return s;
  }

  template <typename T, size_t = T::scale_tie>
  ScaleDecoderStream &operator>>(ScaleDecoderStream &s, T &v) {
    as_tie(v, [&](auto v) {
      std::apply([&](auto &...v) { (..., (s >> v)); }, v);
    });
    return s;
  }
}  // namespace scale

#endif  // KAGOME_SCALE_TIE_HPP
