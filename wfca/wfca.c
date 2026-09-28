#include <android/log.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
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
    int fd;
    sa_family_t family;
    uint32_t req_id;
    struct sockaddr_storage peer;
    socklen_t peer_len;
} filter_slot_t;

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

static pthread_mutex_t g_pdn_lock = PTHREAD_MUTEX_INITIALIZER;
static pdn_slot_t g_pdn[MAX_PDN];

static imsa_ctx_t g_imsa = {.fd = -1, .lock = PTHREAD_MUTEX_INITIALIZER};
static relay_queue_t g_ul_queue = {.cap = RELAY_UL_CAP, .lock = PTHREAD_MUTEX_INITIALIZER};
static relay_queue_t g_dl_queue = {.cap = RELAY_DL_CAP, .lock = PTHREAD_MUTEX_INITIALIZER};
static mdm_ctrl_ctx_t g_mdm = {.lock = PTHREAD_MUTEX_INITIALIZER};

static int g_wake_lock_fd = -1;
static int g_wake_unlock_fd = -1;
static pthread_mutex_t g_wake_lock_init = PTHREAD_MUTEX_INITIALIZER;
static int g_wake_lock_ready;

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
    rec->len = 4 + len;
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
    for (int i = 0; i < MAX_FILTERS; i++)
        if (g_filters[i].in_use && g_filters[i].req_id == id)
            return i;
    return -1;
}

static int filter_find_free(void)
{
    for (int i = 0; i < MAX_FILTERS; i++)
        if (!g_filters[i].in_use)
            return i;
    return -1;
}

static void handle_imsa_filter_register(const uint8_t *body, size_t len)
{
    uint32_t id;
    uint32_t ack[2];

    memcpy(&id, body, sizeof(id));
    pthread_mutex_lock(&g_filters_lock);
    int slot = filter_find_by_id(id);
    if (slot < 0)
        slot = filter_find_free();
    if (slot < 0) {
        pthread_mutex_unlock(&g_filters_lock);
        LOGE("filter_allocate error");
        return;
    }
    g_filters[slot].in_use = 1;
    g_filters[slot].req_id = id;
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
        if (g_filters[slot].fd >= 0)
            close(g_filters[slot].fd);
        memset(&g_filters[slot], 0, sizeof(g_filters[slot]));
        g_filters[slot].fd = -1;
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
        if (g_filters[i].in_use && g_filters[i].fd >= 0)
            close(g_filters[i].fd);
        memset(&g_filters[i], 0, sizeof(g_filters[i]));
        g_filters[i].fd = -1;
    }
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
    pdn_slot_t *slot = &g_pdn[pdn_id];
    slot->pdn_id = pdn_id;
    slot->status = RDS_STATUS_FRESH;
    slot->body_len = len < sizeof(slot->body) ? len : sizeof(slot->body);
    memcpy(slot->body, body, slot->body_len);
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
    filter_slot_t local;
    int have = 0;
    if (slot >= 0 && g_filters[slot].fd >= 0) {
        local = g_filters[slot];
        have = 1;
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
    sendto(local.fd, body + IMSA_BODY_SEND_HDR, payload_len, 0,
           (struct sockaddr *)&local.peer, local.peer_len);
}

static void imsa_reader_drain(void)
{
    uint8_t hdr[IMSA_HDR_LEN];
    uint8_t body[IMSA_MSG_MAX];

    for (;;) {
        ssize_t n = read(g_imsa.fd, hdr, IMSA_HDR_LEN);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return;
            LOGE("read:%d, %s, remain:%d", errno, strerror(errno), IMSA_HDR_LEN);
            return;
        }
        if (n == 0)
            return;
        if (n != IMSA_HDR_LEN) {
            ssize_t rest = read_full(g_imsa.fd, hdr + n, IMSA_HDR_LEN - n);
            if (rest < 0)
                return;
        }

        uint8_t type = hdr[0];
        ssize_t body_len = imsa_body_size_for(type, hdr);
        size_t to_read;

        if (body_len == -10) {
            if (read_full(g_imsa.fd, body, IMSA_BODY_PKT_LOSS_RPT_BASE) != IMSA_BODY_PKT_LOSS_RPT_BASE)
                return;
            uint32_t version;
            memcpy(&version, body, sizeof(version));
            to_read = IMSA_BODY_PKT_LOSS_RPT_BASE;
            if (version == 1) {
                if (read_full(g_imsa.fd, body + IMSA_BODY_PKT_LOSS_RPT_BASE,
                               IMSA_BODY_PKT_LOSS_RPT_EXT) != IMSA_BODY_PKT_LOSS_RPT_EXT)
                    return;
                LOGD("ext body size:%d\n", IMSA_BODY_PKT_LOSS_RPT_EXT);
                to_read += IMSA_BODY_PKT_LOSS_RPT_EXT;
            }
            handle_imsa_pkt_loss_rpt(body, to_read);
            continue;
        }
        if (body_len == -128) {
            if (read_full(g_imsa.fd, body, IMSA_BODY_SEND_HDR) != IMSA_BODY_SEND_HDR)
                return;
            uint32_t extra;
            memcpy(&extra, body + 52, sizeof(extra));
            if (extra > sizeof(body) - IMSA_BODY_SEND_HDR)
                extra = sizeof(body) - IMSA_BODY_SEND_HDR;
            if (extra && read_full(g_imsa.fd, body + IMSA_BODY_SEND_HDR, extra) != (ssize_t)extra)
                return;
            handle_imsa_send(body, IMSA_BODY_SEND_HDR + extra);
            continue;
        }
        if (body_len < 0) {
            LOGE("Unknown size Message ID: %x\n", type);
            return;
        }
        to_read = (size_t)body_len;
        if (to_read && read_full(g_imsa.fd, body, to_read) != (ssize_t)to_read)
            return;

        switch (type) {
        case IMSA_MSG_FILTER_REGISTER_REQ:
            handle_imsa_filter_register(body, to_read);
            break;
        case IMSA_MSG_FILTER_DEREGISTER_REQ:
            handle_imsa_filter_deregister(body, to_read);
            break;
        case IMSA_MSG_FILTER_UPDATE_REQ:
            handle_imsa_filter_update(body, to_read);
            break;
        case IMSA_MSG_RESET_REQ:
            handle_imsa_reset();
            break;
        case IMSA_MSG_PING_PCSCF_TRIGGER:
            LOGD("trigger ping pcscf success\n");
            break;
        default:
            LOGD("unknown message id");
            break;
        }
    }
}

static void *imsa_reader_thread(void *arg)
{
    for (;;) {
        struct pollfd pfd = {.fd = g_imsa.fd, .events = POLLIN};
        int r = poll(&pfd, 1, 300 * 1000);

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
    ioctl(fd, CCCI_IMSA_IOC_REGISTER, -1);
    int flags = fcntl(fd, F_GETFL, 0);
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        LOGE("imsx open set fd nonblock:%s", strerror(errno));

    for (;;) {
        int status = 0;
        if (ioctl(fd, CCCI_IMSA_IOC_QUERY_STATUS, &status) < 0) {
            LOGE("ccci query md status error:%s", strerror(errno));
            return fd;
        }
        if (status == CCCI_MD_STATUS_READY)
            break;
        usleep(100 * 1000);
    }
    LOGD("CCCI status:MD ready");
    return fd;
}

static int wfc_ipc_recv_fd(int conn_fd)
{
    struct msghdr msg;
    struct iovec iov;
    uint8_t iobuf;
    char cmsgbuf[CMSG_SPACE(sizeof(int))];

    memset(&msg, 0, sizeof(msg));
    iov.iov_base = &iobuf;
    iov.iov_len = 1;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsgbuf;
    msg.msg_controllen = sizeof(cmsgbuf);

    if (recvmsg(conn_fd, &msg, 0) != 1) {
        LOGE("recvmsg %s", strerror(errno));
        LOGE("%s:fail", "wfc_ipc_recv_fd");
        return -1;
    }
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    if (!cmsg || msg.msg_controllen < sizeof(struct cmsghdr)) {
        LOGE("pass msg fail %s", strerror(errno));
        LOGE("%s:fail", "wfc_ipc_recv_fd");
        return -1;
    }
    int fd;
    memcpy(&fd, CMSG_DATA(cmsg), sizeof(fd));
    if (fd == -1) {
        LOGE("no passed fd %s", strerror(errno));
        LOGE("%s:fail", "wfc_ipc_recv_fd");
        return -1;
    }
    return fd;
}

static int filter_insert_socket(int sock_fd)
{
    struct sockaddr_storage local;
    socklen_t local_len = sizeof(local);
    int mark;

    if (getsockname(sock_fd, (struct sockaddr *)&local, &local_len) < 0) {
        LOGE("get local socket %s", strerror(errno));
        close(sock_fd);
        LOGE("[%s]:fail", "wfc_socket_insert");
        return -1;
    }

    pthread_mutex_lock(&g_filters_lock);
    int slot = -1;
    for (int i = 0; i < MAX_FILTERS; i++) {
        if (g_filters[i].in_use && g_filters[i].peer_len == local_len &&
            memcmp(&g_filters[i].peer, &local, local_len) == 0) {
            slot = i;
            break;
        }
    }
    if (slot >= 0) {
        LOGD("[ua] find existed socket");
        if (g_filters[slot].fd >= 0)
            close(g_filters[slot].fd);
        g_filters[slot].fd = sock_fd;
    } else {
        slot = filter_find_free();
        if (slot < 0) {
            pthread_mutex_unlock(&g_filters_lock);
            close(sock_fd);
            LOGE("[%s]:fail", "wfc_socket_insert");
            return -1;
        }
        g_filters[slot].in_use = 1;
        g_filters[slot].fd = sock_fd;
        g_filters[slot].peer_len = local_len;
        memcpy(&g_filters[slot].peer, &local, local_len);
        g_filters[slot].family = local.ss_family;
    }
    pthread_mutex_unlock(&g_filters_lock);

    mark = 0xF0000;
    if (setsockopt(sock_fd, SOL_SOCKET, SO_MARK, &mark, sizeof(mark)) >= 0)
        LOGD("[ua] setsockopt recv udp socket SO_MARK success, id:0x%x", mark);
    else
        LOGE("[ua] setsockopt recv udp socket SO_MARK fail:%s, id:%x", strerror(errno), mark);

    int tos = 0xB8;
    setsockopt(sock_fd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
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
        size_t reply_len = has_ext ? sizeof(reply) : IMSA_BODY_PKT_LOSS_RPT_BASE;
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

            memcpy(reply, hdr, sizeof(hdr));
        } else if (cmd == 2) {
            LOGD("[rds query]pdn_id:%u", pdn_id);
            pthread_mutex_lock(&g_pdn_lock);
            memcpy(reply, &g_pdn[pdn_id], IMSA_BODY_PKT_LOSS_RPT_BASE);
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
            pthread_mutex_unlock(&g_pdn_lock);
        }
        close(conn);
    }
    return NULL;
}

static void *rds_gc_thread(void *arg)
{
    for (;;) {
        sleep(50);
        pthread_mutex_lock(&g_pdn_lock);
        for (int i = 0; i < MAX_PDN; i++) {
            if (g_pdn[i].status == RDS_STATUS_FRESH) {
                g_pdn[i].status = RDS_STATUS_STALE;
                LOGD("clear pdn_id:%d status", i);
            }
        }
        pthread_mutex_unlock(&g_pdn_lock);
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
    for (;;) {
        struct pollfd pfds[MAX_FILTERS];
        int idx_map[MAX_FILTERS];
        int nfds = 0;

        pthread_mutex_lock(&g_filters_lock);
        for (int i = 0; i < MAX_FILTERS; i++) {
            if (g_filters[i].in_use && g_filters[i].fd >= 0) {
                pfds[nfds].fd = g_filters[i].fd;
                pfds[nfds].events = POLLIN;
                idx_map[nfds] = i;
                nfds++;
            }
        }
        pthread_mutex_unlock(&g_filters_lock);

        if (nfds == 0) {
            usleep(200 * 1000);
            continue;
        }
        int r = poll(pfds, nfds, 1000);
        if (r <= 0)
            continue;

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
            id = g_filters[idx_map[i]].req_id;
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

    for (int i = 0; i < MAX_FILTERS; i++)
        g_filters[i].fd = -1;
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
        sleep(3600);
    return 0;
}
