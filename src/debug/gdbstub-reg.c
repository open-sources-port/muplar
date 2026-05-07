/* GDB register snapshot helpers
 *
 * Copyright 2026 elfuse contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <string.h>

#include "hvutil.h"
#include "utils.h"

#include "debug/gdbstub-reg.h"
#include "debug/gdbstub-rsp.h"

void gdb_snap_vcpu(thread_entry_t *t)
{
    hv_vcpu_t vcpu = t->vcpu;
    uint8_t *s = t->gdb_reg_snapshot;
    memset(s, 0, GDBREGSNAPSIZE);

    for (int i = 0; i < 31; i++) {
        uint64_t val = vcpu_get_gpr(vcpu, (unsigned) i);
        memcpy(s + GDBREGOFFGPR(i), &val, 8);
    }

    uint64_t sp = vcpu_get_sysreg(vcpu, HV_SYS_REG_SP_EL0);
    memcpy(s + GDBREGOFFSP, &sp, 8);

    uint64_t pc = vcpu_get_sysreg(vcpu, HV_SYS_REG_ELR_EL1);
    memcpy(s + GDBREGOFFPC, &pc, 8);

    uint64_t pstate = vcpu_get_sysreg(vcpu, HV_SYS_REG_SPSR_EL1);
    uint32_t cpsr = (uint32_t) pstate;
    memcpy(s + GDBREGOFFCPSR, &cpsr, 4);

    for (int i = 0; i < 32; i++) {
        hv_simd_fp_uchar16_t val = vcpu_get_simd(vcpu, (unsigned) i);
        memcpy(s + GDBREGOFFV(i), &val, 16);
    }

    uint64_t fpsr;
    HV_CHECK(hv_vcpu_get_reg(vcpu, HV_REG_FPSR, &fpsr));
    uint32_t fpsr32 = (uint32_t) fpsr;
    memcpy(s + GDBREGOFFFPSR, &fpsr32, 4);

    uint64_t fpcr;
    HV_CHECK(hv_vcpu_get_reg(vcpu, HV_REG_FPCR, &fpcr));
    uint32_t fpcr32 = (uint32_t) fpcr;
    memcpy(s + GDBREGOFFFPCR, &fpcr32, 4);

    t->gdb_regs_dirty = false;
}

void gdb_restore_vcpu(thread_entry_t *t, int tde_stop)
{
    hv_vcpu_t vcpu = t->vcpu;
    const uint8_t *s = t->gdb_reg_snapshot;

    for (int i = 0; i < 31; i++) {
        uint64_t val;
        memcpy(&val, s + GDBREGOFFGPR(i), 8);
        vcpu_set_gpr(vcpu, (unsigned) i, val);
    }

    uint64_t sp;
    memcpy(&sp, s + GDBREGOFFSP, 8);
    vcpu_set_sysreg(vcpu, HV_SYS_REG_SP_EL0, sp);

    uint64_t pc;
    memcpy(&pc, s + GDBREGOFFPC, 8);
    vcpu_set_sysreg(vcpu, HV_SYS_REG_ELR_EL1, pc);
    if (tde_stop)
        HV_CHECK(hv_vcpu_set_reg(vcpu, HV_REG_PC, pc));

    uint32_t cpsr;
    memcpy(&cpsr, s + GDBREGOFFCPSR, 4);
    vcpu_set_sysreg(vcpu, HV_SYS_REG_SPSR_EL1, (uint64_t) cpsr);
    if (tde_stop)
        HV_CHECK(hv_vcpu_set_reg(vcpu, HV_REG_CPSR, (uint64_t) cpsr));

    for (int i = 0; i < 32; i++) {
        hv_simd_fp_uchar16_t val;
        memcpy(&val, s + GDBREGOFFV(i), 16);
        vcpu_set_simd(vcpu, (unsigned) i, val);
    }

    uint32_t fpsr32;
    memcpy(&fpsr32, s + GDBREGOFFFPSR, 4);
    HV_CHECK(hv_vcpu_set_reg(vcpu, HV_REG_FPSR, (uint64_t) fpsr32));

    uint32_t fpcr32;
    memcpy(&fpcr32, s + GDBREGOFFFPCR, 4);
    HV_CHECK(hv_vcpu_set_reg(vcpu, HV_REG_FPCR, (uint64_t) fpcr32));

    t->gdb_regs_dirty = false;
}

int gdb_reg_offset(uint64_t regnum, int *out_off, int *out_size)
{
    if (regnum < 31) {
        *out_off = GDBREGOFFGPR((int) regnum);
        *out_size = 8;
        return 0;
    }
    if (regnum >= 34 && regnum < 66) {
        *out_off = GDBREGOFFV((int) (regnum - 34));
        *out_size = 16;
        return 0;
    }

    static const struct {
        int off, size;
    } singletons[68] = {
        [31] = {GDBREGOFFSP, 8},   [32] = {GDBREGOFFPC, 8},
        [33] = {GDBREGOFFCPSR, 4}, [66] = {GDBREGOFFFPSR, 4},
        [67] = {GDBREGOFFFPCR, 4},
    };

    if (regnum >= ARRAY_SIZE(singletons) || singletons[regnum].size == 0)
        return -1;
    *out_off = singletons[regnum].off;
    *out_size = singletons[regnum].size;
    return 0;
}

static const char target_xml[] =
    "<?xml version=\"1.0\"?>\n"
    "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">\n"
    "<target version=\"1.0\">\n"
    "  <architecture>aarch64</architecture>\n"
    "  <osabi>GNU/Linux</osabi>\n"
    "  <feature name=\"org.gnu.gdb.aarch64.core\">\n"
    "    <reg name=\"x0\"  bitsize=\"64\" type=\"uint64\"/>\n"
    "    <reg name=\"x1\"  bitsize=\"64\" type=\"uint64\"/>\n"
    "    <reg name=\"x2\"  bitsize=\"64\" type=\"uint64\"/>\n"
    "    <reg name=\"x3\"  bitsize=\"64\" type=\"uint64\"/>\n"
    "    <reg name=\"x4\"  bitsize=\"64\" type=\"uint64\"/>\n"
    "    <reg name=\"x5\"  bitsize=\"64\" type=\"uint64\"/>\n"
    "    <reg name=\"x6\"  bitsize=\"64\" type=\"uint64\"/>\n"
    "    <reg name=\"x7\"  bitsize=\"64\" type=\"uint64\"/>\n"
    "    <reg name=\"x8\"  bitsize=\"64\" type=\"uint64\"/>\n"
    "    <reg name=\"x9\"  bitsize=\"64\" type=\"uint64\"/>\n"
    "    <reg name=\"x10\" bitsize=\"64\" type=\"uint64\"/>\n"
    "    <reg name=\"x11\" bitsize=\"64\" type=\"uint64\"/>\n"
    "    <reg name=\"x12\" bitsize=\"64\" type=\"uint64\"/>\n"
    "    <reg name=\"x13\" bitsize=\"64\" type=\"uint64\"/>\n"
    "    <reg name=\"x14\" bitsize=\"64\" type=\"uint64\"/>\n"
    "    <reg name=\"x15\" bitsize=\"64\" type=\"uint64\"/>\n"
    "    <reg name=\"x16\" bitsize=\"64\" type=\"uint64\"/>\n"
    "    <reg name=\"x17\" bitsize=\"64\" type=\"uint64\"/>\n"
    "    <reg name=\"x18\" bitsize=\"64\" type=\"uint64\"/>\n"
    "    <reg name=\"x19\" bitsize=\"64\" type=\"uint64\"/>\n"
    "    <reg name=\"x20\" bitsize=\"64\" type=\"uint64\"/>\n"
    "    <reg name=\"x21\" bitsize=\"64\" type=\"uint64\"/>\n"
    "    <reg name=\"x22\" bitsize=\"64\" type=\"uint64\"/>\n"
    "    <reg name=\"x23\" bitsize=\"64\" type=\"uint64\"/>\n"
    "    <reg name=\"x24\" bitsize=\"64\" type=\"uint64\"/>\n"
    "    <reg name=\"x25\" bitsize=\"64\" type=\"uint64\"/>\n"
    "    <reg name=\"x26\" bitsize=\"64\" type=\"uint64\"/>\n"
    "    <reg name=\"x27\" bitsize=\"64\" type=\"uint64\"/>\n"
    "    <reg name=\"x28\" bitsize=\"64\" type=\"uint64\"/>\n"
    "    <reg name=\"x29\" bitsize=\"64\" type=\"uint64\"/>\n"
    "    <reg name=\"x30\" bitsize=\"64\" type=\"uint64\"/>\n"
    "    <reg name=\"sp\"  bitsize=\"64\" type=\"data_ptr\"/>\n"
    "    <reg name=\"pc\"  bitsize=\"64\" type=\"code_ptr\"/>\n"
    "    <reg name=\"cpsr\" bitsize=\"32\" type=\"uint32\"/>\n"
    "  </feature>\n"
    "  <feature name=\"org.gnu.gdb.aarch64.fpu\">\n"
    "    <vector id=\"v2d\" type=\"ieee_double\" count=\"2\"/>\n"
    "    <vector id=\"v2u\" type=\"uint64\" count=\"2\"/>\n"
    "    <vector id=\"v2i\" type=\"int64\" count=\"2\"/>\n"
    "    <vector id=\"v4f\" type=\"ieee_single\" count=\"4\"/>\n"
    "    <vector id=\"v4u\" type=\"uint32\" count=\"4\"/>\n"
    "    <vector id=\"v4i\" type=\"int32\" count=\"4\"/>\n"
    "    <vector id=\"v8u\" type=\"uint16\" count=\"8\"/>\n"
    "    <vector id=\"v8i\" type=\"int16\" count=\"8\"/>\n"
    "    <vector id=\"v16u\" type=\"uint8\" count=\"16\"/>\n"
    "    <vector id=\"v16i\" type=\"int8\" count=\"16\"/>\n"
    "    <union id=\"vnd\">\n"
    "      <field name=\"f\" type=\"v2d\"/>\n"
    "      <field name=\"u\" type=\"v2u\"/>\n"
    "      <field name=\"s\" type=\"v2i\"/>\n"
    "    </union>\n"
    "    <union id=\"vns\">\n"
    "      <field name=\"f\" type=\"v4f\"/>\n"
    "      <field name=\"u\" type=\"v4u\"/>\n"
    "      <field name=\"s\" type=\"v4i\"/>\n"
    "    </union>\n"
    "    <union id=\"vnh\">\n"
    "      <field name=\"u\" type=\"v8u\"/>\n"
    "      <field name=\"s\" type=\"v8i\"/>\n"
    "    </union>\n"
    "    <union id=\"vnb\">\n"
    "      <field name=\"u\" type=\"v16u\"/>\n"
    "      <field name=\"s\" type=\"v16i\"/>\n"
    "    </union>\n"
    "    <union id=\"vnq\">\n"
    "      <field name=\"d\" type=\"vnd\"/>\n"
    "      <field name=\"s\" type=\"vns\"/>\n"
    "      <field name=\"h\" type=\"vnh\"/>\n"
    "      <field name=\"b\" type=\"vnb\"/>\n"
    "      <field name=\"q\" type=\"uint128\"/>\n"
    "    </union>\n";

static const char target_xml_2[] =
    "    <reg name=\"v0\"  bitsize=\"128\" type=\"vnq\"/>\n"
    "    <reg name=\"v1\"  bitsize=\"128\" type=\"vnq\"/>\n"
    "    <reg name=\"v2\"  bitsize=\"128\" type=\"vnq\"/>\n"
    "    <reg name=\"v3\"  bitsize=\"128\" type=\"vnq\"/>\n"
    "    <reg name=\"v4\"  bitsize=\"128\" type=\"vnq\"/>\n"
    "    <reg name=\"v5\"  bitsize=\"128\" type=\"vnq\"/>\n"
    "    <reg name=\"v6\"  bitsize=\"128\" type=\"vnq\"/>\n"
    "    <reg name=\"v7\"  bitsize=\"128\" type=\"vnq\"/>\n"
    "    <reg name=\"v8\"  bitsize=\"128\" type=\"vnq\"/>\n"
    "    <reg name=\"v9\"  bitsize=\"128\" type=\"vnq\"/>\n"
    "    <reg name=\"v10\" bitsize=\"128\" type=\"vnq\"/>\n"
    "    <reg name=\"v11\" bitsize=\"128\" type=\"vnq\"/>\n"
    "    <reg name=\"v12\" bitsize=\"128\" type=\"vnq\"/>\n"
    "    <reg name=\"v13\" bitsize=\"128\" type=\"vnq\"/>\n"
    "    <reg name=\"v14\" bitsize=\"128\" type=\"vnq\"/>\n"
    "    <reg name=\"v15\" bitsize=\"128\" type=\"vnq\"/>\n"
    "    <reg name=\"v16\" bitsize=\"128\" type=\"vnq\"/>\n"
    "    <reg name=\"v17\" bitsize=\"128\" type=\"vnq\"/>\n"
    "    <reg name=\"v18\" bitsize=\"128\" type=\"vnq\"/>\n"
    "    <reg name=\"v19\" bitsize=\"128\" type=\"vnq\"/>\n"
    "    <reg name=\"v20\" bitsize=\"128\" type=\"vnq\"/>\n"
    "    <reg name=\"v21\" bitsize=\"128\" type=\"vnq\"/>\n"
    "    <reg name=\"v22\" bitsize=\"128\" type=\"vnq\"/>\n"
    "    <reg name=\"v23\" bitsize=\"128\" type=\"vnq\"/>\n"
    "    <reg name=\"v24\" bitsize=\"128\" type=\"vnq\"/>\n"
    "    <reg name=\"v25\" bitsize=\"128\" type=\"vnq\"/>\n"
    "    <reg name=\"v26\" bitsize=\"128\" type=\"vnq\"/>\n"
    "    <reg name=\"v27\" bitsize=\"128\" type=\"vnq\"/>\n"
    "    <reg name=\"v28\" bitsize=\"128\" type=\"vnq\"/>\n"
    "    <reg name=\"v29\" bitsize=\"128\" type=\"vnq\"/>\n"
    "    <reg name=\"v30\" bitsize=\"128\" type=\"vnq\"/>\n"
    "    <reg name=\"v31\" bitsize=\"128\" type=\"vnq\"/>\n"
    "    <reg name=\"fpsr\" bitsize=\"32\" type=\"uint32\"/>\n"
    "    <reg name=\"fpcr\" bitsize=\"32\" type=\"uint32\"/>\n"
    "  </feature>\n"
    "</target>\n";

#define TARGET_XML_TOTAL_LEN \
    ((sizeof(target_xml) - 1) + (sizeof(target_xml_2) - 1))

static void target_xml_copy_range(char *dst, size_t offset, size_t len)
{
    size_t xml1_len = sizeof(target_xml) - 1;

    if (offset < xml1_len) {
        size_t first = xml1_len - offset;
        if (first > len)
            first = len;
        memcpy(dst, target_xml + offset, first);
        dst += first;
        len -= first;
        offset = 0;
    } else {
        offset -= xml1_len;
    }

    if (len > 0)
        memcpy(dst, target_xml_2 + offset, len);
}

int gdb_reply_target_xml(int fd, const char *pkt)
{
    const char *annex_start = pkt;
    const char *colon = strchr(pkt, ':');
    if (!colon)
        return gdb_rsp_send(fd, "E01", 3);

    size_t annex_len = (size_t) (colon - annex_start);
    if (annex_len != 10 || strncmp(annex_start, "target.xml", 10) != 0)
        return gdb_rsp_send(fd, "E01", 3);

    const char *p = colon + 1;
    uint64_t offset = gdb_parse_hex(&p);
    if (*p == ',')
        p++;
    uint64_t length = gdb_parse_hex(&p);

    size_t total_len = TARGET_XML_TOTAL_LEN;
    if (offset >= total_len)
        return gdb_rsp_send(fd, "l", 1);

    size_t remain = total_len - (size_t) offset;
    size_t chunk = remain < (size_t) length ? remain : (size_t) length;
    bool last = (offset + chunk >= total_len);

    char reply[TARGET_XML_TOTAL_LEN + 2];
    reply[0] = last ? 'l' : 'm';
    target_xml_copy_range(reply + 1, (size_t) offset, chunk);
    reply[chunk + 1] = '\0';

    return gdb_rsp_send(fd, reply, chunk + 1);
}
