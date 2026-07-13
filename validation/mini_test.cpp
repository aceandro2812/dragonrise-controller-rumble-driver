#include <stdio.h>
#include "effect_map.h"
int main() {
  printf("hello\n");
  FfbEffectParams p{};
  p.klass = FfbEffectClass::Constant;
  p.magnitude = 8000;
  p.effectGain = 10000;
  p.deviceGain = 10000;
  p.durationUs = 1000000;
  auto s = FfbSampleMotors(p, 0);
  printf("LF=%u HF=%u\n", s.lowFreq, s.highFreq);
  return 0;
}
