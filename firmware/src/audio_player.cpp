#include "audio_player.h"
#ifdef WAVESHARE_154
#include "es8311.h"
#endif

AudioPlayer::AudioPlayer()
  : _playing(false), _playPtr(nullptr), _playSamples(0),
    _fillPtr(nullptr), _fillSamples(0), _playIdx(0), _volume(128), _totalSamples(0), _startUs(0),
#ifdef M5STACK
    _chunkStartUs(0) {}
#else
    _baseSamples(0) {}
#endif

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
#ifndef M5STACK
  _baseSamples = SPEAKER_OBJ.samplesWritten();
#endif
  size_t br = _file.read((uint8_t*)_buf0, AUDIO_READ_SIZE);
  if (br == 0) {
    _file.close();
    return false;
  }

  _playPtr = _buf0;
  _playSamples = br / 2;
  _totalSamples += _playSamples;
#ifdef M5STACK
  _chunkStartUs = micros();
#endif
  SPEAKER_OBJ.playRaw((const int16_t*)_playPtr, _playSamples, AUDIO_SAMPLE_RATE, false, 1, _volume, true);

  br = _file.read((uint8_t*)_buf1, AUDIO_READ_SIZE);
  if (br > 0) {
    _fillPtr = _buf1;
    _fillSamples = br / 2;
    _playIdx = 1;
  } else {
    _fillPtr = nullptr;
    _fillSamples = 0;
    _file.close();
  }

  _playing = true;
  _startUs = micros();
  log_i("Playing audio: %s", path);
  return true;
}

void AudioPlayer::stop() {
  _playing = false;
  SPEAKER_OBJ.stop(0);
  if (_file) _file.close();
}

bool AudioPlayer::needsFill() {
  if (!_playing) return false;
  if (SPEAKER_OBJ.isPlaying(0)) return false;
  return true;
}

// No SD access here (the caller does not hold the SD mutex): at EOF the file
// was already closed by fillNext() or playFile().
void AudioPlayer::playFilled() {
  if (!_fillPtr) {
    _playing = false;
    SPEAKER_OBJ.stop(0);
    return;
  }
  _playPtr = _fillPtr;
  _playSamples = _fillSamples;
  _totalSamples += _playSamples;
#ifdef M5STACK
  _chunkStartUs = micros();
#endif
  SPEAKER_OBJ.playRaw((const int16_t*)_playPtr, _playSamples, AUDIO_SAMPLE_RATE, false, 1, _volume, true);
}

void AudioPlayer::fillNext() {
  if (!_playing || !_file) return;
  int16_t* nextBuf = (_playIdx == 0) ? _buf0 : _buf1;
  size_t br = _file.read((uint8_t*)nextBuf, AUDIO_READ_SIZE);
  if (br > 0) {
    _fillPtr = nextBuf;
    _fillSamples = br / 2;
    _playIdx = 1 - _playIdx;
  } else {
    _fillPtr = nullptr;
    _fillSamples = 0;
    _file.close();
  }
}

// Called from both cores. Does not check _file: it is only open while
// _playing is true, and reading it here would race with fillNext() closing it.
bool AudioPlayer::isPlaying() {
  if (_playing) return true;
  if (SPEAKER_OBJ.isPlaying(0)) return true;
  return false;
}

void AudioPlayer::setVolume(uint8_t vol) {
#ifdef WAVESHARE_154
  // The ES8311 attenuates in 0.5 dB steps at no CPU cost; PCM goes out as is.
  es8311_set_volume(vol);
  _volume = 255;
#else
  _volume = vol;
#endif
  SPEAKER_OBJ.setVolume(_volume);
}

uint32_t AudioPlayer::getPlaybackTimeUs() {
  if (_startUs == 0) return 0;
  uint64_t elapsed = micros() - _startUs;
  return (uint32_t)elapsed;
}

uint32_t AudioPlayer::totalSamples() const {
  return _totalSamples;
}

uint32_t AudioPlayer::playedSamples() const {
#ifdef M5STACK
  // M5Unified queues a whole chunk and returns: interpolate within the
  // current chunk by the time elapsed since it was queued.
  uint32_t chunk = _playSamples;
  uint32_t total = _totalSamples;
  if (total < chunk) return 0;
  uint32_t elapsed = (uint64_t)(micros() - _chunkStartUs) * AUDIO_SAMPLE_RATE / 1000000;
  if (elapsed > chunk) elapsed = chunk;
  return total - chunk + elapsed;
#else
  // Everything handed to I2S, minus what still sits in the DMA ring
  // (it is kept full because i2s_write blocks).
  const uint32_t queued = AUDIO_DMA_BUF_COUNT * AUDIO_DMA_BUF_LEN;
  uint32_t written = SPEAKER_OBJ.samplesWritten() - _baseSamples;
  return written > queued ? written - queued : 0;
#endif
}
