#ifndef NETWORK_H
#define NETWORK_H

#include <cstdint>
#include <optional>
#include <string>

struct NetworkProbeResult {
  bool cold_ok = false;
  std::optional<double> dns_ms;
  std::optional<double> tcp_handshake_ms;
  std::optional<double> tls_handshake_ms;

  std::optional<double> tls_handshake_resumed_ms;
  std::optional<bool> resumption_supported;

  std::string error;
  std::string warm_error;
};

struct TcpInfoSnapshot {
  std::optional<uint32_t> rtt_us;
  std::optional<uint32_t> rttvar_us;
  std::optional<uint32_t> snd_cwnd;
  std::optional<uint32_t> total_retrans;
};

class NetworkProbe {
 public:
  NetworkProbe(const std::string& host, int port);
  NetworkProbeResult probe();

 private:
  std::string host_;
  int port_;
};

TcpInfoSnapshot collectTcpInfo(int remote_port);

#endif  // NETWORK_H
