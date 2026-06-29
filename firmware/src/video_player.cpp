#include "video_player.h"

uint16_t* VideoPlayer::_drawTarget = nullptr;

VideoPlayer::VideoPlayer()
  : _frameCount(0), _currentFrame(0), _fps(VIDEO_FPS),
    _jpegBuf(nullptr), _jpegBufSize(0), _currentFrameSize(0) {}

bool VideoPlayer::begin() {
  _jpegBuf = (uint8_t*)malloc(65536);
  if (!_jpegBuf) _jpegBuf = (uint8_t*)ps_malloc(65536);
  if (!_jpegBuf) {
    log_e("Failed to allocate JPEG buffer");
    return false;
  }
  _jpegBufSize = 65536;
  return true;
}

bool VideoPlayer::openFile(const char* path) {
  close();
  _file = SD_FS.open(path, FILE_READ);
  if (!_file) {
    log_e("Failed to open: %s", path);
    return false;
  }
  _currentFrame = 0;
  _frameCount = _file.size() / 50;
  log_i("Opened %s (%u bytes, ~%d frames)", path, _file.size(), _frameCount);
  return true;
}

uint32_t VideoPlayer::readNextFrame() {
  if (!_file || !_file.available()) return 0;

  uint32_t frameSize = 0;
  size_t bytesRead = _file.read((uint8_t*)&frameSize, 4);
  if (bytesRead != 4) return 0;

  if (frameSize == 0 || frameSize > _jpegBufSize) {
    log_w("Bad frame size: %u (max %u)", frameSize, _jpegBufSize);
    return 0;
  }

  bytesRead = _file.read(_jpegBuf, frameSize);
  if (bytesRead != frameSize) {
    log_w("Short read: %u != %u", bytesRead, frameSize);
    return 0;
  }

  if (frameSize & 1) _file.seek(_file.position() + 1);

  _currentFrameSize = frameSize;
  _currentFrame++;
  return frameSize;
}

bool VideoPlayer::decodeFrame(uint16_t* framebuffer) {
  _drawTarget = framebuffer;

  if (!_jpeg.openRAM(_jpegBuf, _currentFrameSize, JPEGDraw)) {
    return false;
  }

  _jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
  _jpeg.decode(0, 0, 0);
  _jpeg.close();

  return true;
}

void VideoPlayer::close() {
  if (_file) _file.close();
  _currentFrame = 0;
  _frameCount = 0;
  _currentFrameSize = 0;
}

bool VideoPlayer::isOpen() { return _file ? true : false; }
int VideoPlayer::totalFrames() { return _frameCount; }
int VideoPlayer::currentFrame() { return _currentFrame; }
float VideoPlayer::frameTimeMs() { return 1000.0f / _fps; }

int VideoPlayer::JPEGDraw(JPEGDRAW* draw) {
  if (!_drawTarget) return 0;
  uint16_t* dst = _drawTarget + draw->y * DISPLAY_WIDTH + draw->x;
  uint16_t* src = (uint16_t*)draw->pPixels;
  int w = draw->iWidth;

  for (int row = 0; row < draw->iHeight; row++) {
    memcpy(dst, src, w * 2);
    dst += DISPLAY_WIDTH;
    src += w;
  }
  return 1;
}

int VideoPlayer::scanEpisodes(VideoFile* episodes, int maxEpisodes) {
  int count = 0;
  File root = SD_FS.open("/");
  if (!root) return 0;

  while (count < maxEpisodes) {
    File entry = root.openNextFile();
    if (!entry) break;
    const char* name = entry.name();
    if (!entry.isDirectory()) {
      size_t len = strlen(name);
      if (len > 6 && strcasecmp(name + len - 6, MJPEG_EXT) == 0) {
        snprintf(episodes[count].path, sizeof(episodes[count].path), "/%s", name);
        strncpy(episodes[count].pcmPath, episodes[count].path, sizeof(episodes[count].pcmPath));
        char* dot = strrchr(episodes[count].pcmPath, '.');
        if (dot) strcpy(dot, PCM_EXT);
        count++;
      }
    }
    entry.close();
  }
  root.close();
  log_i("Found %d episodes", count);
  return count;
}

bool VideoPlayer::hasAudio(const char* videoPath) {
  char pcmPath[64];
  strncpy(pcmPath, videoPath, 64);
  char* dot = strrchr(pcmPath, '.');
  if (dot) strcpy(dot, PCM_EXT);
  return SD_FS.exists(pcmPath);
}
