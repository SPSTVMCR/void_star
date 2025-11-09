#pragma once
// Tuning defaults for Mimir mode. UI can override min/max at runtime.

// Output brightness range when Mimir is active (0..255)
#ifndef MIMIR_BRIGHT_MIN
#define MIMIR_BRIGHT_MIN 20
#endif

#ifndef MIMIR_BRIGHT_MAX
#define MIMIR_BRIGHT_MAX 220
#endif

// Nonlinear response: t' = t^gamma (0<gamma<1 boosts low-light sensitivity)
#ifndef MIMIR_GAMMA
#define MIMIR_GAMMA 0.7f
#endif

// Ignore tiny changes to avoid flicker; minimum step to apply (in brightness units)
#ifndef MIMIR_MIN_STEP
#define MIMIR_MIN_STEP 3
#endif

// Smoothing speed while Mimir is ON (0..1). Higher = faster.
#ifndef MIMIR_ALPHA
#define MIMIR_ALPHA 0.25f
#endif