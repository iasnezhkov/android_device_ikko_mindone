/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#if defined(CONTROL_CHAT)
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void ignored(const char *value, ...) { (void)value; }
#define LOGE(...) ignored(__VA_ARGS__)
#define LOGW(...) ignored(__VA_ARGS__)
#define LOGI(...) ignored(__VA_ARGS__)
#define LOGD(...) ignored(__VA_ARGS__)
@CHAT_DEFINES@
_Static_assert(CHAT_TIMEOUT_MS == 5000, "Unexpected production AT timeout");
static int64_t clock_ms;
static unsigned waits;
static char chat_buf[128];
static int64_t now_ms(void) { return clock_ms; }
static void chat_clear(void) { chat_buf[0]=0; }
static void chat_log(void) { chat_clear(); }
static int chat_write(const char *value) { (void)value; return 0; }
static int serial_wait(int64_t deadline) {
    if (deadline != clock_ms + CHAT_TIMEOUT_MS) exit(2);
    ++waits;
    clock_ms += 1000;
    return waits < 60 ? 1 : 0;
}
static int chat_read(void) { strcpy(chat_buf,"+URC: 1\r\n"); return 0; }
@CHAT@
int main(void) {
    int result=chat("AT");
    printf("CONTROL command_timeout_ms=5000 simulated_elapsed_ms=%" PRId64 " polls=%u result=%d\n",clock_ms,waits,result);
    return result == -1 && waits == 60 && clock_ms == 60000 ? 0 : 1;
}
#endif

#if defined(CONTROL_REUSED)
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void ignored(const char *value, ...) { (void)value; }
#define LOGE(...) ignored(__VA_ARGS__)
#define LOGW(...) ignored(__VA_ARGS__)
#define LOGI(...) ignored(__VA_ARGS__)
#define LOGD(...) ignored(__VA_ARGS__)
#include <limits.h>
#include <poll.h>
#include <sys/types.h>
@POLL_DEFINES@
@CHANNEL@
@PHASE@
static struct chan chans[MAX_CHANS];
static int nchans=1;
static int serial_fd=3;
static int term_signal;
static bool setup_done=true;
static bool tx_blocked;
static size_t txq_len;
static int64_t setup_deadline=30000;
static enum mux_state state=ST_RUN;
static unsigned generation;
static unsigned stale_reads;
static unsigned polls;
static int64_t now_ms(void) { return 0; }
static void report_and_exit(const char *value) { fprintf(stderr,"unexpected fatal %s\n",value); exit(2); }
static void graceful_exit(void) { exit(2); }
static void send_msc(struct chan *channel,bool paused) { (void)channel; (void)paused; }
static void chan_close_pty(struct chan *channel) {
    channel->fd=-1; channel->pending_len=0; channel->peer_fc=false; channel->local_fc=false;
}
static int chan_open_pty(struct chan *channel) {
    channel->fd=7; channel->pending_len=0; channel->peer_fc=false; channel->local_fc=false; ++generation; return 0;
}
static ssize_t fixture_write(int fd,const void *data,size_t length) {
    if (fd!=7 || !data || length!=1) exit(2);
    errno=EIO; return -1;
}
static int fixture_poll(struct pollfd *descriptors,nfds_t count,int timeout) {
    (void)timeout;
    if (++polls>1 || count!=2 || descriptors[1].fd!=7) exit(2);
    descriptors[1].revents=POLLIN|POLLOUT; return 1;
}
static int recovery_timeout(int timeout) { return timeout; }
static bool can_take_pty_data(void) { return true; }
static ssize_t serial_read(void) { return 0; }
static int tx_flush(void) { return 0; }
static void chan_read(struct chan *channel) {
    if (channel->fd==7 && generation==1) ++stale_reads;
    state=ST_PEER_CLOSING;
}
@CHAN_REOPEN@
@CHAN_DRAIN@
@RUN@
int main(void) {
    static uint8_t pending=1;
    chans[0].dlci=1; chans[0].fd=7; chans[0].open=true; chans[0].pending=&pending; chans[0].pending_len=1;
    run();
    printf("CONTROL reused_fd=7 generation=%u stale_poll_reads=%u polls=%u\n",generation,stale_reads,polls);
    return generation==1 && stale_reads==1 && polls==1 ? 0 : 1;
}
#endif

#if defined(CONTROL_INTERRUPT)
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void ignored(const char *value, ...) { (void)value; }
#define LOGE(...) ignored(__VA_ARGS__)
#define LOGW(...) ignored(__VA_ARGS__)
#define LOGI(...) ignored(__VA_ARGS__)
#define LOGD(...) ignored(__VA_ARGS__)
#include <sys/types.h>
static int serial_fd=3;
static const char *serial_path="fixture";
static uint8_t txq[16];
static size_t txq_len=1;
static bool tx_blocked;
static unsigned writes;
static void prop_set(const char *key,const char *value) { (void)key; (void)value; }
static ssize_t fixture_write(int fd,const void *data,size_t length) {
    if (fd!=3 || !data || length!=1) exit(2);
    errno=++writes <= 4096 ? EINTR : EAGAIN;
    return -1;
}
@TX_FLUSH@
int main(void) {
    int result=tx_flush();
    printf("CONTROL interrupted_writes_before_yield=%u pending_bytes=%zu blocked=%d result=%d\n",writes-1,txq_len,tx_blocked,result);
    return result==0 && writes==4097 && txq_len==1 && tx_blocked ? 0 : 1;
}
#endif
