/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#include <android/log.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define LOG_TAG "[WFCA] "
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define WFCA_SOCK_NAME "wfca"
#define WFCA_RDS_SOCK_NAME "wfca_rds"
#define MDM_CTRL_SOCK_NAME "MDM_rtp_OTA_msg_ctrl"
#define MDM_DATA_SOCK_NAME "MDM_rtp_OTA_msg_data"

#define CCCI_IMSA_DEV "/dev/ccci_imsa"
#define CCCI_IMSA_IOC_REGISTER 0x4371
#define CCCI_IMSA_IOC_QUERY_STATUS 0x80044301
#define CCCI_MD_STATUS_READY 2

#define WAKE_LOCK_PATH "/sys/power/wake_lock"
#define WAKE_UNLOCK_PATH "/sys/power/wake_unlock"
#define WAKE_LOCK_NAME "wfca"

#define MDM_CTRL_MAGIC 0xFEDC9A76u

enum {
    MDM_CTRL_CMD_START = 0,
    MDM_CTRL_CMD_STOP = 1,
    MDM_CTRL_CMD_INCALL = 7,
    MDM_CTRL_CMD_NOT_INCALL = 8,
};

enum {
    IMSA_MSG_FILTER_REGISTER_REQ = 1,
    IMSA_MSG_FILTER_REGISTER_ACK = 2,
    IMSA_MSG_FILTER_DEREGISTER_ACK = 3,
    IMSA_MSG_FILTER_DEREGISTER_REQ = 4,
    IMSA_MSG_FILTER_UPDATE_REQ = 5,
    IMSA_MSG_FILTER_UPDATE_ACK = 6,
    IMSA_MSG_RESET_REQ = 7,
    IMSA_MSG_RESET_ACK = 8,
    IMSA_MSG_RDS_CONFIG_FWD = 9,
    IMSA_MSG_PKT_LOSS_RPT = 10,
    IMSA_MSG_PING_PCSCF_TRIGGER = 11,
    IMSA_MSG_SEND = 128,
    IMSA_MSG_UPLINK_DATA = 0x81,
    IMSA_MSG_ERROR = 0xFF,
};

#define IMSA_HDR_LEN 4
#define IMSA_BODY_FILTER_REGISTER 52
#define IMSA_BODY_FILTER_DEREGISTER 8
#define IMSA_BODY_FILTER_UPDATE 52
#define IMSA_BODY_PKT_LOSS_RPT_BASE 20
#define IMSA_BODY_PKT_LOSS_RPT_EXT 512
#define IMSA_BODY_PING_PCSCF 24
#define IMSA_BODY_SEND_HDR 56
#define IMSA_MSG_MAX (IMSA_HDR_LEN + IMSA_BODY_SEND_HDR + 1500)

#define MAX_FILTERS 16
#define MAX_PDN 16

#define RDS_STATUS_EMPTY 255
#define RDS_STATUS_FRESH 1
#define RDS_STATUS_STALE 2

#define RELAY_UL_CAP 10
#define RELAY_DL_CAP 50
#define RELAY_MAX_PAYLOAD 640
#define RELAY_RECORD_LEN (4 + RELAY_MAX_PAYLOAD)

typedef struct {
    int in_use;
    uint32_t req_id;
    uint8_t body[IMSA_BODY_FILTER_REGISTER];
    int socket_id;
} filter_slot_t;

typedef struct {
    int in_use;
    int fd;
    int filter_id;
    struct sockaddr_storage local;
    socklen_t local_len;
} socket_slot_t;

typedef struct {
    uint8_t status;
    uint32_t pdn_id;
    uint32_t threshold;
    uint32_t duration;
    uint8_t body[IMSA_BODY_PKT_LOSS_RPT_BASE + IMSA_BODY_PKT_LOSS_RPT_EXT];
    size_t body_len;
} pdn_slot_t;

typedef struct {
    uint8_t data[RELAY_RECORD_LEN];
    size_t len;
} relay_record_t;

typedef struct {
    relay_record_t items[RELAY_DL_CAP];
    int cap;
    int head;
    int count;
    pthread_mutex_t lock;
} relay_queue_t;

typedef struct {
    int fd;
    pthread_mutex_t lock;
} imsa_ctx_t;

typedef struct {
    uint8_t started;
    uint8_t incall;
    uint8_t data_thread_running;
    uint8_t data_thread_stop_req;
    pthread_t data_tid;
    pthread_mutex_t lock;
} mdm_ctrl_ctx_t;

static pthread_mutex_t g_filters_lock = PTHREAD_MUTEX_INITIALIZER;
static filter_slot_t g_filters[MAX_FILTERS];
static socket_slot_t g_sockets[MAX_FILTERS];
static int g_filters_event_fd = -1;

static pthread_mutex_t g_pdn_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_pdn_changed;
static struct timespec g_pdn_gc_deadline;
static pdn_slot_t g_pdn[MAX_PDN];

static imsa_ctx_t g_imsa = {.fd = -1, .lock = PTHREAD_MUTEX_INITIALIZER};
static relay_queue_t g_ul_queue = {.cap = RELAY_UL_CAP, .lock = PTHREAD_MUTEX_INITIALIZER};
static relay_queue_t g_dl_queue = {.cap = RELAY_DL_CAP, .lock = PTHREAD_MUTEX_INITIALIZER};
static mdm_ctrl_ctx_t g_mdm = {.lock = PTHREAD_MUTEX_INITIALIZER};

static int g_wake_lock_fd = -1;
static int g_wake_unlock_fd = -1;
static pthread_mutex_t g_wake_lock_init = PTHREAD_MUTEX_INITIALIZER;
static int g_wake_lock_ready;

static void filters_changed_locked(void)
{
    uint64_t value = 1;
    ssize_t n;

    do {
        n = write(g_filters_event_fd, &value, sizeof(value));
    } while (n < 0 && errno == EINTR);
    if (n < 0 && errno != EAGAIN)
        LOGE("filter notification fail:%s", strerror(errno));
}

static void pdn_gc_advance_locked(const struct timespec *now)
{
    if (now->tv_sec > g_pdn_gc_deadline.tv_sec ||
        (now->tv_sec == g_pdn_gc_deadline.tv_sec &&
         now->tv_nsec >= g_pdn_gc_deadline.tv_nsec)) {
        for (int i = 0; i < MAX_PDN; i++) {
            if (g_pdn[i].status == RDS_STATUS_FRESH) {
                g_pdn[i].status = RDS_STATUS_STALE;
                LOGD("clear pdn_id:%d status", i);
            }
        }
        time_t elapsed = now->tv_sec - g_pdn_gc_deadline.tv_sec;
        if (now->tv_nsec < g_pdn_gc_deadline.tv_nsec)
            elapsed--;
        g_pdn_gc_deadline.tv_sec += (elapsed / 50 + 1) * 50;
    }
}

static void wake_lock_open(void)
{
    pthread_mutex_lock(&g_wake_lock_init);
    if (!g_wake_lock_ready) {
        g_wake_lock_fd = open(WAKE_LOCK_PATH, O_RDWR);
        g_wake_unlock_fd = open(WAKE_UNLOCK_PATH, O_RDWR);
        g_wake_lock_ready = 1;
    }
    pthread_mutex_unlock(&g_wake_lock_init);
}

static void wake_lock_acquire(const char *name)
{
    wake_lock_open();
    if (g_wake_lock_fd < 0) {
        LOGE("acquire wake_lock fail %s: %s", name, strerror(ENOENT));
        return;
    }
    if (write(g_wake_lock_fd, name, strlen(name)) < 0)
        LOGE("acquire wake_lock fail %s: %s", name, strerror(errno));
}

static void wake_lock_release(const char *name)
{
    wake_lock_open();
    if (g_wake_unlock_fd < 0) {
        LOGE("release wake_lock fail %s: %s", name, strerror(ENOENT));
        return;
    }
    if (write(g_wake_unlock_fd, name, strlen(name)) < 0)
        LOGE("release wake_lock fail %s: %s", name, strerror(errno));
}

static ssize_t read_full(int fd, void *buf, size_t want)
{
    size_t got = 0;
    uint8_t *p = buf;

    while (got < want) {
        ssize_t n = read(fd, p + got, want - got);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0)
            return -2;
        if (n == 0)
            break;
        got += (size_t)n;
    }
    return (ssize_t)got;
}

static ssize_t write_full(int fd, const void *buf, size_t want)
{
    size_t done = 0;
    const uint8_t *p = buf;

    while (done < want) {
        ssize_t n = write(fd, p + done, want - done);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0)
            return -2;
        if (n == 0) {
            LOGE("end of write size:%d", (int)done);
            return -1;
        }
        done += (size_t)n;
    }
    return (ssize_t)done;
}

static int android_get_control_socket(const char *name)
{
    char key[64];
    const char *val;
    long fd;
    char *end;

    snprintf(key, sizeof(key), "ANDROID_SOCKET_%s", name);
    val = getenv(key);
    if (!val)
        return -1;
    errno = 0;
    fd = strtol(val, &end, 10);
    if (errno)
        return -1;
    return (int)fd;
}

static int bind_local_abstract_stream(const char *name)
{
    struct sockaddr_un addr;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    int reuse = 1;

    if (fd < 0) {
        LOGE("cannot create socket:%s", strerror(errno));
        return -1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path + 1, sizeof(addr.sun_path) - 1, "%s", name);
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOGE("domain_socket bind:%s", strerror(errno));
        close(fd);
        return -1;
    }
    if (listen(fd, 50) < 0) {
        LOGE("domain_socket listen:%s", strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

static int open_ipc_endpoint(const char *name)
{
    int fd = android_get_control_socket(name);

    LOGD("open ipc %s", name);
    if (fd < 0) {
        LOGE("cannot create socket(android):%s", strerror(errno));
        return -1;
    }
    if (listen(fd, 50) < 0) {
        LOGE("domain_socket listen:%s", strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

static void relay_queue_push(relay_queue_t *q, uint8_t dir_uplink, const uint8_t *payload, size_t len)
{
    if (len > RELAY_MAX_PAYLOAD)
        len = RELAY_MAX_PAYLOAD;
    pthread_mutex_lock(&q->lock);
    if (q->count >= q->cap) {
        pthread_mutex_unlock(&q->lock);
        LOGE("[MDM][%s] Queue is full. Drop!!!", dir_uplink ? "UL" : "DL");
        return;
    }
    int idx = (q->head + q->count) % q->cap;
    relay_record_t *rec = &q->items[idx];
    rec->data[0] = 0;
    rec->data[1] = (uint8_t)(dir_uplink << 7);
    rec->data[2] = (uint8_t)(len & 0xFF);
    rec->data[3] = (uint8_t)((len >> 8) & 0xFF);
    memcpy(rec->data + 4, payload, len);
    memset(rec->data + 4 + len, 0, RELAY_MAX_PAYLOAD - len);
    rec->len = RELAY_RECORD_LEN;
    q->count++;
    pthread_mutex_unlock(&q->lock);
}

static int relay_queue_pop(relay_queue_t *q, relay_record_t *out)
{
    int ok = 0;

    pthread_mutex_lock(&q->lock);
    if (q->count > 0) {
        *out = q->items[q->head];
        q->head = (q->head + 1) % q->cap;
        q->count--;
        ok = 1;
    }
    pthread_mutex_unlock(&q->lock);
    return ok;
}

static int imsa_send(uint8_t type, const void *body, size_t body_len)
{
    uint8_t frame[IMSA_MSG_MAX];

    if (IMSA_HDR_LEN + body_len > sizeof(frame))
        return -1;
    frame[0] = type;
    frame[1] = 0;
    frame[2] = 0;
    frame[3] = 0;
    if (body_len)
        memcpy(frame + IMSA_HDR_LEN, body, body_len);

    pthread_mutex_lock(&g_imsa.lock);
    ssize_t n = write_full(g_imsa.fd, frame, IMSA_HDR_LEN + body_len);
    pthread_mutex_unlock(&g_imsa.lock);
    if (n < 0) {
        LOGE("write error: %s", strerror(errno));
        return -1;
    }
    return 0;
}

static ssize_t imsa_body_size_for(uint8_t type, const uint8_t *hdr4)
{
    switch (type) {
    case IMSA_MSG_FILTER_REGISTER_REQ:
    case IMSA_MSG_FILTER_UPDATE_REQ:
        return IMSA_BODY_FILTER_REGISTER;
    case IMSA_MSG_FILTER_DEREGISTER_REQ:
        return IMSA_BODY_FILTER_DEREGISTER;
    case IMSA_MSG_RESET_REQ:
        return 0;
    case IMSA_MSG_PING_PCSCF_TRIGGER:
        return IMSA_BODY_PING_PCSCF;
    case IMSA_MSG_PKT_LOSS_RPT:
        return -10;
    case IMSA_MSG_SEND:
        return -128;
    default:
        return -1;
    }
    (void)hdr4;
}

static int filter_find_by_id(uint32_t id)
{
    for (int n = 0; n < MAX_FILTERS; n++) {
        int i = (id + n) % MAX_FILTERS;
        if (g_filters[i].in_use && g_filters[i].req_id == id)
            return i;
    }
    return -1;
}

static int filter_find_free(uint32_t id)
{
    for (int n = 0; n < MAX_FILTERS; n++) {
        int i = (id + n) % MAX_FILTERS;
        if (!g_filters[i].in_use)
            return i;
    }
    return -1;
}

static int filter_addr(uint8_t version, const uint8_t *ip, const uint8_t *port,
                       struct sockaddr_storage *addr, socklen_t *addr_len)
{
    uint16_t value;

    memcpy(&value, port, sizeof(value));
    memset(addr, 0, sizeof(*addr));
    if (version == 1) {
        struct sockaddr_in *v4 = (struct sockaddr_in *)addr;
        v4->sin_family = AF_INET;
        v4->sin_port = htons(value);
        memcpy(&v4->sin_addr, ip, sizeof(v4->sin_addr));
        *addr_len = sizeof(*v4);
    } else if (version == 2) {
        struct sockaddr_in6 *v6 = (struct sockaddr_in6 *)addr;
        v6->sin6_family = AF_INET6;
        v6->sin6_port = htons(value);
        memcpy(&v6->sin6_addr, ip, sizeof(v6->sin6_addr));
        *addr_len = sizeof(*v6);
    } else {
        return -1;
    }
    return 0;
}

static int socket_find(const struct sockaddr_storage *addr)
{
    const struct sockaddr_in *v4 = (const struct sockaddr_in *)addr;
    uint16_t port = ntohs(v4->sin_port);

    for (int n = 0; n < MAX_FILTERS; n++) {
        int i = (port + n) % MAX_FILTERS;
        const struct sockaddr_storage *local = &g_sockets[i].local;
        if (!g_sockets[i].in_use || local->ss_family != addr->ss_family)
            continue;
        if (addr->ss_family == AF_INET) {
            const struct sockaddr_in *a = (const struct sockaddr_in *)addr;
            const struct sockaddr_in *b = (const struct sockaddr_in *)local;
            if (a->sin_port == b->sin_port &&
                (a->sin_addr.s_addr == INADDR_ANY || b->sin_addr.s_addr == INADDR_ANY ||
                 a->sin_addr.s_addr == b->sin_addr.s_addr))
                return i;
        } else if (addr->ss_family == AF_INET6) {
            const struct sockaddr_in6 *a = (const struct sockaddr_in6 *)addr;
            const struct sockaddr_in6 *b = (const struct sockaddr_in6 *)local;
            if (a->sin6_port == b->sin6_port &&
                (IN6_IS_ADDR_UNSPECIFIED(&a->sin6_addr) ||
                 IN6_IS_ADDR_UNSPECIFIED(&b->sin6_addr) ||
                 memcmp(&a->sin6_addr, &b->sin6_addr, sizeof(a->sin6_addr)) == 0))
                return i;
        }
    }
    return -1;
}

static int socket_find_free(const struct sockaddr_storage *addr)
{
    const struct sockaddr_in *v4 = (const struct sockaddr_in *)addr;
    uint16_t port = ntohs(v4->sin_port);

    for (int n = 0; n < MAX_FILTERS; n++) {
        int i = (port + n) % MAX_FILTERS;
        if (!g_sockets[i].in_use)
            return i;
    }
    return -1;
}

static void socket_configure(int sid, const uint8_t *body)
{
    int fd = g_sockets[sid].fd;
    uint32_t mark;
    int tos = body[46] << 2;
    int priority = body[47];

    memcpy(&mark, body + 48, sizeof(mark));
    mark |= 0xF0000;
    setsockopt(fd, SOL_SOCKET, SO_MARK, &mark, sizeof(mark));
    if (body[9] == 1)
        setsockopt(fd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
    else
        setsockopt(fd, IPPROTO_IPV6, IPV6_TCLASS, &tos, sizeof(tos));
    setsockopt(fd, SOL_SOCKET, SO_PRIORITY, &priority, sizeof(priority));
}

static int filter_bind_socket(int slot, const uint8_t *body)
{
    struct sockaddr_storage local;
    socklen_t local_len;

    if (filter_addr(body[9], body + 10, body + 42, &local, &local_len) < 0)
        return -1;
    int sid = socket_find(&local);
    if (sid < 0) {
        sid = socket_find_free(&local);
        if (sid < 0)
            return -1;
        int fd = socket(local.ss_family, SOCK_DGRAM, IPPROTO_UDP);
        if (fd < 0)
            return -1;
        if (bind(fd, (struct sockaddr *)&local, local_len) < 0)
            LOGE("bind fail, error:%s", strerror(errno));
        g_sockets[sid].in_use = 1;
        g_sockets[sid].fd = fd;
        g_sockets[sid].local = local;
        g_sockets[sid].local_len = local_len;
    }
    g_sockets[sid].filter_id = slot;
    g_filters[slot].socket_id = sid;
    memcpy(g_filters[slot].body, body, sizeof(g_filters[slot].body));
    socket_configure(sid, body);
    return 0;
}

static void socket_release(int sid)
{
    if (sid < 0 || sid >= MAX_FILTERS || !g_sockets[sid].in_use)
        return;
    close(g_sockets[sid].fd);
    memset(&g_sockets[sid], 0, sizeof(g_sockets[sid]));
    g_sockets[sid].fd = -1;
    g_sockets[sid].filter_id = -1;
}

static void handle_imsa_filter_register(const uint8_t *body, size_t len)
{
    uint32_t id;
    uint32_t ack[2];

    memcpy(&id, body, sizeof(id));
    pthread_mutex_lock(&g_filters_lock);
    int slot = filter_find_free(id);
    if (slot < 0) {
        pthread_mutex_unlock(&g_filters_lock);
        LOGE("filter_allocate error");
        return;
    }
    if (filter_bind_socket(slot, body) < 0) {
        pthread_mutex_unlock(&g_filters_lock);
        LOGE("filter_allocate error");
        return;
    }
    g_filters[slot].in_use = 1;
    g_filters[slot].req_id = id;
    filters_changed_locked();
    pthread_mutex_unlock(&g_filters_lock);

    LOGD("Register wfc downlink filter\n");
    ack[0] = id;
    ack[1] = (uint32_t)slot;
    imsa_send(IMSA_MSG_FILTER_REGISTER_ACK, ack, sizeof(ack));
    LOGD("Register wfc downlink filter success, tid:%u, fid:%u\n", ack[0], ack[1]);
    (void)len;
}

static void handle_imsa_filter_update(const uint8_t *body, size_t len)
{
    uint32_t id;
    uint32_t ack[2];

    memcpy(&id, body, sizeof(id));
    pthread_mutex_lock(&g_filters_lock);
    int slot = filter_find_by_id(id);
    if (slot < 0 || filter_bind_socket(slot, body) < 0) {
        pthread_mutex_unlock(&g_filters_lock);
        LOGE("filter_allocate error");
        return;
    }
    filters_changed_locked();
    pthread_mutex_unlock(&g_filters_lock);

    LOGD("Update wfc downlink filter \n");
    ack[0] = id;
    ack[1] = (uint32_t)(slot < 0 ? 0xFFFFFFFFu : (uint32_t)slot);
    imsa_send(IMSA_MSG_FILTER_UPDATE_ACK, ack, sizeof(ack));
    LOGD("Update wfc downlink filter success, tid:%u, fid:%u\n", ack[0], ack[1]);
    (void)len;
}

static void handle_imsa_filter_deregister(const uint8_t *body, size_t len)
{
    uint32_t id;
    uint32_t ack[2];

    memcpy(&id, body, sizeof(id));
    pthread_mutex_lock(&g_filters_lock);
    int slot = filter_find_by_id(id);
    if (slot >= 0) {
        socket_release(g_filters[slot].socket_id);
        memset(&g_filters[slot], 0, sizeof(g_filters[slot]));
        g_filters[slot].socket_id = -1;
        filters_changed_locked();
    }
    pthread_mutex_unlock(&g_filters_lock);

    LOGD("De-register wfc downlink filter\n");
    ack[0] = id;
    ack[1] = (uint32_t)(slot < 0 ? 0xFFFFFFFFu : (uint32_t)slot);
    imsa_send(IMSA_MSG_FILTER_DEREGISTER_ACK, ack, sizeof(ack));
    LOGD("De-register wfc downlink filter success, tid:%u, fid:%u\n", ack[0], ack[1]);
    (void)len;
}

static void handle_imsa_reset(void)
{
    pthread_mutex_lock(&g_filters_lock);
    for (int i = 0; i < MAX_FILTERS; i++) {
        socket_release(i);
        memset(&g_filters[i], 0, sizeof(g_filters[i]));
        g_filters[i].socket_id = -1;
    }
    filters_changed_locked();
    pthread_mutex_unlock(&g_filters_lock);
    LOGD("WFCA Reset success");
    imsa_send(IMSA_MSG_RESET_ACK, NULL, 0);
}

static void handle_imsa_pkt_loss_rpt(const uint8_t *body, size_t len)
{
    uint32_t version, pdn_id;

    memcpy(&version, body, sizeof(version));
    memcpy(&pdn_id, body + 8, sizeof(pdn_id));
    if (pdn_id >= MAX_PDN) {
        LOGE("pdn_id overflow set to zero:%u", pdn_id);
        pdn_id = 0;
    }
    LOGD("PKT_LOSS_RPT version:%u", version);

    pthread_mutex_lock(&g_pdn_lock);
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    pdn_gc_advance_locked(&now);
    pdn_slot_t *slot = &g_pdn[pdn_id];
    slot->pdn_id = pdn_id;
    slot->status = RDS_STATUS_FRESH;
    slot->body_len = len < sizeof(slot->body) ? len : sizeof(slot->body);
    memcpy(slot->body, body, slot->body_len);
    pthread_cond_signal(&g_pdn_changed);
    pthread_mutex_unlock(&g_pdn_lock);
}

static void handle_imsa_send(const uint8_t *body, size_t len)
{
    uint32_t id, payload_len;
    uint8_t ip_ver;

    if (len < IMSA_BODY_SEND_HDR)
        return;
    memcpy(&id, body, sizeof(id));
    ip_ver = body[9];
    memcpy(&payload_len, body + 52, sizeof(payload_len));
    if (IMSA_BODY_SEND_HDR + payload_len > len)
        payload_len = (uint32_t)(len - IMSA_BODY_SEND_HDR);

    relay_queue_push(&g_ul_queue, 1, body + IMSA_BODY_SEND_HDR, payload_len);

    pthread_mutex_lock(&g_filters_lock);
    int slot = filter_find_by_id(id);
    socket_slot_t local;
    int have = 0;
    if (slot >= 0) {
        int sid = g_filters[slot].socket_id;
        if (sid >= 0 && sid < MAX_FILTERS && g_sockets[sid].in_use) {
            local = g_sockets[sid];
            have = 1;
        }
    }
    pthread_mutex_unlock(&g_filters_lock);

    if (!have) {
        LOGE("cannot find wfc filter");
        LOGE("[%s]: fail", "wfc_filter_send");
        return;
    }
    if (ip_ver != 1 && ip_ver != 2) {
        LOGE("IPver is wrong\n");
        return;
    }
    struct sockaddr_storage peer;
    socklen_t peer_len;
    if (filter_addr(ip_ver, body + 26, body + 44, &peer, &peer_len) < 0)
        return;
    sendto(local.fd, body + IMSA_BODY_SEND_HDR, payload_len, 0,
           (struct sockaddr *)&peer, peer_len);
}

typedef struct {
    uint8_t request[IMSA_BODY_PING_PCSCF];
    uint8_t filter[IMSA_BODY_FILTER_REGISTER];
} ping_job_t;

static uint16_t ping_checksum(const uint8_t *data, size_t len)
{
    uint32_t sum = 0;
    while (len > 1) {
        sum += ((uint32_t)data[0] << 8) | data[1];
        data += 2;
        len -= 2;
    }
    if (len)
        sum += (uint32_t)data[0] << 8;
    while (sum >> 16)
        sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)~sum;
}

static int64_t ping_millis(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
        return -1;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int ping_probe(const ping_job_t *job)
{
    struct sockaddr_storage local, peer;
    socklen_t local_len, peer_len;
    const uint8_t zero_port[2] = {0};
    uint8_t version = job->filter[9];
    uint32_t mark;
    int received = 0;

    if (filter_addr(version, job->filter + 10, zero_port, &local, &local_len) < 0 ||
        filter_addr(version, job->request + 4, zero_port, &peer, &peer_len) < 0)
        return -1;
    int protocol = version == 1 ? IPPROTO_ICMP : IPPROTO_ICMPV6;
    int fd = socket(local.ss_family, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, protocol);
    if (fd < 0)
        return -1;
    memcpy(&mark, job->filter + 48, sizeof(mark));
    mark |= 0xf0000;
    if (setsockopt(fd, SOL_SOCKET, SO_MARK, &mark, sizeof(mark)) < 0 ||
        bind(fd, (struct sockaddr *)&local, local_len) < 0) {
        close(fd);
        return -1;
    }

    uint16_t ident = htons((uint16_t)getpid());
    for (uint16_t seq = 1; seq <= 5; seq++) {
        uint8_t request[42] = {0};
        uint16_t sequence = htons(seq);
        request[0] = version == 1 ? 8 : 128;
        memcpy(request + 4, &ident, sizeof(ident));
        memcpy(request + 6, &sequence, sizeof(sequence));
        if (version == 1) {
            uint16_t checksum = htons(ping_checksum(request, sizeof(request)));
            memcpy(request + 2, &checksum, sizeof(checksum));
        }
        if (sendto(fd, request, sizeof(request), 0,
                   (struct sockaddr *)&peer, peer_len) != (ssize_t)sizeof(request)) {
            received = -1;
            break;
        }
        int64_t start = ping_millis();
        if (start < 0) {
            received = -1;
            break;
        }
        int64_t deadline = start + 1000;
        int matched = 0;
        for (;;) {
            int64_t now = ping_millis();
            if (now < 0 || now >= deadline)
                break;
            struct pollfd pfd = {.fd = fd, .events = POLLIN};
            int ready = poll(&pfd, 1, (int)(deadline - now));
            if (ready < 0 && errno == EINTR)
                continue;
            if (ready <= 0 || (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)))
                break;
            uint8_t reply[2048];
            struct sockaddr_storage from;
            socklen_t from_len = sizeof(from);
            ssize_t n = recvfrom(fd, reply, sizeof(reply), 0,
                                 (struct sockaddr *)&from, &from_len);
            if (n < 0) {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                    continue;
                break;
            }
            size_t offset = 0;
            if (version == 1) {
                if (n < 20 || (reply[0] >> 4) != 4 || reply[9] != IPPROTO_ICMP)
                    continue;
                offset = (reply[0] & 15u) * 4u;
                if (offset < 20)
                    continue;
            }
            if ((size_t)n < offset + 8 || reply[offset] != (version == 1 ? 0 : 129) ||
                reply[offset + 1] != 0 || memcmp(reply + offset + 4, &ident, 2) ||
                memcmp(reply + offset + 6, &sequence, 2))
                continue;
            if (from.ss_family != peer.ss_family)
                continue;
            if (version == 1) {
                if (((struct sockaddr_in *)&from)->sin_addr.s_addr !=
                    ((struct sockaddr_in *)&peer)->sin_addr.s_addr)
                    continue;
            } else if (memcmp(&((struct sockaddr_in6 *)&from)->sin6_addr,
                              &((struct sockaddr_in6 *)&peer)->sin6_addr,
                              sizeof(struct in6_addr))) {
                continue;
            }
            if (!matched) {
                received++;
                matched = 1;
            }
        }
    }
    close(fd);
    return received;
}

static void *ping_thread(void *arg)
{
    ping_job_t *job = arg;
    int result = ping_probe(job);
    LOGD("P-CSCF probe result:%d", result);
    imsa_send(IMSA_MSG_PING_PCSCF_TRIGGER, job->request, sizeof(job->request));
    free(job);
    return NULL;
}

static void handle_imsa_ping(const uint8_t *body, size_t len)
{
    uint32_t id;
    pthread_t tid;
    if (len != IMSA_BODY_PING_PCSCF)
        return;
    memcpy(&id, body, sizeof(id));
    ping_job_t *job = malloc(sizeof(*job));
    if (!job)
        return;
    pthread_mutex_lock(&g_filters_lock);
    int slot = filter_find_by_id(id);
    if (slot >= 0)
        memcpy(job->filter, g_filters[slot].body, sizeof(job->filter));
    pthread_mutex_unlock(&g_filters_lock);
    if (slot < 0) {
        free(job);
        return;
    }
    memcpy(job->request, body, sizeof(job->request));
    int error = pthread_create(&tid, NULL, ping_thread, job);
    if (error) {
        LOGE("P-CSCF thread: %s", strerror(error));
        free(job);
        return;
    }
    pthread_detach(tid);
}

static void imsa_reader_drain(void)
{
    static uint8_t frame[IMSA_MSG_MAX];
    static size_t have;
    static size_t need = IMSA_HDR_LEN;
    static int stage;
    static uint32_t discard;

    for (;;) {
        uint8_t scratch[512];
        void *dst = discard ? scratch : frame + have;
        size_t want = discard ? (discard < sizeof(scratch) ? discard : sizeof(scratch)) :
                                need - have;
        ssize_t n = read(g_imsa.fd, dst, want);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return;
        if (n <= 0) {
            have = 0;
            need = IMSA_HDR_LEN;
            stage = 0;
            discard = 0;
            return;
        }
        if (discard) {
            discard -= (uint32_t)n;
            if (!discard) {
                have = 0;
                need = IMSA_HDR_LEN;
                stage = 0;
            }
            continue;
        }
        have += (size_t)n;
        if (have != need)
            continue;
        uint8_t type = frame[0];
        uint8_t *body = frame + IMSA_HDR_LEN;
        if (stage == 0) {
            ssize_t size = imsa_body_size_for(type, frame);
            if (size == -10)
                size = IMSA_BODY_PKT_LOSS_RPT_BASE;
            else if (size == -128)
                size = IMSA_BODY_SEND_HDR;
            if (size < 0) {
                LOGE("Unknown size Message ID: %x", type);
                have = 0;
                return;
            }
            need += (size_t)size;
            stage = 1;
            if (have != need)
                continue;
        }
        if (stage == 1 && (type == IMSA_MSG_SEND || type == IMSA_MSG_PKT_LOSS_RPT)) {
            uint32_t extra;
            memcpy(&extra, body + (type == IMSA_MSG_SEND ? 52 : 0), sizeof(extra));
            if (type == IMSA_MSG_PKT_LOSS_RPT)
                extra = extra == 1 ? IMSA_BODY_PKT_LOSS_RPT_EXT : 0;
            stage = 2;
            if (extra > sizeof(frame) - need) {
                discard = extra;
                continue;
            }
            need += extra;
            if (have != need)
                continue;
        }
        size_t len = have - IMSA_HDR_LEN;
        switch (type) {
        case IMSA_MSG_FILTER_REGISTER_REQ:
            handle_imsa_filter_register(body, len);
            break;
        case IMSA_MSG_FILTER_DEREGISTER_REQ:
            handle_imsa_filter_deregister(body, len);
            break;
        case IMSA_MSG_FILTER_UPDATE_REQ:
            handle_imsa_filter_update(body, len);
            break;
        case IMSA_MSG_RESET_REQ:
            handle_imsa_reset();
            break;
        case IMSA_MSG_PING_PCSCF_TRIGGER:
            handle_imsa_ping(body, len);
            break;
        case IMSA_MSG_PKT_LOSS_RPT:
            handle_imsa_pkt_loss_rpt(body, len);
            break;
        case IMSA_MSG_SEND:
            handle_imsa_send(body, len);
            break;
        }
        have = 0;
        need = IMSA_HDR_LEN;
        stage = 0;
    }
}

static void *imsa_reader_thread(void *arg)
{
    for (;;) {
        struct pollfd pfd = {.fd = g_imsa.fd, .events = POLLIN};
        int r = poll(&pfd, 1, -1);

        if (r < 0) {
            if (errno == EINTR)
                continue;
            LOGE("Some error: %s !!", strerror(errno));
            usleep(500 * 1000);
            continue;
        }
        if (r == 0)
            continue;
        wake_lock_acquire(WAKE_LOCK_NAME);
        imsa_reader_drain();
        wake_lock_release(WAKE_LOCK_NAME);
    }
    (void)arg;
    return NULL;
}

static int ccci_imsa_open_and_wait_ready(void)
{
    int fd = open(CCCI_IMSA_DEV, O_RDWR);

    if (fd < 0) {
        LOGE("open imsx failed: %s", CCCI_IMSA_DEV);
        exit(1);
    }
    ioctl(fd, CCCI_IMSA_IOC_REGISTER);
    int flags = fcntl(fd, F_GETFL, 0);
    int status = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (status < 0)
        LOGE("imsx open set fd nonblock:%s", strerror(errno));

    for (;;) {
        ioctl(fd, CCCI_IMSA_IOC_QUERY_STATUS, &status);
        if (status < 0) {
            LOGE("ccci query md status error:%s", strerror(errno));
            return fd;
        }
        if (status == CCCI_MD_STATUS_READY)
            break;
        usleep(1000 * 1000);
    }
    LOGD("CCCI status:MD ready");
    return fd;
}

static int wfc_ipc_recv_fd(int conn_fd)
{
    struct msghdr msg;
    struct iovec iov;
    uint8_t iobuf;
    union {
        struct cmsghdr align;
        char bytes[CMSG_SPACE(MAX_FILTERS * sizeof(int))];
    } control;

    memset(&msg, 0, sizeof(msg));
    iov.iov_base = &iobuf;
    iov.iov_len = 1;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control.bytes;
    msg.msg_controllen = sizeof(control.bytes);

    if (recvmsg(conn_fd, &msg, 0) != 1) {
        LOGE("recvmsg %s", strerror(errno));
        LOGE("%s:fail", "wfc_ipc_recv_fd");
        return -1;
    }
    int fd = -1;
    int count = 0;
    for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg;
         cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS ||
            cmsg->cmsg_len < CMSG_LEN(0))
            continue;
        size_t bytes = cmsg->cmsg_len - CMSG_LEN(0);
        for (size_t offset = 0; offset + sizeof(int) <= bytes; offset += sizeof(int)) {
            int received;
            memcpy(&received, (uint8_t *)CMSG_DATA(cmsg) + offset, sizeof(received));
            if (count++ == 0)
                fd = received;
            else
                close(received);
        }
    }
    if (count != 1 || (msg.msg_flags & (MSG_CTRUNC | MSG_TRUNC))) {
        if (fd >= 0)
            close(fd);
        return -1;
    }
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int filter_insert_socket(int sock_fd)
{
    struct sockaddr_storage local;
    socklen_t local_len = sizeof(local);

    if (getsockname(sock_fd, (struct sockaddr *)&local, &local_len) < 0) {
        LOGE("get local socket %s", strerror(errno));
        close(sock_fd);
        LOGE("[%s]:fail", "wfc_socket_insert");
        return -1;
    }

    pthread_mutex_lock(&g_filters_lock);
    int sid = socket_find(&local);
    if (sid >= 0) {
        LOGD("[ua] find existed socket");
        close(g_sockets[sid].fd);
        g_sockets[sid].fd = sock_fd;
        int slot = g_sockets[sid].filter_id;
        if (slot >= 0 && slot < MAX_FILTERS && g_filters[slot].in_use)
            socket_configure(sid, g_filters[slot].body);
    } else {
        sid = socket_find_free(&local);
        if (sid < 0) {
            pthread_mutex_unlock(&g_filters_lock);
            close(sock_fd);
            LOGE("[%s]:fail", "wfc_socket_insert");
            return -1;
        }
        g_sockets[sid].in_use = 1;
        g_sockets[sid].fd = sock_fd;
        g_sockets[sid].filter_id = -1;
        g_sockets[sid].local_len = local_len;
        g_sockets[sid].local = local;
    }
    filters_changed_locked();
    pthread_mutex_unlock(&g_filters_lock);

    return 0;
}

static void *wfca_listener_thread(void *arg)
{
    int listen_fd = *(int *)arg;

    LOGD("listen_fd thread created");
    for (;;) {
        struct sockaddr_un peer;
        socklen_t peer_len = sizeof(peer);
        int conn = accept(listen_fd, (struct sockaddr *)&peer, &peer_len);

        if (conn < 0) {
            LOGE("[server] domain_socket accept error:%s", strerror(errno));
            break;
        }
        for (;;) {
            int recv_fd = wfc_ipc_recv_fd(conn);
            LOGD("receive fd:%d", recv_fd);
            if (recv_fd < 0)
                break;
            if (filter_insert_socket(recv_fd) < 0)
                break;
        }
        close(conn);
    }
    return NULL;
}

static void *rds_listener_thread(void *arg)
{
    int listen_fd = *(int *)arg;

    for (;;) {
        struct sockaddr_un peer;
        socklen_t peer_len = sizeof(peer);
        int conn = accept(listen_fd, (struct sockaddr *)&peer, &peer_len);
        uint32_t hdr[5];

        if (conn < 0) {
            LOGE("[rds default] domain_socket accept error:%s", strerror(errno));
            continue;
        }
        if (read_full(conn, hdr, sizeof(hdr)) != (ssize_t)sizeof(hdr)) {
            LOGE("[rds default] read basic:%s", strerror(errno));
            close(conn);
            continue;
        }
        uint8_t ext[512];
        int has_ext = hdr[1] == 1;
        if (has_ext && read_full(conn, ext, sizeof(ext)) != (ssize_t)sizeof(ext)) {
            LOGE("[rds default] read extension:%s", strerror(errno));
            close(conn);
            continue;
        }
        uint32_t cmd = hdr[0];
        uint32_t pdn_id = hdr[2];

        if (pdn_id >= MAX_PDN) {
            LOGE("ERROR:pdn id overflow");
            close(conn);
            continue;
        }

        uint8_t reply[IMSA_BODY_PKT_LOSS_RPT_BASE + IMSA_BODY_PKT_LOSS_RPT_EXT];
        size_t reply_len = hdr[1] ? sizeof(reply) : IMSA_BODY_PKT_LOSS_RPT_BASE;
        memset(reply, 0, sizeof(reply));

        if (cmd == 1) {
            LOGD("[rds config]pdn_id:%u, threshold:%u, duration:%u", hdr[2], hdr[3], hdr[4]);
            pthread_mutex_lock(&g_pdn_lock);
            g_pdn[pdn_id].pdn_id = pdn_id;
            g_pdn[pdn_id].threshold = hdr[3];
            g_pdn[pdn_id].duration = hdr[4];
            pthread_mutex_unlock(&g_pdn_lock);

            uint8_t fwd[528];
            memset(fwd, 0, sizeof(fwd));
            memcpy(fwd, &hdr[1], sizeof(hdr) - sizeof(hdr[0]));
            if (has_ext)
                memcpy(fwd + sizeof(hdr) - sizeof(hdr[0]), ext, sizeof(ext));
            imsa_send(IMSA_MSG_RDS_CONFIG_FWD, fwd, sizeof(fwd));

        } else if (cmd == 2) {
            LOGD("[rds query]pdn_id:%u", pdn_id);
            pthread_mutex_lock(&g_pdn_lock);
            memcpy(reply, g_pdn[pdn_id].body, IMSA_BODY_PKT_LOSS_RPT_BASE);
            uint32_t status = g_pdn[pdn_id].status;
            memcpy(reply + 4, &status, sizeof(status));
            if (has_ext && g_pdn[pdn_id].body_len > IMSA_BODY_PKT_LOSS_RPT_BASE)
                memcpy(reply + IMSA_BODY_PKT_LOSS_RPT_BASE,
                       g_pdn[pdn_id].body + IMSA_BODY_PKT_LOSS_RPT_BASE,
                       g_pdn[pdn_id].body_len - IMSA_BODY_PKT_LOSS_RPT_BASE);
            pthread_mutex_unlock(&g_pdn_lock);
        } else {
            LOGD("[rds command] ignore dump hex");
        }

        ssize_t sent = write_full(conn, reply, reply_len);
        if (sent < 0)
            LOGE("write error: %s", strerror(errno));
        else
            LOGD("[rds cnf] send %d", (int)reply_len);

        if (cmd == 1 || cmd == 2) {
            pthread_mutex_lock(&g_pdn_lock);
            if (g_pdn[pdn_id].status != RDS_STATUS_EMPTY)
                g_pdn[pdn_id].status = RDS_STATUS_STALE;
            pthread_cond_signal(&g_pdn_changed);
            pthread_mutex_unlock(&g_pdn_lock);
        }
        close(conn);
    }
    return NULL;
}

static void *rds_gc_thread(void *arg)
{
    pthread_mutex_lock(&g_pdn_lock);
    for (;;) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        pdn_gc_advance_locked(&now);
        int fresh = 0;
        for (int i = 0; i < MAX_PDN; i++) {
            if (g_pdn[i].status == RDS_STATUS_FRESH)
                fresh = 1;
        }
        if (!fresh)
            pthread_cond_wait(&g_pdn_changed, &g_pdn_lock);
        else
            pthread_cond_timedwait(&g_pdn_changed, &g_pdn_lock, &g_pdn_gc_deadline);
    }
    (void)arg;
    return NULL;
}

static void mdm_data_thread_stop_locked(void)
{
    if (g_mdm.data_thread_running)
        g_mdm.data_thread_stop_req = 1;
}

static void *mdm_data_thread(void *arg)
{
    LOGD("MDM data thread created");
    for (;;) {
        pthread_mutex_lock(&g_mdm.lock);
        int stop = g_mdm.data_thread_stop_req;
        if (stop) {
            g_mdm.data_thread_running = 0;
            g_mdm.data_thread_stop_req = 0;
        }
        pthread_mutex_unlock(&g_mdm.lock);
        if (stop)
            return NULL;
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un addr;

        if (fd < 0) {
            LOGE("[MDM data]cannot create socket:%s", strerror(errno));
            sleep(3);
            continue;
        }
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        snprintf(addr.sun_path + 1, sizeof(addr.sun_path) - 1, "%s", MDM_DATA_SOCK_NAME);
        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            close(fd);
            sleep(3);
            continue;
        }
        LOGD("[MDM data]connect to MDM successfully!!");

        for (;;) {
            relay_record_t rec;
            int did_work = 0;

            if (relay_queue_pop(&g_ul_queue, &rec)) {
                if (write_full(fd, rec.data, rec.len) < 0)
                    LOGE("[MDM][UL] write error: %s", strerror(errno));
                did_work = 1;
            }
            if (relay_queue_pop(&g_dl_queue, &rec)) {
                if (write_full(fd, rec.data, rec.len) < 0)
                    LOGE("[MDM][DL] write error: %s", strerror(errno));
                did_work = 1;
            }
            pthread_mutex_lock(&g_mdm.lock);
            int stop = g_mdm.data_thread_stop_req;
            pthread_mutex_unlock(&g_mdm.lock);
            if (stop) {
                close(fd);
                pthread_mutex_lock(&g_mdm.lock);
                g_mdm.data_thread_running = 0;
                g_mdm.data_thread_stop_req = 0;
                pthread_mutex_unlock(&g_mdm.lock);
                return NULL;
            }
            if (!did_work)
                usleep(100 * 1000);
        }
    }
    (void)arg;
    return NULL;
}

static void mdm_apply_gate_locked(void)
{
    if (g_mdm.started && g_mdm.incall) {
        if (!g_mdm.data_thread_running) {
            g_mdm.data_thread_stop_req = 0;
            if (pthread_create(&g_mdm.data_tid, NULL, mdm_data_thread, NULL) != 0) {
                LOGE("MDM data thread create fail:%s", strerror(errno));
            } else {
                pthread_detach(g_mdm.data_tid);
                g_mdm.data_thread_running = 1;
            }
        } else {
            LOGD("[MDM] data thread has already created");
        }
    } else if (g_mdm.data_thread_running) {
        LOGD("[MDM] data thread will exit now");
        mdm_data_thread_stop_locked();
    } else {
        LOGD("[MDM] data thread is not existed");
    }
}

static void handle_mdm_ctrl_cmd(uint8_t cmd)
{
    const char *name = NULL;

    pthread_mutex_lock(&g_mdm.lock);
    switch (cmd) {
    case MDM_CTRL_CMD_START:
        g_mdm.started = 1;
        name = "MDM_WFCA_START";
        break;
    case MDM_CTRL_CMD_STOP:
        g_mdm.started = 0;
        name = "MDM_WFCA_STOP";
        break;
    case MDM_CTRL_CMD_INCALL:
        g_mdm.incall = 1;
        name = "WFCA_INCALL";
        break;
    case MDM_CTRL_CMD_NOT_INCALL:
        g_mdm.incall = 0;
        name = "WFCA_NOT_INCALL";
        break;
    default:
        break;
    }
    if (name)
        LOGD("[MDM] cmd type: %s", name);
    else
        LOGE("[MDM] unknown cmd type: %d", cmd);
    mdm_apply_gate_locked();
    pthread_mutex_unlock(&g_mdm.lock);
}

static void *mdm_ctrl_listener_thread(void *arg)
{
    int listen_fd = *(int *)arg;

    for (;;) {
        struct sockaddr_un peer;
        socklen_t peer_len = sizeof(peer);
        int conn = accept(listen_fd, (struct sockaddr *)&peer, &peer_len);
        uint32_t hdr[2];

        if (conn < 0)
            continue;
        if (read_full(conn, hdr, sizeof(hdr)) != (ssize_t)sizeof(hdr)) {
            close(conn);
            continue;
        }
        uint32_t magic = hdr[0];
        uint8_t cmd = (uint8_t)(hdr[1] & 0xFF);
        uint8_t version = (uint8_t)((hdr[1] >> 8) & 0xFF);

        if (magic != MDM_CTRL_MAGIC) {
            LOGE("[MDM] unknown magic num: %x", magic);
        } else if (version != 0) {
            LOGE("[MDM] unmatched version: %d", version);
        } else {
            handle_mdm_ctrl_cmd(cmd);
        }
        close(conn);
    }
    (void)arg;
    return NULL;
}

static void *wfc_filter_poll_thread(void *arg)
{
    struct pollfd pfds[MAX_FILTERS + 1];
    int idx_map[MAX_FILTERS];
    int nfds = 0;
    int rebuild = 1;

    for (;;) {
        if (rebuild) {
            uint64_t value;
            ssize_t n;

            pthread_mutex_lock(&g_filters_lock);
            do {
                n = read(g_filters_event_fd, &value, sizeof(value));
            } while (n >= 0 || errno == EINTR);
            nfds = 0;
            for (int i = 0; i < MAX_FILTERS; i++) {
                if (g_sockets[i].in_use && g_sockets[i].filter_id >= 0) {
                    pfds[nfds].fd = g_sockets[i].fd;
                    pfds[nfds].events = POLLIN;
                    idx_map[nfds] = i;
                    nfds++;
                }
            }
            pthread_mutex_unlock(&g_filters_lock);
            pfds[nfds].fd = g_filters_event_fd;
            pfds[nfds].events = POLLIN;
            rebuild = 0;
        }
        int r = poll(pfds, nfds + 1, -1);
        if (r <= 0)
            continue;
        if (pfds[nfds].revents) {
            rebuild = 1;
            continue;
        }

        for (int i = 0; i < nfds; i++) {
            if (!(pfds[i].revents & POLLIN))
                continue;
            uint8_t buf[1500];
            struct sockaddr_storage from;
            socklen_t from_len = sizeof(from);
            ssize_t n = recvfrom(pfds[i].fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &from_len);

            if (n <= 0)
                continue;

            uint32_t id;
            pthread_mutex_lock(&g_filters_lock);
            int slot = g_sockets[idx_map[i]].filter_id;
            if (slot < 0 || slot >= MAX_FILTERS || !g_filters[slot].in_use) {
                pthread_mutex_unlock(&g_filters_lock);
                continue;
            }
            id = g_filters[slot].req_id;
            pthread_mutex_unlock(&g_filters_lock);

            uint8_t msg_type;
            if (n >= 12 && (buf[0] >> 6) == 2 && buf[1] >= 200 && buf[1] <= 204)
                msg_type = IMSA_MSG_ERROR;
            else if (n >= 12 && (buf[0] >> 6) == 2)
                msg_type = IMSA_MSG_UPLINK_DATA;
            else {
                LOGD("Non RTP/RTCP error !!\n");
                relay_queue_push(&g_dl_queue, 0, buf, (size_t)n);
                continue;
            }

            uint8_t frame[8 + 1500];
            memcpy(frame, &id, sizeof(id));
            uint32_t len32 = (uint32_t)n;
            memcpy(frame + 4, &len32, sizeof(len32));
            memcpy(frame + 8, buf, (size_t)n);
            imsa_send(msg_type, frame, 8 + (size_t)n);
            relay_queue_push(&g_dl_queue, 0, buf, (size_t)n);
        }
    }
    (void)arg;
    return NULL;
}

int main(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_flags = SA_NOCLDWAIT;
    sigaction(SIGCHLD, &sa, NULL);

    g_filters_event_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (g_filters_event_fd < 0) {
        LOGE("filter eventfd fail:%s", strerror(errno));
        return 1;
    }
    pthread_condattr_t attr;
    int error = pthread_condattr_init(&attr);
    if (!error) {
        error = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
        if (!error)
            error = pthread_cond_init(&g_pdn_changed, &attr);
        pthread_condattr_destroy(&attr);
    }
    if (error) {
        LOGE("PDN condition init fail:%s", strerror(error));
        return 1;
    }
    for (int i = 0; i < MAX_FILTERS; i++) {
        g_filters[i].socket_id = -1;
        g_sockets[i].fd = -1;
        g_sockets[i].filter_id = -1;
    }
    for (int i = 0; i < MAX_PDN; i++)
        g_pdn[i].status = RDS_STATUS_EMPTY;
    g_imsa.fd = -1;

    int wfca_fd;
    for (;;) {
        wfca_fd = open_ipc_endpoint(WFCA_SOCK_NAME);
        if (wfca_fd >= 0)
            break;
        sleep(1);
    }
    static int wfca_fd_store;
    wfca_fd_store = wfca_fd;
    pthread_t tid;
    if (pthread_create(&tid, NULL, wfca_listener_thread, &wfca_fd_store) != 0)
        LOGE("UA recv thread create fail:%s", strerror(errno));
    else
        pthread_detach(tid);

    static int rds_fd_store;
    clock_gettime(CLOCK_MONOTONIC, &g_pdn_gc_deadline);
    g_pdn_gc_deadline.tv_sec += 50;
    int rds_fd = open_ipc_endpoint(WFCA_RDS_SOCK_NAME);
    if (rds_fd >= 0) {
        rds_fd_store = rds_fd;
        if (pthread_create(&tid, NULL, rds_listener_thread, &rds_fd_store) != 0)
            LOGE("RDS recv thread create fail:%s", strerror(errno));
        else
            pthread_detach(tid);
        if (pthread_create(&tid, NULL, rds_gc_thread, NULL) == 0)
            pthread_detach(tid);
    }

    static int mdm_ctrl_fd_store;
    int mdm_ctrl_fd = bind_local_abstract_stream(MDM_CTRL_SOCK_NAME);
    if (mdm_ctrl_fd >= 0) {
        mdm_ctrl_fd_store = mdm_ctrl_fd;
        if (pthread_create(&tid, NULL, mdm_ctrl_listener_thread, &mdm_ctrl_fd_store) != 0)
            LOGE("MDM ctrl thread create fail:%s", strerror(errno));
        else
            pthread_detach(tid);
    }

    g_imsa.fd = ccci_imsa_open_and_wait_ready();
    if (pthread_create(&tid, NULL, imsa_reader_thread, NULL) == 0)
        pthread_detach(tid);

    if (pthread_create(&tid, NULL, wfc_filter_poll_thread, NULL) == 0)
        pthread_detach(tid);

    for (;;)
        pause();
    return 0;
}
