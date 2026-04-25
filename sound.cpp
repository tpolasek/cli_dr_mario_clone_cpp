#include "raylib.h"

// Undef raylib color macros that conflict with our game color constants
#undef RED
#undef YELLOW
#undef BLUE

#include "sound.h"

MusicPlayer::MusicPlayer()
    : available_(false), playing_(false), loaded_(false), music_({0}) {
  InitAudioDevice();
  available_ = IsAudioDeviceReady();
}

MusicPlayer::~MusicPlayer() {
  stop();
  if (loaded_) {
    UnloadMusicStream(music_);
    loaded_ = false;
  }
  if (IsAudioDeviceReady()) {
    CloseAudioDevice();
  }
}

void MusicPlayer::start(const char *filename) {
  if (!available_)
    return;

  // If already playing the same file, just restart
  if (loaded_ && current_file_ == filename) {
    SeekMusicStream(music_, 0.0f);
    PlayMusicStream(music_);
    playing_ = true;
    return;
  }

  // Unload previous
  stop();
  if (loaded_) {
    UnloadMusicStream(music_);
    loaded_ = false;
  }

  music_ = LoadMusicStream(filename);
  if (music_.stream.sampleRate != 0 || music_.frameCount != 0) {
    music_.looping = true;
    current_file_ = filename;
    loaded_ = true;
    PlayMusicStream(music_);
    playing_ = true;
  }
}

void MusicPlayer::stop() {
  if (playing_ && loaded_) {
    StopMusicStream(music_);
    playing_ = false;
  }
}

void MusicPlayer::update() {
  if (playing_ && loaded_) {
    UpdateMusicStream(music_);
  }
}
