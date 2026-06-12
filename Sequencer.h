#ifndef SEQUENCER_H
#define SEQUENCER_H

// Sequencer — transport facade over the lookahead Scheduler.
//
// M2..M5 this module fired steps directly on the grid. At M6 the Scheduler
// (scheduler_spec.md) took over the entire timing/firing path: steps are
// scheduled ahead into an event queue with swing/push-pull/nudge/humanize
// offsets. This facade keeps the public transport API stable for Controls,
// Display and the main sketch.

#include <stdint.h>
#include "Config.h"
#include "Clock.h"
#include "Scheduler.h"

class SequencerClass {
public:
  void begin(const ClockSource* clk) { Scheduler.begin(clk); }

  void start()  { Scheduler.start(); }
  void stop()   { Scheduler.stop(); }
  void toggle() { isRunning() ? stop() : start(); }
  bool isRunning() const { return Scheduler.isRunning(); }

  // Call every loop(). Non-blocking. Returns true when the visual grid
  // step advanced (callers use this to mark the LEDs dirty).
  bool tick() { return Scheduler.tick(); }

  // Grid-timeline playhead for the LEDs (audio rides the offset timeline).
  uint8_t currentStep() const { return Scheduler.currentGridStep(); }

  // µs until the next on-time obligation (queued event or grid step) —
  // the OLED redraw gate (a ~23 ms blocking transfer must fit before it).
  uint32_t timeToNextStepUs() const { return Scheduler.timeToNextFireUs(); }
};

extern SequencerClass Sequencer;

#endif // SEQUENCER_H
