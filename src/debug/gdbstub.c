/* GDB Remote Serial Protocol stub for guest debugging
 *
 * Copyright 2026 elfuse contributors
 * Copyright 2025 Moritz Angermann, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Implements a minimal GDB RSP server over TCP. When the guest hits a
 * hardware breakpoint/watchpoint or receives Ctrl+C from GDB, all vCPUs stop
 * (all-stop mode) and the stub services register/memory queries.
 *
 * Architecture:
 *   - A listener thread accepts one GDB connection at a time
 *   - When a stop event occurs, the stopped thread notifies the stub via
 *     gdb_stub_handle_stop() which blocks until GDB resumes
 *   - All other threads are stopped via hv_vcpus_exit() (all-stop)
 *   - The stub thread reads GDB packets and responds synchronously
 *   - Hardware debug registers (DBGBVR/DBGBCR, DBGWVR/DBGWCR) are programmed on
 *     all vCPUs via gdb_stub_sync_debug_regs()
 *
 * Thread safety: the stub's internal state (breakpoint table, stop state) is
 * protected by gdb_lock. The vCPU threads call handle_stop which blocks on
 * gdb_resume_cond under gdb_lock.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <libkern/OSCacheControl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <poll.h>

#include "hvutil.h"
#include "utils.h"

#include "debug/gdbstub-reg.h"
#include "debug/gdbstub-rsp.h"
#include "debug/gdbstub.h"
#include "debug/log.h"

#include "runtime/thread.h"
#include "syscall/abi.h" /* linux_user_pt_regs_t, LINUX_* errno */

/* Constants. */

#define GDB_PKT_BUF_SIZE ((size_t) 128 * 1024) /* Max packet size (128KiB) */
#define MAX_HW_BREAKPOINTS 16
#define MAX_HW_WATCHPOINTS 16

/* aarch64 DBGBCR enable bits for EL0 address match:
 *   [0]     E=1       (enable)
 *   [2:1]   PMC=0b10  (EL0 only)
 *   [8:5]   BAS=0b1111 (match all bytes in word)
 *   [23:20] BT=0b0000  (unlinked address match)
 * = 0x1E0 | 0x4 | 0x1 = 0x1E5
 */
#define DBGBCR_ENABLE_EL0 0x1E5

/* aarch64 DBGWCR enable bits for watchpoints:
 *   [0]     E     = 1 (enable)
 *   [2:1]   PAC   = 0b10 (EL0 only)
 *   [4:3]   LSC   = depends on type (01=load, 10=store, 11=both)
 *   [12:5]  BAS   = byte address select (depends on length)
 *   [28:24] MASK  = 0b00000 (no address mask)
 */
#define DBGWCR_BASE 0x5             /* E=1, PAC=0b10 */
#define DBGWCR_LSC_STORE (0x2 << 3) /* Write watchpoint */
#define DBGWCR_LSC_LOAD (0x1 << 3)  /* Read watchpoint */
#define DBGWCR_LSC_BOTH (0x3 << 3)  /* Access watchpoint */

/* HVF debug register ID lookup tables.
 * The HVF enum values use stride 8 per index:
 *   BVR0=0x8004, BCR0=0x8005, BVR1=0x800c, BCR1=0x800d, etc.
 *   WVR0=0x8006, WCR0=0x8007, WVR1=0x800e, WCR1=0x800f, etc.
 */
static const hv_sys_reg_t dbgbvr_regs[MAX_HW_BREAKPOINTS] = {
    HV_SYS_REG_DBGBVR0_EL1,  HV_SYS_REG_DBGBVR1_EL1,  HV_SYS_REG_DBGBVR2_EL1,
    HV_SYS_REG_DBGBVR3_EL1,  HV_SYS_REG_DBGBVR4_EL1,  HV_SYS_REG_DBGBVR5_EL1,
    HV_SYS_REG_DBGBVR6_EL1,  HV_SYS_REG_DBGBVR7_EL1,  HV_SYS_REG_DBGBVR8_EL1,
    HV_SYS_REG_DBGBVR9_EL1,  HV_SYS_REG_DBGBVR10_EL1, HV_SYS_REG_DBGBVR11_EL1,
    HV_SYS_REG_DBGBVR12_EL1, HV_SYS_REG_DBGBVR13_EL1, HV_SYS_REG_DBGBVR14_EL1,
    HV_SYS_REG_DBGBVR15_EL1,
};
static const hv_sys_reg_t dbgbcr_regs[MAX_HW_BREAKPOINTS] = {
    HV_SYS_REG_DBGBCR0_EL1,  HV_SYS_REG_DBGBCR1_EL1,  HV_SYS_REG_DBGBCR2_EL1,
    HV_SYS_REG_DBGBCR3_EL1,  HV_SYS_REG_DBGBCR4_EL1,  HV_SYS_REG_DBGBCR5_EL1,
    HV_SYS_REG_DBGBCR6_EL1,  HV_SYS_REG_DBGBCR7_EL1,  HV_SYS_REG_DBGBCR8_EL1,
    HV_SYS_REG_DBGBCR9_EL1,  HV_SYS_REG_DBGBCR10_EL1, HV_SYS_REG_DBGBCR11_EL1,
    HV_SYS_REG_DBGBCR12_EL1, HV_SYS_REG_DBGBCR13_EL1, HV_SYS_REG_DBGBCR14_EL1,
    HV_SYS_REG_DBGBCR15_EL1,
};
static const hv_sys_reg_t dbgwvr_regs[MAX_HW_WATCHPOINTS] = {
    HV_SYS_REG_DBGWVR0_EL1,  HV_SYS_REG_DBGWVR1_EL1,  HV_SYS_REG_DBGWVR2_EL1,
    HV_SYS_REG_DBGWVR3_EL1,  HV_SYS_REG_DBGWVR4_EL1,  HV_SYS_REG_DBGWVR5_EL1,
    HV_SYS_REG_DBGWVR6_EL1,  HV_SYS_REG_DBGWVR7_EL1,  HV_SYS_REG_DBGWVR8_EL1,
    HV_SYS_REG_DBGWVR9_EL1,  HV_SYS_REG_DBGWVR10_EL1, HV_SYS_REG_DBGWVR11_EL1,
    HV_SYS_REG_DBGWVR12_EL1, HV_SYS_REG_DBGWVR13_EL1, HV_SYS_REG_DBGWVR14_EL1,
    HV_SYS_REG_DBGWVR15_EL1,
};
static const hv_sys_reg_t dbgwcr_regs[MAX_HW_WATCHPOINTS] = {
    HV_SYS_REG_DBGWCR0_EL1,  HV_SYS_REG_DBGWCR1_EL1,  HV_SYS_REG_DBGWCR2_EL1,
    HV_SYS_REG_DBGWCR3_EL1,  HV_SYS_REG_DBGWCR4_EL1,  HV_SYS_REG_DBGWCR5_EL1,
    HV_SYS_REG_DBGWCR6_EL1,  HV_SYS_REG_DBGWCR7_EL1,  HV_SYS_REG_DBGWCR8_EL1,
    HV_SYS_REG_DBGWCR9_EL1,  HV_SYS_REG_DBGWCR10_EL1, HV_SYS_REG_DBGWCR11_EL1,
    HV_SYS_REG_DBGWCR12_EL1, HV_SYS_REG_DBGWCR13_EL1, HV_SYS_REG_DBGWCR14_EL1,
    HV_SYS_REG_DBGWCR15_EL1,
};

/* Internal state. */

/* Hardware breakpoint table entry */
typedef struct {
    uint64_t addr; /* Virtual address */
    bool used;     /* Slot is active */
    bool temp;     /* Temporary breakpoint (for single-step) */
} hw_bp_t;

/* Hardware watchpoint table entry */
typedef struct {
    uint64_t addr; /* Virtual address */
    uint64_t len;  /* Byte length (1, 2, 4, or 8) */
    int type;      /* 2=write, 3=read, 4=access (matches Z packet type) */
    bool used;     /* Slot is active */
} hw_wp_t;

/* GDB stub global state, protected by gdb_lock */
static struct {
    int initialized;
    int listen_fd;              /* TCP listener socket */
    int client_fd;              /* Connected GDB client (-1 if none) */
    gdb_rsp_ctx_t *rsp_ctx;     /* Active per-connection transport state */
    guest_t *guest;             /* Guest memory context */
    pthread_t listener_thread;  /* Accepts connections + processes packets */
    pthread_mutex_t lock;       /* Protects all mutable state */
    pthread_cond_t stop_cond;   /* Signaled when a thread stops */
    pthread_cond_t resume_cond; /* Signaled when GDB resumes threads */

    /* Stop state */
    int all_stopped;    /* All threads are stopped */
    int64_t stop_tid;   /* TID of thread that triggered the stop */
    int stop_reason;    /* GDB_STOP_* value */
    uint64_t stop_addr; /* Address associated with stop (bp/wp addr) */
    int resume_action;  /* 0=continue, 1=step (per stop_tid) */
    int stop_requested; /* GDB sent Ctrl+C or bp hit */

    /* Breakpoints and watchpoints */
    hw_bp_t breakpoints[MAX_HW_BREAKPOINTS];
    hw_wp_t watchpoints[MAX_HW_WATCHPOINTS];

    /* Thread tracking (which thread GDB is focused on) */
    int64_t current_g_tid; /* Thread for 'g'/'G' register ops */
    int64_t current_c_tid; /* Thread for 'c'/'s' continue/step */
} gdb = {
    .initialized = 0,
    .listen_fd = -1,
    .client_fd = -1,
    .rsp_ctx = NULL,
    .lock = PTHREAD_MUTEX_INITIALIZER,
};

/* Send an unescaped RSP response body. */
static int rsp_reply(const char *data)
{
    if (gdb.client_fd < 0)
        return -1;
    return gdb_rsp_send(gdb.client_fd, data, strlen(data));
}

/* Send an empty response (unsupported packet). */
static int rsp_reply_empty(void)
{
    return rsp_reply("");
}

/* Send an OK response. */
static int rsp_reply_ok(void)
{
    return rsp_reply("OK");
}

/* Send an error response (Enn). */
static int rsp_reply_error(int errnum)
{
    char buf[4];
    snprintf(buf, sizeof(buf), "E%02x", errnum & 0xFF);
    return rsp_reply(buf);
}

/* Read one RSP packet from the client. Strips $...# framing and
 * verifies checksum. Sends + acknowledgment on success.
 * Returns packet length, 0 on EOF, -1 on error.
 * Also handles bare 0x03 (Ctrl+C) by returning "\x03" as a 1-byte packet.
 *
 * Uses a static read buffer to batch socket reads instead of reading
 * one byte at a time. Packets exceeding bufsz are rejected with E00.
 */
/* Breakpoint / watchpoint management. */

/* BAS (Byte Address Select) for a watchpoint of given length aligned
 * to the watch address modulo 8.
 */
static uint32_t wp_bas_for_len(uint64_t addr, uint64_t len)
{
    /* Watchpoint address must be doubleword-aligned in DBGWVR;
     * BAS selects which bytes within the doubleword to watch.
     */
    unsigned offset = (unsigned) (addr & 7);
    uint32_t mask = 0;
    for (unsigned i = 0; i < (unsigned) len && (offset + i) < 8; i++)
        mask |= (1U << (offset + i));
    return mask << 5; /* BAS is bits [12:5] */
}

static int bp_insert(uint64_t addr)
{
    for (int i = 0; i < MAX_HW_BREAKPOINTS; i++) {
        if (!gdb.breakpoints[i].used) {
            gdb.breakpoints[i].addr = addr;
            gdb.breakpoints[i].used = true;
            gdb.breakpoints[i].temp = false;
            return 0;
        }
    }
    return -1; /* No free slots */
}

static int bp_remove(uint64_t addr)
{
    for (int i = 0; i < MAX_HW_BREAKPOINTS; i++) {
        if (gdb.breakpoints[i].used && gdb.breakpoints[i].addr == addr) {
            gdb.breakpoints[i].used = false;
            return 0;
        }
    }
    return -1;
}

static int wp_insert(uint64_t addr, uint64_t len, int type)
{
    for (int i = 0; i < MAX_HW_WATCHPOINTS; i++) {
        if (!gdb.watchpoints[i].used) {
            gdb.watchpoints[i].addr = addr;
            gdb.watchpoints[i].len = len;
            gdb.watchpoints[i].type = type;
            gdb.watchpoints[i].used = true;
            return 0;
        }
    }
    return -1;
}

static int wp_remove(uint64_t addr, uint64_t len, int type)
{
    for (int i = 0; i < MAX_HW_WATCHPOINTS; i++) {
        if (gdb.watchpoints[i].used && gdb.watchpoints[i].addr == addr &&
            gdb.watchpoints[i].len == len && gdb.watchpoints[i].type == type) {
            gdb.watchpoints[i].used = false;
            return 0;
        }
    }
    return -1;
}

/* Remove all temporary breakpoints (used after single-step completes). */
static void bp_remove_temps(void)
{
    for (int i = 0; i < MAX_HW_BREAKPOINTS; i++) {
        if (gdb.breakpoints[i].used && gdb.breakpoints[i].temp)
            gdb.breakpoints[i].used = false;
    }
}

/* Debug register programming. */

void gdb_stub_sync_debug_regs(hv_vcpu_t vcpu)
{
    if (!gdb.initialized)
        return;

    /* Enable debug exceptions to trap to EL2 (host) */
    HV_CHECK(hv_vcpu_set_trap_debug_exceptions(vcpu, true));

    /* MDSCR_EL1: enable monitor debug (MDE, bit 15).
     * MDE is required for hardware breakpoint/watchpoint exceptions
     * to fire at EL0.
     */
    uint64_t mdscr;
    HV_CHECK(hv_vcpu_get_sys_reg(vcpu, HV_SYS_REG_MDSCR_EL1, &mdscr));
    mdscr |= (1ULL << 15); /* MDE */
    HV_CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_MDSCR_EL1, mdscr));

    /* Program hardware breakpoints using lookup tables */
    for (int i = 0; i < MAX_HW_BREAKPOINTS; i++) {
        if (gdb.breakpoints[i].used) {
            hv_return_t r1 = hv_vcpu_set_sys_reg(vcpu, dbgbvr_regs[i],
                                                 gdb.breakpoints[i].addr);
            hv_return_t r2 =
                hv_vcpu_set_sys_reg(vcpu, dbgbcr_regs[i], DBGBCR_ENABLE_EL0);
            if (r1 != HV_SUCCESS || r2 != HV_SUCCESS) {
                /* Hardware may not support this many breakpoints; skip */
                gdb.breakpoints[i].used = false;
            }
        } else {
            /* Disable this breakpoint slot (ignore errors for
             * unsupported hardware indices)
             */
            (void) hv_vcpu_set_sys_reg(vcpu, dbgbcr_regs[i], 0);
        }
    }

    /* Program hardware watchpoints */
    for (int i = 0; i < MAX_HW_WATCHPOINTS; i++) {
        if (gdb.watchpoints[i].used) {
            /* Align address to doubleword boundary for DBGWVR */
            uint64_t aligned = gdb.watchpoints[i].addr & ~7ULL;
            hv_return_t r1 = hv_vcpu_set_sys_reg(vcpu, dbgwvr_regs[i], aligned);

            uint32_t wcr_val = DBGWCR_BASE;
            switch (gdb.watchpoints[i].type) {
            case 2:
                wcr_val |= DBGWCR_LSC_STORE;
                break;
            case 3:
                wcr_val |= DBGWCR_LSC_LOAD;
                break;
            case 4:
                wcr_val |= DBGWCR_LSC_BOTH;
                break;
            default:
                wcr_val |= DBGWCR_LSC_BOTH;
                break;
            }
            wcr_val |=
                wp_bas_for_len(gdb.watchpoints[i].addr, gdb.watchpoints[i].len);
            hv_return_t r2 =
                hv_vcpu_set_sys_reg(vcpu, dbgwcr_regs[i], (uint64_t) wcr_val);
            if (r1 != HV_SUCCESS || r2 != HV_SUCCESS) {
                gdb.watchpoints[i].used = false;
            }
        } else {
            (void) hv_vcpu_set_sys_reg(vcpu, dbgwcr_regs[i], 0);
        }
    }
}

/* Thread helpers. */

/* Find the thread entry for a given guest TID. Returns NULL if not found.
 * This returns thread_entry_t * rather than hv_vcpu_t because a valid HVF
 * vCPU handle can be 0 for the first created vCPU.
 */
static thread_entry_t *find_thread_for_tid(int64_t tid)
{
    return thread_find(tid);
}

static void gdb_invalidate_written_code(uint64_t gva, size_t len)
{
    size_t flushed = 0;

    while (flushed < len) {
        uint64_t avail = 0;
        void *host_ptr = guest_ptr_bound(gdb.guest, gva + flushed, &avail,
                                         MEM_PERM_R, len - flushed);
        if (!host_ptr || avail == 0)
            break;
        sys_icache_invalidate(host_ptr, (size_t) avail);
        flushed += (size_t) avail;
    }
}

/* Build the T05 stop reply with thread info. */
static void build_stop_reply(char *buf, size_t bufsz)
{
    int64_t tid = gdb.stop_tid;
    const thread_entry_t *t;
    uint64_t sp = 0;
    uint64_t pc = 0;
    char sp_hex[17];
    char pc_hex[17];

    if (tid <= 0)
        tid = 1;
    t = find_thread_for_tid(tid);
    if (t) {
        memcpy(&sp, t->gdb_reg_snapshot + GDBREGOFFSP, sizeof(sp));
        memcpy(&pc, t->gdb_reg_snapshot + GDBREGOFFPC, sizeof(pc));
    }
    gdb_hex_encode(sp_hex, (const uint8_t *) &sp, sizeof(sp));
    gdb_hex_encode(pc_hex, (const uint8_t *) &pc, sizeof(pc));

    switch (gdb.stop_reason) {
    case GDB_STOP_WATCHPOINT:
        snprintf(buf, bufsz, "T05watch:%llx;thread:%llx;1f:%s;20:%s;",
                 (unsigned long long) gdb.stop_addr, (unsigned long long) tid,
                 sp_hex, pc_hex);
        break;
    case GDB_STOP_SIGNAL:
        snprintf(buf, bufsz, "T05thread:%llx;1f:%s;20:%s;",
                 (unsigned long long) tid, sp_hex, pc_hex);
        break;
    default:
        snprintf(buf, bufsz, "T05thread:%llx;1f:%s;20:%s;",
                 (unsigned long long) tid, sp_hex, pc_hex);
        break;
    }
}

/* Packet handlers. */

/* Return the snapshot offset and byte size for a GDB register number.
 * Returns 0 on success, -1 for invalid register. The two regular families
 * (X0-X30, V0-V31) use closed-form math; the five irregular slots live in a
 * sparse table so each one is a data entry instead of a switch case.
 */
/* Handle 'g': read all registers from snapshot for current_g_tid. */
static void handle_read_regs(void)
{
    const thread_entry_t *t = find_thread_for_tid(gdb.current_g_tid);
    if (!t) {
        rsp_reply_error(1);
        return;
    }

    char hex_buf[GDBREGSNAPSIZE * 2 + 1];
    gdb_hex_encode(hex_buf, t->gdb_reg_snapshot, GDBREGSNAPSIZE);
    rsp_reply(hex_buf);
}

/* Handle 'G': write all registers to snapshot for current_g_tid. */
static void handle_write_regs(const char *pkt)
{
    thread_entry_t *t = find_thread_for_tid(gdb.current_g_tid);
    if (!t) {
        rsp_reply_error(1);
        return;
    }

    size_t hex_len = strlen(pkt), decode_bytes = hex_len / 2;
    if (decode_bytes > GDBREGSNAPSIZE)
        decode_bytes = GDBREGSNAPSIZE;

    /* Pad with zeros if GDB sends fewer registers */
    if (decode_bytes < GDBREGSNAPSIZE)
        memset(t->gdb_reg_snapshot + decode_bytes, 0,
               GDBREGSNAPSIZE - decode_bytes);

    if (gdb_hex_decode(t->gdb_reg_snapshot, pkt, decode_bytes) < 0) {
        rsp_reply_error(1);
        return;
    }
    t->gdb_regs_dirty = true;
    rsp_reply_ok();
}

/* Handle 'p': read single register from snapshot. */
static void handle_read_reg(const char *pkt)
{
    const char *p = pkt;
    uint64_t regnum = gdb_parse_hex(&p);

    const thread_entry_t *t = find_thread_for_tid(gdb.current_g_tid);
    if (!t) {
        rsp_reply_error(1);
        return;
    }

    int off, nbytes;
    if (gdb_reg_offset(regnum, &off, &nbytes) < 0) {
        rsp_reply_error(14); /* EFAULT, invalid register */
        return;
    }

    char hex_buf[64];
    gdb_hex_encode(hex_buf, t->gdb_reg_snapshot + off, (size_t) nbytes);
    rsp_reply(hex_buf);
}

/* Handle 'P': write single register to snapshot. */
static void handle_write_reg(const char *pkt)
{
    const char *p = pkt;
    uint64_t regnum = gdb_parse_hex(&p);
    if (*p == '=')
        p++;

    thread_entry_t *t = find_thread_for_tid(gdb.current_g_tid);
    if (!t) {
        rsp_reply_error(1);
        return;
    }

    int off, nbytes;
    if (gdb_reg_offset(regnum, &off, &nbytes) < 0) {
        rsp_reply_error(14);
        return;
    }

    uint8_t raw[16];
    size_t hex_len = strlen(p), decode_len = hex_len / 2;
    if (decode_len > (size_t) nbytes)
        decode_len = (size_t) nbytes;
    if (gdb_hex_decode(raw, p, decode_len) < 0) {
        rsp_reply_error(1);
        return;
    }

    memcpy(t->gdb_reg_snapshot + off, raw, decode_len);
    if (decode_len < (size_t) nbytes) {
        memset(t->gdb_reg_snapshot + off + decode_len, 0,
               (size_t) nbytes - decode_len);
    }
    t->gdb_regs_dirty = true;
    rsp_reply_ok();
}

/* Handle 'm': read memory (mADDR,LENGTH). */
static void handle_read_mem(const char *pkt)
{
    const char *p = pkt;
    uint64_t addr = gdb_parse_hex(&p);
    if (*p == ',')
        p++;
    uint64_t len = gdb_parse_hex(&p);

    if (len == 0) {
        rsp_reply_ok();
        return;
    }
    if (len > GDB_PKT_BUF_SIZE / 2 - 16) {
        rsp_reply_error(14);
        return;
    }

    /* Read from guest memory */
    uint8_t *tmp = malloc(len);
    if (!tmp) {
        rsp_reply_error(12); /* ENOMEM */
        return;
    }

    if (guest_read(gdb.guest, addr, tmp, len) < 0) {
        free(tmp);
        rsp_reply_error(14); /* EFAULT */
        return;
    }

    char *hex = malloc(len * 2 + 1);
    if (!hex) {
        free(tmp);
        rsp_reply_error(12);
        return;
    }
    gdb_hex_encode(hex, tmp, len);
    rsp_reply(hex);
    free(hex);
    free(tmp);
}

/* Handle 'M': write memory (MADDR,LENGTH:XX...). */
static void handle_write_mem(const char *pkt)
{
    const char *p = pkt;
    uint64_t addr = gdb_parse_hex(&p);
    if (*p == ',')
        p++;
    uint64_t len = gdb_parse_hex(&p);
    if (*p == ':')
        p++;

    if (len == 0) {
        rsp_reply_ok();
        return;
    }
    if (len > GDB_PKT_BUF_SIZE / 2 - 16) {
        rsp_reply_error(14);
        return;
    }

    uint8_t *tmp = malloc(len);
    if (!tmp) {
        rsp_reply_error(12);
        return;
    }

    if (gdb_hex_decode(tmp, p, len) < 0) {
        free(tmp);
        rsp_reply_error(1);
        return;
    }

    if (guest_write(gdb.guest, addr, tmp, len) < 0) {
        free(tmp);
        rsp_reply_error(14);
        return;
    }
    gdb_invalidate_written_code(addr, len);

    free(tmp);
    rsp_reply_ok();
}

/* Handle 'H': set thread (HOP,THREADID). */
static void handle_set_thread(const char *pkt)
{
    char op = pkt[0];
    const char *p = pkt + 1;
    int64_t tid;
    bool negative = false;
    if (*p == '-') {
        negative = true;
        p++;
    }
    tid = (int64_t) gdb_parse_hex(&p);
    if (negative)
        tid = -tid;

    /* tid 0 means "any thread"; pick the stop thread or first active */
    if (tid == 0 || tid == -1)
        tid = gdb.stop_tid > 0 ? gdb.stop_tid : 1;

    switch (op) {
    case 'g':
        gdb.current_g_tid = tid;
        break;
    case 'c':
        gdb.current_c_tid = tid;
        break;
    default:
        break;
    }
    rsp_reply_ok();
}

/* Handle 'T': thread alive check (TTHREADID). */
static void handle_thread_alive(const char *pkt)
{
    const char *p = pkt;
    int64_t tid = (int64_t) gdb_parse_hex(&p);
    if (thread_tid_alive(tid))
        rsp_reply_ok();
    else
        rsp_reply_error(1);
}

/* Handle 'Z'/'z': insert/remove breakpoint or watchpoint.
 * Format: Z/z TYPE,ADDR,KIND
 */
static void handle_breakpoint(const char *pkt, int insert)
{
    const char *p = pkt;
    uint64_t type = gdb_parse_hex(&p);
    if (*p == ',')
        p++;
    uint64_t addr = gdb_parse_hex(&p);
    if (*p == ',')
        p++;
    uint64_t kind = gdb_parse_hex(&p);

    int rc;
    switch (type) {
    case 0:
        /* Software breakpoint: for MVP, treat as hardware breakpoint
         * since hardware support is available and this avoids I-cache issues
         */
        /* fallthrough */
    case 1:
        /* Hardware breakpoint */
        if (insert)
            rc = bp_insert(addr);
        else
            rc = bp_remove(addr);
        break;
    case 2:
    case 3:
    case 4:
        /* Watchpoint: 2=write, 3=read, 4=access */
        if (kind == 0)
            kind = 4; /* Default 4-byte watch */
        if (insert)
            rc = wp_insert(addr, kind, (int) type);
        else
            rc = wp_remove(addr, kind, (int) type);
        break;
    default:
        rsp_reply_empty(); /* Unsupported type */
        return;
    }

    if (rc < 0)
        rsp_reply_error(28); /* ENOSPC, no free HW slots */
    else
        rsp_reply_ok();
}

/* Handle qSupported: feature negotiation. */
static void handle_q_supported(const char *pkt)
{
    (void) pkt;
    /* Advertise features:
     * - PacketSize: max packet the stub accepts
     * - hwbreak+: the GDB stub supports hardware breakpoints
     * - swbreak+: the GDB stub handles Z0 as hardware breakpoints
     * - qXfer:features:read+: the GDB stub serves target.xml
     * - QStartNoAckMode+: suppress per-packet ACK/NACK traffic on TCP
     * - multiprocess-: no multiprocess support
     * - vContSupported+: the GDB stub supports vCont
     */
    char reply[256];
    snprintf(reply, sizeof(reply),
             "PacketSize=%zx;hwbreak+;swbreak+;"
             "qXfer:features:read+;QStartNoAckMode+;"
             "vContSupported+",
             GDB_PKT_BUF_SIZE);
    rsp_reply(reply);
}

static void handle_start_noack_mode(void)
{
    if (rsp_reply_ok() == 0 && gdb.rsp_ctx)
        gdb_rsp_set_noack(gdb.rsp_ctx, true);
}

/* Handle qXfer:features:read:target.xml:OFFSET,LENGTH */
static void handle_xfer_features(const char *pkt)
{
    if (gdb_reply_target_xml(gdb.client_fd, pkt) < 0)
        rsp_reply_error(1);
}

/* Callback for thread_for_each to collect TIDs into an array. */
typedef struct {
    int64_t tids[MAX_THREADS];
    int count;
} tid_collector_t;

static void collect_tids_cb(thread_entry_t *t, void *c)
{
    tid_collector_t *cc = c;
    if (cc->count < MAX_THREADS)
        cc->tids[cc->count++] = t->guest_tid;
}

/* Handle qfThreadInfo / qsThreadInfo: list guest threads. */
static void handle_thread_info(int first)
{
    if (!first) {
        rsp_reply("l"); /* End of list */
        return;
    }

    /* Build comma-separated hex thread ID list.
     * Worst case: MAX_THREADS(64) × 17 chars (16 hex + comma) + 1 prefix + NUL.
     */
    char reply[2048];
    int pos = 0;
    reply[pos++] = 'm';

    tid_collector_t ctx = {.count = 0};
    thread_for_each(collect_tids_cb, &ctx);

    for (int i = 0; i < ctx.count; i++) {
        if (i > 0 && pos < (int) sizeof(reply) - 20)
            reply[pos++] = ',';
        pos += snprintf(reply + pos, sizeof(reply) - (size_t) pos, "%llx",
                        (unsigned long long) ctx.tids[i]);
    }
    reply[pos] = '\0';
    rsp_reply(reply);
}

/* Handle vCont: continue/step with per-thread control.
 * vCont;ACTION[:THREADID][;ACTION[:THREADID]]...
 */
static void handle_vcont(const char *pkt)
{
    if (*pkt == '?') {
        /* vCont? reports supported actions */
        rsp_reply("vCont;c;C;s;S");
        return;
    }

    /* Parse actions. For MVP the GDB stub supports c (continue) and s (step).
     * In all-stop mode, all threads are resumed together.
     */
    bool do_step = false;
    int64_t step_tid = -1; /* -1 = all threads */
    const char *p = pkt;

    while (*p == ';') {
        p++;
        char action = *p++;

        int64_t tid = -1;
        if (*p == ':') {
            p++;
            tid = (int64_t) gdb_parse_hex(&p);
        }

        switch (action) {
        case 's':
        case 'S':
            do_step = true;
            step_tid = (tid > 0) ? tid : gdb.stop_tid;
            break;
        case 'c':
        case 'C':
            /* Continue (default action) */
            break;
        default:
            break;
        }
    }

    /* Set up single-step by placing a temporary hardware breakpoint at PC+4.
     * This covers straight-line instructions; branch-accurate stepping would
     * require an instruction decoder. GDB can still set explicit breakpoints
     * at branch targets when it needs that precision.
     */
    if (do_step && step_tid > 0) {
        const thread_entry_t *step_t = find_thread_for_tid(step_tid);
        if (step_t) {
            uint64_t pc;
            memcpy(&pc, step_t->gdb_reg_snapshot + GDBREGOFFPC, 8);
            /* Set temporary breakpoint at next instruction */
            for (int i = 0; i < MAX_HW_BREAKPOINTS; i++) {
                if (!gdb.breakpoints[i].used) {
                    gdb.breakpoints[i].addr = pc + 4;
                    gdb.breakpoints[i].used = true;
                    gdb.breakpoints[i].temp = true;
                    break;
                }
            }
        }
    }

    gdb.resume_action = do_step ? 1 : 0;

    /* Signal all stopped threads to resume */
    pthread_mutex_lock(&gdb.lock);
    gdb.all_stopped = 0;
    gdb.stop_requested = 0;
    pthread_cond_broadcast(&gdb.resume_cond);
    pthread_mutex_unlock(&gdb.lock);
}

/* Handle 'c': continue execution. */
static void handle_continue(const char *pkt)
{
    /* Optional address argument: cADDR. Writes to snapshot (applied on resume)
     */
    if (pkt[0] != '\0') {
        const char *p = pkt;
        uint64_t addr = gdb_parse_hex(&p);
        thread_entry_t *t = find_thread_for_tid(gdb.current_c_tid);
        if (t) {
            memcpy(t->gdb_reg_snapshot + GDBREGOFFPC, &addr, 8);
            t->gdb_regs_dirty = true;
        }
    }

    gdb.resume_action = 0;
    pthread_mutex_lock(&gdb.lock);
    gdb.all_stopped = 0;
    gdb.stop_requested = 0;
    pthread_cond_broadcast(&gdb.resume_cond);
    pthread_mutex_unlock(&gdb.lock);
}

/* Handle 's': single-step. */
static void handle_step(const char *pkt)
{
    /* Optional address argument: writes to snapshot (applied on resume) */
    int64_t tid = gdb.current_c_tid;
    thread_entry_t *t = find_thread_for_tid(tid);
    if (pkt[0] != '\0' && t) {
        const char *p = pkt;
        uint64_t addr = gdb_parse_hex(&p);
        memcpy(t->gdb_reg_snapshot + GDBREGOFFPC, &addr, 8);
        t->gdb_regs_dirty = true;
    }

    /* Set temporary breakpoint at PC+4 (read from snapshot) */
    if (t) {
        uint64_t pc;
        memcpy(&pc, t->gdb_reg_snapshot + GDBREGOFFPC, 8);
        for (int i = 0; i < MAX_HW_BREAKPOINTS; i++) {
            if (!gdb.breakpoints[i].used) {
                gdb.breakpoints[i].addr = pc + 4;
                gdb.breakpoints[i].used = true;
                gdb.breakpoints[i].temp = true;
                break;
            }
        }
    }

    gdb.resume_action = 1;
    pthread_mutex_lock(&gdb.lock);
    gdb.all_stopped = 0;
    gdb.stop_requested = 0;
    pthread_cond_broadcast(&gdb.resume_cond);
    pthread_mutex_unlock(&gdb.lock);
}

/* Handle Ctrl+C (0x03): interrupt all threads. */
static void handle_interrupt(void)
{
    pthread_mutex_lock(&gdb.lock);
    gdb.stop_requested = 1;
    pthread_mutex_unlock(&gdb.lock);

    /* Force all vCPUs out of hv_vcpu_run */
    thread_interrupt_all();
}

/* Handle 'D': detach. Disable all breakpoints and continue. */
static void handle_detach(void)
{
    /* Clear all breakpoints and watchpoints */
    memset(gdb.breakpoints, 0, sizeof(gdb.breakpoints));
    memset(gdb.watchpoints, 0, sizeof(gdb.watchpoints));

    rsp_reply_ok();

    /* Resume all threads */
    pthread_mutex_lock(&gdb.lock);
    gdb.all_stopped = 0;
    gdb.stop_requested = 0;
    pthread_cond_broadcast(&gdb.resume_cond);
    pthread_mutex_unlock(&gdb.lock);

    /* Close client connection */
    close(gdb.client_fd);
    gdb.client_fd = -1;
}

/* Handle 'k': kill. Terminate the guest. */
static void handle_kill(void)
{
    /* Close connection and exit process */
    if (gdb.client_fd >= 0) {
        close(gdb.client_fd);
        gdb.client_fd = -1;
    }
    log_warn("GDB kill request, exiting");
    exit(0);
}

/* Main packet dispatch. */

static void handle_packet(const char *pkt, int pkt_len)
{
    if (pkt_len == 0)
        return;

    /* Ctrl+C */
    if (pkt[0] == 0x03) {
        handle_interrupt();
        return;
    }

    switch (pkt[0]) {
    case '?':
        /* Stop reason query */
        {
            char reply[128];
            build_stop_reply(reply, sizeof(reply));
            rsp_reply(reply);
        }
        break;

    case 'g':
        handle_read_regs();
        break;

    case 'G':
        handle_write_regs(pkt + 1);
        break;

    case 'p':
        handle_read_reg(pkt + 1);
        break;

    case 'P':
        handle_write_reg(pkt + 1);
        break;

    case 'm':
        handle_read_mem(pkt + 1);
        break;

    case 'M':
        handle_write_mem(pkt + 1);
        break;

    case 'c':
        handle_continue(pkt + 1);
        break;

    case 's':
        handle_step(pkt + 1);
        break;

    case 'H':
        handle_set_thread(pkt + 1);
        break;

    case 'T':
        handle_thread_alive(pkt + 1);
        break;

    case 'Z':
        handle_breakpoint(pkt + 1, 1);
        break;

    case 'z':
        handle_breakpoint(pkt + 1, 0);
        break;

    case 'D':
        handle_detach();
        break;

    case 'k':
        handle_kill();
        break;

    case 'v':
        if (!strncmp(pkt, "vCont", 5)) {
            handle_vcont(pkt + 5);
        } else if (!strncmp(pkt, "vMustReplyEmpty", 15)) {
            rsp_reply_empty();
        } else {
            rsp_reply_empty();
        }
        break;

    case 'q':
        if (!strncmp(pkt, "qSupported", 10)) {
            handle_q_supported(pkt + 10);
        } else if (!strcmp(pkt, "qfThreadInfo")) {
            handle_thread_info(1);
        } else if (!strcmp(pkt, "qsThreadInfo")) {
            handle_thread_info(0);
        } else if (!strncmp(pkt, "qXfer:features:read:", 20)) {
            handle_xfer_features(pkt + 20);
        } else if (!strcmp(pkt, "qAttached")) {
            rsp_reply("1"); /* Attached to existing process */
        } else if (!strncmp(pkt, "qC", 2)) {
            /* Current thread */
            char reply[32];
            snprintf(
                reply, sizeof(reply), "QC%llx",
                (unsigned long long) (gdb.stop_tid > 0 ? gdb.stop_tid : 1));
            rsp_reply(reply);
        } else if (!strcmp(pkt, "qOffsets")) {
            rsp_reply("Text=0;Data=0;Bss=0");
        } else if (!strcmp(pkt, "qSymbol::")) {
            rsp_reply_ok();
        } else {
            rsp_reply_empty();
        }
        break;

    case 'Q':
        if (!strcmp(pkt, "QStartNoAckMode")) {
            handle_start_noack_mode();
        } else {
            /* Set commands, mostly unsupported */
            rsp_reply_empty();
        }
        break;

    default:
        rsp_reply_empty();
        break;
    }
}

/* GDB client session. */

/* Main loop for servicing a connected GDB client. Runs on the listener
 * thread. Reads packets and dispatches them. Returns when the client
 * disconnects.
 */
static void gdb_client_session(void)
{
    char *pkt_buf = malloc(GDB_PKT_BUF_SIZE);
    gdb_rsp_ctx_t rsp_ctx = {0};
    if (!pkt_buf) {
        log_error("gdb: failed to allocate packet buffer");
        return;
    }

    gdb_rsp_reset(&rsp_ctx);
    gdb.rsp_ctx = &rsp_ctx;

    while (gdb.client_fd >= 0) {
        /* Wait for either a packet from GDB or a stop event from a vCPU.
         * Use poll() so the GDB stub can wake up when a thread stops.
         */
        struct pollfd pfd = {
            .fd = gdb.client_fd,
            .events = POLLIN,
        };

        int pr = poll(&pfd, 1, -1);
        if (pr <= 0) {
            if (pr < 0 && errno == EINTR)
                continue;
            break;
        }

        if (pfd.revents & (POLLERR | POLLHUP))
            break;

        int pkt_len =
            gdb_rsp_recv(&rsp_ctx, gdb.client_fd, pkt_buf, GDB_PKT_BUF_SIZE);
        if (pkt_len <= 0)
            break;

        handle_packet(pkt_buf, pkt_len);
    }

    gdb.rsp_ctx = NULL;
    free(pkt_buf);
}

/* Listener thread. */

static void *listener_thread_fn(void *arg)
{
    (void) arg;
    pthread_setname_np("gdb-stub");

    while (gdb.listen_fd >= 0) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int fd =
            accept(gdb.listen_fd, (struct sockaddr *) &client_addr, &addr_len);
        if (fd < 0) {
            if (errno == EINTR)
                continue;
            break;
        }

        if (fd_set_cloexec(fd) < 0) {
            log_warn("gdb: failed to set FD_CLOEXEC on client socket: %s",
                     strerror(errno));
            close(fd);
            continue;
        }

        /* Disable Nagle's algorithm for responsive packet exchange */
        int opt = 1;
        if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0) {
            log_warn("gdb: failed to set TCP_NODELAY on client socket: %s",
                     strerror(errno));
        }

        log_info("GDB client connected from %s:%d",
                 inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        gdb.client_fd = fd;

        /* If the GDB stub is in stop-on-entry mode, the main thread is already
         * blocked in gdb_stub_wait_for_attach(). Wake it up now that
         * the GDB stub has a client.
         */
        pthread_mutex_lock(&gdb.lock);
        pthread_cond_broadcast(&gdb.stop_cond);
        pthread_mutex_unlock(&gdb.lock);

        gdb_client_session();

        if (gdb.client_fd >= 0) {
            close(gdb.client_fd);
            gdb.client_fd = -1;
        }

        log_info("GDB client disconnected");

        /* Resume all threads if client disconnects while stopped */
        pthread_mutex_lock(&gdb.lock);
        if (gdb.all_stopped) {
            gdb.all_stopped = 0;
            gdb.stop_requested = 0;
            memset(gdb.breakpoints, 0, sizeof(gdb.breakpoints));
            memset(gdb.watchpoints, 0, sizeof(gdb.watchpoints));
            pthread_cond_broadcast(&gdb.resume_cond);
        }
        pthread_mutex_unlock(&gdb.lock);
    }

    return NULL;
}

/* Public API. */

int gdb_stub_init(int port, guest_t *g)
{
    if (gdb.initialized)
        return 0;

    gdb.guest = g;
    gdb.current_g_tid = 1;
    gdb.current_c_tid = 1;
    pthread_cond_init(&gdb.stop_cond, NULL);
    pthread_cond_init(&gdb.resume_cond, NULL);

    /* Create TCP listener socket */
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("elfuse: gdb: socket");
        return -1;
    }

    if (fd_set_cloexec(fd) < 0) {
        perror("elfuse: gdb: fcntl(FD_CLOEXEC)");
        close(fd);
        return -1;
    }

    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("elfuse: gdb: setsockopt(SO_REUSEADDR)");
        close(fd);
        return -1;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons((uint16_t) port),
        .sin_addr = {.s_addr = htonl(INADDR_LOOPBACK)},
    };

    if (bind(fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        perror("elfuse: gdb: bind");
        close(fd);
        return -1;
    }

    if (listen(fd, 1) < 0) {
        perror("elfuse: gdb: listen");
        close(fd);
        return -1;
    }

    gdb.listen_fd = fd;
    gdb.initialized = 1;

    log_info("GDB stub listening on localhost:%d", port);
    log_info(
        "Connect with: aarch64-linux-gnu-gdb -ex "
        "\"target remote :%d\" <binary>",
        port);

    /* Start listener thread */
    if (pthread_create(&gdb.listener_thread, NULL, listener_thread_fn, NULL) !=
        0) {
        perror("elfuse: gdb: pthread_create");
        close(fd);
        gdb.listen_fd = -1;
        gdb.initialized = 0;
        return -1;
    }

    return 0;
}

void gdb_stub_wait_for_attach(void)
{
    if (!gdb.initialized)
        return;

    log_info("Waiting for GDB to attach...");

    /* Snapshot registers before blocking. Runs on the vCPU owner thread
     * This lets GDB inspect initial register state at entry.
     */
    if (current_thread)
        gdb_snap_vcpu(current_thread);

    pthread_mutex_lock(&gdb.lock);

    /* Wait until a client connects */
    while (gdb.client_fd < 0)
        pthread_cond_wait(&gdb.stop_cond, &gdb.lock);

    /* Enter stopped state so GDB can inspect initial state */
    gdb.all_stopped = 1;
    gdb.stop_tid = current_thread ? current_thread->guest_tid : 1;
    gdb.stop_reason = GDB_STOP_ENTRY;
    gdb.stop_addr = 0;
    gdb.current_g_tid = gdb.stop_tid;
    gdb.current_c_tid = gdb.stop_tid;

    /* Block until GDB resumes the inferior (via 'c' or 's') */
    while (gdb.all_stopped)
        pthread_cond_wait(&gdb.resume_cond, &gdb.lock);

    pthread_mutex_unlock(&gdb.lock);

    /* Apply any register changes GDB made.
     * Stop-on-entry is shim-mediated (not TDE), so tde_stop=0.
     */
    if (current_thread && current_thread->gdb_regs_dirty)
        gdb_restore_vcpu(current_thread, 0);

    /* Re-sync debug registers because breakpoints/watchpoints may have been
     * set by GDB during the initial attach (e.g., step sets a temp bp).
     * The initial sync in main.c ran before any breakpoints existed.
     */
    if (current_thread)
        gdb_stub_sync_debug_regs(current_thread->vcpu);

    log_info("GDB attached, starting guest");
}

int gdb_stub_is_active(void)
{
    return gdb.initialized && gdb.client_fd >= 0;
}

int gdb_stub_handle_stop(int stop_reason, uint64_t stop_addr)
{
    if (!gdb.initialized || gdb.client_fd < 0)
        return 0;

    int64_t my_tid = current_thread ? current_thread->guest_tid : 1;

    /* Snapshot vCPU registers into thread entry. Must happen on the
     * vCPU's owning thread (HVF requirement). The GDB handler thread
     * reads/writes this snapshot while the vCPU thread is blocked.
     */
    if (current_thread)
        gdb_snap_vcpu(current_thread);

    pthread_mutex_lock(&gdb.lock);

    /* In all-stop mode, only the first stopped thread reports to GDB.
     * Other stopped threads wait here until GDB resumes the inferior.
     */
    int first_to_stop = !gdb.all_stopped;

    if (first_to_stop) {
        /* Remove temporary breakpoints (must be under lock to avoid
         * concurrent modification of the breakpoint table).
         */
        bp_remove_temps();

        /* Record stop info */
        gdb.all_stopped = 1;
        gdb.stop_tid = my_tid;
        gdb.stop_reason = stop_reason;
        gdb.stop_addr = stop_addr;
        gdb.stop_requested = 0;

        /* Update focus thread */
        gdb.current_g_tid = my_tid;
        gdb.current_c_tid = my_tid;
    }

    pthread_mutex_unlock(&gdb.lock);

    if (first_to_stop) {
        /* Stop all other vCPUs (all-stop mode) */
        thread_interrupt_all();

        /* Send stop reply to GDB. Only the first thread to stop sends it,
         * avoiding duplicate replies that would corrupt the RSP protocol.
         */
        char reply[128];
        build_stop_reply(reply, sizeof(reply));
        rsp_reply(reply);
    }

    /* Block this thread until GDB resumes */
    pthread_mutex_lock(&gdb.lock);
    while (gdb.all_stopped)
        pthread_cond_wait(&gdb.resume_cond, &gdb.lock);

    int do_step = gdb.resume_action;
    pthread_mutex_unlock(&gdb.lock);

    /* Apply any register changes GDB made to the snapshot.
     * TDE debug stops (breakpoint, step, watchpoint) bypassed EL1,
     * so HV_REG_PC must also be written for the resume to work.
     * Shim-mediated stops (signals, Ctrl+C) only need ELR_EL1.
     */
    int tde =
        (stop_reason == GDB_STOP_BREAKPOINT || stop_reason == GDB_STOP_STEP ||
         stop_reason == GDB_STOP_WATCHPOINT);
    if (current_thread && current_thread->gdb_regs_dirty)
        gdb_restore_vcpu(current_thread, tde);

    /* Re-sync debug registers before resuming */
    if (current_thread)
        gdb_stub_sync_debug_regs(current_thread->vcpu);

    return do_step;
}

int gdb_stub_stop_requested(void)
{
    if (!gdb.initialized)
        return 0;
    return __atomic_load_n(&gdb.stop_requested, __ATOMIC_ACQUIRE);
}

void gdb_stub_shutdown(void)
{
    if (!gdb.initialized)
        return;

    /* Close listener socket to break accept() in listener thread */
    if (gdb.listen_fd >= 0) {
        close(gdb.listen_fd);
        gdb.listen_fd = -1;
    }

    /* Close client if connected */
    if (gdb.client_fd >= 0) {
        close(gdb.client_fd);
        gdb.client_fd = -1;
    }

    /* Resume any stopped threads so they can exit */
    pthread_mutex_lock(&gdb.lock);
    gdb.all_stopped = 0;
    gdb.stop_requested = 0;
    pthread_cond_broadcast(&gdb.resume_cond);
    pthread_mutex_unlock(&gdb.lock);

    gdb.initialized = 0;
}
