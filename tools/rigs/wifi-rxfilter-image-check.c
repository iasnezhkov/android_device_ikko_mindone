#include <stdint.h>

typedef uint32_t u32;
typedef uint8_t u8;
static u8 command[16] __attribute__((aligned(4)));
static u8 raw[512] __attribute__((aligned(4)));
static u8 state[0x204] __attribute__((aligned(4)));
static u8 bss[4][64] __attribute__((aligned(4)));
static u8 station[4][16] __attribute__((aligned(4)));
static u8 statistics[4][0x314] __attribute__((aligned(4)));
static u8 addresses[4][128] __attribute__((aligned(4)));
static u8 descriptor[112] __attribute__((aligned(4)));
static u8 frame[256] __attribute__((aligned(4)));
static u8 event[48] __attribute__((aligned(4)));
static u32 result[8], trace[12], allocation_failure, configuration_failure;
static const u8 macs[12] = {1, 0, 0x5e, 0, 0, 0xfb, 0x33, 0x33, 0, 0, 0, 1};

static void put(u8* p, u32 value) { *(u32*)(void*)p = value; }
static u32 get(const u8* p) { return *(const u32*)(const void*)p; }
static void clear(u8* p, u32 size) { while (size--) *p++ = 0; }
static void copy(u8* to, const u8* from, u32 size) { while (size--) *to++ = *from++; }
static void be16(u8* p, u32 value) { p[0] = value >> 8; p[1] = value; }
static void le16(u8* p, u32 value) { p[0] = value; p[1] = value >> 8; }
static void check(u32 actual, u32 expected, u32 line) {
    ++result[0];
    if (actual != expected) {
        if (!result[1]) {
            result[2] = actual; result[3] = expected; result[4] = line;
        }
        ++result[1];
    }
}
#define CHECK(actual, expected) check((actual), (expected), __LINE__)

struct buffer { u32 type, address; uint16_t token, reserved; };
u32 sim_allocate(u32 size, struct buffer* output) {
    ++trace[0]; trace[1] = size;
    if (allocation_failure) return 0xc0000001u;
    output->type = 2; output->address = (u32)event; output->token = 0x753;
    return 0;
}
void sim_prepare(const u8* input, u32 size) {
    CHECK((u32)input, (u32)event); CHECK(size, trace[1]);
    trace[2] = trace[2] * 5 + 1;
}
u32 sim_channel(void) { trace[2] = trace[2] * 5 + 2; return 0x176; }
void sim_send(u32 zero, u32 channel, u32 reserved, u32 token) {
    CHECK(zero, 0); CHECK(channel, 0x176); CHECK(reserved, 0); CHECK(token, 0x753);
    trace[2] = trace[2] * 5 + 3; ++trace[3];
}
void sim_clear(void) { ++trace[4]; }
void sim_program(u32 slot, u32 reserved, const u8* mac) {
    CHECK(slot < 32, 1); CHECK(reserved, 0);
    CHECK((u32)mac, (u32)(state + 0x102 + 8 * slot)); ++trace[5];
}
u32 sim_configure(void) { ++trace[6]; return configuration_failure; }
void sim_assert_log(u32 file, u32 line) { CHECK(file, 0xf0392560); CHECK(line, 0x45d); ++trace[7]; }
void sim_assert(u32 zero, u32 reserved, u32 file, u32 line) {
    CHECK(zero, 0); CHECK(reserved, 0); CHECK(file, 0xf0392560); CHECK(line, 0x45d); ++trace[8];
}

static u32 call_command(void) { return ((u32 (*)(u8*))0xf032b614)(command); }
static u32 call_rx(u32 broadcast) { return ((u32 (*)(const u8*, u32))0xf032a398)(descriptor, broadcast); }
static void reset(u32 index) { ((void (*)(u32))0xf032aa64)(index); }
static u32 seek(u32 index, const u8* mac) { return ((u32 (*)(u32, const u8*))0xf032b564)(index, mac); }

static void initialize(void) {
    clear(state, sizeof(state)); clear((u8*)bss, sizeof(bss));
    clear((u8*)station, sizeof(station)); clear((u8*)statistics, sizeof(statistics));
    clear((u8*)addresses, sizeof(addresses)); clear(descriptor, sizeof(descriptor));
    clear((u8*)trace, sizeof(trace)); allocation_failure = configuration_failure = 0;
    put((u8*)0xf03adc2c, (u32)state);
    put((u8*)0xf03b37f4, (u32)sim_clear); put((u8*)0xf03b37f0, (u32)sim_program);
    put((u8*)0xf03b33c4, (u32)sim_send);
    u32 gp;
    __asm__("mov55 %0, $gp" : "=r"(gp));
    put((u8*)(gp - 0x22c00), (u32)sim_assert);
    put((u8*)(gp - 0x21cf0), (u32)sim_prepare);
    put((u8*)(gp - 0x217ac), (u32)sim_channel);
    for (u32 i = 0; i < 4; ++i) {
        put((u8*)(gp - 0x20b74 + i * 4), (u32)bss[i]);
        put((u8*)(0xf03c65c8 + i * 4), (u32)addresses[i]);
        bss[i][0] = i; bss[i][0x18] = 1;
        put(bss[i] + 0x38, (u32)statistics[i]); put(station[i] + 12, (u32)bss[i]);
        reset(i);
    }
    clear((u8*)0xf03b6a18, 128);
    *(u8*)0xf03add1e = 0;
    CHECK(((u32 (*)(void))0xf036b754)(), 0);
    put(command + 12, (u32)raw);
}

static void header(u32 index, u32 count, u32 total, u32 set) {
    clear(raw, sizeof(raw)); clear((u8*)trace, sizeof(trace));
    for (u32 i = 0; i < sizeof(event); ++i) event[i] = 0xca;
    le16(raw + 32, total); raw[36] = 0xc1; raw[38] = set; raw[39] = 0x5a;
    put(raw + 64, count); raw[68] = index;
}

static void set(u32 index, u32 a, u32 s, u32 t, u32 count) {
    header(index, count, count == 255 ? 40 : 40 + 6 * count, 1);
    raw[69] = a; raw[70] = s; raw[71] = t;
    for (u32 i = 0; i < count && i < 34; ++i) {
        copy(raw + 72 + 6 * i, macs, 6);
        if (i < 2) copy(raw + 72 + 6 * i, macs + i * 6, 6);
        else raw[77 + 6 * i] = i;
    }
    CHECK(call_command(), 0); CHECK(trace[4], 1); CHECK(trace[6], 1);
}

static void query(u32 index, u32 policy, u32 nonce) {
    header(index, 0, 44, 0); raw[69] = 'R'; raw[70] = 'X'; raw[71] = 1;
    put(raw + 72, nonce);
    CHECK(call_command(), 0); CHECK(trace[0], 1); CHECK(trace[1], 40);
    CHECK(trace[2], 38); CHECK(trace[3], 1); CHECK(get(event + 20), 0xe0000014);
    CHECK(get(event + 24), 0x5afd); CHECK(get(event + 28), 0x1c100 | 0xa0 | (index << 3) | policy);
    CHECK(get(event + 32), nonce);
    CHECK(get(event + 36), state[0xf4 + index] | (u32)state[0xf8 + index] << 8 | (u32)state[0xfc + index] << 16);
    CHECK(get(event + 40), 0xcacacaca); CHECK(trace[4], 0);
}

static void packet(u32 index, u32 family, u32 kind) {
    clear(frame, sizeof(frame)); clear(descriptor, sizeof(descriptor));
    copy(frame, macs + (family == 6 ? 6 : 0), 6);
    u8* ip = frame + 14;
    u32 size;
    if (family == 4) {
        be16(frame + 12, 0x800); ip[0] = 0x45; ip[9] = 17;
        ip[16] = 239; ip[17] = 1; ip[18] = 2; ip[19] = 3;
        be16(ip + 2, 28); be16(ip + 20, 1200); be16(ip + 22, 1201); be16(ip + 24, 8);
        if (kind == 1) { be16(ip + 20, 67); be16(ip + 22, 68); }
        if (kind == 2) ip[9] = 2;
        if (kind == 3) ip[6] = 0x20;
        if (kind == 4) { ip[16] = 224; ip[17] = 0; ip[18] = 0; ip[19] = 0xfb; }
        if (kind == 5) ip[16] = 192;
        size = 42;
    } else if (family == 6) {
        be16(frame + 12, 0x86dd); ip[0] = 0x60; ip[6] = 17;
        ip[24] = 255; ip[25] = 2; ip[39] = 1;
        be16(ip + 4, 8); be16(ip + 40, 1200); be16(ip + 42, 1201); be16(ip + 44, 8);
        if (kind == 1) { be16(ip + 40, 547); be16(ip + 42, 546); }
        if (kind == 2) { ip[6] = 58; ip[40] = 134; }
        if (kind == 3) ip[6] = 44;
        if (kind == 4) { ip[6] = 58; ip[40] = 135; }
        if (kind == 5) ip[24] = 0x20;
        size = 62;
    } else {
        be16(frame + 12, 0x806); size = 42;
    }
    put(descriptor + 0x24, (u32)frame); descriptor[0x28] = 2;
    le16(descriptor + 0x2a, size); put(descriptor + 0x2c, (u32)frame);
    put(descriptor + 0x30, (u32)ip); le16(descriptor + 0x34, size - 14);
    descriptor[0x3c] = 1; descriptor[0x48] = 1; descriptor[0x69] = index;
    put(descriptor + 0x64, (u32)station[index]);
}

static void receive(u32 index, u32 broadcast, u32 expected, u32 reason) {
    u32 low = get(state + 0x58), high = get(state + 0x5c);
    u32 count = get(statistics[index] + 0x2a8), old = get(statistics[index] + 0x310);
    u32 reasons[3];
    for (u32 i = 0; i < 3; ++i) reasons[i] = get((u8*)(0xf03b6a18 + 4 * (27 + i)));
    CHECK(call_rx(broadcast), expected);
    CHECK(get(state + 0x58), low + expected);
    CHECK(get(state + 0x5c), high + (expected && low == 0xffffffffu));
    CHECK(get(statistics[index] + 0x2a8), count + expected);
    CHECK(get(statistics[index] + 0x310), expected ? get(statistics[index] + 0x308) + 1 : old);
    for (u32 i = 0; i < 3; ++i)
        CHECK(get((u8*)(0xf03b6a18 + 4 * (27 + i))), reasons[i] + (expected && reason == i + 9));
}

static void lifecycle(void) {
    for (u32 index = 0; index < 4; ++index) {
        set(index, 0, 0, 0, 2); query(index, 0, 0x10203040 + index);
        CHECK(state[0xf4 + index], 0x53); CHECK(state[0xf8 + index], 0x42);
        for (u32 policy = 4; policy < 8; ++policy) {
            u32 mode = (policy + 4) << 4;
            set(index, mode | 1, mode | 2, 0x32, 2);
            CHECK(state[0xf4 + index], mode | 3); CHECK(state[0xf8 + index], mode | 3);
            query(index, policy, 0xa5a50000 | index << 8 | policy);
            for (u32 asleep = 0; asleep < 2; ++asleep) {
                *(u8*)0xf03add1e = asleep;
                for (u32 family = 4; family <= 6; family += 2)
                    for (u32 kind = 0; kind < 6; ++kind) {
                        packet(index, family, kind);
                        u32 filtered = family == 4 ? !(policy & 1) : !(policy & 2);
                        u32 ordinary = kind == 0 || (family == 4 && kind == 4);
                        receive(index, 0, asleep && filtered && ordinary, 10);
                    }
                packet(index, 0, 0); receive(index, 0, 0, 10);
            }
            packet(index, 4, 0); put(state + 0x58, 0xffffffff); put(state + 0x5c, 0x7654);
            put(statistics[index] + 0x308, 0x1738); receive(index, 0x101, 1, 9);
            receive(index, 0x100, !(policy & 1), 10);
        }
        set(index, 0x53, 0x42, 0x42, 2); query(index, 0, 0x76540000 | index);
        packet(index, 4, 4); receive(index, 0, 0, 10);
        frame[5] = 0xfc; receive(index, 0, 1, 10);
        packet(index, 6, 0); receive(index, 0, 0, 10);
    }
}

static void table_and_bitmap(void) {
    initialize();
    CHECK(seek(0, macs), 1); CHECK(seek(1, macs), 1);
    CHECK(state[0x100], 1); CHECK(state[0x101], 3);
    reset(0); CHECK(state[0x100], 1); CHECK(state[0x101], 2);
    reset(1); CHECK(state[0x100], 0); CHECK(state[0x101], 0);
    for (u32 index = 0; index < 4; ++index) {
        set(index, 0x80, 0x80, 0x42, 33);
        CHECK(state[0xf4 + index], 0x83); CHECK(state[0xf8 + index], 0x83); CHECK(state[0xfc + index], 0x53);
        query(index, 4, 0x12340000 + index);
        set(index, 0x53, 0x42, 0x42, 255);
        CHECK(state[0xf4 + index], 0x53); CHECK(state[0xf8 + index], 0x53); CHECK(state[0xfc + index], 0x53);
        set(index, 0xb0, 0xb0, 0x42, 255); query(index, 7, 0x56780000 + index);
    }
    addresses[0][0x20] = 1; addresses[0][0] = 192;
    CHECK(((u32 (*)(u32))0xf036bcf8)(0), 1);
    CHECK(((u32 (*)(u32))0xf036bcf8)(1), 0);
    header(0, 0, 40, 1); raw[0x42] = 0x72; raw[0x43] = 24;
    put(raw + 0x44, 1); put(raw + 0x4c, 2);
    CHECK(((u32 (*)(u8*))0xf036b6e0)(command), 0);
    CHECK(get((u8*)0xf03c6410), 1); CHECK(get((u8*)0xf03c6418), 2);
    *(u8*)0xf03add1e = 1;
    packet(0, 4, 0); CHECK(((u32 (*)(const u8*))0xf036b81c)(descriptor), 1); receive(0, 0, 1, 11);
    packet(0, 6, 0); receive(0, 0, 1, 11);
    packet(0, 4, 0); receive(0, 1, 1, 9);
    state[0xf8] = 0x11; receive(0, 0, 1, 10); state[0xf8] = 0xb3;
    CHECK(((u32 (*)(void))0xf036b754)(), 0); receive(0, 0, 0, 11);
}

static void failures(void) {
    initialize(); set(0, 0x83, 0x83, 0x42, 2);
    for (u32 length = 0; length < 40; ++length) {
        header(0, 0, length, 1); CHECK(call_command(), 0xc0000001); CHECK(trace[4], 0);
        CHECK(state[0xf4], 0x83);
    }
    header(0, 2, 46, 1); CHECK(call_command(), 0xc0000001); CHECK(trace[4], 0);
    header(0, 0xffffffff, 0xffff, 1); CHECK(call_command(), 0xc0000001); CHECK(trace[4], 0);
    header(4, 0, 40, 1); CHECK(call_command(), 0xc0000001); CHECK(trace[4], 0);
    header(0, 0, 40, 0); CHECK(call_command(), 0); CHECK(trace[0], 0);
    for (u32 mode = 0; mode < 5; ++mode) {
        header(mode == 2 ? 4 : 0, mode == 3 ? 1 : 0, mode == 0 ? 40 : 44, 0);
        raw[69] = 'R'; raw[70] = 'X'; raw[71] = mode == 1 ? 2 : 1;
        put(raw + 72, 0xface1234); allocation_failure = mode == 4;
        CHECK(call_command(), mode == 4 ? 0x103 : 0);
        CHECK(trace[3], mode == 4 ? 0 : 1);
        if (mode != 4) {
            CHECK(get(event + 28), 0x1c1fc); CHECK(get(event + 36), 0);
            CHECK(get(event + 32), mode == 0 ? 0 : 0xface1234);
        }
        allocation_failure = 0;
    }
    configuration_failure = 1; set(0, 0x83, 0x83, 0x42, 2);
    CHECK(trace[7], 1); CHECK(trace[8], 1); configuration_failure = 0;
    for (u32 status = 0; status < 512; ++status) {
        header(0, 0, 40, 0);
        CHECK(((u32 (*)(u8*, u32))0xf031c1c0)(command, status), 0);
        CHECK(trace[1], 32); CHECK(trace[2], 38); CHECK(trace[3], 1);
        CHECK(get(event + 20), 0xe000000c); CHECK(get(event + 24), 0x5afd);
        CHECK(get(event + 28), 0xc100 | (status & 255)); CHECK(get(event + 32), 0xcacacaca);
    }
    header(0, 0, 40, 0); allocation_failure = 1;
    CHECK(((u32 (*)(u8*, u32))0xf031c1c0)(command, 1), 0x103); CHECK(trace[3], 0);
    allocation_failure = 0; *(u8*)0xf03add1e = 1;
    for (u32 length = 0; length < 42; ++length) {
        packet(0, 4, 0); le16(descriptor + 0x2a, length); receive(0, 0, 0, 10);
    }
    packet(0, 4, 0); descriptor[0x49] = 1; receive(0, 0, 0, 10);
    for (u32 variant = 0; variant < 4; ++variant) {
        packet(0, 4, 0);
        if (variant == 0) put(descriptor + 0x24, 0);
        if (variant == 1) put(descriptor + 0x24, 0xfffffff0);
        if (variant == 2) put(descriptor + 0x24, (u32)frame + 1);
        if (variant == 3) { put(descriptor + 0x24, (u32)frame - 64); le16(descriptor + 0x2a, 63); }
        receive(0, 0, 0, 10);
    }
    for (u32 low = 1; low < 3; ++low) {
        state[0xf4] = 0x80 | low;
        packet(0, 4, 0); receive(0, 0, 0, 10);
        header(0, 0, 44, 0); raw[69] = 'R'; raw[70] = 'X'; raw[71] = 1;
        put(raw + 72, 0xdeadbeef);
        CHECK(call_command(), 0); CHECK(get(event + 28), 0x1c1fc);
        CHECK(get(event + 32), 0xdeadbeef); CHECK(get(event + 36), 0);
    }
}

int main(void) {
    u32 entry_gp, final_gp;
    __asm__ volatile("mov55 %0, $gp" : "=r"(entry_gp));
    initialize(); lifecycle(); table_and_bitmap(); failures();
    __asm__ volatile("mov55 %0, $gp" : "=r"(final_gp));
    CHECK(final_gp, entry_gp);
    result[5] = entry_gp;
    register u32 r0 __asm__("$r0") = 1;
    register void* r1 __asm__("$r1") = result;
    register u32 r2 __asm__("$r2") = sizeof(result);
    __asm__ volatile("syscall 5" : "+r"(r0) : "r"(r1), "r"(r2) : "memory");
    return result[1] != 0;
}
