// SPDX-License-Identifier: MIT
#pragma once

namespace toggle { 

inline constexpr auto max = [](auto a, auto b) { return a < b ? b : a; };
inline constexpr auto min = [](auto a, auto b) { return a < b ? a : b; };
inline constexpr auto sum = [](auto a, auto b) { return a + b; };
inline constexpr auto mul = [](auto a, auto b) { return a * b; };

} // namespace toggle
