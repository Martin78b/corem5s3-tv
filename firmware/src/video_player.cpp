#include "video_player.h"

uint16_t* VideoPlayer::_drawTarget = nullptr;
void (*VideoPlayer::_drawHook)() = nullptr;

VideoPlayer::VideoPlayer()
  : _frameCount(0), _currentFrame(0), _fps(VIDEO_FPS), _sampleRate(AUDIO_SAMPLE_RATE),
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
  readHeader();
  log_i("Opened %s (%u bytes, ~%d frames, %u fps, %u Hz)", path, _file.size(),
        _frameCount, _fps, _sampleRate);
  return true;
}

// Optional 16-byte header written by tools/convert_episodes.py (little-endian):
//   "CMTV" | u8 version=1 | u8 reserved | u16 fps | u16 width | u16 height | u32 sample_rate
// Legacy files start directly with a frame size; "CMTV" read as a size is far
// larger than any valid frame, so both formats can be told apart.
void VideoPlayer::readHeader() {
  _fps = VIDEO_FPS;
  _sampleRate = AUDIO_SAMPLE_RATE;

  uint8_t hdr[16];
  if (_file.read(hdr, sizeof(hdr)) != sizeof(hdr) || memcmp(hdr, "CMTV", 4) != 0) {
    _file.seek(0);  // Legacy file: no header
    return;
  }
  if (hdr[4] != 1) {
    log_w("Unknown header version %u, using defaults", hdr[4]);
    return;
  }

  uint16_t fps = hdr[6] | (hdr[7] << 8);
  uint16_t w = hdr[8] | (hdr[9] << 8);
  uint16_t h = hdr[10] | (hdr[11] << 8);
  uint32_t rate = hdr[12] | (hdr[13] << 8) | (hdr[14] << 16) | ((uint32_t)hdr[15] << 24);

  if (fps >= 1 && fps <= 60) {
    _fps = fps;
  } else {
    log_w("Bad FPS %u in header, using %d", fps, VIDEO_FPS);
  }
  if (rate > 0) _sampleRate = rate;
  if (w != DISPLAY_WIDTH || h != DISPLAY_HEIGHT) {
    log_w("Video is %ux%u but display is %dx%d (will be cropped)", w, h,
          DISPLAY_WIDTH, DISPLAY_HEIGHT);
  }
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

bool VideoPlayer::decodeFrame(uint16_t* framebuffer, bool bigEndian) {
  _drawTarget = framebuffer;

  if (!_jpeg.openRAM(_jpegBuf, _currentFrameSize, JPEGDraw)) {
    return false;
  }

  _jpeg.setPixelType(bigEndian ? RGB565_BIG_ENDIAN : RGB565_LITTLE_ENDIAN);
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
  // Clip to the framebuffer: a frame larger than the display (e.g. 320x240
  // on a 240x240 board) would otherwise write past the end of it.
  if (draw->x >= DISPLAY_WIDTH || draw->y >= DISPLAY_HEIGHT) return 1;
  int w = draw->iWidth;
  int h = draw->iHeight;
  if (draw->x + w > DISPLAY_WIDTH) w = DISPLAY_WIDTH - draw->x;
  if (draw->y + h > DISPLAY_HEIGHT) h = DISPLAY_HEIGHT - draw->y;

  uint16_t* dst = _drawTarget + draw->y * DISPLAY_WIDTH + draw->x;
  uint16_t* src = (uint16_t*)draw->pPixels;

  for (int row = 0; row < h; row++) {
    memcpy(dst, src, w * 2);
    dst += DISPLAY_WIDTH;
    src += draw->iWidth;
  }
  if (_drawHook) _drawHook();
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
      const size_t extLen = sizeof(MJPEG_EXT) - 1;
      if (len > extLen && strcasecmp(name + len - extLen, MJPEG_EXT) == 0) {
        VideoFile& ep = episodes[count];
        // "/<base>.mjpeg" and "/<base>.pcm"; skip names that would be truncated
        int baseLen = (int)(len - extLen);
        int n1 = snprintf(ep.path, sizeof(ep.path), "/%s", name);
        int n2 = snprintf(ep.pcmPath, sizeof(ep.pcmPath), "/%.*s%s", baseLen, name, PCM_EXT);
        if (n1 >= (int)sizeof(ep.path) || n2 >= (int)sizeof(ep.pcmPath)) {
          log_w("Skipping %s: file name too long (max %d chars)", name, EPISODE_PATH_MAX - 2);
        } else {
          count++;
        }
      }
    }
    entry.close();
  }
  root.close();

  // FAT returns directory order; sort so episode indices are stable
  qsort(episodes, count, sizeof(VideoFile), [](const void* a, const void* b) {
    return strcasecmp(((const VideoFile*)a)->path, ((const VideoFile*)b)->path);
  });

  log_i("Found %d episodes", count);
  return count;
}

bool VideoPlayer::hasAudio(const VideoFile& episode) {
  return SD_FS.exists(episode.pcmPath);
}
