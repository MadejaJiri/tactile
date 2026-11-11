#pragma once

#include <chrono>

// One clock for samples and for the chart that scrolls them, unaffected by
// the wall clock being set.
inline double monotonicSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
