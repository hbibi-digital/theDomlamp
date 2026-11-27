// See domlamp_link.h.
#ifdef ARDUINO_ARCH_ESP32

#include "domlamp_link.h"

#include "domlamp_net.h"

#include <memory>
#include <new>

#include "domlamp_certs.h"

namespace domlamp {

namespace {

constexpr uint32_t TASK_STACK = 12288;       // as the plan fetch: BearSSL works on the stack
constexpr uint32_t HEAP_NEEDED = 70000;      // free in total before a TLS stream is opened: it keeps about
                                             // 45 kB for as long as it is up, and the plan fetch needs its own
constexpr uint32_t BLOCK_NEEDED = 20000;     // in one piece: the receive buffer
constexpr uint32_t SILENCE_MS = 40000;       // the relay sends a sign of life every 10 s
constexpr uint32_t RETRY_MIN_MS = 5000, RETRY_MAX_MS = 60000;

portMUX_TYPE frameLock = portMUX_INITIALIZER_UNLOCKED;

}  // namespace

bool Linker::start(const Url &url, bool verify) {
  if (running_.load()) return false;
  url_ = url;
  verify_ = verify;
  stop_.store(false);
  running_.store(true);
  if (xTaskCreateUniversal(taskMain, "domlink", TASK_STACK, this, 1, nullptr, 0) != pdPASS) {
    running_.store(false);
    setError("no memory for the link task");
    return false;
  }
  return true;
}

bool Linker::take(LinkFrame &out) {
  if (!fresh_.load()) return false;
  portENTER_CRITICAL(&frameLock);
  out = frame_;
  fresh_.store(false);
  portEXIT_CRITICAL(&frameLock);
  return true;
}

void Linker::setError(const char *text) { strlcpy(error_, text, sizeof(error_)); }

bool Linker::pause(uint32_t ms) {
  for (uint32_t waited = 0; waited < ms; waited += 100) {
    if (stop_.load()) return false;
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  return !stop_.load();
}

void Linker::taskMain(void *self) {
  Linker *linker = static_cast<Linker *>(self);
  linker->run();
  linker->connected_.store(false);
  linker->running_.store(false);
  vTaskDelete(nullptr);
}

void Linker::run() {
  uint32_t retry = RETRY_MIN_MS;
  while (!stop_.load()) {
    if (WiFi.status() != WL_CONNECTED) {
      setError("no network");
    } else if (url_.tls && verify_ && now_.load() == 0) {
      setError("waiting for the clock");
    } else if (stream()) {
      retry = RETRY_MIN_MS;                  // it had been up: try again soon
    } else {
      retry = min(retry * 2, RETRY_MAX_MS);
    }
    connected_.store(false);
    if (!pause(retry)) return;
  }
}

bool Linker::stream() {
  if (url_.tls && url_.port != 443) {
    setError("https works on port 443 only");
    return false;
  }
  if (url_.tls && (ESP.getFreeHeap() < HEAP_NEEDED || ESP.getMaxAllocHeap() < BLOCK_NEEDED)) {
    setError("not enough free memory for TLS");
    return false;
  }
  WiFiClient tcp;
  PoliteClient polite(tcp);
  std::unique_ptr<ESP_SSLClient> tls;
  Client *io = &tcp;

  if (!tcp.connect(url_.host, url_.port, 8000)) {
    setError("cannot connect to the server");
    return false;
  }
  tcp.setTimeout(8);
  tcp.setNoDelay(true);
  if (url_.tls) {
    tls.reset(new (std::nothrow) ESP_SSLClient());
    if (!tls) {
      setError("no memory for TLS");
      tcp.stop();
      return false;
    }
    tls->setClient(&polite);
    tls->setBufferSizes(TLS_RX_BUFFER, TLS_TX_BUFFER);
    tls->setHandshakeTimeout(8);
    if (verify_) {
      tls->setCACert(ROOTS_PEM);
      tls->setX509Time(now_.load());
    } else {
      tls->setInsecure();
    }
    if (!tls->connect(url_.host, url_.port)) {
      setError("TLS handshake failed");
      tcp.stop();
      return false;
    }
    io = tls.get();
  }

  // HTTP/1.0: the answer then comes as it is, not in chunks, until the connection closes.
  char request[sizeof(url_.host) + sizeof(url_.path) + 96];
  const int request_len = snprintf(request, sizeof(request),
                                   "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: domlamp-wled/1\r\n"
                                   "Accept: text/event-stream\r\n\r\n",
                                   url_.path, url_.host);
  io->write(reinterpret_cast<const uint8_t *>(request), (size_t)request_len);

  char line[LINK_LINE_LEN + 8];
  size_t len = 0;
  bool overflow = false, up = false;
  int headerLines = 0;                       // -1 once the headers are over
  uint32_t lastData = millis();
  while (!stop_.load()) {
    const int available = io->available();
    if (available <= 0) {
      if (!io->connected()) {
        if (!up) setError("the server closed the stream");
        break;
      }
      if (millis() - lastData > SILENCE_MS) {
        setError("the stream went silent");
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    lastData = millis();
    uint8_t chunk[96];
    const int got = io->read(chunk, min((size_t)available, sizeof(chunk)));
    for (int k = 0; k < got; k++) {
      const char c = (char)chunk[k];
      if (c != '\n') {
        if (len < sizeof(line) - 1) line[len++] = c;
        else overflow = true;
        continue;
      }
      line[len] = 0;
      if (headerLines >= 0) {
        if (headerLines == 0 && !strstr(line, " 200")) {          // the status line
          snprintf(error_, sizeof(error_), "the relay answered: %.24s", line);
          io->stop();
          tcp.stop();
          return false;
        }
        const bool blank = len == 0 || (len == 1 && line[0] == '\r');
        headerLines = blank && !overflow ? -1 : headerLines + 1;
        if (headerLines < 0) {
          up = true;
          error_[0] = 0;
          connects_.fetch_add(1);
          connected_.store(true);
        }
      } else if (!overflow) {
        LinkFrame frame = {};
        bool is_frame = false;
        if (line[0] == 'X') {
          frame.end = true;
          is_frame = true;
        } else if (parseLinkLine(line, len, frame.zones)) {
          is_frame = true;
          frames_.fetch_add(1);
        }
        if (is_frame) {
          portENTER_CRITICAL(&frameLock);
          frame_ = frame;
          fresh_.store(true);
          portEXIT_CRITICAL(&frameLock);
        }
      }
      len = 0;
      overflow = false;
    }
  }
  io->stop();
  tcp.stop();
  return up;
}

}  // namespace domlamp

#endif  // ARDUINO_ARCH_ESP32
