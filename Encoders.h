#ifndef ENCODERS_H
#define ENCODERS_H

// Encoders — 4x quadrature encoder wrapper.
//
// detentDelta(i) returns whole detents turned since the last call
// (quadrature counts / 4), so callers see clean -2,-1,0,+1,+2 values.
// Per-mode bindings and acceleration arrive at M5; this layer stays raw.

#include <stdint.h>
#include "Config.h"

class EncodersClass {
public:
  void begin();

  // Whole detents since last call for encoder 0..3. Drains the counter.
  int detentDelta(uint8_t idx);

private:
  long residual[4] = {0, 0, 0, 0};
};

extern EncodersClass Encoders;

#endif // ENCODERS_H
