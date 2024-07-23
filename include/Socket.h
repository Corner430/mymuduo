#pragma once

#include "noncopyable.h"

class InetAddress;

/*
 * Wrapper of socket file descriptor.
 *
 * It closes the sockfd when desctructs.
 * It's thread safe, all operations are delagated to OS. */
class Socket : noncopyable {
public:
  explicit Socket(int sockfd) : sockfd_(sockfd) {}

  // Socket(Socket&&) // move constructor in C++11
  ~Socket();

  int fd() const { return sockfd_; }

  /* abort if address in use */
  void bindAddress(const InetAddress &localaddr);
  void listen();

  /* On success, returns a non-negative integer that is
   * a descriptor for the accepted socket, which has been
   * set to non-blocking and close-on-exec. *peeraddr is assigned.
   * On error, -1 is returned, and *peeraddr is untouched. */
  int accept(InetAddress *peeraddr);

  void shutdownWrite();

  // Enable/disable TCP_NODELAY (disable/enable Nagle's algorithm).
  void setTcpNoDelay(bool on);
  void setReuseAddr(bool on); // Enable/disable SO_REUSEADDR
  void setReusePort(bool on); // Enable/disable SO_REUSEPORT
  void setKeepAlive(bool on); // Enable/disable SO_KEEPALIVE

private:
  const int sockfd_;
};