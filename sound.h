#pragma once

#include <string>
#include <sys/types.h>

class MusicPlayer {
public:
  MusicPlayer();
  ~MusicPlayer();

  void start(const char *filename);
  void stop();

  bool is_available() const { return available_; }

  MusicPlayer(const MusicPlayer &) = delete;
  MusicPlayer &operator=(const MusicPlayer &) = delete;

private:
  bool available_;
  std::string player_cmd_;
  pid_t music_pid_ = 0;
};
