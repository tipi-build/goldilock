// Copyright 2024 Yannic Staudt, tipi technologies Ltd and the goldilock contributors
// SPDX-License-Identifier: GPL-2.0-only OR Proprietary
#pragma once

#include <random>
#include <chrono>

namespace tipi::goldilock::random {

  template<typename Rep>
  inline Rep random_in_range(Rep lower, Rep upper) {
    static_assert(std::is_integral<Rep>::value, "Integral required.");

    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<Rep> dist(lower, upper); 
    return dist(gen);
  }

  template<typename Rep, typename Period>
  inline std::chrono::duration<Rep, Period> random_sleep_duration(std::chrono::duration<Rep, Period> min, std::chrono::duration<Rep, Period> max) {
    std::chrono::duration<Rep, Period> result(random_in_range(min.count(), max.count()));
    return result;
  }

}



    