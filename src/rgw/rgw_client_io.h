// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#ifndef CEPH_RGW_CLIENT_IO_H
#define CEPH_RGW_CLIENT_IO_H

#include <string>
#include <streambuf>
#include <istream>
#include <stdlib.h>

#include "include/types.h"

#include "rgw_common.h"

class RGWClientIO {
protected:
  RGWEnv env;

  virtual void init_env(CephContext *cct) = 0;

public:
  virtual ~RGWClientIO() {}

  void init(CephContext *cct);
  RGWEnv& get_env() {
    return env;
  }

  virtual int complete_request() = 0; /* XXX signature likely changing */
}; /* RGWClient IO */


class RGWClientIOAccounter {
public:
  virtual ~RGWClientIOAccounter() {}

  virtual void set_account(bool enabled) = 0;

  virtual uint64_t get_bytes_sent() const = 0;
  virtual uint64_t get_bytes_received() const = 0;
};


class RGWStreamIOEngine : public RGWClientIO {
  virtual int read_data(char *buf, int max) = 0;
  virtual int write_data(const char *buf, int len) = 0;
public:
  virtual int send_status(int status, const char *status_name) = 0;
  virtual int send_100_continue() = 0;
  virtual int complete_header() = 0;
  virtual int send_content_length(uint64_t len) = 0;
  virtual void flush() = 0;

  /* High-level utilities to move out to a client's facade. Those functions
   * are not intended for overriding by a front-end glue code. That's the
   * reason why they aren't virtuals. */
  int print(const char *format, ...);
  int write(const char *buf, int len);
  int read(char *buf, int max, int *actual);
};


/* HTTP IO: compatibility layer */
class RGWStreamIO : public RGWStreamIOEngine,
                    public RGWClientIOAccounter {
  bool _account;
  size_t bytes_sent;
  size_t bytes_received;

  SHA256 *sha256_hash;

  bool account() const {
    return _account;
  }

public:
  virtual ~RGWStreamIO() {}
  RGWStreamIO()
    : _account(false),
      bytes_sent(0),
      bytes_received(0),
      sha256_hash(nullptr) {
  }

  int write(const char *buf, int len);

  int read(char *buf, int max, int *actual);
  int read(char *buf, int max, int *actual, bool hash);

  std::string grab_aws4_sha256_hash();

  void set_account(bool _accnt) override {
    _account = _accnt;
  }

  uint64_t get_bytes_sent() const override {
    return bytes_sent;
  }

  uint64_t get_bytes_received() const override {
    return bytes_received;
  }
}; /* RGWStreamIO */


class RGWClientIOStreamBuf : public std::streambuf {
protected:
  RGWStreamIO &sio;
  std::size_t const window_size;
  std::size_t const putback_size;
  std::vector<char> buffer;

public:
  RGWClientIOStreamBuf(RGWStreamIO &s, std::size_t ws, std::size_t ps = 1)
    : sio(s),
      window_size(ws),
      putback_size(ps),
      buffer(ws + ps)
  {
    setg(nullptr, nullptr, nullptr);
  }

  std::streambuf::int_type underflow() {
    if (gptr() < egptr()) {
      return traits_type::to_int_type(*gptr());
    }

    char * const base = buffer.data();
    char * start;

    if (nullptr != eback()) {
      /* We need to skip moving bytes on first underflow. In such case
       * there is simply no previous data we should preserve for unget()
       * or something similar. */
      std::memmove(base, egptr() - putback_size, putback_size);
      start = base + putback_size;
    } else {
      start = base;
    }

    int read_len;
    int ret = sio.read(base, window_size, &read_len);
    if (ret < 0 || 0 == read_len) {
      return traits_type::eof();
    }

    setg(base, start, start + read_len);

    return traits_type::to_int_type(*gptr());
  }
};

class RGWClientIOStream : private RGWClientIOStreamBuf, public std::istream {
/* Inheritance from RGWClientIOStreamBuf is a kind of shadow, undirect
 * form of composition here. We cannot do that explicitly because istream
 * ctor is being called prior to construction of any member of this class. */

public:
  explicit RGWClientIOStream(RGWStreamIO &s)
    : RGWClientIOStreamBuf(s, 1, 2),
      istream(static_cast<RGWClientIOStreamBuf *>(this)) {
  }
};

#endif /* CEPH_RGW_CLIENT_IO_H */
