#include "SampleStore.h"
#include "samples/kit_default.h"
#include <string.h>

SampleStoreClass SampleStore;

static const Sample EMPTY_SAMPLE;

void SampleStoreClass::begin() {
  for (uint8_t i = 0; i < NUM_SAMPLE_SLOTS; i++) {
    slots[i] = SampleSlot();
  }
  // Install the PROGMEM kit (kick/snare/hat/clap) into the first slots.
  uint8_t n = PROGMEM_KIT_COUNT < NUM_SAMPLE_SLOTS ? PROGMEM_KIT_COUNT
                                                   : NUM_SAMPLE_SLOTS;
  for (uint8_t i = 0; i < n; i++) {
    slots[i].sample.data       = PROGMEM_KIT[i].data;
    slots[i].sample.length     = PROGMEM_KIT[i].length;
    slots[i].sample.sampleRate = PROGMEM_KIT[i].sampleRate;
    slots[i].source            = SRC_PROGMEM;
    strlcpy(slots[i].name, PROGMEM_KIT[i].name, sizeof(slots[i].name));
  }
}

const Sample& SampleStoreClass::get(uint8_t slot) const {
  if (slot >= NUM_SAMPLE_SLOTS || slots[slot].source == SRC_NONE)
    return EMPTY_SAMPLE;
  return slots[slot].sample;
}

const char* SampleStoreClass::name(uint8_t slot) const {
  if (slot >= NUM_SAMPLE_SLOTS) return "?";
  return slots[slot].name;
}

SampleSource SampleStoreClass::source(uint8_t slot) const {
  if (slot >= NUM_SAMPLE_SLOTS) return SRC_NONE;
  return slots[slot].source;
}

void SampleStoreClass::assignRam(uint8_t slot, const int16_t* data,
                                 uint32_t length, uint32_t sampleRate,
                                 const char* nm) {
  if (slot >= NUM_SAMPLE_SLOTS) return;
  slots[slot].sample.data       = data;
  slots[slot].sample.length     = length;
  slots[slot].sample.sampleRate = sampleRate;
  slots[slot].source            = SRC_RAM;
  strlcpy(slots[slot].name, nm, sizeof(slots[slot].name));
}

void SampleStoreClass::resetSlot(uint8_t slot) {
  if (slot >= NUM_SAMPLE_SLOTS) return;
  if (slot < PROGMEM_KIT_COUNT) {
    slots[slot].sample.data       = PROGMEM_KIT[slot].data;
    slots[slot].sample.length     = PROGMEM_KIT[slot].length;
    slots[slot].sample.sampleRate = PROGMEM_KIT[slot].sampleRate;
    slots[slot].source            = SRC_PROGMEM;
    strlcpy(slots[slot].name, PROGMEM_KIT[slot].name, sizeof(slots[slot].name));
  } else {
    slots[slot] = SampleSlot();
  }
}
