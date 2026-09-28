/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace mindone::ril {

enum class AtCommandType {
    kNoResult,    // no intermediate response expected (e.g. plain "ATH")
    kNumeric,     // a single intermediate response starting with a digit (e.g. "+CSQ" reply
                  // to a NUMERIC-style command -- rare, kept for API parity with reference-ril)
    kSingleLine,  // one intermediate response line starting with the given prefix
    kMultiLine,   // zero or more intermediate response lines starting with the given prefix
};

struct AtResult {
    bool ok = false;                    // true iff the final response was OK-class
    std::string finalLine;              // e.g. "OK", "ERROR", "+CME ERROR: 10"
    std::vector<std::string> intermediates;  // in the order they arrived
    bool timedOut = false;
    // Parsed out of "+CME ERROR: N" / "+CMS ERROR: N" when finalLine carries one, else -1.
    int cmeError = -1;
};

// Called from the reader thread for every line classified as unsolicited (a URC) -- do not
// block. `smsPdu` mirrors reference-ril's ATUnsolHandler: non-empty only for the second line of
// a two-line SMS-delivery URC such as "+CMT: ...\r\n<PDU hex>".
using UrcCallback = std::function<void(const std::string& line, const std::string& smsPdu)>;

// Called from the reader thread if the fd closes unexpectedly (modem reset, mux channel torn
// down). The caller should stop issuing commands and, for the RIL, drive setRadioPower-style
// state to "unavailable" and try to reconnect -- mirrors reference-ril's onReaderClosed hook.
using ClosedCallback = std::function<void()>;

class AtChannel {
  public:
    AtChannel() = default;
    ~AtChannel();

    AtChannel(const AtChannel&) = delete;
    AtChannel& operator=(const AtChannel&) = delete;

    // Takes ownership of `fd` (closed by close()/destructor). `urcMode = true` skips the
    // command/response state machine entirely: every non-empty line is handed to `onUrc`
    // unconditionally, matching pttynoti's role (see class comment). `urcMode = false` is the
    // normal command-channel mode (pttycmd*): lines are classified and only genuine URCs go to
    // `onUrc`, everything else feeds the pending sendCommand() call.
    bool open(int fd, bool urcMode, UrcCallback onUrc, ClosedCallback onClosed = nullptr);
    void close();
    bool isOpen() const { return mFd >= 0; }

    // Command-channel API (invalid to call in urcMode; returns a timed-out-shaped AtResult).
    // `responsePrefix` selects kSingleLine/kMultiLine matching (reference-ril's
    // at_send_command_singleline/_multiline); pass an empty prefix for kNoResult.
    AtResult sendCommand(const std::string& command, AtCommandType type,
                          const std::string& responsePrefix = "",
                          int timeoutMs = kDefaultTimeoutMs);

    // AT+CMGS-style two-step SMS send: writes `command` (e.g. "AT+CMGS=23\r"), waits for the
    // "> " prompt (reference-ril's at_send_command_sms does the equivalent via a fixed short
    // sleep + write; we watch for the literal prompt byte instead, see AtChannel.cpp), then
    // writes `pdu` followed by Ctrl-Z (0x1A) and waits for the real final response.
    AtResult sendSmsCommand(const std::string& command, const std::string& pdu,
                             const std::string& responsePrefix, int timeoutMs = kSmsTimeoutMs);

    static constexpr int kDefaultTimeoutMs = 10000;
    static constexpr int kSmsTimeoutMs = 30000;
    // Boot handshake commands can legitimately take longer (modem still bringing up SIM/network
    // registration state machines) -- see RadioImpl_core.cpp's connect-time probe.
    static constexpr int kBootTimeoutMs = 15000;

  private:
    void readerLoop();
    // Returns kFinalOk/kFinalError/kNotFinal per reference-ril's isFinalResponseSuccess/
    // isFinalResponseError/isFinalResponse trio, collapsed into one tri-state helper.
    enum class LineClass { kIntermediate, kFinalOk, kFinalError, kUrc, kSmsPromptChar };
    LineClass classifyLine(const std::string& line) const;
    bool writeAll(const void* buf, size_t len);

    int mFd = -1;
    bool mUrcMode = false;
    UrcCallback mOnUrc;
    ClosedCallback mOnClosed;
    std::thread mReader;
    std::atomic<bool> mStopReader{false};

    // Single-command-in-flight state, guarded by mCmdMutex (mirrors reference-ril's
    // s_commandmutex/s_commandcond + s_type/s_responsePrefix/s_smsPDU globals, made
    // instance-local and RAII-safe instead of process-global statics).
    std::mutex mCmdMutex;
    std::condition_variable mCmdCv;
    bool mCommandPending = false;
    AtCommandType mPendingType = AtCommandType::kNoResult;
    std::string mPendingPrefix;
    AtResult mPendingResult;
    bool mPendingDone = false;
    // Set while waiting for the "> " SMS prompt; the reader thread flips this instead of
    // treating the prompt as a stray intermediate line.
    bool mAwaitingSmsPrompt = false;
    bool mSmsPromptSeen = false;
};

}  // namespace mindone::ril
