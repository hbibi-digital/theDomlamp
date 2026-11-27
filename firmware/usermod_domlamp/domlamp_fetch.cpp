// See domlamp_fetch.h. The TLS library and its switches: domlamp_net.h.
#ifdef ARDUINO_ARCH_ESP32

#include "domlamp_fetch.h"

#include "domlamp_net.h"

#include <memory>
#include <new>

#include "domlamp_certs.h"

namespace domlamp {

namespace {

constexpr size_t RESPONSE_MAX = 6144;        // headers (about 600 bytes) plus a plan with every entry used
constexpr uint32_t DEADLINE_MS = 20000;      // for the whole fetch
constexpr uint32_t TASK_STACK = 12288;       // BearSSL's elliptic curve and RSA code works on the stack
constexpr uint32_t HEAP_NEEDED = 60000;      // free in total: a TLS fetch takes about 45 kB for a few seconds
constexpr uint32_t BLOCK_NEEDED = 20000;     // in one piece: the receive buffer

void setError(FetchResult &result, const char *text) {
  strlcpy(result.error, text, sizeof(result.error));
}

}  // namespace

bool Fetcher::start(const FetchRequest &request) {
  if (state_.load() == State::Running) return false;
  request_ = request;
  state_.store(State::Running);
  // On a dual-core ESP32 the task goes to core 0, away from WLED's loop on core 1. On a single core (C3)
  // it shares the core with the loop at the same priority.
  const BaseType_t made = xTaskCreateUniversal(taskMain, "domlamp", TASK_STACK, this, 1, nullptr, 0);
  if (made != pdPASS) {
    result_ = FetchResult{};
    setError(result_, "no memory for the fetch task");
    state_.store(State::Done);
    return false;
  }
  return true;
}

const FetchResult *Fetcher::take() {
  if (state_.load() != State::Done) return nullptr;
  state_.store(State::Idle);
  return &result_;
}

void Fetcher::taskMain(void *self) {
  Fetcher *fetcher = static_cast<Fetcher *>(self);
  const uint32_t started = millis();
  fetcher->run();
  fetcher->result_.took_ms = millis() - started;
  fetcher->result_.stack_free = uxTaskGetStackHighWaterMark(nullptr);
  fetcher->state_.store(State::Done);
  vTaskDelete(nullptr);
}

void Fetcher::run() {
  result_ = FetchResult{};
  const Url &url = request_.url;
  const uint32_t started = millis();

  if (url.tls && url.port != 443) {
    // ESP_SSLClient decides by the port number whether connect() does a handshake; on any other port it
    // would talk plain text to an https address.
    setError(result_, "https works on port 443 only");
    return;
  }
  if (url.tls && (ESP.getFreeHeap() < HEAP_NEEDED || ESP.getMaxAllocHeap() < BLOCK_NEEDED)) {
    setError(result_, "not enough free memory for TLS");
    return;
  }
  char *buf = static_cast<char *>(malloc(RESPONSE_MAX + 1));
  if (!buf) {
    setError(result_, "no memory for the response");
    return;
  }

  // Scope: the clients are gone before the buffer is freed.
  {
    WiFiClient tcp;
    PoliteClient polite(tcp);
    std::unique_ptr<ESP_SSLClient> tls;        // on the heap: it holds the whole BearSSL context
    Client *io = &tcp;

    if (!tcp.connect(url.host, url.port, 8000)) {
      setError(result_, "cannot connect to the server");
      free(buf);
      return;
    }
    tcp.setTimeout(8);                         // seconds, for each read

    if (url.tls) {
      tls.reset(new (std::nothrow) ESP_SSLClient());
      if (!tls) {
        setError(result_, "no memory for TLS");
        tcp.stop();
        free(buf);
        return;
      }
      tls->setClient(&polite);
      tls->setBufferSizes(TLS_RX_BUFFER, TLS_TX_BUFFER);
      tls->setHandshakeTimeout(8);             // seconds; below the 10 s of the task watchdog
      if (request_.verify) {
        tls->setCACert(ROOTS_PEM);
        tls->setX509Time(request_.now);        // WLED never sets the system clock, so hand over its own
      } else {
        tls->setInsecure();
      }
      // The TCP connection is open already, so this only does the handshake (with the host name for SNI
      // and for the name check).
      if (!tls->connect(url.host, url.port)) {
        // The library has no last error before its buffers exist. They were just released again, so if
        // they fit now they fitted a moment ago and the error is there to read.
        char reason[48] = "";
        if (ESP.getMaxAllocHeap() > (uint32_t)(TLS_RX_BUFFER + TLS_TX_BUFFER + 1024)) {
          tls->getLastSSLError(reason, sizeof(reason));
          snprintf(result_.error, sizeof(result_.error), "TLS: %s", reason[0] ? reason : "handshake failed");
        } else {
          setError(result_, "TLS: out of memory");
        }
        tcp.stop();
        free(buf);
        return;
      }
      io = tls.get();
    }

    char request[sizeof(url.host) + sizeof(url.path) + 128];
    const int request_len = snprintf(request, sizeof(request),
                                     "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: domlamp-wled/1\r\n"
                                     "Accept: application/json\r\nConnection: close\r\n\r\n",
                                     url.path, url.host);
    io->write(reinterpret_cast<const uint8_t *>(request), (size_t)request_len);

    size_t len = 0;
    HttpResponse response = {};
    HttpState state = HttpState::Incomplete;
    while (state == HttpState::Incomplete) {
      if (millis() - started > DEADLINE_MS) break;
      const int available = io->available();
      if (available > 0) {
        if (len >= RESPONSE_MAX) {
          state = HttpState::Bad;              // more than a plan can be
          break;
        }
        const int got = io->read(reinterpret_cast<uint8_t *>(buf) + len, RESPONSE_MAX - len);
        if (got > 0) len += (size_t)got;
        state = parseHttpResponse(buf, len, false, response);
      } else if (!io->connected()) {
        state = parseHttpResponse(buf, len, true, response);
        break;
      } else {
        vTaskDelay(pdMS_TO_TICKS(10));
      }
    }
    io->stop();
    tcp.stop();

    result_.http_status = response.status;
    if (state == HttpState::Incomplete) {
      setError(result_, "the server did not answer in time");
    } else if (state == HttpState::Bad) {
      setError(result_, len >= RESPONSE_MAX ? "answer too large" : "not an HTTP answer");
    } else if (response.status != 200) {
      snprintf(result_.error, sizeof(result_.error), "HTTP %d", response.status);
    } else {
      const ParseError parsed = parsePlan(response.body, response.body_len, result_.plan);
      if (parsed == ParseError::Ok) result_.ok = true;
      else setError(result_, parseErrorText(parsed));
    }
  }
  free(buf);
}

}  // namespace domlamp

#endif  // ARDUINO_ARCH_ESP32
