#include "cesdial.h"

#include "cesidentity.h"
#include "names.h"

#include <ces/account.h>
#include <ces/buffer.h>
#include <ces/cesplex/wire.h>
#include <ces/l2/compute_client.h>
#include <ces/l2/file_client.h>
#include <ces/types.h>
#include <ces/util/resolver.h>
#include <ces/util/wallet.h>

#include <minx/minx.h>
#include <minx/rudp/rudp.h>
#include <minx/rudp/rudp_stream.h>
#include <minx/stdext.h>
#include <minx/types.h>

#include <boost/asio/buffer.hpp>
#include <boost/asio/buffers_iterator.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <set>
#include <random>
#include <span>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace cwb {
namespace {

constexpr const char* kLuaProto = "/ces/lua/1";
constexpr const char* kLuaRpcProto = "/ces/luarpc/1";
constexpr uint8_t kVerbAttach = 0x01;
constexpr auto kBindTimeout = std::chrono::seconds(10);
constexpr auto kAttachTimeout = std::chrono::seconds(10);

uint64_t nowMicros() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

class NoopListener : public minx::MinxListener {};

// Outbound-only Rudp listener: forward onSend to the local Minx.
class DialRudpListener : public minx::Rudp::Listener {
 public:
  void setMinx(minx::Minx* m) { minx_ = m; }
  void onSend(const minx::SockAddr& peer, const minx::Bytes& bytes) override {
    if (!minx_) return;
    try {
      minx_->sendExtension(peer, bytes);
    } catch (const std::exception&) {
    }
  }

 private:
  minx::Minx* minx_ = nullptr;
};

bool resolvePeer(const std::string& host, uint16_t port, minx::SockAddr& out,
                 std::string& err) {
  boost::asio::io_context ioc;
  boost::asio::ip::udp::resolver res(ioc);
  boost::system::error_code ec;
  auto results = res.resolve(host, std::to_string(port), ec);
  if (ec || results.empty()) {
    err = "resolve(" + host + "): " +
          (ec ? ec.message() : std::string("no results"));
    return false;
  }
  auto addr = results.begin()->endpoint().address();
  if (addr.is_v4()) {
    addr = boost::asio::ip::make_address_v6(boost::asio::ip::v4_mapped,
                                            addr.to_v4());
  }
  out = minx::SockAddr(addr, port);
  return true;
}

// Native CesPlex dialer: MINX + Rudp + two io threads, ported from cesh's dial.
class Dialer {
 public:
  using WorkGuard = boost::asio::executor_work_guard<
      boost::asio::io_context::executor_type>;

  ~Dialer() { stop(); }

  std::string start(const minx::SockAddr& peer) {
    peer_ = peer;

    minx::MinxConfig mc{};
    mc.instanceName = "cwb-dial";
    mc.randomXVMsToKeep = 0;
    mc.randomXInitThreads = 0;
    mc.trustLoopback = true;
    minx_ = std::make_unique<minx::Minx>(&listener_, mc);

    minx::RudpConfig rcfg{};
    rcfg.maxChannelsPerPeer = 2;
    rcfg.baseTickInterval = std::chrono::milliseconds(1);
    rudpListener_.setMinx(minx_.get());
    rudp_ = std::make_unique<minx::Rudp>(&rudpListener_, rcfg);

    {
      minx::MinxStdExtensions stdExt;
      stdExt.registerExtension(
          minx::Rudp::KEY_V0,
          [this](const minx::SockAddr& p, uint64_t key,
                 const minx::Bytes& payload) {
            if (rudp_) rudp_->onPacket(p, key, payload, nowMicros());
          });
      minx_->setExtensionHandler(std::move(stdExt).build());
    }

    boundPort_ = minx_->openSocket(boost::asio::ip::address_v6::any(), 0, netIO_,
                                   taskIO_);
    if (boundPort_ == 0) return "failed to open local UDP socket";

    netGuard_ = std::make_unique<WorkGuard>(netIO_.get_executor());
    taskGuard_ = std::make_unique<WorkGuard>(taskIO_.get_executor());
    netThread_ = std::thread([this]() { netIO_.run(); });
    taskThread_ = std::thread([this]() { taskIO_.run(); });

    tickTimer_ = std::make_shared<boost::asio::steady_timer>(taskIO_);
    boost::asio::post(taskIO_, [this]() { scheduleTick(); });

    std::mt19937 rng(std::random_device{}());
    channel_ = 0;
    while (channel_ == 0) channel_ = rng();
    return "";
  }

  void stop() {
    if (!minx_) return;
    if (tickTimer_) {
      boost::system::error_code ec;
      tickTimer_->cancel();
    }
    minx_->closeSocket(false);
    if (netGuard_) netGuard_->reset();
    if (taskGuard_) taskGuard_->reset();
    netIO_.stop();
    taskIO_.stop();
    if (netThread_.joinable()) netThread_.join();
    if (taskThread_.joinable()) taskThread_.join();
    stream_.reset();
    tickTimer_.reset();
    rudp_.reset();
    minx_.reset();
    rudpListener_.setMinx(nullptr);
    netGuard_.reset();
    taskGuard_.reset();
    boundPort_ = 0;
  }

  std::string bind(const ces::KeyPair& signer, uint64_t& sessionToken,
                   minx::Hash& serverPubkey, std::string proto = kLuaProto) {
    auto run = std::make_shared<std::promise<std::string>>();
    auto fut = run->get_future();
    auto tokenOut = std::make_shared<uint64_t>(0);
    auto pubkeyOut = std::make_shared<minx::Hash>();
    boost::asio::post(taskIO_, [this, &signer, proto, tokenOut, pubkeyOut, run]() {
      rudp_->tick(nowMicros());
      stream_ = std::make_shared<minx::RudpStream>(taskIO_.get_executor());
      if (!rudp_->registerChannel(peer_, channel_, stream_)) {
        run->set_value("rudp registerChannel failed");
        return;
      }
      const uint64_t bindNowUs = nowMicros();
      const std::string name = proto;
      auto bindReq = std::make_shared<minx::Bytes>(
          ces::buildBindRequest(name, bindNowUs, signer));
      const auto& pkArr = signer.getPublicKeyAsHash();
      auto clientDigest =
          std::make_shared<std::array<uint8_t, ces::CES_PLEX_SHA256_SIZE>>(
              ces::computeBindRequestDigest(
                  std::span<const uint8_t>(
                      reinterpret_cast<const uint8_t*>(name.data()),
                      name.size()),
                  bindNowUs,
                  std::span<const uint8_t>(pkArr.data(), pkArr.size())));

      boost::asio::async_write(
          *stream_, boost::asio::buffer(*bindReq),
          [this, bindReq, clientDigest, tokenOut, pubkeyOut, run](
              const boost::system::error_code& ec, std::size_t) {
            if (ec) {
              run->set_value("bind write: " + ec.message());
              return;
            }
            auto reply = std::make_shared<
                std::array<uint8_t, ces::CES_PLEX_BIND_REPLY_TOTAL_SIZE>>();
            boost::asio::async_read(
                *stream_, boost::asio::buffer(*reply),
                [reply, clientDigest, tokenOut, pubkeyOut, run](
                    const boost::system::error_code& ec2, std::size_t) {
                  if (ec2) {
                    run->set_value("bind read: " + ec2.message());
                    return;
                  }
                  auto r = ces::parseBindReply(
                      std::span<const uint8_t,
                                ces::CES_PLEX_BIND_REPLY_TOTAL_SIZE>(
                          reply->data(), reply->size()));
                  if (r.status != ces::CES_PLEX_OK) {
                    run->set_value("bind NACK from server");
                    return;
                  }
                  if (!ces::verifyBindReply(
                          r, std::span<const uint8_t>(clientDigest->data(),
                                                      clientDigest->size()))) {
                    run->set_value("bind reply verify failed");
                    return;
                  }
                  std::memcpy(pubkeyOut->data(), r.serverPubkey.data(),
                              pubkeyOut->size());
                  *tokenOut = r.channelSessionToken;
                  run->set_value("");
                });
          });
    });
    if (fut.wait_for(kBindTimeout) != std::future_status::ready)
      return "bind handshake timeout";
    std::string err = fut.get();
    if (err.empty()) {
      sessionToken = *tokenOut;
      serverPubkey = *pubkeyOut;
    }
    return err;
  }

  std::string attach(const ces::KeyPair& signer, uint64_t sessionToken,
                     uint64_t pid, uint8_t& outStatus, uint64_t& outConnId,
                     std::string& outHello) {
    ces::Bytes preamble;
    ces::Buffer::put<uint64_t>(preamble, pid);
    ces::Signature sig = ces::signPerOp(
        signer, kVerbAttach,
        std::span<const uint8_t>(preamble.data(), preamble.size()),
        sessionToken);

    const size_t totalSize = ces::CES_PLEX_VERB_SIZE +
                             ces::CES_PLEX_PREAMBLE_LEN_SIZE + preamble.size() +
                             sig.size();
    minx::Bytes wire(totalSize);
    minx::Buffer buf(wire);
    buf.put<uint8_t>(kVerbAttach);
    buf.put<uint32_t>(static_cast<uint32_t>(preamble.size()));
    buf.put(std::span<const uint8_t>(preamble.data(), preamble.size()));
    buf.put(sig);

    auto run = std::make_shared<std::promise<std::string>>();
    auto fut = run->get_future();
    auto wireBuf = std::make_shared<minx::Bytes>(std::move(wire));
    auto strm = stream_;
    auto statusOut = std::make_shared<uint8_t>(0xFF);
    auto connIdOut = std::make_shared<uint64_t>(0);
    auto helloOut = std::make_shared<std::string>();

    boost::asio::post(taskIO_, [strm, wireBuf, statusOut, connIdOut, helloOut,
                                run]() {
      boost::asio::async_write(
          *strm, boost::asio::buffer(*wireBuf),
          [strm, wireBuf, statusOut, connIdOut, helloOut, run](
              const boost::system::error_code& ec, std::size_t) {
            if (ec) {
              run->set_value("attach write: " + ec.message());
              return;
            }
            auto stBuf = std::make_shared<std::array<uint8_t, 1>>();
            boost::asio::async_read(
                *strm, boost::asio::buffer(*stBuf),
                [strm, stBuf, statusOut, connIdOut, helloOut, run](
                    const boost::system::error_code& ec2, std::size_t) {
                  if (ec2) {
                    run->set_value("attach read status: " + ec2.message());
                    return;
                  }
                  uint8_t status = (*stBuf)[0];
                  *statusOut = status;
                  if (status != ces::CES_OK) {
                    auto tr = std::make_shared<ces::Bytes>(
                        ces::CES_PLEX_RESP_TRAILER_SIZE);
                    boost::asio::async_read(
                        *strm, boost::asio::buffer(*tr),
                        [tr, run](const boost::system::error_code& ec3,
                                  std::size_t) {
                          run->set_value(
                              ec3 ? ("attach read tail: " + ec3.message())
                                  : std::string());
                        });
                    return;
                  }
                  // OK preamble: [connId u64][helloLen u32], then [hello][trailer].
                  auto head = std::make_shared<std::array<uint8_t, 12>>();
                  boost::asio::async_read(
                      *strm, boost::asio::buffer(*head),
                      [strm, head, connIdOut, helloOut, run](
                          const boost::system::error_code& ec3, std::size_t) {
                        if (ec3) {
                          run->set_value("attach read head: " + ec3.message());
                          return;
                        }
                        *connIdOut = ces::Buffer::peek<uint64_t>(head->data());
                        uint32_t helloLen = ces::Buffer::peek<uint32_t>(
                            std::span<const uint8_t>(head->data(), head->size()),
                            8);
                        auto rest = std::make_shared<ces::Bytes>(
                            static_cast<size_t>(helloLen) +
                            ces::CES_PLEX_RESP_TRAILER_SIZE);
                        boost::asio::async_read(
                            *strm, boost::asio::buffer(*rest),
                            [rest, helloLen, helloOut, run](
                                const boost::system::error_code& ec4,
                                std::size_t) {
                              if (!ec4 && helloLen > 0)
                                helloOut->assign(rest->begin(),
                                                 rest->begin() + helloLen);
                              run->set_value(
                                  ec4 ? ("attach read rest: " + ec4.message())
                                      : std::string());
                            });
                      });
                });
          });
    });
    if (fut.wait_for(kAttachTimeout) != std::future_status::ready)
      return "attach timeout";
    std::string err = fut.get();
    if (err.empty()) {
      outStatus = *statusOut;
      outConnId = *connIdOut;
      outHello = *helloOut;
    }
    return err;
  }

  std::string writeAll(std::span<const uint8_t> data) {
    if (!stream_) return "no stream";
    auto run = std::make_shared<std::promise<std::string>>();
    auto fut = run->get_future();
    auto strm = stream_;
    auto buf = std::make_shared<std::vector<uint8_t>>(data.begin(), data.end());
    boost::asio::post(taskIO_, [strm, buf, run]() {
      boost::asio::async_write(
          *strm, boost::asio::buffer(*buf),
          [buf, run](const boost::system::error_code& ec, std::size_t) {
            run->set_value(ec ? ("write: " + ec.message()) : std::string());
          });
    });
    if (fut.wait_for(std::chrono::seconds(15)) != std::future_status::ready)
      return "write timeout";
    return fut.get();
  }

  // Read until the peer closes the channel (HTTP/1.0 Connection: close).
  std::string readAll(std::string& out, std::chrono::milliseconds timeout) {
    if (!stream_) return "no stream";
    auto run =
        std::make_shared<std::promise<std::pair<std::string, std::string>>>();
    auto fut = run->get_future();
    auto strm = stream_;
    auto sbuf = std::make_shared<boost::asio::streambuf>();
    boost::asio::post(taskIO_, [strm, sbuf, run]() {
      boost::asio::async_read(
          *strm, *sbuf, boost::asio::transfer_all(),
          [sbuf, run](const boost::system::error_code& ec, std::size_t) {
            std::string data(boost::asio::buffers_begin(sbuf->data()),
                             boost::asio::buffers_end(sbuf->data()));
            if (ec && ec != boost::asio::error::eof)
              run->set_value({"read: " + ec.message(), std::move(data)});
            else
              run->set_value({std::string(), std::move(data)});
          });
    });
    if (fut.wait_for(timeout) != std::future_status::ready) return "read timeout";
    auto pr = fut.get();
    out = std::move(pr.second);
    return pr.first;
  }

  // Fire-and-forget write for a live duplex session.
  void postWrite(std::string data) {
    if (!stream_) return;
    auto strm = stream_;
    auto buf = std::make_shared<std::string>(std::move(data));
    boost::asio::post(taskIO_, [strm, buf]() {
      boost::asio::async_write(
          *strm, boost::asio::buffer(*buf),
          [buf](const boost::system::error_code&, std::size_t) {});
    });
  }

  // Persistent read loop: onData per chunk, onClose on EOF/error. Callbacks run
  // on the task io thread.
  void startReadLoop(std::function<void(std::string)> onData,
                     std::function<void(std::string)> onClose) {
    onData_ = std::move(onData);
    onClose_ = std::move(onClose);
    boost::asio::post(taskIO_, [this]() { readSome(); });
  }

 private:
  void readSome() {
    if (!stream_) return;
    auto strm = stream_;
    auto buf = std::make_shared<std::array<uint8_t, 4096>>();
    strm->async_read_some(
        boost::asio::buffer(*buf),
        [this, strm, buf](const boost::system::error_code& ec, std::size_t n) {
          if (ec) {
            if (onClose_) onClose_(ec.message());
            return;
          }
          if (n > 0 && onData_)
            onData_(std::string(buf->begin(), buf->begin() + n));
          readSome();
        });
  }

  void scheduleTick() {
    if (!tickTimer_ || !rudp_) return;
    tickTimer_->expires_after(std::chrono::milliseconds(10));
    tickTimer_->async_wait([this](const boost::system::error_code& ec) {
      if (ec || !rudp_) return;
      rudp_->tick(nowMicros());
      scheduleTick();
    });
  }

  NoopListener listener_;
  DialRudpListener rudpListener_;
  std::unique_ptr<minx::Minx> minx_;
  std::unique_ptr<minx::Rudp> rudp_;
  boost::asio::io_context netIO_;
  boost::asio::io_context taskIO_;
  std::unique_ptr<WorkGuard> netGuard_;
  std::unique_ptr<WorkGuard> taskGuard_;
  std::thread netThread_;
  std::thread taskThread_;
  std::shared_ptr<boost::asio::steady_timer> tickTimer_;
  uint16_t boundPort_ = 0;
  minx::SockAddr peer_;
  uint32_t channel_ = 0;
  std::shared_ptr<minx::RudpStream> stream_;
  std::function<void(std::string)> onData_;
  std::function<void(std::string)> onClose_;
};

}  // namespace

std::string cesLuaFetch(const std::string& host, uint16_t rpcPort, uint64_t pid,
                        const ces::KeyPair& signer, const std::string& request,
                        std::string& response, uint8_t& attachStatus) {
  attachStatus = 0xFF;
  minx::SockAddr peer;
  std::string err;
  if (!resolvePeer(host, rpcPort, peer, err)) return err;

  Dialer dialer;
  if (auto e = dialer.start(peer); !e.empty()) return e;

  uint64_t sessionToken = 0;
  minx::Hash serverPk{};
  if (auto e = dialer.bind(signer, sessionToken, serverPk); !e.empty()) return e;

  uint64_t connId = 0;
  std::string ignoredHello;
  if (auto e = dialer.attach(signer, sessionToken, pid, attachStatus, connId,
                             ignoredHello);
      !e.empty())
    return e;
  if (attachStatus != ces::CES_OK) return "ATTACH rejected";

  if (auto e = dialer.writeAll(std::span<const uint8_t>(
          reinterpret_cast<const uint8_t*>(request.data()), request.size()));
      !e.empty())
    return e;

  return dialer.readAll(response, std::chrono::seconds(15));
}

uint8_t cesFileFetch(const std::string& host, uint16_t rpcPort,
                     const ces::KeyPair& signer, const std::string& path,
                     std::string& out, uint64_t maxBytes) {
  out.clear();
  ces::CesFileClient fc;
  try {
    uint8_t rc = fc.connect(host, rpcPort, signer);
    if (rc != ces::CES_OK) return rc;
    ces::CesFileClient::StatInfo info;
    rc = fc.stat(path, info);
    if (rc != ces::CES_OK) {
      fc.disconnect();
      return rc;
    }
    // Refuse an oversized (or hostile-STAT) file up front instead of trying to
    // buffer it all in RAM: a large video used to bad_alloc here and, on this
    // detached worker thread, an uncaught throw terminated the whole browser.
    if (info.size > maxBytes) {
      fc.disconnect();
      return CWB_FETCH_TOO_LARGE;
    }
    out.reserve(std::min<uint64_t>(info.size, 16ull * 1024 * 1024));
    uint64_t off = 0;
    while (off < info.size) {
      const uint32_t chunk = static_cast<uint32_t>(
          std::min<uint64_t>(info.size - off, 1024 * 1024));
      ces::Bytes buf;
      minx::Hash rangeHash{};
      rc = fc.read(path, off, chunk, buf, rangeHash);
      if (rc != ces::CES_OK) {
        fc.disconnect();
        out.clear();
        return rc;
      }
      if (buf.empty()) break;  // no-progress guard against a lying STAT size
      out.append(reinterpret_cast<const char*>(buf.data()), buf.size());
      off += buf.size();
      if (out.size() > maxBytes) {  // STAT undersized; stop before OOM
        fc.disconnect();
        out.clear();
        return CWB_FETCH_TOO_LARGE;
      }
    }
    fc.disconnect();
    return ces::CES_OK;
  } catch (...) {
    try {
      fc.disconnect();
    } catch (...) {
    }
    out.clear();
    return CWB_FETCH_INTERNAL;
  }
}

uint8_t cesFileDownload(
    const std::string& host, uint16_t rpcPort, const ces::KeyPair& signer,
    const std::string& path,
    const std::function<bool(const uint8_t*, size_t)>& sink,
    const std::function<void(uint64_t, uint64_t)>& progress) {
  ces::CesFileClient fc;
  try {
    uint8_t rc = fc.connect(host, rpcPort, signer);
    if (rc != ces::CES_OK) return rc;
    ces::CesFileClient::StatInfo info;
    rc = fc.stat(path, info);
    if (rc != ces::CES_OK) {
      fc.disconnect();
      return rc;
    }
    if (progress) progress(0, info.size);
    uint64_t off = 0;
    while (off < info.size) {
      const uint32_t chunk = static_cast<uint32_t>(
          std::min<uint64_t>(info.size - off, 1024 * 1024));
      ces::Bytes buf;
      minx::Hash rangeHash{};
      rc = fc.read(path, off, chunk, buf, rangeHash);
      if (rc != ces::CES_OK) {
        fc.disconnect();
        return rc;
      }
      if (buf.empty()) break;  // no-progress guard against a lying STAT size
      if (!sink(buf.data(), buf.size())) {  // disk write failed
        fc.disconnect();
        return CWB_FETCH_INTERNAL;
      }
      off += buf.size();
      if (progress) progress(off, info.size);
    }
    fc.disconnect();
    return ces::CES_OK;
  } catch (...) {
    try {
      fc.disconnect();
    } catch (...) {
    }
    return CWB_FETCH_INTERNAL;
  }
}

uint8_t cesComputeFetch(const std::string& host, uint16_t rpcPort,
                        const ces::KeyPair& signer, const std::string& path,
                        std::string& out) {
  out.clear();
  ces::CesComputeClient cc;
  uint8_t rc = cc.connect(host, rpcPort, signer);
  if (rc != ces::CES_OK) return rc;
  std::vector<ces::CesComputeClient::InstanceInfo> inst;
  const bool byPath = (!path.empty() && path != "/");
  rc = byPath ? cc.instances(path, inst) : cc.list(inst);
  cc.disconnect();
  if (rc != ces::CES_OK) return rc;

  const uint64_t nowUs = nowMicros();
  auto uptime = [](uint64_t s) {
    std::ostringstream o;
    if (s >= 3600) o << (s / 3600) << "h " << ((s % 3600) / 60) << "m";
    else if (s >= 60) o << (s / 60) << "m " << (s % 60) << "s";
    else o << s << "s";
    return o.str();
  };

  std::ostringstream h;
  h << "<!doctype html><html><head><meta charset=utf-8><style>"
       "body{font-family:sans-serif;max-width:780px;margin:32px auto;padding:0 "
       "20px;color:#222}h1{font-size:22px;margin-bottom:2px}"
       ".src{color:#666;font-size:13px}table{width:100%;border-collapse:collapse;"
       "margin-top:16px;font-size:14px}th{text-align:left;color:#666;"
       "border-bottom:2px solid #ddd;padding:7px 8px}td{border-bottom:1px solid "
       "#eee;padding:7px 8px}.mono{font-family:monospace}.empty{color:#888;"
       "margin-top:22px}</style></head><body><h1>Compute instances</h1>"
       "<div class=src>host <span class=mono>"
    << host << ":" << rpcPort << "</span> &middot; "
    << (byPath ? path : std::string("your instances")) << "</div>"
       "<object type=\"application/x-cwb-compute\" width=\"560\" height=\"200\">"
       "</object>";
  if (inst.empty()) {
    h << "<p class=empty>No running instances"
      << (byPath ? (" under " + path) : std::string()) << ".</p>";
  } else {
    h << "<table><tr><th>pid</th><th>source</th><th>uptime</th><th>cpu</th>"
         "<th>rss</th><th>open</th></tr>";
    for (const auto& i : inst) {
      const uint64_t upS =
          (nowUs > i.startedAtUs) ? (nowUs - i.startedAtUs) / 1000000 : 0;
      h << "<tr><td class=mono>" << i.pid << "</td><td>";
      if (i.sourceName.empty()) {
        h << "?";
      } else {
        // The source lives in the same server's file store; link it so a click
        // renders the program text over /ces/file/1.
        h << "<a href=\"file://" << host << ":" << rpcPort << i.sourceName
          << "\">" << i.sourceName << "</a>";
      }
      h << "</td><td>" << uptime(upS) << "</td><td>" << (i.cpuBasisPoints / 100)
        << "%</td><td>" << (i.rssBytes / (1024 * 1024)) << " MB</td><td>"
        << "<a href=\"lua://" << i.pid << "@" << host << ":" << rpcPort
        << "/\">relay</a>";
      if (i.rpcPort)
        h << " <a href=\"luarpc://" << host << ":" << i.rpcPort
          << "/\">direct</a>";
      h << "</td></tr>";
    }
    h << "</table>";
  }
  h << "</body></html>";
  out = h.str();
  return ces::CES_OK;
}

// Render a raw credit-unit count as whole.fraction with 8 decimals.
static std::string fmtCredits(int64_t units) {
  const bool neg = units < 0;
  const uint64_t u =
      neg ? static_cast<uint64_t>(-units) : static_cast<uint64_t>(units);
  char buf[40];
  std::snprintf(buf, sizeof buf, "%s%llu.%08llu", neg ? "-" : "",
                static_cast<unsigned long long>(u / ces::PRICE_UNIT),
                static_cast<unsigned long long>(u % ces::PRICE_UNIT));
  return buf;
}

uint8_t cesServerDirectoryFetch(const std::string& host, uint16_t port,
                                std::string& out) {
  out.clear();
  const ces::KeyPair identity = loadOrCreateIdentity();
  const ces::Hash ownKey = identity.getPublicKeyAsHash();
  int64_t balance = 0;
  uint32_t nonce = 0;
  uint8_t accountRc = ces::CES_ERROR_TARGET_NOT_FOUND;
  ces::HashPrefix serverId{};
  uint8_t minDifficulty = 0;
  uint16_t rpcPort = 0;
  std::vector<std::pair<std::string, std::string>> peers;

  try {
    const auto ep =
        ces::Resolver::resolveUdp(host + ":" + std::to_string(port));
    ces::ClientSession sess(true, static_cast<uint16_t>(0), ep, nullptr, 2);
    accountRc = sess.client().queryAccount(ces::Account::getMapKey(ownKey),
                                           balance, nonce);
    if (!sess.client().getInfo()) return ces::CES_ERROR_TIMEOUT;
    serverId = sess.client().getServerId();
    minDifficulty = sess.client().getMinDifficulty();
    rpcPort = sess.client().getServerRpcPort();

    uint16_t peerCount = 0;
    bool found = false;
    ces::Hash peerKey{};
    std::string address;
    if (sess.client().queryPeerInfo(0, peerCount, found, peerKey, address) ==
        ces::CES_OK) {
      if (found && !address.empty())
        peers.emplace_back(
            address,
            ces::hashPrefixToString(ces::Account::getMapKey(peerKey)));
      const uint16_t limit = std::min<uint16_t>(peerCount, 24);
      for (uint16_t i = 1; i < limit; ++i) {
        uint16_t ignoredCount = 0;
        bool peerFound = false;
        ces::Hash pk{};
        std::string peerAddress;
        if (sess.client().queryPeerInfo(i, ignoredCount, peerFound, pk,
                                        peerAddress) != ces::CES_OK)
          break;
        if (peerFound && !peerAddress.empty())
          peers.emplace_back(
              peerAddress,
              ces::hashPrefixToString(ces::Account::getMapKey(pk)));
      }
    }
  } catch (const std::exception&) {
    return ces::CES_ERROR_TIMEOUT;
  }

  auto esc = [](std::string s) {
    auto replaceAll = [](std::string& in, const std::string& from,
                         const std::string& to) {
      size_t at = 0;
      while ((at = in.find(from, at)) != std::string::npos) {
        in.replace(at, from.size(), to);
        at += to.size();
      }
    };
    replaceAll(s, "&", "&amp;");
    replaceAll(s, "<", "&lt;");
    replaceAll(s, "\"", "&quot;");
    return s;
  };
  const std::string eh = esc(host);
  const std::string mainHp =
      port == 53830 ? eh : eh + ":" + std::to_string(port);
  const std::string rpcHp = eh + ":" + std::to_string(rpcPort);

  std::ostringstream b;
  b << "<!doctype html><html><head><meta charset=utf-8><style>"
       "body{font-family:sans-serif;max-width:820px;margin:42px auto;padding:0 26px;color:#202124}"
       "h1{font-family:serif;font-size:34px;margin:0 0 3px}.eyebrow{color:#8a8f98;font-size:11px;letter-spacing:1.2px;text-transform:uppercase}"
       ".sub{color:#68707c;font-size:14px;margin:7px 0 27px}.grid{display:block}.card{border-top:1px solid #e2e4e8;padding:17px 4px 18px}"
       ".card a{font-family:serif;font-size:22px;color:#18212b;text-decoration:none}.card a:hover{color:#1a8917}"
       ".desc{color:#68707c;font-size:13px;line-height:1.5;margin-top:4px}.proto{font-family:monospace;color:#969ca5;font-size:11px;margin-top:5px}"
       ".facts{margin:25px 0 31px;padding:14px 17px;background:#f7f8fa;border:1px solid #e5e7eb}"
       ".facts span{display:inline-block;margin-right:25px;color:#5f6670;font-size:12px}.facts b{color:#282d33}"
       "h2{font-size:15px;margin:30px 0 9px}.peer{font-size:13px;line-height:1.9}.peer a{color:#1a8917}.mono{font-family:monospace}"
       "</style></head><body>"
       "<div class=eyebrow>CES server</div><h1>" << eh << "</h1>"
       "<div class=sub>A directory of the capabilities this server exposes.</div>"
       "<div class=facts><span>identity <b class=mono>"
    << ces::hashPrefixToString(serverId) << "</b></span><span>main <b>" << port
    << "</b></span><span>CesPlex <b>" << rpcPort
    << "</b></span><span>minimum work <b>"
    << static_cast<unsigned>(minDifficulty) << "</b></span></div><div class=grid>";

  b << "<div class=card><a href=\"ces://" << mainHp
    << "/account\">Your account</a><div class=desc>Balance, transfers, payments, assets, and account-owned execution on this server.";
  if (accountRc == ces::CES_OK)
    b << " Current balance: " << fmtCredits(balance) << " credits.";
  b << "</div><div class=proto>accounts · transfers and payments · assets and CesVM</div></div>"
       "<div class=card><a href=\"ces://"
    << mainHp
    << "/identity\">Identity and names</a><div class=desc>Bind this key to a user name on this server.</div>"
       "<div class=proto>CES_REGISTER_KEYNAME · QUERY_KEYNAME</div></div>"
       "<div class=card><a href=\"ces://"
    << mainHp
    << "/info\">Server info</a><div class=desc>The server's terms: identity, "
       "fee schedule, capacity, and load, composed live from the protocol.</div>"
       "<div class=proto>MINX GetInfo · CES_QUERY_SERVER_INFO</div></div>";

  if (rpcPort != 0) {
    b << "<div class=card><a href=\"ces://" << mainHp
      << "/apps\">Applications</a><div class=desc>Enter the live CWB application directory running on this server.</div>"
         "<div class=proto>CES discovery + /ces/compute/1 + /ces/luarpc/1</div></div>"
         "<div class=card><a href=\"file://"
      << rpcHp
      << "/s/index.html\">Public catalog</a><div class=desc>Operator-published pages and resources.</div>"
         "<div class=proto>/ces/file/1</div></div>"
         "<div class=card><a href=\"file://"
      << rpcHp
      << "/\">Files</a><div class=desc>File zones, exact capability paths, and file management.</div>"
         "<div class=proto>/ces/file/1</div></div>"
         "<div class=card><a href=\"compute://"
      << rpcHp
      << "/\">Compute</a><div class=desc>Inspect this identity's running compute instances.</div>"
         "<div class=proto>/ces/compute/1</div></div>"
         "<div class=card><a href=\"file://"
      << rpcHp
      << "/s/instances.html\">Running services</a><div class=desc>Every live "
         "instance on this server, connectable by relay or direct.</div>"
         "<div class=proto>/ces/lua/1 · /ces/luarpc/1</div></div>";
  }
  b << "</div>";
  if (!peers.empty()) {
    b << "<h2>Peer mesh</h2>";
    for (const auto& [address, id] : peers)
      b << "<div class=peer><span class=mono>" << esc(id)
        << "</span> &nbsp; <a href=\"ces://" << esc(address) << "/\">"
        << esc(address) << "</a></div>";
  }
  b << "</body></html>";
  out = b.str();
  return ces::CES_OK;
}

uint8_t cesServerInfoFetch(const std::string& host, uint16_t port,
                           std::string& out) {
  out.clear();
  const ces::KeyPair identity = loadOrCreateIdentity();

  // Free half: the MINX GetInfo handshake.
  ces::HashPrefix serverId{};
  uint8_t minDifficulty = 0;
  uint16_t rpcPort = 0;
  // Paid half: the signed KV query (feeQuery, idle-discounted).
  std::vector<ces::ServerInfoEntry> entries;
  uint8_t paidRc = ces::CES_ERROR_TIMEOUT;
  try {
    const auto ep =
        ces::Resolver::resolveUdp(host + ":" + std::to_string(port));
    ces::ClientSession sess(true, static_cast<uint16_t>(0), ep, &identity, 3);
    if (!sess.client().getInfo()) return ces::CES_ERROR_TIMEOUT;
    serverId = sess.client().getServerId();
    minDifficulty = sess.client().getMinDifficulty();
    rpcPort = sess.client().getServerRpcPort();
    paidRc = sess.client().queryServerInfo(entries);
  } catch (const std::exception&) {
    return ces::CES_ERROR_TIMEOUT;
  }

  // The reply is an OPEN key/value array: the server may say anything, and a
  // client needs no schema to receive it. We curate the keys we understand
  // into sections, and render every remaining entry verbatim at the end --
  // the page stays complete when the server grows keys this build never
  // heard of.
  std::map<std::string, std::string> kv;
  for (const auto& e : entries) kv[e.key] = e.value;
  std::set<std::string> used;
  auto u64 = [&kv, &used](const char* k) -> uint64_t {
    used.insert(k);
    auto it = kv.find(k);
    if (it == kv.end()) return 0;
    try {
      return std::stoull(it->second);
    } catch (...) {
      return 0;
    }
  };
  auto str = [&kv, &used](const char* k) -> std::string {
    used.insert(k);
    auto it = kv.find(k);
    return it == kv.end() ? std::string() : it->second;
  };

  auto esc = [](std::string s) {
    auto replaceAll = [](std::string& in, const std::string& from,
                         const std::string& to) {
      size_t at = 0;
      while ((at = in.find(from, at)) != std::string::npos) {
        in.replace(at, from.size(), to);
        at += to.size();
      }
    };
    replaceAll(s, "&", "&amp;");
    replaceAll(s, "<", "&lt;");
    replaceAll(s, "\"", "&quot;");
    return s;
  };
  auto mb = [](uint64_t bytes) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.0f MB", bytes / (1024.0 * 1024.0));
    return std::string(buf);
  };

  const std::string eh = esc(host);
  const std::string mainHp =
      port == 53830 ? eh : eh + ":" + std::to_string(port);
  const std::string hello = esc(str("hello"));
  const std::string sname = esc(str("serverName"));
  const std::string version = esc(str("version"));

  std::ostringstream b;
  b << "<!doctype html><html><head><meta charset=utf-8><style>"
       "body{font-family:sans-serif;max-width:820px;margin:42px auto;padding:0 26px;color:#202124}"
       "h1{font-family:serif;font-size:34px;margin:0 0 3px}.eyebrow{color:#8a8f98;font-size:11px;letter-spacing:1.2px;text-transform:uppercase}"
       ".sub{color:#68707c;font-size:14px;margin:7px 0 27px}"
       ".hello{font-family:serif;font-size:19px;color:#3c4043;margin:0 0 24px}"
       "h2{font-size:15px;margin:30px 0 9px}.mono{font-family:monospace}"
       "table{border-collapse:collapse;width:100%;font-size:13px}"
       "td,th{border-top:1px solid #e2e4e8;padding:8px 10px 8px 4px;text-align:left;vertical-align:top}"
       "th{color:#5f6670;font-weight:normal;width:38%}"
       "td b{font-family:monospace;font-weight:600;color:#18212b}"
       ".what{color:#969ca5;font-size:12px}"
       ".note{color:#68707c;font-size:12px;margin-top:9px}"
       ".warn{background:#fdf6ec;border:1px solid #f0e0c8;padding:11px 14px;font-size:13px;color:#6b5b3e;margin:0 0 22px}"
       "a{color:#1a8917;text-decoration:none}"
       "</style></head><body>"
       "<div class=eyebrow>CES server</div><h1>"
    << (sname.empty() ? eh : sname) << "</h1>"
    << "<div class=sub>The server's terms, composed live from the free "
       "handshake and the paid server-info query.</div>";
  if (!hello.empty()) b << "<div class=hello>&#8220;" << hello << "&#8221;</div>";
  if (paidRc != ces::CES_OK)
    b << "<div class=warn>The paid half of this page is unavailable ("
      << esc(ces::errorString(paidRc))
      << "): showing only the free handshake. Mine a few credits on this "
         "server and reload.</div>";

  b << "<h2>Identity</h2><table>"
       "<tr><th>public key</th><td><b>"
    << (kv.count("serverPublicKey") ? esc(str("serverPublicKey"))
                                    : ces::hashPrefixToString(serverId))
    << "</b></td></tr>";
  if (!sname.empty())
    b << "<tr><th>name</th><td><b>" << sname << "</b></td></tr>";
  if (!version.empty())
    b << "<tr><th>version</th><td><b>" << version << "</b></td></tr>";
  b << "<tr><th>ports</th><td><b>" << port << "</b> main &nbsp;&#183;&nbsp; <b>"
    << (kv.count("rpcPort") ? str("rpcPort") : std::to_string(rpcPort))
    << "</b> CesPlex</td></tr>"
       "<tr><th>minimum work</th><td><b>"
    << static_cast<unsigned>(minDifficulty)
    << "</b> <span class=what>PoW difficulty floor for minting</span></td></tr>"
       "</table>";
  used.insert("minDifficulty");  // shown from the free handshake

  if (paidRc == ces::CES_OK) {
    struct FeeRow {
      const char* key;
      const char* label;
      const char* what;
    };
    static constexpr FeeRow kFees[] = {
        {"feeTx", "transfer", "per transfer on the main port"},
        {"feeQuery", "query", "per signed query (this page paid one)"},
        {"feeError", "error", "per rejected op"},
        {"feeAccount", "account rent", "per account per day; 3x to create"},
        {"feeAsset", "asset", "per asset created"},
        {"feeFileRent", "file rent", "per byte-day of stored file"},
        {"feeFileWrite", "file write", "per KB written"},
        {"feeFileRead", "file read", "per KB read (burned)"},
    };
    b << "<h2>The economy</h2><table>";
    for (const auto& f : kFees)
      b << "<tr><th>" << f.label << "</th><td><b>"
        << fmtCredits(static_cast<int64_t>(u64(f.key)))
        << "</b> credits <span class=what>" << f.what << "</span></td></tr>";
    b << "<tr><th>VM gas</th><td><b>&#215;" << u64("feeVmMult")
      << "</b> <span class=what>execution cost multiplier</span></td></tr>"
         "</table>"
         "<div class=note>Base rates. Every fee is scaled by live load: an "
         "idle server discounts toward zero.</div>";

    b << "<h2>The ledger</h2><table>"
         "<tr><th>credits in circulation</th><td><b>"
      << fmtCredits(static_cast<int64_t>(u64("totalCredits")))
      << "</b></td></tr>"
         "<tr><th>accounts</th><td><b>"
      << u64("totalAccounts") << "</b> <span class=what>of "
      << u64("maxAccounts") << " max</span></td></tr>"
         "<tr><th>assets</th><td><b>" << u64("totalAssets")
      << "</b> <span class=what>of " << u64("maxAssets")
      << " max</span></td></tr>"
         "<tr><th>aliases</th><td><b>" << u64("totalAliases")
      << "</b> <span class=what>of " << u64("maxAliases")
      << " max</span></td></tr>"
         "<tr><th>file store</th><td><b>"
      << mb(u64("cesFileStoreMaxBytes")) << "</b> <span class=what>cap; 0 = "
         "feature disabled</span></td></tr>"
         "<tr><th>peers</th><td><b>" << u64("peerCount") << "</b></td></tr>"
         "<tr><th>throughput</th><td><b>" << u64("tps")
      << "</b> <span class=what>transactions per second, now</span></td></tr>"
         "</table>";

    // Every entry the sections above did not consume, verbatim: the query is
    // schema-free, so an unknown key is content, not an error.
    bool anyExtra = false;
    for (const auto& [k, v] : kv) {
      if (used.count(k)) continue;
      if (!anyExtra) {
        b << "<h2>Everything else the server said</h2><table>";
        anyExtra = true;
      }
      b << "<tr><th class=mono>" << esc(k) << "</th><td><b>" << esc(v)
        << "</b></td></tr>";
    }
    if (anyExtra) b << "</table>";
  }

  b << "<div class=note style=\"margin-top:26px\"><a href=\"ces://" << mainHp
    << "/\">&#8592; server directory</a></div>"
       "</body></html>";
  out = b.str();
  return ces::CES_OK;
}

uint16_t cesServerRpcPort(const std::string& host, uint16_t mainPort) {
  try {
    const auto ep = ces::Resolver::resolveUdp(
        host + ":" + std::to_string(mainPort));
    ces::ClientSession sess(true, static_cast<uint16_t>(0), ep, nullptr, 2);
    int64_t ignoredBalance = 0;
    uint32_t ignoredNonce = 0;
    const ces::Hash own = loadOrCreateIdentity().getPublicKeyAsHash();
    (void)sess.client().queryAccount(ces::Account::getMapKey(own),
                                     ignoredBalance, ignoredNonce);
    if (sess.client().getInfo()) return sess.client().getServerRpcPort();
  } catch (const std::exception&) {
  }
  return 0;
}

uint8_t cesAccountFetch(const std::string& host, uint16_t port,
                        const std::string& pubkeyHex, std::string& out,
                        AccountView view) {
  out.clear();
  const bool own = pubkeyHex.empty();
  std::string hex = own ? loadOrCreateIdentity().getPublicKeyHexStr() : pubkeyHex;

  ces::Hash h;
  try {
    minx::stringToHash(h, hex);
  } catch (const std::exception&) {
    return ces::CES_ERROR_BAD_INPUT;
  }

  int64_t bal = 0;
  uint32_t nonce = 0;
  ces::HashPrefix xd{};
  uint64_t xa = 0;
  uint32_t xt = 0;
  uint8_t rc = ces::CES_ERROR_TIMEOUT;
  ces::HashPrefix srvId{};
  uint8_t srvMinDiff = 0;
  uint16_t srvRpc = 0;
  bool haveSrv = false;
  QString regName;  // the key_name registered to this account here ("" = none)
  std::vector<std::pair<std::string, std::string>> peers;  // (address, id-prefix)
  try {
    const auto ep =
        ces::Resolver::resolveUdp(host + ":" + std::to_string(port));
    ces::ClientSession sess(true, static_cast<uint16_t>(0), ep, nullptr, 2);
    rc = sess.client().queryAccount(ces::Account::getMapKey(h), bal, nonce, xd,
                                    xa, xt);
    if (sess.client().getInfo()) {  // free server-info: identity, difficulty, rpc
      haveSrv = true;
      srvId = sess.client().getServerId();
      srvMinDiff = sess.client().getMinDifficulty();
      srvRpc = sess.client().getServerRpcPort();
    }
    // The account's name (key_names, ledger-verified, unsquattable): works for
    // this browser's key and anyone else's.
    {
      ces::Bytes nm;
      bool nf = false;
      if (sess.client().queryKeyName(h, nm, nf) == ces::CES_OK && nf)
        regName = QString::fromUtf8(reinterpret_cast<const char*>(nm.data()),
                                    static_cast<int>(nm.size()));
    }
    // Enumerate the peer table (unsigned/public slot reads) so the page can link
    // into the mesh. Slot 0 reports the total; walk the rest, capped. Only when
    // the server answered getInfo -- otherwise the host is unreachable and this
    // would just add another timeout to an already-slow failure.
    uint16_t pcount = 0;
    bool pfound = false;
    ces::Hash ppub{};
    std::string paddr;
    if (haveSrv && sess.client().queryPeerInfo(0, pcount, pfound, ppub, paddr) ==
                       ces::CES_OK) {
      if (pfound && !paddr.empty())
        peers.emplace_back(paddr,
                           ces::hashPrefixToString(ces::Account::getMapKey(ppub)));
      const uint16_t lim = std::min<uint16_t>(pcount, 64);
      for (uint16_t i = 1; i < lim; ++i) {
        uint16_t c2 = 0;
        bool f2 = false;
        ces::Hash pk2{};
        std::string a2;
        if (sess.client().queryPeerInfo(i, c2, f2, pk2, a2) != ces::CES_OK)
          break;
        if (f2 && !a2.empty())
          peers.emplace_back(
              a2, ces::hashPrefixToString(ces::Account::getMapKey(pk2)));
      }
    }
  } catch (const std::exception&) {
    return ces::CES_ERROR_TIMEOUT;  // unreachable host / connect failed
  }
  if (rc != ces::CES_OK) return rc;

  const bool payment = bal < 0;
  std::ostringstream b;
  b << "<!doctype html><html><head><meta charset=utf-8><style>"
       "body{font-family:sans-serif;max-width:720px;margin:32px auto;padding:0 "
       "20px;color:#222}h1{font-size:22px;margin-bottom:2px}"
       ".you{color:#1a7f37;font-size:13px}.key{font-family:monospace;"
       "font-size:13px;color:#444;word-break:break-all;margin:6px 0 4px}"
       "table{border-collapse:collapse;margin-top:14px;font-size:15px}"
       "th{text-align:left;color:#666;padding:6px 20px 6px 0;vertical-align:top;"
       "font-weight:600}td{padding:6px 0}.big{font-size:20px;font-weight:600}"
       ".mono{font-family:monospace}a{color:#1a8917}</style></head><body>";
  // The name (and handle) come first when set; the key is machinery below.
  // regName is the stored underscore form: the handle IS it; the byline pretties.
  const QString myName = prettyName(regName);
  const QString myHandle = regName;
  auto esc = [](QString s) {
    s.replace('&', QStringLiteral("&amp;"));
    s.replace('<', QStringLiteral("&lt;"));
    return s;
  };
  if (view == AccountView::Identity && !myName.isEmpty()) {
    b << "<h1>" << esc(myName).toUtf8().constData() << "</h1>"
      << "<div class=you>@" << esc(myHandle).toUtf8().constData()
      << (own ? " &#183; this browser's identity" : "") << "</div>";
  } else if (view == AccountView::Identity) {
    b << "<h1>Choose your name</h1>"
         "<div class=you>this browser's identity</div>";
  } else if (own) {
    b << "<h1>Your account</h1>";
  } else {
    b << "<h1>Account</h1>";
  }
  b << "<div class=key>" << hex << "</div><table>";
  if (payment) {
    b << "<tr><th>owed</th><td class=big>" << fmtCredits(-bal)
      << " credits</td></tr><tr><th>type</th><td>unsettled payment "
         "account</td></tr><tr><th>expires</th><td>in "
      << nonce << " day" << (nonce == 1 ? "" : "s") << "</td></tr>";
  } else {
    b << "<tr><th>balance</th><td class=big>" << fmtCredits(bal)
      << " credits</td></tr><tr><th>type</th><td>ordinary account</td></tr>"
         "<tr><th>nonce</th><td>"
      << nonce << "</td></tr>";
  }
  if (xa > 0 || xt > 0) {
    b << "<tr><th>last transfer</th><td>"
      << fmtCredits(static_cast<int64_t>(xa)) << " credits to <span class=mono>"
      << ces::hashPrefixToString(xd) << "</span></td></tr>";
    if (xt > 0) {
      char when[40] = "";
      const std::time_t t = static_cast<std::time_t>(xt);
      std::tm tmv{};
#ifdef _WIN32
      gmtime_s(&tmv, &t);
#else
      gmtime_r(&t, &tmv);
#endif
      std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S UTC", &tmv);
      b << "<tr><th>at</th><td>" << when << "</td></tr>";
    }
  }
  b << "</table>";
  if (view == AccountView::Identity) {
    if (own)
      b << "<div style=\"margin-top:22px\"></div>"
         "<object type=\"application/x-cwb-identity\" width=\"560\" "
         "height=\"180\"></object>";
    if (own && bal <= 0)
      b << "<p style=\"color:#b45309;font-size:13px;margin-top:8px\">You need a "
           "few credits to register a name. Open Mining (File &#8594; Mining) "
           "and mine some first.</p>";
    if (haveSrv)
      b << "<p style=\"font-size:13px;margin-top:16px\"><a href=\"ces://"
        << host << (port == 53830 ? "" : ":" + std::to_string(port))
        << "/\">Browse this server</a></p>";
    b << "</body></html>";
    out = b.str();
    return ces::CES_OK;
  }
  if (own) {  // your own account page becomes a working wallet + asset tools
    b << "<h2 id=send style=\"font-size:16px;margin-top:24px\">Send</h2>"
         "<object type=\"application/x-cwb-wallet\" width=\"560\" height=\"230\">"
         "</object>"
         "<h2 id=assets style=\"font-size:16px;margin-top:24px\">Assets</h2>"
         "<object type=\"application/x-cwb-asset\" width=\"560\" height=\"270\">"
         "</object>";
  }
  b << "</body></html>";
  out = b.str();
  return ces::CES_OK;
}

uint8_t cesSquery(const std::string& host, uint16_t port,
                  const std::string& pubkeyHex, std::string& out) {
  out.clear();
  const ces::KeyPair id = loadOrCreateIdentity();
  const std::string hex = pubkeyHex.empty() ? id.getPublicKeyHexStr() : pubkeyHex;
  ces::Hash h;
  try {
    minx::stringToHash(h, hex);
  } catch (const std::exception&) {
    return ces::CES_ERROR_BAD_INPUT;
  }
  try {
    const auto ep =
        ces::Resolver::resolveUdp(host + ":" + std::to_string(port));
    ces::ClientSession sess(true, static_cast<uint16_t>(0), ep, &id, 3);
    std::vector<ces::AccountEntry> accts;
    const uint8_t rc =
        sess.client().queryAccountSigned(ces::Account::getMapKey(h), 1, accts);
    if (rc != ces::CES_OK) return rc;
    std::ostringstream o;
    if (accts.empty()) {
      o << "no such account (server-signed empty response)\n";
    } else {
      const auto& a = accts.front();
      o << "server-verified account " << hex << "\n"
        << "  balance: " << fmtCredits(a.balance) << " credits\n"
        << "  nonce:   " << a.nonce << "\n";
    }
    out = o.str();
    return ces::CES_OK;
  } catch (const std::exception&) {
    return ces::CES_ERROR_TIMEOUT;
  }
}

// ---------------------------------------------------------------------------
// CesLuaSession: a persistent duplex channel (bind + ATTACH, held open).
// ---------------------------------------------------------------------------
struct CesLuaSession::Impl {
  Dialer dialer;
  std::function<void(const std::string&)> onData;
  std::function<void(const std::string&)> onClose;
};

CesLuaSession::CesLuaSession() : impl_(std::make_unique<Impl>()) {}
CesLuaSession::~CesLuaSession() { impl_->dialer.stop(); }

void CesLuaSession::onData(std::function<void(const std::string&)> cb) {
  impl_->onData = std::move(cb);
}
void CesLuaSession::onClose(std::function<void(const std::string&)> cb) {
  impl_->onClose = std::move(cb);
}
void CesLuaSession::write(const std::string& bytes) {
  impl_->dialer.postWrite(bytes);
}
void CesLuaSession::close() { impl_->dialer.stop(); }

std::string CesLuaSession::open(const std::string& host, uint16_t rpcPort,
                                uint64_t pid, const ces::KeyPair& signer,
                                std::string& hello, uint8_t& attachStatus) {
  attachStatus = 0xFF;
  minx::SockAddr peer;
  std::string err;
  if (!resolvePeer(host, rpcPort, peer, err)) return err;
  if (auto e = impl_->dialer.start(peer); !e.empty()) return e;
  uint64_t token = 0;
  minx::Hash spk{};
  if (auto e = impl_->dialer.bind(signer, token, spk); !e.empty()) return e;
  uint64_t connId = 0;
  if (auto e = impl_->dialer.attach(signer, token, pid, attachStatus, connId,
                                    hello);
      !e.empty())
    return e;
  if (attachStatus != ces::CES_OK) return "ATTACH rejected";
  auto od = impl_->onData;
  auto oc = impl_->onClose;
  impl_->dialer.startReadLoop(
      [od](std::string s) { if (od) od(s); },
      [oc](std::string s) { if (oc) oc(s); });
  return "";
}

std::string CesLuaSession::openDirect(const std::string& host, uint16_t port,
                                      const ces::KeyPair& signer) {
  minx::SockAddr peer;
  std::string err;
  if (!resolvePeer(host, port, peer, err)) return err;
  if (auto e = impl_->dialer.start(peer); !e.empty()) return e;
  uint64_t token = 0;
  minx::Hash spk{};
  if (auto e = impl_->dialer.bind(signer, token, spk, kLuaRpcProto); !e.empty())
    return e;
  // No ATTACH: binding /ces/luarpc/1 at the instance's own port is already the
  // pipe. Start reading; a terminal program speaks first, arriving via onData.
  auto od = impl_->onData;
  auto oc = impl_->onClose;
  impl_->dialer.startReadLoop(
      [od](std::string s) { if (od) od(s); },
      [oc](std::string s) { if (oc) oc(s); });
  return "";
}

}  // namespace cwb
