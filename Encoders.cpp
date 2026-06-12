#include <Encoder.h>
#include "Encoders.h"

EncodersClass Encoders;

static Encoder enc1(ENC1_A, ENC1_B);
static Encoder enc2(ENC2_A, ENC2_B);
static Encoder enc3(ENC3_A, ENC3_B);
static Encoder enc4(ENC4_A, ENC4_B);

static Encoder* const encs[4] = { &enc1, &enc2, &enc3, &enc4 };

void EncodersClass::begin() {
  for (uint8_t i = 0; i < 4; i++) {
    encs[i]->write(0);
    residual[i] = 0;
  }
}

int EncodersClass::detentDelta(uint8_t idx) {
  if (idx >= 4) return 0;
  residual[idx] += encs[idx]->readAndReset();
  int detents = residual[idx] / 4;       // 4 quadrature counts per detent
  residual[idx] -= (long)detents * 4;
  return detents;
}
