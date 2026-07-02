#pragma once

#include <Arduino.h>
#include "config.h"
#include "audio_hal.h"

class AudioPlayer {
public:
  AudioPlayer();
  bool begin();
  bool playFile(const char* path);
  void stop();
  bool needsFill();
  void playFilled();
  void fillNext();
  bool isPlaying();
  void setVolume(uint8_t vol);
  uint32_t getPlaybackTimeUs();
  uint32_t totalSamples() const;

private:
  File _file;
  bool _playing;
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
};
