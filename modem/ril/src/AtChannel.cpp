/* SPDX-License-Identifier: Apache-2.0 */
// See AtChannel.h for design provenance (ported from reference-ril's atchannel.c).
#include "AtChannel.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstring>

#define LOG_TAG "mindone_ril_atchan"
#include <log/log.h>

extern "C" {
#include "at_tok.h"
}

namespace mindone::ril {

namespace {

// Final-response literal tables, same members as reference-ril's isFinalResponseSuccess()/
// isFinalResponseError() (atchannel.c:150-189), reproduced here since we do not link that file.
bool startsWith(const std::string& s, const char* prefix) {
    return s.compare(0, strlen(prefix), prefix) == 0;
}

// A plain aggregate literal with a subset of AtResult's fields designated trips this toolchain's
// -Wmissing-designated-field-initializers (stricter than plain -Wmissing-field-initializers);
// this helper avoids that everywhere a "command failed before we could even send it" result is
// needed -- AtResult's own default member initializers already give ok=false, timedOut=false,
// cmeError=-1, so only timedOut needs flipping.
AtResult timedOutResult() {
    AtResult r;
    r.timedOut = true;
    return r;
}

bool isFinalOk(const std::string& l) {
    return l == "OK" || l == "CONNECT";
}

bool isFinalError(const std::string& l) {
    return l == "ERROR" || l == "+CMS ERROR" /* prefix-checked below */ ||
           l == "NO CARRIER" || l == "NO ANSWER" || l == "NO DIALTONE" || l == "BUSY" ||
           startsWith(l, "+CME ERROR") || startsWith(l, "+CMS ERROR");
}

}  // namespace

AtChannel::~AtChannel() { close(); }

bool AtChannel::open(int fd, bool urcMode, UrcCallback onUrc, ClosedCallback onClosed) {
    if (mFd >= 0) close();
    mFd = fd;
    mUrcMode = urcMode;
    mOnUrc = std::move(onUrc);
    mOnClosed = std::move(onClosed);
    mStopReader = false;
    mReader = std::thread(&AtChannel::readerLoop, this);
    return true;
}

void AtChannel::close() {
    if (mFd < 0) return;
    mStopReader = true;
    // Unblock the reader's read(2); shutdown() is not meaningful on a pty, so just close and
    // let read() return 0/EBADF -- matches reference-ril's at_close() (atchannel.c:601-616).
    int fd = mFd;
    mFd = -1;
    ::close(fd);
    if (mReader.joinable()) mReader.join();
    {
        std::lock_guard<std::mutex> lk(mCmdMutex);
        if (mCommandPending) {
            mPendingResult = timedOutResult();
            mPendingDone = true;
            mCmdCv.notify_all();
        }
    }
}

bool AtChannel::writeAll(const void* buf, size_t len) {
    const char* p = static_cast<const char*>(buf);
    size_t left = len;
    while (left > 0) {
        ssize_t n = ::write(mFd, p, left);
        if (n < 0) {
            if (errno == EINTR) continue;
            ALOGE("AtChannel write failed: %s", strerror(errno));
            return false;
        }
        p += n;
        left -= static_cast<size_t>(n);
    }
    return true;
}

AtChannel::LineClass AtChannel::classifyLine(const std::string& line) const {
    if (line.empty()) return LineClass::kIntermediate;  // caller filters blank lines already
    if (mAwaitingSmsPrompt && line.find('>') != std::string::npos) {
        return LineClass::kSmsPromptChar;
    }
    if (isFinalOk(line)) return LineClass::kFinalOk;
    if (isFinalError(line)) return LineClass::kFinalError;
    // Everything else is either an expected intermediate (matches mPendingPrefix while a
    // command is outstanding) or, if no command is outstanding, a URC by definition -- exactly
    // reference-ril's processLine() dispatch (atchannel.c:234-300), simplified because our
    // notification channel already separates URCs physically (see AtChannel.h).
    return LineClass::kIntermediate;
}

void AtChannel::readerLoop() {
    std::string buf;
    char chunk[512];
    for (;;) {
        if (mStopReader) break;
        ssize_t n = ::read(mFd, chunk, sizeof(chunk));
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            break;  // EOF or error: channel closed under us
        }
        buf.append(chunk, static_cast<size_t>(n));

        size_t pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, pos);
            buf.erase(0, pos + 1);
            // Strip a trailing '\r' (AT lines are CRLF-terminated) and any stray leading '\r'.
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
            size_t start = line.find_first_not_of('\r');
            if (start == std::string::npos) continue;  // blank line, reference-ril skips these
            line = line.substr(start);
            if (line.empty()) continue;

            if (mUrcMode) {
                if (mOnUrc) mOnUrc(line, "");
                continue;
            }

            std::unique_lock<std::mutex> lk(mCmdMutex);
            LineClass cls = classifyLine(line);
            if (cls == LineClass::kSmsPromptChar) {
                mSmsPromptSeen = true;
                mCmdCv.notify_all();
                continue;
            }
            if (!mCommandPending) {
                lk.unlock();
                if (mOnUrc) mOnUrc(line, "");
                continue;
            }
            switch (cls) {
                case LineClass::kFinalOk:
                case LineClass::kFinalError:
                    mPendingResult.finalLine = line;
                    mPendingResult.ok = (cls == LineClass::kFinalOk);
                    if (!mPendingResult.ok) {
                        char* cur = const_cast<char*>(mPendingResult.finalLine.c_str());
                        int code;
                        if ((startsWith(line, "+CME ERROR") || startsWith(line, "+CMS ERROR")) &&
                            at_tok_start(&cur) == 0 && at_tok_nextint(&cur, &code) == 0) {
                            mPendingResult.cmeError = code;
                        }
                    }
                    mPendingDone = true;
                    mCmdCv.notify_all();
                    break;
                case LineClass::kIntermediate:
                    // Only keep lines matching the expected prefix for kSingleLine/kMultiLine,
                    // matching reference-ril's addIntermediate() gating (atchannel.c:121-149);
                    // kNoResult/kNumeric commands accept whatever arrives.
                    if (mPendingType == AtCommandType::kNoResult) {
                        break;  // unexpected chatter on a NO_RESULT command; drop it
                    }
                    if (mPendingPrefix.empty() || startsWith(line, mPendingPrefix.c_str())) {
                        mPendingResult.intermediates.push_back(line);
                    }
                    break;
                default:
                    break;
            }
        }
    }
    if (!mStopReader && mOnClosed) mOnClosed();
}

AtResult AtChannel::sendCommand(const std::string& command, AtCommandType type,
                                 const std::string& responsePrefix, int timeoutMs) {
    if (mUrcMode || mFd < 0) {
        return timedOutResult();
    }
    std::unique_lock<std::mutex> lk(mCmdMutex);
    if (mCommandPending) {
        // Reference-ril returns AT_ERROR_COMMAND_PENDING here (atchannel.c:672-738); our
        // callers (RadioImpl) are expected to serialize through their own request queue, so
        // reaching this means a real bug upstream -- log loudly rather than silently blocking.
        ALOGE("sendCommand(%s) called while another command is in flight", command.c_str());
        return timedOutResult();
    }
    mCommandPending = true;
    mPendingType = type;
    mPendingPrefix = responsePrefix;
    mPendingResult = AtResult{};
    mPendingDone = false;
    lk.unlock();

    std::string line = command;
    if (line.empty() || line.back() != '\r') line += '\r';
    ALOGD("AT> %s", command.c_str());
    if (!writeAll(line.data(), line.size())) {
        lk.lock();
        mCommandPending = false;
        return timedOutResult();
    }

    lk.lock();
    bool got = mCmdCv.wait_for(lk, std::chrono::milliseconds(timeoutMs),
                                [this] { return mPendingDone; });
    AtResult result = mPendingResult;
    result.timedOut = !got;
    mCommandPending = false;
    if (!got) {
        ALOGE("AT command timed out: %s", command.c_str());
    } else {
        ALOGD("AT< %s (%zu intermediate line(s))", result.finalLine.c_str(),
              result.intermediates.size());
    }
    return result;
}

AtResult AtChannel::sendSmsCommand(const std::string& command, const std::string& pdu,
                                    const std::string& responsePrefix, int timeoutMs) {
    if (mUrcMode || mFd < 0) return timedOutResult();
    std::unique_lock<std::mutex> lk(mCmdMutex);
    if (mCommandPending) return timedOutResult();
    mCommandPending = true;
    mPendingType = AtCommandType::kSingleLine;
    mPendingPrefix = responsePrefix;
    mPendingResult = AtResult{};
    mPendingDone = false;
    mAwaitingSmsPrompt = true;
    mSmsPromptSeen = false;
    lk.unlock();

    std::string line = command;
    if (line.empty() || line.back() != '\r') line += '\r';
    ALOGD("AT> %s", command.c_str());
    writeAll(line.data(), line.size());

    lk.lock();
    bool gotPrompt = mCmdCv.wait_for(lk, std::chrono::milliseconds(5000),
                                      [this] { return mSmsPromptSeen; });
    mAwaitingSmsPrompt = false;
    if (!gotPrompt) {
        mCommandPending = false;
        ALOGE("sendSmsCommand: no '>' prompt for %s", command.c_str());
        return timedOutResult();
    }
    lk.unlock();

    std::string body = pdu;
    body += static_cast<char>(0x1A);  // Ctrl-Z terminates the PDU, per 3GPP TS 27.005
    writeAll(body.data(), body.size());

    lk.lock();
    bool got = mCmdCv.wait_for(lk, std::chrono::milliseconds(timeoutMs),
                                [this] { return mPendingDone; });
    AtResult result = mPendingResult;
    result.timedOut = !got;
    mCommandPending = false;
    return result;
}

}  // namespace mindone::ril
