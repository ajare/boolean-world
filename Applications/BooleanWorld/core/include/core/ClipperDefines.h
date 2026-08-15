#pragma once

#define BW_CLIPPER_LERP_PROPERTIES 0x0001
#define BW_CLIPPER_SET_PRIMITIVE 0x0002
#define BW_CLIPPER_GEN_INTER_ON_UNION 0x0004

// This value must not be greater than Clipper2's internal precision, which is 10^8
#define BW_CLIPPER_SCALE 1000.0

#ifdef USINGZ
#define BW_CLIPPER_MAKE_POINT(x, y, z) Clipper2Lib::Point64(static_cast<int64_t>(std::llround(x * BW_CLIPPER_SCALE)), static_cast<int64_t>(std::llround(y * BW_CLIPPER_SCALE)), z)
#else
#define BW_CLIPPER_MAKE_POINT(x, y) Clipper2Lib::Point64(static_cast<int64_t>(std::llround(x * BW_CLIPPER_SCALE)), static_cast<int64_t>(std::llround(y * BW_CLIPPER_SCALE)))
#endif
