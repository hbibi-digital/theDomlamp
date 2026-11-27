// The link through the server: the lamp keeps one stream open to the relay (web/deploy/koppel/koppel.py)
// and shows what a browser with the lamp's code sends there. For a lamp that the browser cannot reach
// itself: a page on https may not talk to a device in the house (web/src/lamplink.js has the direct way).
// Runs in a task of its own, like the plan fetch. ESP32 family only.
#pragma once

#include <atomic>

#include "domlamp_core.h"

namespace domlamp {

struct LinkFrame {
  bool end;                    // the browser let go: back to the tower at once
  Zone zones[ZONE_COUNT];
};

class Linker {
 public:
  // Starts the task that connects, reads and reconnects until stop(). False when it runs already or cannot be made.
  bool start(const Url &url, bool verify);
  // Asks the task to close the stream and end; running() turns false once it has.
  void stop() { stop_.store(true); }
  bool running() const { return running_.load(); }
  bool connected() const { return connected_.load(); }
  // Unix seconds for the certificate's validity dates; 0 = no clock yet (https then waits).
  void setClock(uint32_t now) { now_.store(now); }
  // The newest frame since the last call, if any. Older ones in between are skipped: only the last counts.
  bool take(LinkFrame &out);
  uint32_t frames() const { return frames_.load(); }
  uint32_t connects() const { return connects_.load(); }
  // Text of the last failure, empty when the stream is up. Read from the loop while the task may write it:
  // a torn text is possible and harmless (it is only shown).
  const char *error() const { return error_; }

 private:
  static void taskMain(void *self);
  void run();
  bool stream();               // one connection, until it ends; true when it had been up
  void setError(const char *text);
  bool pause(uint32_t ms);     // false when stop() was asked meanwhile

  Url url_ = {};
  bool verify_ = true;
  std::atomic<bool> stop_{false};
  std::atomic<bool> running_{false};
  std::atomic<bool> connected_{false};
  std::atomic<uint32_t> now_{0};
  std::atomic<uint32_t> frames_{0};
  std::atomic<uint32_t> connects_{0};
  std::atomic<bool> fresh_{false};
  LinkFrame frame_ = {};
  char error_[48] = "";
};

}  // namespace domlamp
