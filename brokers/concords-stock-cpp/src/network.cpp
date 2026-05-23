#include "network.h"

#include <arpa/inet.h>
#include <curl/curl.h>
#include <dirent.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <cstdlib>
#include <format>
#include <mutex>
#include <string>

// ---------- TCP info collection ----------

namespace {

bool isFdConnectedToPort(int fd, int target_port) {
  sockaddr_storage addr{};
  socklen_t len = sizeof(addr);
  if (getpeername(fd, reinterpret_cast<sockaddr*>(&addr), &len) < 0) {
    return false;
  }
  uint16_t port = 0;
  if (addr.ss_family == AF_INET) {
    port = ntohs(reinterpret_cast<sockaddr_in*>(&addr)->sin_port);
  } else if (addr.ss_family == AF_INET6) {
    port = ntohs(reinterpret_cast<sockaddr_in6*>(&addr)->sin6_port);
  } else {
    return false;
  }
  return port == target_port;
}

int findFdForPort(int target_port) {
  DIR* dir = opendir("/proc/self/fd");
  if (!dir) return -1;
  int found = -1;
  while (dirent* ent = readdir(dir)) {
    if (ent->d_name[0] == '.') continue;
    int fd = std::atoi(ent->d_name);
    if (fd <= 0) continue;
    if (isFdConnectedToPort(fd, target_port)) {
      found = fd;
      break;
    }
  }
  closedir(dir);
  return found;
}

struct FdCacheEntry {
  int port = -1;
  int fd = -1;
};
std::mutex fd_cache_mu;
FdCacheEntry fd_cache;

int getOrFindFd(int target_port) {
  std::lock_guard<std::mutex> lock(fd_cache_mu);
  if (fd_cache.port == target_port && fd_cache.fd >= 0 &&
      isFdConnectedToPort(fd_cache.fd, target_port)) {
    return fd_cache.fd;
  }
  int fd = findFdForPort(target_port);
  fd_cache = (fd >= 0) ? FdCacheEntry{target_port, fd} : FdCacheEntry{};
  return fd;
}

void invalidateFdCache() {
  std::lock_guard<std::mutex> lock(fd_cache_mu);
  fd_cache = {};
}

}  // namespace

TcpInfoSnapshot collectTcpInfo(int remote_port) {
  TcpInfoSnapshot snap;
  int fd = getOrFindFd(remote_port);
  if (fd < 0) return snap;

  tcp_info ti{};
  socklen_t len = sizeof(ti);
  if (getsockopt(fd, IPPROTO_TCP, TCP_INFO, &ti, &len) < 0) {
    invalidateFdCache();  // stale FD; force re-discovery next call
    return snap;
  }

  snap.rtt_us = ti.tcpi_rtt;
  snap.rttvar_us = ti.tcpi_rttvar;
  snap.snd_cwnd = ti.tcpi_snd_cwnd;
  snap.total_retrans = ti.tcpi_total_retrans;
  return snap;
}

// ---------- NetworkProbe ----------

NetworkProbe::NetworkProbe(const std::string& host, int port)
    : host_(host), port_(port) {}

namespace {

void setProbeOpts(CURL* curl, const std::string& url, CURLSH* share) {
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1L);
  curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1L);
  if (share) curl_easy_setopt(curl, CURLOPT_SHARE, share);
}

}  // namespace

NetworkProbeResult NetworkProbe::probe() {
  NetworkProbeResult r;
  std::string url = std::format("https://{}:{}", host_, port_);

  CURLSH* share = curl_share_init();
  if (!share) {
    r.error = "curl_share_init failed";
    return r;
  }
  curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);

  // --- Cold probe ---
  CURL* curl1 = curl_easy_init();
  if (!curl1) {
    curl_share_cleanup(share);
    r.error = "curl_easy_init failed";
    return r;
  }
  setProbeOpts(curl1, url, share);

  CURLcode res = curl_easy_perform(curl1);
  if (res != CURLE_OK) {
    r.error = curl_easy_strerror(res);
    curl_easy_cleanup(curl1);
    curl_share_cleanup(share);
    return r;
  }

  double dns_t = 0, conn_t = 0, app_t = 0;
  curl_easy_getinfo(curl1, CURLINFO_NAMELOOKUP_TIME, &dns_t);
  curl_easy_getinfo(curl1, CURLINFO_CONNECT_TIME, &conn_t);
  curl_easy_getinfo(curl1, CURLINFO_APPCONNECT_TIME, &app_t);

  r.cold_ok = true;
  r.dns_ms = dns_t * 1000.0;
  r.tcp_handshake_ms = (conn_t - dns_t) * 1000.0;
  r.tls_handshake_ms = (app_t - conn_t) * 1000.0;
  curl_easy_cleanup(curl1);

  // --- Warm probe  ---
  CURL* curl2 = curl_easy_init();
  if (!curl2) {
    curl_share_cleanup(share);
    return r;  // cold metrics still valid
  }
  setProbeOpts(curl2, url, share);

  res = curl_easy_perform(curl2);
  if (res == CURLE_OK) {
    double conn2 = 0, app2 = 0;
    curl_easy_getinfo(curl2, CURLINFO_CONNECT_TIME, &conn2);
    curl_easy_getinfo(curl2, CURLINFO_APPCONNECT_TIME, &app2);
    double warm_tls_ms = (app2 - conn2) * 1000.0;
    r.tls_handshake_resumed_ms = warm_tls_ms;
    r.resumption_supported = r.tls_handshake_ms.has_value() &&
                             warm_tls_ms < *r.tls_handshake_ms * 0.7;
  } else {
    r.warm_error = curl_easy_strerror(res);
  }
  curl_easy_cleanup(curl2);
  curl_share_cleanup(share);
  return r;
}
