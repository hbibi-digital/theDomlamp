// What the plan fetch (domlamp_fetch.cpp) and the link stream (domlamp_link.cpp) share: the TLS library
// with the same switches in both, and the TCP client that keeps the core free while TLS waits.
//
// WLED 16 builds on a framework whose mbedTLS has TLS switched off (CONFIG_MBEDTLS_TLS_DISABLED) and that
// ships no WiFiClientSecure, so the TLS here is BearSSL, through the ESP_SSLClient library, on top of a
// plain WiFiClient. BearSSL speaks TLS 1.2.
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>

// Keep BearSSL's client context inside the ESP_SSLClient object instead of in a separate allocation that
// the library frees when the handshake fails: only then can the reason of the failure still be read.
#define STATIC_SSLCLIENT_CONTEXT
// WLED sets BEARSSL_SSL_BASIC for every build (meant for the ESP8266 core). In this library it would
// switch off elliptic curves, and the plan server's certificate chain is ECDSA.
#undef BEARSSL_SSL_BASIC
#include <ESP_SSLClient.h>

namespace domlamp {

constexpr int TLS_RX_BUFFER = 16384;         // a full TLS record: the server does not negotiate smaller ones
constexpr int TLS_TX_BUFFER = 1024;          // a request is about 150 bytes

// The TCP connection as the TLS library sees it. The library polls available() in a tight loop while it
// waits for the server; sleeping here when nothing has arrived gives the core back to WLED's loop (on a
// single-core chip) and to the idle task, whose watchdog would otherwise restart the lamp after 10 s.
class PoliteClient : public Client {
 public:
  explicit PoliteClient(WiFiClient &tcp) : tcp_(tcp) {}
  int connect(IPAddress ip, uint16_t port) override { return tcp_.connect(ip, port); }
  int connect(const char *host, uint16_t port) override { return tcp_.connect(host, port); }
  size_t write(uint8_t b) override { return tcp_.write(b); }
  size_t write(const uint8_t *buf, size_t size) override { return tcp_.write(buf, size); }
  int available() override {
    const int n = tcp_.available();
    if (n <= 0) vTaskDelay(pdMS_TO_TICKS(2));
    return n;
  }
  int read() override { return tcp_.read(); }
  int read(uint8_t *buf, size_t size) override { return tcp_.read(buf, size); }
  int peek() override { return tcp_.peek(); }
  void flush() override { tcp_.flush(); }
  void stop() override { tcp_.stop(); }
  uint8_t connected() override { return tcp_.connected(); }
  operator bool() override { return tcp_.connected(); }

 private:
  WiFiClient &tcp_;
};

}  // namespace domlamp
