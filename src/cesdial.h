#pragma once
#include <ces/keys.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

// Native CesPlex lua dial: bind /ces/lua/1, ATTACH a compute instance (pid),
// exchange raw bytes. The browser's lua:// handler uses this. Blocking -- it
// owns its own MINX/Rudp io threads for the call, so run it off the GUI thread.
namespace cwb {

// Bind + ATTACH `pid` on host:rpcPort as `signer`, send `request`, then read the
// reply until the channel closes (HTTP/1.0 Connection: close). Returns "" on
// success (response filled, attachStatus = the ATTACH status byte); otherwise a
// human-readable error string.
std::string cesLuaFetch(const std::string& host, uint16_t rpcPort, uint64_t pid,
                        const ces::KeyPair& signer, const std::string& request,
                        std::string& response, uint8_t& attachStatus);

// cwb-local status sentinels for cesFileFetch, outside the CES error range
// (all CES codes are <= CES_ERROR_LAST). The UI maps these to its own messages.
constexpr uint8_t CWB_FETCH_TOO_LARGE = 0xF0;  // file exceeds the in-RAM cap
constexpr uint8_t CWB_FETCH_INTERNAL = 0xF1;   // an exception was caught

// Whole-file fetch from /ces/file/1: STAT, then READ in <=1MB chunks. CES_OK
// with `out` filled, else a CES code or CWB_FETCH_* sentinel (`out` cleared).
// Exception-safe. Files over `maxBytes` are refused (render path only; large
// downloads stream via cesFileDownload). Blocking; run off the GUI thread.
uint8_t cesFileFetch(const std::string& host, uint16_t rpcPort,
                     const ces::KeyPair& signer, const std::string& path,
                     std::string& out,
                     uint64_t maxBytes = 128ull * 1024 * 1024);

// Stream a file to `sink` (return false to abort) without buffering it whole;
// `progress` may be null. CES_OK on full transfer, else a CES code or
// CWB_FETCH_* sentinel. Exception-safe; blocking.
uint8_t cesFileDownload(const std::string& host, uint16_t rpcPort,
                        const ces::KeyPair& signer, const std::string& path,
                        const std::function<bool(const uint8_t*, size_t)>& sink,
                        const std::function<void(uint64_t done, uint64_t total)>&
                            progress);

// Query /ces/compute/1 and render an HTML directory of instances, each linking
// into its lua:// relay and (if it has one) luarpc:// direct address. `path`
// empty or "/" lists the signer's own instances; otherwise the public instances
// of that source path. Returns CES_OK with `out` = the HTML, else a CES code.
uint8_t cesComputeFetch(const std::string& host, uint16_t rpcPort,
                        const ces::KeyPair& signer, const std::string& path,
                        std::string& out);

// Query an account over the server's MAIN UDP port (CesClient, not CesPlex) and
// render an HTML account view. `pubkeyHex` empty = this browser's own identity;
// otherwise a 64-hex account key. host:port is the MAIN port. Unsigned and
// read-only -- no wallet key, no money moves. Returns CES_OK with `out` = the
// HTML, else a CES code (NOT_FOUND = no such account). Blocking -- owns its own
// io for the call, so run it off the GUI thread.
enum class AccountView { Overview, Identity };
uint8_t cesAccountFetch(const std::string& host, uint16_t port,
                        const std::string& pubkeyHex, std::string& out,
                        AccountView view = AccountView::Overview);

// Query the CES main port and render the server-root capability directory.
// Uses the free handshake/server info plus public account/peer queries; links
// point only to real CES protocol addresses.
uint8_t cesServerDirectoryFetch(const std::string& host, uint16_t port,
                                std::string& out);

// Server-info page: composes the free GetInfo handshake plus the paid
// CES_QUERY_SERVER_INFO (signed, costs feeQuery) into one HTML page. A failed
// paid half still renders the free half with the reason. Blocking.
uint8_t cesServerInfoFetch(const std::string& host, uint16_t port,
                           std::string& out);

// Free main-protocol GetInfo discovery. Returns the server-advertised CesPlex
// port, or 0 when unreachable/disabled.
uint16_t cesServerRpcPort(const std::string& host, uint16_t mainPort);

// Signed account query (squery): the SERVER signs the response, so the returned
// state is cryptographically verified (not just an unsigned read). Signs with
// the browser identity -> needs a PoW-enabled server. `pubkeyHex` empty = own.
// host:port is the MAIN port. Returns CES_OK with `out` = a human summary.
uint8_t cesSquery(const std::string& host, uint16_t port,
                  const std::string& pubkeyHex, std::string& out);

// A persistent duplex CesPlex /ces/lua/1 channel: bind + ATTACH and stay open.
// `open` returns the program's greeting (empty = request-driven). Callbacks fire
// on an internal io thread -- marshal to the GUI thread yourself; set them before
// open().
class CesLuaSession {
 public:
  CesLuaSession();
  ~CesLuaSession();
  CesLuaSession(const CesLuaSession&) = delete;
  CesLuaSession& operator=(const CesLuaSession&) = delete;

  std::string open(const std::string& host, uint16_t rpcPort, uint64_t pid,
                   const ces::KeyPair& signer, std::string& hello,
                   uint8_t& attachStatus);
  // Direct dial of an instance's own /ces/luarpc/1 port: no pid, no ATTACH --
  // the bind is the pipe. There is no accept-greeting on this path; the program
  // speaks first if it is a terminal, arriving via the onData callback.
  std::string openDirect(const std::string& host, uint16_t port,
                         const ces::KeyPair& signer);
  void onData(std::function<void(const std::string&)> cb);
  void onClose(std::function<void(const std::string&)> cb);
  void write(const std::string& bytes);
  void close();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace cwb
