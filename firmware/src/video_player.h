#pragma once

#include <Arduino.h>
#include <JPEGDEC.h>
#include "config.h"

struct VideoFile {
  char path[64];
  char pcmPath[64];
};

class VideoPlayer {
public:
  VideoPlayer();
  bool begin();
  bool openFile(const char* path);
  uint32_t readNextFrame();
  bool     decodeFrame(uint16_t* framebuffer);
  void close();
  bool isOpen();
  int  totalFrames();
  int  currentFrame();
  float frameTimeMs();

  static int scanEpisodes(VideoFile* episodes, int maxEpisodes);
  static bool hasAudio(const char* videoPath);

private:
  File _file;
  int _frameCount;
  int _currentFrame;
  float _fps;

  uint8_t* _jpegBuf;
  size_t _jpegBufSize;
  uint32_t _currentFrameSize;
  JPEGDEC _jpeg;

  static int JPEGDraw(JPEGDRAW* draw);
  static uint16_t* _drawTarget;
};
