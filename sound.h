#pragma once

#include "raylib.h"
#include <string>

class MusicPlayer {
public:
  MusicPlayer();
  ~MusicPlayer();

  void start(const char *filename);
  void stop();
  void update(); // call every frame
  bool is_available() const { return available_; }

  MusicPlayer(const MusicPlayer &) = delete;
  MusicPlayer &operator=(const MusicPlayer &) = delete;

private:
  bool available_;
  bool playing_;
  bool loaded_;
  Music music_;
  std::string current_file_;
};
