#include <Arduino.h>
#include <SD.h>
#include <EEPROM.h>
#include <string.h>
#include "Storage.h"
#include "AppState.h"
#include "SampleStore.h"
#include "AudioEngine.h"
#include "Sampler.h"
#include "Display.h"

StorageClass Storage;

// ---------------------------------------------------------------------------
// Formats

static const uint32_t PATTERN_MAGIC        = 0x504D5244;  // "DRMP" LE
static const uint8_t  PATTERN_FILE_VERSION = 1;

struct PatternFileHeader {
  uint32_t magic;
  uint8_t  version;
  uint8_t  patternIndex;
  uint16_t size;          // sizeof(Pattern) — catches struct drift
};

struct GlobalsBlob {      // EEPROM image at address 0
  uint16_t    magic;      // EEPROM_MAGIC
  uint8_t     version;    // EEPROM_VERSION (v2 added ch — old blobs rejected)
  GlobalState g;
  ChainState  ch;         // song chain survives a power-cycle (M10)
  uint8_t     csum;       // XOR of g's and ch's bytes
};

static const int           EEPROM_ADDR_GLOBALS = 0;
static const unsigned long EEPROM_CHECK_MS     = 5000;  // wear-friendly

static uint8_t xorSum(const void* p, size_t n) {
  const uint8_t* b = (const uint8_t*)p;
  uint8_t c = 0;
  while (n--) c ^= *b++;
  return c;
}

// Snapshot of what's in EEPROM, to detect changes cheaply.
static GlobalState savedGlobals;
static ChainState  savedChain;

// ---------------------------------------------------------------------------
// EEPROM globals

void StorageClass::loadGlobals() {
  GlobalsBlob b;
  EEPROM.get(EEPROM_ADDR_GLOBALS, b);
  uint8_t csum = xorSum(&b.g, sizeof(GlobalState)) ^
                 xorSum(&b.ch, sizeof(ChainState));
  if (b.magic != EEPROM_MAGIC || b.version != EEPROM_VERSION ||
      b.csum != csum) {
    Serial.println("EEPROM: no valid globals - using defaults");
    savedGlobals = globalState;
    savedChain   = chain;
    return;
  }
  globalState = b.g;
  chain       = b.ch;

  // sanitize: a kid's machine never boots into a broken state
  if (globalState.bpm < 40 || globalState.bpm > 240) globalState.bpm = 120;
  if (globalState.swing < 50 || globalState.swing > 75) globalState.swing = 50;
  if (globalState.swingSubdiv != 8 && globalState.swingSubdiv != 16)
    globalState.swingSubdiv = 16;
  if (globalState.accentBoost > 50) globalState.accentBoost = 27;
  if (globalState.humanize > 15)    globalState.humanize = 0;
  if (globalState.currentPattern >= NUM_PATTERN_SLOTS)
    globalState.currentPattern = 0;
  if (globalState.volume < 1 || globalState.volume > 10) globalState.volume = 6;
  if (globalState.micGain > 63)          globalState.micGain = 36;
  if (globalState.masterFilterCut > 127) globalState.masterFilterCut = 127;
  if (globalState.reverbSend > 127)      globalState.reverbSend = 0;

  // sanitize the chain too; never boot mid-song
  if (chain.count > CHAIN_MAX) chain.count = 0;
  for (uint8_t i = 0; i < chain.count; i++)
    if (chain.entries[i] >= NUM_PATTERN_SLOTS) chain.entries[i] = 0;
  chain.pos    = 0;
  chain.active = false;

  savedGlobals = globalState;
  savedChain   = chain;
  Serial.println("EEPROM: globals restored");
}

void StorageClass::saveGlobalsNow() {
  // pos/active are runtime state (pos advances every bar) — normalize them
  // out so chain playback doesn't churn EEPROM bytes
  ChainState chSnap = chain;
  chSnap.pos    = 0;
  chSnap.active = false;
  if (memcmp(&globalState, &savedGlobals, sizeof(GlobalState)) == 0 &&
      memcmp(&chSnap, &savedChain, sizeof(ChainState)) == 0) return;
  GlobalsBlob b;
  b.magic   = EEPROM_MAGIC;
  b.version = EEPROM_VERSION;
  b.g       = globalState;
  b.ch      = chSnap;
  b.csum    = xorSum(&b.g, sizeof(GlobalState)) ^
              xorSum(&b.ch, sizeof(ChainState));
  EEPROM.put(EEPROM_ADDR_GLOBALS, b);   // put() only rewrites changed bytes
  savedGlobals = globalState;
  savedChain   = chSnap;
}

void StorageClass::update() {
  // a running chain flips currentPattern every bar — defer the autosave
  // until the song stops rather than rewrite that byte every 5 s
  if (chain.active) return;
  unsigned long now = millis();
  if (now - lastEepromCheckMs < EEPROM_CHECK_MS) return;
  lastEepromCheckMs = now;
  saveGlobalsNow();                     // no-op when nothing changed
}

// ---------------------------------------------------------------------------
// SD bring-up + boot loading

void StorageClass::begin() {
  sdOk = SD.begin(BUILTIN_SDCARD);
  if (!sdOk) {
    Serial.println("SD: no card - saves disabled, PROGMEM kit as usual");
    return;
  }
  Serial.println("SD: ok");
  loadRecordings();
  loadPatterns();   // silently skips absent/corrupt slot files
}

// ---------------------------------------------------------------------------
// Pattern files (versioned binary + trailing checksum)

static void patternPath(char* buf, size_t n, uint8_t slot) {
  snprintf(buf, n, "/PATTERNS/PAT%u.DAT", slot);
}

// Whatever was on the card must not crash the machine.
static void sanitizePattern(Pattern& p) {
  if (p.length < 1 || p.length > NUM_STEPS) p.length = NUM_STEPS;
  for (uint8_t v = 0; v < NUM_VOICES; v++) {
    Voice& vc = p.voices[v];
    if (vc.sampleId >= NUM_SAMPLE_SLOTS ||
        SampleStore.source(vc.sampleId) == SRC_NONE)
      vc.sampleId = v;                       // back to the default kit sound
    if (vc.rootSemis < -24) vc.rootSemis = -24;
    if (vc.rootSemis >  24) vc.rootSemis =  24;
    if (vc.fineCents < -100) vc.fineCents = -100;
    if (vc.fineCents >  100) vc.fineCents =  100;
    if (vc.level > 127)     vc.level = 127;
    if (vc.decay > 127)     vc.decay = 127;
    if (vc.filterCut > 127) vc.filterCut = 127;
    if (vc.pushPull < -20)  vc.pushPull = -20;
    if (vc.pushPull >  20)  vc.pushPull =  20;
    for (uint8_t s = 0; s < NUM_STEPS; s++) {
      Step& st = vc.steps[s];
      if (st.velocity > 127) st.velocity = 127;
      if (st.pitch < -24) st.pitch = -24;
      if (st.pitch >  24) st.pitch =  24;
      if (st.nudge < -20) st.nudge = -20;
      if (st.nudge >  20) st.nudge =  20;
    }
  }
}

// One slot -> one file (kept separate so a card yank corrupts at most the
// slot being written, never the whole bank).
static bool savePatternSlot(uint8_t slot) {
  char path[24];
  patternPath(path, sizeof(path), slot);
  SD.remove(path);                           // FILE_WRITE appends otherwise
  File f = SD.open(path, FILE_WRITE);
  if (!f) return false;
  PatternFileHeader h;
  h.magic        = PATTERN_MAGIC;
  h.version      = PATTERN_FILE_VERSION;
  h.patternIndex = slot;
  h.size         = sizeof(Pattern);
  const Pattern& p = patternBank[slot];
  uint8_t csum = xorSum(&p, sizeof(Pattern));
  bool ok = f.write((const uint8_t*)&h, sizeof(h)) == sizeof(h)
         && f.write((const uint8_t*)&p, sizeof(Pattern)) == sizeof(Pattern)
         && f.write(&csum, 1) == 1;
  f.close();
  return ok;
}

static bool loadPatternSlot(uint8_t slot) {
  char path[24];
  patternPath(path, sizeof(path), slot);
  File f = SD.open(path, FILE_READ);
  if (!f) return false;                      // nothing saved yet — fine

  PatternFileHeader h;
  Pattern tmp;
  uint8_t csum = 0;
  bool ok = f.read(&h, sizeof(h)) == (int)sizeof(h)
         && h.magic == PATTERN_MAGIC
         && h.version == PATTERN_FILE_VERSION
         && h.size == sizeof(Pattern)
         && f.read(&tmp, sizeof(tmp)) == (int)sizeof(tmp)
         && f.read(&csum, 1) == 1
         && csum == xorSum(&tmp, sizeof(tmp));
  f.close();

  if (!ok) {
    Serial.print(path);
    Serial.println(": invalid/corrupt - ignored");
    return false;
  }

  sanitizePattern(tmp);
  patternBank[slot] = tmp;
  return true;
}

bool StorageClass::savePatterns() {
  patternBankStore();                        // working edits -> current slot
  saveGlobalsNow();                          // one button persists everything
  if (!sdOk) {
    Display.flash("no SD");
    return false;
  }
  SD.mkdir("/PATTERNS");
  bool ok = true;
  for (uint8_t s = 0; s < NUM_PATTERN_SLOTS; s++)
    if (!savePatternSlot(s)) ok = false;
  Display.flash(ok ? "saved" : "SD error");
  return ok;
}

bool StorageClass::loadPatterns() {
  if (!sdOk) return false;
  uint8_t loaded = 0;
  for (uint8_t s = 0; s < NUM_PATTERN_SLOTS; s++)
    if (loadPatternSlot(s)) loaded++;
  if (loaded == 0) return false;

  // re-enter the current slot from the (re)loaded bank
  patternBankLoad(globalState.currentPattern);
  for (uint8_t v = 0; v < NUM_VOICES; v++) AudioEngine.applyVoiceParams(v);
  Serial.print("SD: patterns loaded (");
  Serial.print(loaded);
  Serial.print("/");
  Serial.print(NUM_PATTERN_SLOTS);
  Serial.println(")");
  return true;
}

// ---------------------------------------------------------------------------
// Recorded samples as WAV (PC-compatible; also the SD sample-loading path)

static void wavPath(char* buf, size_t n, uint8_t recSlot) {
  snprintf(buf, n, "/SAMPLES/REC%u.WAV", recSlot + 1);
}

static void put16(uint8_t* p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void put32(uint8_t* p, uint32_t v) {
  p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}
static uint16_t get16(const uint8_t* p) { return p[0] | (p[1] << 8); }
static uint32_t get32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void writeWavHeader(File& f, uint32_t samples, uint32_t rate) {
  uint32_t dataBytes = samples * 2;
  uint8_t h[44];
  memcpy(h, "RIFF", 4);      put32(h + 4, 36 + dataBytes);
  memcpy(h + 8, "WAVEfmt ", 8);
  put32(h + 16, 16);         // fmt chunk size
  put16(h + 20, 1);          // PCM
  put16(h + 22, 1);          // mono
  put32(h + 24, rate);
  put32(h + 28, rate * 2);   // byte rate
  put16(h + 32, 2);          // block align
  put16(h + 34, 16);         // bits per sample
  memcpy(h + 36, "data", 4); put32(h + 40, dataBytes);
  f.write(h, 44);
}

// Minimal chunk-walking parser: mono 16-bit PCM only, capped at `cap`
// samples. Anything else is rejected cleanly (the machine never hangs
// on a weird file someone copied onto the card).
static bool readWav(File& f, int16_t* dst, uint32_t cap,
                    uint32_t* outLen, uint32_t* outRate) {
  uint8_t hdr[12];
  if (f.read(hdr, 12) != 12) return false;
  if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0)
    return false;

  bool     haveFmt = false;
  uint16_t fmt = 0, channels = 0, bits = 0;
  uint32_t rate = 0;

  while (f.available() >= 8) {
    uint8_t ch[8];
    if (f.read(ch, 8) != 8) return false;
    uint32_t sz = get32(ch + 4);

    if (memcmp(ch, "fmt ", 4) == 0) {
      uint8_t fb[16];
      if (sz < 16 || f.read(fb, 16) != 16) return false;
      fmt      = get16(fb);
      channels = get16(fb + 2);
      rate     = get32(fb + 4);
      bits     = get16(fb + 14);
      if (sz > 16) f.seek(f.position() + (sz - 16));
      haveFmt = true;
    } else if (memcmp(ch, "data", 4) == 0) {
      if (!haveFmt || fmt != 1 || channels != 1 || bits != 16 ||
          rate < 8000 || rate > 48000)
        return false;
      uint32_t n = sz / 2;
      if (n > cap) n = cap;                  // longer files: keep what fits
      if (f.read((uint8_t*)dst, n * 2) != (int)(n * 2)) return false;
      *outLen  = n;
      *outRate = rate;
      return n >= 32;
    } else {
      f.seek(f.position() + sz + (sz & 1)); // skip unknown chunk (+pad)
    }
  }
  return false;
}

bool StorageClass::saveRecording(uint8_t recSlot, const int16_t* data,
                                 uint32_t length, uint32_t sampleRate) {
  if (!sdOk || recSlot >= NUM_RECORD_SLOTS) return false;
  SD.mkdir("/SAMPLES");
  char path[24];
  wavPath(path, sizeof(path), recSlot);
  SD.remove(path);
  File f = SD.open(path, FILE_WRITE);
  if (!f) {
    Serial.println("SD: could not write recording");
    return false;
  }
  writeWavHeader(f, length, sampleRate);
  bool ok = f.write((const uint8_t*)data, length * 2) == length * 2;
  f.close();
  Serial.print(ok ? "SD: saved " : "SD: write failed ");
  Serial.println(path);
  return ok;
}

void StorageClass::loadRecordings() {
  for (uint8_t n = 0; n < NUM_RECORD_SLOTS; n++) {
    char path[24];
    wavPath(path, sizeof(path), n);
    File f = SD.open(path, FILE_READ);
    if (!f) continue;

    uint32_t len = 0, rate = 0;
    bool ok = readWav(f, Sampler.poolSlot(n), RECORD_SLOT_SAMPLES,
                      &len, &rate);
    f.close();
    if (!ok) {
      Serial.print(path);
      Serial.println(": not mono 16-bit PCM - skipped");
      continue;
    }

    char nm[8];
    snprintf(nm, sizeof(nm), "rec%u", n + 1);
    uint8_t storeSlot = SampleStore.progmemCount() + n;
    if (storeSlot >= NUM_SAMPLE_SLOTS) break;
    SampleStore.assignRam(storeSlot, Sampler.poolSlot(n), len, rate, nm);
    Serial.print(path);
    Serial.print(": loaded into slot ");
    Serial.println(storeSlot);
  }
}
