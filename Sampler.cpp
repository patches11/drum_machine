#include <Audio.h>
#include "Sampler.h"
#include "AppState.h"
#include "SampleStore.h"
#include "AudioEngine.h"
#include "Sequencer.h"
#include "Display.h"
#include "Storage.h"

SamplerClass Sampler;

// ---------------------------------------------------------------------------
// Mic input graph. Static like the rest of the audio objects. The peak
// analyzer is always live (cheap; feeds the RECORD-mode meter); the record
// queue only holds audio blocks between begin() and end() of a take.

static AudioInputI2S    i2sIn;
static AudioAnalyzePeak peak;
static AudioRecordQueue recQueue;
static AudioConnection  pMicPeak(i2sIn, 0, peak, 0);
static AudioConnection  pMicQueue(i2sIn, 0, recQueue, 0);

// The RAM pool: fixed partitions, round-robin, never freed (plan §"Dual
// PROGMEM / SD / mic sample story" — slot-partitioned by design).
static int16_t pool[NUM_RECORD_SLOTS][RECORD_SLOT_SAMPLES];

static const char* const REC_NAMES[NUM_RECORD_SLOTS] = {
  "rec1", "rec2", "rec3", "rec4"
};

// ---------------------------------------------------------------------------

void SamplerClass::begin() {
  // codec input select + mic gain happen in AudioEngine.begin();
  // nothing to do until a take is armed.
}

int16_t* SamplerClass::poolSlot(uint8_t n) {
  return pool[n < NUM_RECORD_SLOTS ? n : 0];
}

void SamplerClass::toggleArm() {
  switch (st) {
    case SMP_IDLE:
      recQueue.begin();
      recLen = 0;
      st = SMP_ARMED;
      Display.flash("ARMED");
      Serial.println("sampler: ARMED - make a sound");
      break;
    case SMP_ARMED:
      cancel();
      Display.flash("cancel");
      Serial.println("sampler: canceled");
      break;
    case SMP_RECORDING:
      finish();
      break;
  }
}

void SamplerClass::cancel() {
  recQueue.end();
  recQueue.clear();
  st = SMP_IDLE;
}

void SamplerClass::update() {
  // input level meter (Display reads it in RECORD mode)
  if (peak.available()) {
    float p = peak.read();
    lastLevel = (uint8_t)(p * 100.0f + 0.5f);
  }

  if (st == SMP_IDLE) return;

  // leaving RECORD mode abandons the take — no surprise captures later
  if (appMode != MODE_SAMPLE_RECORD) {
    cancel();
    return;
  }

  while (recQueue.available() > 0) {
    int16_t* block = recQueue.readBuffer();
    uint32_t start = 0;

    if (st == SMP_ARMED) {
      // hunt for the onset; discard fully-quiet blocks
      int found = -1;
      for (uint32_t i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
        int16_t s = block[i];
        if (s > RECORD_THRESHOLD || s < -RECORD_THRESHOLD) { found = (int)i; break; }
      }
      if (found < 0) { recQueue.freeBuffer(); continue; }
      start = (uint32_t)found & ~1u;   // even index keeps decimation pairs aligned
      st = SMP_RECORDING;
      Display.flash("REC");
      Serial.println("sampler: RECORDING");
    }

    // decimate by 2 (pair-average) into the current partition
    int16_t* dst = pool[recSlot];
    for (uint32_t i = start;
         i + 1 < AUDIO_BLOCK_SAMPLES && recLen < RECORD_SLOT_SAMPLES; i += 2) {
      dst[recLen++] = (int16_t)(((int32_t)block[i] + (int32_t)block[i + 1]) >> 1);
    }
    recQueue.freeBuffer();

    if (recLen >= RECORD_SLOT_SAMPLES) {   // partition full = auto-stop
      finish();
      break;
    }
  }
}

void SamplerClass::finish() {
  recQueue.end();
  recQueue.clear();
  st = SMP_IDLE;

  int16_t* buf = pool[recSlot];
  uint32_t len = recLen;

  // trim trailing quiet: keep RECORD_TAIL_PAD past the last loud sample
  uint32_t lastLoud = 0;
  for (uint32_t i = 0; i < len; i++) {
    int16_t s = buf[i];
    if (s > RECORD_TAIL_LEVEL || s < -RECORD_TAIL_LEVEL) lastLoud = i;
  }
  if (lastLoud + RECORD_TAIL_PAD < len) len = lastLoud + RECORD_TAIL_PAD;

  if (len < 32) {
    Display.flash("too short");
    Serial.println("sampler: too short, discarded");
    return;                              // slot NOT consumed
  }

  // normalize to ~90% FS; gain capped so a near-silent take can't
  // amplify the noise floor into a "sample"
  int32_t maxAbs = 1;
  for (uint32_t i = 0; i < len; i++) {
    int32_t a = buf[i];
    if (a < 0) a = -a;
    if (a > maxAbs) maxAbs = a;
  }
  float gain = (32767.0f * 0.9f) / (float)maxAbs;
  if (gain > 8.0f) gain = 8.0f;
  if (gain > 1.01f) {
    for (uint32_t i = 0; i < len; i++)
      buf[i] = (int16_t)((float)buf[i] * gain);
  }

  // repoint the store slot (record slots live just after the PROGMEM kit)
  // and put the new sound on the selected voice
  uint8_t storeSlot = SampleStore.progmemCount() + recSlot;
  if (storeSlot >= NUM_SAMPLE_SLOTS) storeSlot = NUM_SAMPLE_SLOTS - 1;
  AudioEngine.stopVoicesUsing(storeSlot);  // simplest safe rule before repoint
  uint32_t rate = (uint32_t)(AUDIO_SAMPLE_RATE_EXACT / 2.0f + 0.5f); // ~22059
  SampleStore.assignRam(storeSlot, buf, len, rate, REC_NAMES[recSlot]);
  pattern.voices[editState.voice].sampleId = storeSlot;

  // confirmation: flash name + length, audition when transport is stopped
  uint32_t cs = len * 100u / rate;       // centiseconds
  char msg[20];
  snprintf(msg, sizeof(msg), "%s %lu.%02lus", REC_NAMES[recSlot],
           (unsigned long)(cs / 100), (unsigned long)(cs % 100));
  Display.flash(msg);
  Serial.print("sampler: saved ");
  Serial.print(msg);
  Serial.print(" -> slot ");
  Serial.print(storeSlot);
  Serial.print(" on voice ");
  Serial.println(editState.voice + 1);
  if (!Sequencer.isRunning())
    AudioEngine.trigger(editState.voice, 100, 0);

  // M9: persist the take so it survives a power-cycle (no-op without SD)
  Storage.saveRecording(recSlot, buf, len, rate);

  recSlot = (uint8_t)((recSlot + 1) % NUM_RECORD_SLOTS);
}
