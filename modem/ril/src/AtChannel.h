/* SPDX-License-Identifier: Apache-2.0 */
// mind_one minimal RIL (RIL-MINIMAL-1409, MODEM-STACK-1409).
//
// AtChannel ports the *design* of AOSP's hardware/ril/reference-ril/atchannel.c (present
// read-only in the LineageOS tree, hardware/ril/reference-ril/atchannel.{c,h})
// to a small C++ class: a background reader thread that classifies every line coming off an AT
// tty as an intermediate response, a final response (OK/ERROR/+CME ERROR/+CMS ERROR/NO
// CARRIER/...), or an unsolicited result code (URC); a single-command-in-flight command queue
// serialized by a mutex+condition_variable (matching reference-ril's s_commandmutex/
// s_commandcond and the comment on RadioSeClient::mCallLock in
// bridges/secure_element/SecureElementBridge.h -- a real AT-command serial line cannot usefully answer
// two requests at once anyway); and at_send_command_singleline/_numeric/_multiline/_sms
// equivalents as one parameterized sendCommand() call.
//
// One deliberate adaptation from reference-ril, cited here rather than left implicit: the real
// stock stack does NOT multiplex command responses and URCs on the same fd the way
// reference-ril's design (one physical /dev/ttyUSB-style port) assumes. Our mux
// (gsm0710muxd, confirmed live on this exact device/build -- RIL-MINIMAL-1409
// "channel layout") hands out a *dedicated* notification channel (/dev/radio/pttynoti) separate
// from the numbered command channels (/dev/radio/pttycmd1..11) mtkfusionrild opens for AT
// command/response traffic. AtChannel therefore only ever sees intermediate/final response
// lines on its own fd; URCs arrive on a second AtChannel instance (opened read-only in URC-only
// mode: no command queue, every line handed straight to the URC callback) bound to pttynoti.
// See UrcListener.h and service.cpp for how the two are wired together.
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
