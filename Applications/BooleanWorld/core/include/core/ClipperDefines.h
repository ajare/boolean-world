#pragma once

// This value must not be greater than Clipper2's internal precision, which is 10^8
#define BW_CLIPPER_SCALE 1000.0

#ifdef USINGZ
#define BW_CLIPPER_MAKE_POINT(x, y, z) Clipper2Lib::Point64(static_cast<int64_t>(std::llround(x * BW_CLIPPER_SCALE)), static_cast<int64_t>(std::llround(y * BW_CLIPPER_SCALE)), z)
#else
#define BW_CLIPPER_MAKE_POINT(x, y) Clipper2Lib::Point64(static_cast<int64_t>(std::llround(x * BW_CLIPPER_SCALE)), static_cast<int64_t>(std::llround(y * BW_CLIPPER_SCALE)))
#endif
