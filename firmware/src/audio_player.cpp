#include "audio_player.h"

AudioPlayer::AudioPlayer()
  : _playing(false), _playPtr(nullptr), _playSamples(0),
    _fillPtr(nullptr), _fillSamples(0), _playIdx(0), _volume(128), _totalSamples(0) {}

bool AudioPlayer::begin() {
  return true;
}

bool AudioPlayer::playFile(const char* path) {
  stop();
  _file = SD_FS.open(path, FILE_READ);
  if (!_file) {
    log_e("Failed to open audio: %s", path);
    return false;
  }

  _totalSamples = 0;
  _playIdx = 0;
  size_t br = _file.read((uint8_t*)_buf0, AUDIO_READ_SIZE);
  if (br == 0) {
    _file.close();
    return false;
  }

  _playPtr = _buf0;
  _playSamples = br / 2;
  _totalSamples += _playSamples;
  SPEAKER_OBJ.playRaw((const int16_t*)_playPtr, _playSamples, AUDIO_SAMPLE_RATE, false, 1, _volume, true);

  br = _file.read((uint8_t*)_buf1, AUDIO_READ_SIZE);
  if (br > 0) {
    _fillPtr = _buf1;
    _fillSamples = br / 2;
    _playIdx = 1;
  } else {
    _fillPtr = nullptr;
    _fillSamples = 0;
  }

  _playing = true;
  log_i("Playing audio: %s", path);
  return true;
}

void AudioPlayer::stop() {
  _playing = false;
  SPEAKER_OBJ.stop(0);
  if (_file) _file.close();
}

void AudioPlayer::loop() {
  if (!_playing) return;

  if (SPEAKER_OBJ.isPlaying(0)) return;

  if (!_fillPtr) {
    _playing = false;
    _file.close();
    return;
  }

  _playPtr = _fillPtr;
  _playSamples = _fillSamples;
  _totalSamples += _playSamples;
  SPEAKER_OBJ.playRaw((const int16_t*)_playPtr, _playSamples, AUDIO_SAMPLE_RATE, false, 1, _volume, true);

  int16_t* nextBuf = (_playIdx == 0) ? _buf0 : _buf1;
  size_t br = _file.read((uint8_t*)nextBuf, AUDIO_READ_SIZE);
  if (br > 0) {
    _fillPtr = nextBuf;
    _fillSamples = br / 2;
    _playIdx = 1 - _playIdx;
  } else {
    _fillPtr = nullptr;
    _fillSamples = 0;
  }
}

bool AudioPlayer::isPlaying() {
  if (_playing) return true;
  if (_file) return true;
  if (SPEAKER_OBJ.isPlaying(0)) return true;
  return false;
}

void AudioPlayer::setVolume(uint8_t vol) {
  _volume = vol;
  SPEAKER_OBJ.setVolume(vol);
}

uint32_t AudioPlayer::getPlaybackTimeUs() {
  return _totalSamples * 1000000ULL / AUDIO_SAMPLE_RATE;
}
