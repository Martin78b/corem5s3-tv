#pragma once

#include <Arduino.h>
#include <SD.h>
#include <M5Unified.h>
#include "config.h"

class AudioPlayer {
public:
  AudioPlayer();
  bool begin();
  bool playFile(const char* path);
  void stop();
  void loop();
  bool isPlaying();
  void setVolume(uint8_t vol);
  uint32_t getPlaybackTimeUs();

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
  uint64_t _totalSamples;
};
