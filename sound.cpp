#include "sound.h"

#include <cstdlib>
#include <fcntl.h>
#include <signal.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

MusicPlayer::MusicPlayer() : available_(false) {
  // Find an available audio player: afplay (macOS) > ffplay > mpv
  const char *players[] = {"afplay", "ffplay", "mpv"};
  for (const char *p : players) {
    std::string cmd = std::string("command -v ") + p + " >/dev/null 2>&1";
    if (system(cmd.c_str()) == 0) {
      player_cmd_ = p;
      available_ = true;
      break;
    }
  }
}

MusicPlayer::~MusicPlayer() { stop(); }

void MusicPlayer::start(const char *filename) {
  if (!available_ || music_pid_ > 0)
    return;

  music_pid_ = fork();
  if (music_pid_ == 0) {
    setpgid(0, 0);
    while (true) {
      pid_t p = fork();
      if (p == 0) {
        int null_fd = open("/dev/null", O_WRONLY);
        if (null_fd >= 0) {
          dup2(null_fd, STDOUT_FILENO);
          dup2(null_fd, STDERR_FILENO);
          close(null_fd);
        }
        if (player_cmd_ == "afplay") {
          execlp("afplay", "afplay", filename, nullptr);
        } else if (player_cmd_ == "ffplay") {
          execlp("ffplay", "ffplay", "-nodisp", "-autoexit", "-loglevel",
                 "quiet", filename, nullptr);
        } else if (player_cmd_ == "mpv") {
          execlp("mpv", "mpv", "--no-video", "--really-quiet", filename,
                 nullptr);
        }
        _exit(1);
      }
      wait(nullptr);
    }
  }
}

void MusicPlayer::stop() {
  if (music_pid_ > 0) {
    kill(-music_pid_, SIGTERM);
    music_pid_ = 0;
  }
}
