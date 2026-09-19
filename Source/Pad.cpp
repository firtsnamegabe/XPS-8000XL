#include "Pad.h"

// Pad is currently header-only (POD-style struct). This translation unit
// exists so future non-trivial pad logic (e.g. normalize, trim-silence,
// bounce) has an obvious home without restructuring the CMake target.
