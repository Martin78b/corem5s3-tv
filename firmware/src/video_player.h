#pragma once

#include <Arduino.h>
#include <JPEGDEC.h>
#include "config.h"

struct VideoFile {
  char path[EPISODE_PATH_MAX];
  char pcmPath[EPISODE_PATH_MAX];
};

class VideoPlayer {
public:
  VideoPlayer();
  bool begin();
  bool openFile(const char* path);
  uint32_t readNextFrame();
  // bigEndian: write pixels in display byte order (for TFT_eSprite buffers)
  bool     decodeFrame(uint16_t* framebuffer, bool bigEndian = false);
  void close();
  bool isOpen();
  int  totalFrames();
  int  currentFrame();
  float frameTimeMs();
  uint32_t fps() const { return _fps; }
  uint32_t sampleRate() const { return _sampleRate; }

  static int scanEpisodes(VideoFile* episodes, int maxEpisodes);
  static bool hasAudio(const VideoFile& episode);
  // Called after each decoded block (e.g. to keep a display DMA transfer fed)
  static void setDrawHook(void (*hook)()) { _drawHook = hook; }

private:
  File _file;
  int _frameCount;
  int _currentFrame;
  uint32_t _fps;          // From file header, or VIDEO_FPS for legacy files
  uint32_t _sampleRate;   // PCM rate from file header, or AUDIO_SAMPLE_RATE

  uint8_t* _jpegBuf;
  size_t _jpegBufSize;
  uint32_t _currentFrameSize;
  JPEGDEC _jpeg;

  void readHeader();
  static int JPEGDraw(JPEGDRAW* draw);
  static uint16_t* _drawTarget;
  static void (*_drawHook)();
};
