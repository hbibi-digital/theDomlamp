// Fetches the lighting plan over HTTP(S) in a task of its own, so WLED's loop never waits for the network
// or for the TLS handshake. ESP32 family only.
#pragma once

#include <atomic>

#include "domlamp_core.h"

namespace domlamp {

struct FetchRequest {
  Url url;
  bool verify;           // check the server's certificate against ROOTS_PEM
  uint32_t now;          // Unix seconds, for the certificate's validity dates (needed when verify is set)
};

struct FetchResult {
  bool ok;
  int http_status;       // 0 = no answer
  uint32_t took_ms;
  uint32_t stack_free;   // bytes of the task's stack never used
  char error[72];        // empty when ok
  Plan plan;
};

class Fetcher {
 public:
  // Starts a fetch in the background. False when one is still running or the task cannot be created.
  bool start(const FetchRequest &request);
  bool busy() const { return state_.load() == State::Running; }
  // The result of the last fetch, once. The pointer stays valid until the next start().
  const FetchResult *take();

 private:
  enum class State : uint8_t { Idle, Running, Done };
  static void taskMain(void *self);
  void run();

  std::atomic<State> state_{State::Idle};
  FetchRequest request_ = {};
  FetchResult result_ = {};
};

}  // namespace domlamp
