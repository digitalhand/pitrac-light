/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Compatibility shim for libcamera::Span.
 *
 * libcamera >= 0.3 removed <libcamera/base/span.h> and expects callers to
 * use std::span directly (C++20). Older versions still ship the header and
 * define libcamera::Span as their own type.
 *
 * The Meson build sets LIBCAMERA_HAS_BASE_SPAN=1 when the old header exists,
 * 0 otherwise. This header provides a libcamera::Span alias in either case.
 */

#pragma once

#if defined(LIBCAMERA_HAS_BASE_SPAN) && LIBCAMERA_HAS_BASE_SPAN
#include <libcamera/base/span.h>
#else
#include <cstddef>
#include <span>
namespace libcamera {
template <typename T, std::size_t E = std::dynamic_extent>
using Span = std::span<T, E>;
} // namespace libcamera
#endif
