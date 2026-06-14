#ifndef STORAGE_H
#define STORAGE_H

// Storage — SD + EEPROM persistence (M9, plan Phase 9).
//
// What lives where:
//   EEPROM             GlobalState + the song chain (entries/count; M10) in
//                      a versioned blob: magic + version + structs + checksum.
//                      Auto-saved (debounced) whenever either changes.
//   /PATTERNS/PATn.DAT one file per pattern slot (all 8 saved/loaded
//                      together, M10), incl. all per-voice settings —
//                      versioned binary, trailing checksum. A truncated or
//                      corrupt file (card yank mid-save) simply fails
//                      validation and is ignored: that slot keeps its
//                      defaults, never hangs, never loads garbage.
//   /SAMPLES/RECn.WAV  mic recordings as standard mono 16-bit WAV — also
//                      playable/replaceable on a PC. At boot they're read
//                      back into the Sampler pool partitions and the
//                      SampleStore slots repointed (this is the SD-loading
//                      path too: drop any mono 16-bit WAV <= ~0.74 s in as
//                      RECn.WAV and it becomes a kit sound).
//
// Boot contract (plan §"Dual PROGMEM/SD/mic sample story"): PROGMEM kit
// first, SD probe second, EEPROM globals — the machine ALWAYS boots with
// sounds; no SD card = no saves, nothing else changes.
//
// Call order: loadGlobals() right after appStateInit() (AudioEngine.begin
// reads volume/micGain from globalState); begin() after SampleStore,
// AudioEngine and Sampler are up.

#include <stdint.h>
#include "Config.h"

class StorageClass {
public:
  void loadGlobals();    // EEPROM -> globalState; before AudioEngine.begin()
  void begin();          // SD probe; load RECn.WAV + current pattern file
  void update();         // debounced EEPROM autosave; call every loop

  bool sdPresent() const { return sdOk; }

  bool savePatterns();   // ALL pattern slots -> SD (also flushes globals)
  bool loadPatterns();   // all slots from SD (validated), then re-enter current
  void saveGlobalsNow(); // immediate EEPROM write (if changed)

  // Called by Sampler after a finished take. No-op without SD.
  bool saveRecording(uint8_t recSlot, const int16_t* data,
                     uint32_t length, uint32_t sampleRate);

private:
  void loadRecordings();

  bool          sdOk = false;
  unsigned long lastEepromCheckMs = 0;
};

extern StorageClass Storage;

#endif // STORAGE_H
