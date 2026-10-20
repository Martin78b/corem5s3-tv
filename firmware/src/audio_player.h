#pragma once

#include <Arduino.h>
#include <atomic>
#include "config.h"
#include "audio_hal.h"

class AudioPlayer {
public:
  AudioPlayer();
  bool begin();
  bool playFile(const char* path);
  void stop();
  // Pause keeps the file and position. What is still queued in the DMA ring
  // is dropped (< 100 ms), but playedSamples() stays consistent, so video
  // stays in sync after resume(). Call with the audio mutex held.
  void pause();
  void resume();
  bool isPaused() const { return _paused; }
  bool needsFill();
  void playFilled();
  void fillNext();
  bool isPlaying();
  void setVolume(uint8_t vol);
  uint32_t getPlaybackTimeUs();
  uint32_t totalSamples() const;
  // Samples actually played (heard) so far in this file: what video syncs to.
  uint32_t playedSamples() const;

private:
  File _file;
  std::atomic<bool> _playing;  // Read from both cores
  std::atomic<bool> _paused;
  int16_t _buf0[AUDIO_READ_SIZE / 2];
  int16_t _buf1[AUDIO_READ_SIZE / 2];
  int16_t* _playPtr;
  size_t _playSamples;
  int16_t* _fillPtr;
  size_t _fillSamples;
  int _playIdx;
  uint8_t _volume;
  volatile uint32_t _totalSamples;
  uint64_t _startUs;
#ifdef M5STACK
  volatile uint32_t _chunkStartUs;  // When the current chunk was queued
#else
  uint32_t _baseSamples;            // SPEAKER_OBJ.samplesWritten() at file start
#endif
};
