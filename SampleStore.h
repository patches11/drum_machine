#ifndef SAMPLESTORE_H
#define SAMPLESTORE_H

// SampleStore — the only module that knows where sample bytes live.
//
// Slot table unifying three sources behind one Sample descriptor
// (resampling_player_spec.md §1 — flash and RAM read identically on
// Teensy 3.6 because flash is memory-mapped):
//   * PROGMEM default kit (always present, boot default, zero RAM)
//   * SD-loaded kits  -> RAM pool   (M9)
//   * mic recordings  -> RAM pool   (M7)

#include <stdint.h>
#include "Config.h"

struct Sample {
  const int16_t* data       = nullptr;  // mono PCM (flash or RAM)
  uint32_t       length     = 0;        // in samples
  uint32_t       sampleRate = 44100;    // source rate; player corrects pitch
};

enum SampleSource : uint8_t { SRC_NONE = 0, SRC_PROGMEM, SRC_RAM };

struct SampleSlot {
  Sample       sample;
  SampleSource source = SRC_NONE;
  char         name[12] = "";
};

class SampleStoreClass {
public:
  void begin();   // install the PROGMEM kit into slots 0..N-1

  const Sample& get(uint8_t slot) const;
  const char*   name(uint8_t slot) const;
  SampleSource  source(uint8_t slot) const;
  uint8_t       slotCount() const { return NUM_SAMPLE_SLOTS; }
  uint8_t       progmemCount() const;   // PROGMEM kit size; record slots follow

  // M7/M9: point a slot at RAM-pool data. Caller must stop any voice
  // playing this slot first (AudioEngine::stopVoicesUsing).
  void assignRam(uint8_t slot, const int16_t* data, uint32_t length,
                 uint32_t sampleRate, const char* name);

  // Repoint a slot back at its PROGMEM default (if it has one).
  void resetSlot(uint8_t slot);

private:
  SampleSlot slots[NUM_SAMPLE_SLOTS];
};

extern SampleStoreClass SampleStore;

#endif // SAMPLESTORE_H
