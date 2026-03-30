#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define MEM_ADDR_NONE UINT64_MAX

#define FORCE_BOOT_NORAML_MAIN 0x10U
#define FORCE_BOOT_NORAML_SLAVE 0x14U
#define FORCE_BOOT_RECOVERY_MAIN 0x18U
#define FORCE_BOOT_RECOVERY_SLAVE 0x1CU
#define FORCE_BOOT_PXE 0x1EU

#define INDICATE_MDC_ADAPTIVE_VAL 0xB2U
#define SCB_CUSTOMER_HEADER_OFFSET 0x1000U
#define SCB_USERTYPE_HW 0x35933595U
#define SCB_USERTYPE_CUSTOMER 0x39593593U
#define ATF_SRAM_FATAL_FLAG 0x39U

#define FLASH_ADDR_BASE 0xC8000000ULL
#define L3_SRAM_BASE 0x90300000ULL
#define SYSCTRL_REG_BASE 0xC0140000ULL

#define UPDATE_FLAG_FLASH_ADDR_A (FLASH_ADDR_BASE + 0x40000U)
#define UPDATE_AREA_FLASH_ADDR_A (FLASH_ADDR_BASE + 0x40004U)
#define FLASH_RECOV_FORCE_MAIN (FLASH_ADDR_BASE + 0x40010U)
#define UPDATE_FLAG_FLASH_ADDR_B (FLASH_ADDR_BASE + 0x50000U)
#define UPDATE_AREA_FLASH_ADDR_B (FLASH_ADDR_BASE + 0x50004U)
#define FLASH_RECOV_FORCE_BAK (FLASH_ADDR_BASE + 0x50010U)

#define SC_EMMC_INIT_FLAG 0x5A5A5A5AU
#define SC_HSM_READY_FLAG 0xDCU

#define SRAM_RECOV_CNT_ADDR (L3_SRAM_BASE + 0x17654U)
#define SRAM_ADAPT_MOD_ADDR (L3_SRAM_BASE + 0x17650U)
#define SRAM_RECOV_INIT_FLAG (L3_SRAM_BASE + 0x17658U)

#define SC_POR_REG0 (SYSCTRL_REG_BASE + 0xEB00U)
#define SC_POR_REG1 (SYSCTRL_REG_BASE + 0xEB04U)
#define SC_POR_REG2 (SYSCTRL_REG_BASE + 0xEB08U)
#define SC_POR_REG3 (SYSCTRL_REG_BASE + 0xEB0CU)
#define SC_BAK_DATA0 (SYSCTRL_REG_BASE + 0xEC80U)
#define SC_BAK_DATA1 (SYSCTRL_REG_BASE + 0xEC84U)
#define SC_BAK_DATA2 (SYSCTRL_REG_BASE + 0xEC88U)
#define SC_BAK_DATA3 (SYSCTRL_REG_BASE + 0xEC8CU)
#define SC_BAK_DATA4 (SYSCTRL_REG_BASE + 0xEC90U)
#define SC_BAK_DATA6 (SYSCTRL_REG_BASE + 0xEC98U)
#define SC_BAK_DATA9 (SYSCTRL_REG_BASE + 0xECA4U)
#define SC_EMMC_INIT_ADDR (SYSCTRL_REG_BASE + 0xEC94U)
#define OS_VERIF_METHOD (SYSCTRL_REG_BASE + 0xECB4U)
#define SC_HSM_READY_ADDR (SYSCTRL_REG_BASE + 0xECBCU)
#define SC_SW_EXCEP_CODE_REG7 (SYSCTRL_REG_BASE + 0xF01CU)
#define SC_SW_EXCEP_CODE_REG8 (SYSCTRL_REG_BASE + 0xF020U)
#define SC_SW_EXCEP_CODE_REG9 (SYSCTRL_REG_BASE + 0xF024U)
#define SC_SW_EXCEP_CODE_REG10 (SYSCTRL_REG_BASE + 0xF028U)
#define SC_SW_EXCEP_CODE_REG11 (SYSCTRL_REG_BASE + 0xF02CU)
#define SC_SW_EXCEP_CODE_REG12 (SYSCTRL_REG_BASE + 0xF030U)
#define SC_VER_VER_REG_ADDR (SYSCTRL_REG_BASE + 0xFFFCU)

enum command_kind {
    CMD_DUMP,
    CMD_GET,
    CMD_SET,
};

struct mem_field {
    const char *name;
    uint64_t address;
    bool writable;
    const char *description;
};

struct mem_snapshot {
    bool available;
    uint32_t board_id; bool board_id_valid;
    uint32_t run_img_loc; bool run_img_loc_valid;
    uint32_t reset_src; bool reset_src_valid;
    uint32_t platform_info; bool platform_info_valid;
    uint32_t force_boot; bool force_boot_valid;
    uint32_t usb_efuse; bool usb_efuse_valid;
    uint32_t pcie_boot_index; bool pcie_boot_index_valid;
    uint32_t emmc_init_flag; bool emmc_init_flag_valid;
    uint32_t os_verif_method; bool os_verif_method_valid;
    uint32_t hsm_ready_flag; bool hsm_ready_flag_valid;
    uint32_t firmware_resetcnt; bool firmware_resetcnt_valid;
    uint32_t warmstart_flag; bool warmstart_flag_valid;
    uint32_t os_resetcnt; bool os_resetcnt_valid;
    uint32_t recovery_resetcnt; bool recovery_resetcnt_valid;
    uint32_t sw_excep_code7; bool sw_excep_code7_valid;
    uint32_t sw_excep_code8; bool sw_excep_code8_valid;
    uint32_t sw_excep_code9; bool sw_excep_code9_valid;
    uint32_t sw_excep_code10; bool sw_excep_code10_valid;
    uint32_t sw_excep_code11; bool sw_excep_code11_valid;
    uint32_t sw_excep_code12; bool sw_excep_code12_valid;
    uint32_t ver_ver; bool ver_ver_valid;
    uint32_t flash_update_flag_main; bool flash_update_flag_main_valid;
    uint32_t flash_update_area_main; bool flash_update_area_main_valid;
    uint32_t flash_recovery_force_main; bool flash_recovery_force_main_valid;
    uint32_t flash_update_flag_back; bool flash_update_flag_back_valid;
    uint32_t flash_update_area_back; bool flash_update_area_back_valid;
    uint32_t flash_recovery_force_back; bool flash_recovery_force_back_valid;
    uint32_t sram_adapt_mode; bool sram_adapt_mode_valid;
    uint32_t sram_recovery_count; bool sram_recovery_count_valid;
    uint32_t sram_recovery_init_flag; bool sram_recovery_init_flag_valid;
    const char *failed_field;
    uint64_t failed_address;
    int failed_errno;
};

static const struct mem_field g_mem_fields[] = {
    {"mem.board_id", SC_BAK_DATA0, true, "SC_BAK_DATA0"},
    {"mem.run_img_loc", SC_BAK_DATA1, true, "SC_BAK_DATA1 / AVOIDBRICK_IMG_INDICATE_ADDR"},
    {"mem.reset_src", SC_BAK_DATA2, true, "SC_BAK_DATA2"},
    {"mem.platform_info", SC_BAK_DATA3, true, "SC_BAK_DATA3"},
    {"mem.force_boot", SC_BAK_DATA4, true, "SC_BAK_DATA4"},
    {"mem.usb_efuse", SC_BAK_DATA6, true, "SC_BAK_DATA6"},
    {"mem.pcie_boot_index", SC_BAK_DATA9, true, "SC_BAK_DATA9"},
    {"mem.emmc_init_flag", SC_EMMC_INIT_ADDR, true, "SC_EMMC_INIT_ADDR"},
    {"mem.os_verif_method", OS_VERIF_METHOD, true, "OS_VERIF_METHOD"},
    {"mem.hsm_ready_flag", SC_HSM_READY_ADDR, true, "SC_HSM_READY_ADDR"},
    {"mem.firmware_resetcnt", SC_POR_REG0, true, "SC_POR_REG0"},
    {"mem.warmstart_flag", SC_POR_REG1, true, "SC_POR_REG1"},
    {"mem.os_resetcnt", SC_POR_REG2, true, "SC_POR_REG2"},
    {"mem.recovery_resetcnt", SC_POR_REG3, true, "SC_POR_REG3"},
    {"mem.sw_excep_code7", SC_SW_EXCEP_CODE_REG7, true, "SC_SW_EXCEP_CODE_REG7"},
    {"mem.sw_excep_code8", SC_SW_EXCEP_CODE_REG8, true, "SC_SW_EXCEP_CODE_REG8"},
    {"mem.sw_excep_code9", SC_SW_EXCEP_CODE_REG9, true, "SC_SW_EXCEP_CODE_REG9"},
    {"mem.sw_excep_code10", SC_SW_EXCEP_CODE_REG10, true, "SC_SW_EXCEP_CODE_REG10"},
    {"mem.sw_excep_code11", SC_SW_EXCEP_CODE_REG11, true, "SC_SW_EXCEP_CODE_REG11"},
    {"mem.sw_excep_code12", SC_SW_EXCEP_CODE_REG12, true, "SC_SW_EXCEP_CODE_REG12"},
    {"mem.ver_ver", SC_VER_VER_REG_ADDR, true, "SC_VER_VER_REG_ADDR"},
    {"flash.update_flag.main", UPDATE_FLAG_FLASH_ADDR_A, true, "UPDATE_FLAG_FLASH_ADDR_A"},
    {"flash.update_area.main", UPDATE_AREA_FLASH_ADDR_A, true, "UPDATE_AREA_FLASH_ADDR_A"},
    {"flash.recovery_force.main", FLASH_RECOV_FORCE_MAIN, true, "FLASH_RECOV_FORCE_MAIN"},
    {"flash.update_flag.back", UPDATE_FLAG_FLASH_ADDR_B, true, "UPDATE_FLAG_FLASH_ADDR_B"},
    {"flash.update_area.back", UPDATE_AREA_FLASH_ADDR_B, true, "UPDATE_AREA_FLASH_ADDR_B"},
    {"flash.recovery_force.back", FLASH_RECOV_FORCE_BAK, true, "FLASH_RECOV_FORCE_BAK"},
    {"sram.adapt_mode", SRAM_ADAPT_MOD_ADDR, true, "SRAM_ADAPT_MOD_ADDR"},
    {"sram.recovery_count", SRAM_RECOV_CNT_ADDR, true, "SRAM_RECOV_CNT_ADDR"},
    {"sram.recovery_init_flag", SRAM_RECOV_INIT_FLAG, true, "SRAM_RECOV_INIT_FLAG"},
};

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void write_le32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value & 0xFFu);
    p[1] = (uint8_t)((value >> 8) & 0xFFu);
    p[2] = (uint8_t)((value >> 16) & 0xFFu);
    p[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static int read_full_at(int fd, uint8_t *buf, size_t len, uint64_t off)
{
    size_t done = 0;

    while (done < len) {
        ssize_t ret = pread(fd, buf + done, len - done, (off_t)(off + done));
        if (ret < 0) {
            return -1;
        }
        if (ret == 0) {
            errno = EIO;
            return -1;
        }
        done += (size_t)ret;
    }
    return 0;
}

static int write_full_at(int fd, const uint8_t *buf, size_t len, uint64_t off)
{
    size_t done = 0;

    while (done < len) {
        ssize_t ret = pwrite(fd, buf + done, len - done, (off_t)(off + done));
        if (ret < 0) {
            return -1;
        }
        if (ret == 0) {
            errno = EIO;
            return -1;
        }
        done += (size_t)ret;
    }
    return 0;
}

static int read_u32_at(int fd, uint64_t off, uint32_t *value_out)
{
    uint8_t buf[4];

    if (read_full_at(fd, buf, sizeof(buf), off) != 0) {
        return -1;
    }
    *value_out = read_le32(buf);
    return 0;
}

static int write_u32_at(int fd, uint64_t off, uint32_t value)
{
    uint8_t buf[4];

    write_le32(buf, value);
    return write_full_at(fd, buf, sizeof(buf), off);
}

static bool mem_access_uses_mmap(const char *mem_path, int fd)
{
    struct stat st;

    if (mem_path == NULL || strcmp(mem_path, "/dev/mem") != 0) {
        return false;
    }
    if (fstat(fd, &st) != 0) {
        return false;
    }
    return S_ISCHR(st.st_mode);
}

static int read_u32_mmap(int fd, uint64_t off, uint32_t *value_out)
{
    long page_size = sysconf(_SC_PAGE_SIZE);
    uint64_t aligned;
    size_t page_off;
    size_t map_len;
    void *map;
    uint8_t buf[4];

    if (page_size <= 0) {
        errno = EINVAL;
        return -1;
    }

    aligned = off - (off % (uint64_t)page_size);
    page_off = (size_t)(off - aligned);
    if (page_off > SIZE_MAX - sizeof(buf)) {
        errno = EOVERFLOW;
        return -1;
    }
    map_len = page_off + sizeof(buf);

    map = mmap(NULL, map_len, PROT_READ, MAP_SHARED, fd, (off_t)aligned);
    if (map == MAP_FAILED) {
        return -1;
    }

    memcpy(buf, (const uint8_t *)map + page_off, sizeof(buf));
    if (munmap(map, map_len) != 0) {
        return -1;
    }

    *value_out = read_le32(buf);
    return 0;
}

static int write_u32_mmap(int fd, uint64_t off, uint32_t value)
{
    long page_size = sysconf(_SC_PAGE_SIZE);
    uint64_t aligned;
    size_t page_off;
    size_t map_len;
    void *map;
    uint8_t buf[4];
    int saved_errno;

    if (page_size <= 0) {
        errno = EINVAL;
        return -1;
    }

    aligned = off - (off % (uint64_t)page_size);
    page_off = (size_t)(off - aligned);
    if (page_off > SIZE_MAX - sizeof(buf)) {
        errno = EOVERFLOW;
        return -1;
    }
    map_len = page_off + sizeof(buf);

    map = mmap(NULL, map_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)aligned);
    if (map == MAP_FAILED) {
        return -1;
    }

    write_le32(buf, value);
    memcpy((uint8_t *)map + page_off, buf, sizeof(buf));
    if (msync(map, map_len, MS_SYNC) != 0) {
        saved_errno = errno;
        (void)munmap(map, map_len);
        errno = saved_errno;
        return -1;
    }
    if (munmap(map, map_len) != 0) {
        return -1;
    }

    return 0;
}

static int read_phys_u32(int fd, uint64_t off, bool use_mmap, uint32_t *value_out)
{
    if (use_mmap) {
        return read_u32_mmap(fd, off, value_out);
    }
    return read_u32_at(fd, off, value_out);
}

static int write_phys_u32(int fd, uint64_t off, bool use_mmap, uint32_t value)
{
    if (use_mmap) {
        return write_u32_mmap(fd, off, value);
    }
    return write_u32_at(fd, off, value);
}

static int parse_integer_arg(const char *text, uint64_t *value_out)
{
    char *endptr;
    unsigned long long value;

    errno = 0;
    value = strtoull(text, &endptr, 0);
    if (errno != 0 || endptr == text || *endptr != '\0') {
        return -1;
    }
    *value_out = (uint64_t)value;
    return 0;
}

static const char *force_boot_mode_name(uint32_t raw)
{
    uint32_t val = raw & 0x1FU;

    if (val == FORCE_BOOT_NORAML_MAIN) {
        return "normal-main";
    }
    if (val == FORCE_BOOT_NORAML_SLAVE) {
        return "normal-back";
    }
    if (val == FORCE_BOOT_RECOVERY_MAIN) {
        return "recovery-main";
    }
    if (val == FORCE_BOOT_RECOVERY_SLAVE) {
        return "recovery-back";
    }
    if (val == FORCE_BOOT_PXE) {
        return "pxe";
    }
    return "non-force";
}

static const char *platform_type_name(uint32_t ver_ver)
{
    if (ver_ver == 0) {
        return "asic";
    }
    switch ((ver_ver >> 16) & 0xFFFFu) {
    case 0x1U:
        return "emu";
    case 0x2U:
        return "esl";
    default:
        return "fpga";
    }
}

static uint32_t platform_version_value(uint32_t ver_ver)
{
    return ver_ver & 0xFFFFu;
}

static const char *os_verif_method_name(uint32_t value)
{
    if (value == SCB_USERTYPE_HW) {
        return "huawei-header";
    }
    if (value == SCB_USERTYPE_CUSTOMER) {
        return "customer-header";
    }
    return "non-hw/custom-like";
}

static uint32_t os_verif_head_offset(uint32_t value)
{
    return value == SCB_USERTYPE_HW ? 0U : SCB_CUSTOMER_HEADER_OFFSET;
}

static const char *emmc_init_state_name(uint32_t value)
{
    if (value == SC_EMMC_INIT_FLAG) {
        return "init-flag-set";
    }
    if (value == 0) {
        return "cleared";
    }
    return "unknown";
}

static const char *hsm_ready_state_name(uint32_t value)
{
    if ((value & 0xFFu) == SC_HSM_READY_FLAG) {
        return "ready";
    }
    if ((value & 0xFFu) == 0) {
        return "cleared";
    }
    return "waiting";
}

static const char *sram_adapt_mode_name(uint32_t value)
{
    if (value == INDICATE_MDC_ADAPTIVE_VAL) {
        return "mdc-adaptive";
    }
    if (value == 0) {
        return "normal";
    }
    return "unknown";
}

static const char *reboot_reason_name(uint32_t value)
{
    switch (value & 0xFFu) {
    case 0x00U: return "clear";
    case 0x01U: return "bios-exception";
    case 0x02U: return "hotboot";
    case 0x17U: return "a-core-watchdog";
    case 0x18U: return "lpm3-global-watchdog";
    case 0x1AU: return "lpmcu-watchdog";
    case 0x1FU: return "tsensor-reset";
    case 0x20U: return "pmu-exception";
    case 0x21U: return "ddr-watchdog";
    case 0x22U: return "ddr-fatal";
    case 0x24U: return "panic";
    case 0x25U: return "noc-exception";
    case 0x26U: return "soft-lockup";
    case 0x27U: return "ddrc-sec";
    case 0x29U: return "os-coredump";
    case 0x2AU: return "oom";
    case 0x2BU: return "hdc-disconnect";
    case 0x2CU: return "startup-exception";
    case 0x2DU: return "heartbeat-exception";
    case 0x2EU: return "run-exception";
    case 0x32U: return "lpm3-exception";
    case 0x33U: return "ts-exception";
    case 0x34U: return "aicpu-exception";
    case 0x35U: return "dvpp-exception";
    case 0x36U: return "driver-exception";
    case 0x37U: return "zip-exception";
    case 0x38U: return "tee-exception";
    case 0x39U: return "lpfw-exception";
    case 0x3AU: return "network-exception";
    case 0x3BU: return "hsm-exception";
    case 0x3CU: return "atf-exception";
    case 0x3DU: return "isp-exception";
    case 0x3EU: return "safetyisland-exception";
    case 0x3FU: return "toolchain-exception";
    case 0x40U: return "cluster-exception";
    case 0x41U: return "comisolator-exception";
    case 0x42U: return "sd-exception";
    case 0x43U: return "dp-exception";
    case 0x55U: return "cpu-buck-reboot";
    case 0x60U: return "suspend-fail";
    case 0x61U: return "resume-fail";
    case 0x62U: return "cpucore-exception";
    case 0x6AU: return "xloader-ddrinit-fail";
    case 0x6CU: return "xloader-load-fail";
    case 0x6DU: return "xloader-verify-fail";
    case 0x6EU: return "xloader-watchdog";
    case 0x75U: return "fastboot-panic";
    case 0x76U: return "fastboot-watchdog";
    case 0x77U: return "fastboot-ocv-voltage-error";
    case 0x78U: return "uefi-pcie-no-file";
    case 0x79U: return "uefi-pcie-file-too-large";
    case 0x7AU: return "uefi-flash-enter-4b-fail";
    case 0x7BU: return "uefi-flash-exit-4b-fail";
    case 0x7CU: return "uefi-flash-copy-os-fail";
    case 0x7DU: return "uefi-sd-ext2-read-fail";
    case 0x7EU: return "uefi-sd-hw-recovery-fail";
    case 0x7FU: return "uefi-log-atu-remap-err";
    case 0x80U: return "uefi-hot-reset";
    case 0x81U: return "uefi-sd-boot-fail";
    case 0x82U: return "uefi-tee-verify-fail";
    case 0x83U: return "uefi-lpmcu-verify-fail";
    case 0x84U: return "uefi-dtb-verify-fail";
    case 0x85U: return "uefi-img-verify-fail";
    case 0x86U: return "uefi-fs-verify-fail";
    case 0x87U: return "uefi-fdt-update-fail";
    case 0x88U: return "uefi-shutdown-bs-fail";
    case 0x8AU: return "device-load-timeout";
    case 0x8BU: return "device-heartbeat-lost";
    case 0x8CU: return "device-reset-inform";
    case 0x8DU: return "device-aer";
    case 0x90U: return "native-boot-fail";
    case 0x91U: return "boot-timeout";
    case 0x92U: return "framework-boot-fail";
    case 0x93U: return "native-data-fail";
    case 0xFFU: return "invalid";
    default:
        return "unknown";
    }
}

static void print_reboot_reason_line(const char *label, uint32_t value, bool valid, const char *role)
{
    if (!valid) {
        printf("  %-19s: unavailable\n", label);
        return;
    }
    if ((value & 0xFFu) == 0) {
        printf("  %-19s: 0x%08" PRIx32 " (clear, %s)\n", label, value, role);
        return;
    }
    printf("  %-19s: 0x%08" PRIx32 " (%s, %s)\n", label, value, reboot_reason_name(value), role);
}

static void print_mem_field_value(const struct mem_field *field, uint32_t value)
{
    if (strcmp(field->name, "mem.force_boot") == 0) {
        printf("0x%08" PRIx32 " (%s)\n", value, force_boot_mode_name(value));
        return;
    }
    if (strcmp(field->name, "mem.emmc_init_flag") == 0) {
        printf("0x%08" PRIx32 " (%s)\n", value, emmc_init_state_name(value));
        return;
    }
    if (strcmp(field->name, "mem.os_verif_method") == 0) {
        printf("0x%08" PRIx32 " (%s, head_offset=0x%04" PRIx32 ")\n",
               value, os_verif_method_name(value), os_verif_head_offset(value));
        return;
    }
    if (strcmp(field->name, "mem.hsm_ready_flag") == 0) {
        printf("0x%08" PRIx32 " (%s)\n", value, hsm_ready_state_name(value));
        return;
    }
    if (strcmp(field->name, "mem.ver_ver") == 0) {
        printf("0x%08" PRIx32 " (platform=%s, version=0x%04" PRIx32 ")\n",
               value, platform_type_name(value), platform_version_value(value));
        return;
    }
    if (strcmp(field->name, "sram.adapt_mode") == 0) {
        printf("0x%08" PRIx32 " (%s)\n", value, sram_adapt_mode_name(value));
        return;
    }
    if (strcmp(field->name, "mem.sw_excep_code8") == 0) {
        printf("0x%08" PRIx32 " (%s)\n", value,
               value == 0 ? "clear" : "boot-stage marker / expected OS_START_POINT");
        return;
    }
    if (strcmp(field->name, "mem.sw_excep_code12") == 0) {
        if ((value & 0xFFu) == ATF_SRAM_FATAL_FLAG) {
            printf("0x%08" PRIx32 " (atf-sram-fatal)\n", value);
        } else {
            printf("0x%08" PRIx32 " (%s)\n", value, reboot_reason_name(value));
        }
        return;
    }
    if (strncmp(field->name, "mem.sw_excep_code", 17) == 0) {
        printf("0x%08" PRIx32 " (%s)\n", value, reboot_reason_name(value));
        return;
    }
    if (strcmp(field->name, "sram.recovery_init_flag") == 0) {
        printf("0x%08" PRIx32 " (raw; no semantic use found in hboot)\n", value);
        return;
    }
    printf("0x%08" PRIx32 " (%" PRIu32 ")\n", value, value);
}

static bool prompt_yes_no(const char *prompt)
{
    char answer[32];
    size_t len;
    size_t i;

    fprintf(stderr, "%s", prompt);
    fflush(stderr);

    if (fgets(answer, sizeof(answer), stdin) == NULL) {
        return false;
    }

    len = strlen(answer);
    while (len > 0 && isspace((unsigned char)answer[len - 1])) {
        answer[--len] = '\0';
    }
    for (i = 0; i < len; ++i) {
        answer[i] = (char)tolower((unsigned char)answer[i]);
    }

    return strcmp(answer, "y") == 0 || strcmp(answer, "yes") == 0;
}

static const struct mem_field *find_mem_field(const char *name)
{
    size_t i;

    for (i = 0; i < ARRAY_SIZE(g_mem_fields); ++i) {
        if (strcmp(g_mem_fields[i].name, name) == 0) {
            return &g_mem_fields[i];
        }
    }
    return NULL;
}

static int read_mem_field_value(int fd, const struct mem_field *field, bool use_mmap, uint32_t *value_out)
{
    if (field->address == MEM_ADDR_NONE) {
        errno = ENOTSUP;
        return -1;
    }
    return read_phys_u32(fd, field->address, use_mmap, value_out);
}

static int write_mem_field_value(int fd, const struct mem_field *field, bool use_mmap, uint32_t value)
{
    if (field->address == MEM_ADDR_NONE) {
        errno = ENOTSUP;
        return -1;
    }
    return write_phys_u32(fd, field->address, use_mmap, value);
}

static int load_mem_snapshot(int fd, bool use_mmap, struct mem_snapshot *snapshot)
{
    struct {
        const struct mem_field *field;
        uint32_t *value_out;
        bool *valid_out;
    } fields[] = {
        {&g_mem_fields[0], &snapshot->board_id, &snapshot->board_id_valid},
        {&g_mem_fields[1], &snapshot->run_img_loc, &snapshot->run_img_loc_valid},
        {&g_mem_fields[2], &snapshot->reset_src, &snapshot->reset_src_valid},
        {&g_mem_fields[3], &snapshot->platform_info, &snapshot->platform_info_valid},
        {&g_mem_fields[4], &snapshot->force_boot, &snapshot->force_boot_valid},
        {&g_mem_fields[5], &snapshot->usb_efuse, &snapshot->usb_efuse_valid},
        {&g_mem_fields[6], &snapshot->pcie_boot_index, &snapshot->pcie_boot_index_valid},
        {&g_mem_fields[7], &snapshot->emmc_init_flag, &snapshot->emmc_init_flag_valid},
        {&g_mem_fields[8], &snapshot->os_verif_method, &snapshot->os_verif_method_valid},
        {&g_mem_fields[9], &snapshot->hsm_ready_flag, &snapshot->hsm_ready_flag_valid},
        {&g_mem_fields[10], &snapshot->firmware_resetcnt, &snapshot->firmware_resetcnt_valid},
        {&g_mem_fields[11], &snapshot->warmstart_flag, &snapshot->warmstart_flag_valid},
        {&g_mem_fields[12], &snapshot->os_resetcnt, &snapshot->os_resetcnt_valid},
        {&g_mem_fields[13], &snapshot->recovery_resetcnt, &snapshot->recovery_resetcnt_valid},
        {&g_mem_fields[14], &snapshot->sw_excep_code7, &snapshot->sw_excep_code7_valid},
        {&g_mem_fields[15], &snapshot->sw_excep_code8, &snapshot->sw_excep_code8_valid},
        {&g_mem_fields[16], &snapshot->sw_excep_code9, &snapshot->sw_excep_code9_valid},
        {&g_mem_fields[17], &snapshot->sw_excep_code10, &snapshot->sw_excep_code10_valid},
        {&g_mem_fields[18], &snapshot->sw_excep_code11, &snapshot->sw_excep_code11_valid},
        {&g_mem_fields[19], &snapshot->sw_excep_code12, &snapshot->sw_excep_code12_valid},
        {&g_mem_fields[20], &snapshot->ver_ver, &snapshot->ver_ver_valid},
        {&g_mem_fields[21], &snapshot->flash_update_flag_main, &snapshot->flash_update_flag_main_valid},
        {&g_mem_fields[22], &snapshot->flash_update_area_main, &snapshot->flash_update_area_main_valid},
        {&g_mem_fields[23], &snapshot->flash_recovery_force_main, &snapshot->flash_recovery_force_main_valid},
        {&g_mem_fields[24], &snapshot->flash_update_flag_back, &snapshot->flash_update_flag_back_valid},
        {&g_mem_fields[25], &snapshot->flash_update_area_back, &snapshot->flash_update_area_back_valid},
        {&g_mem_fields[26], &snapshot->flash_recovery_force_back, &snapshot->flash_recovery_force_back_valid},
        {&g_mem_fields[27], &snapshot->sram_adapt_mode, &snapshot->sram_adapt_mode_valid},
        {&g_mem_fields[28], &snapshot->sram_recovery_count, &snapshot->sram_recovery_count_valid},
        {&g_mem_fields[29], &snapshot->sram_recovery_init_flag, &snapshot->sram_recovery_init_flag_valid},
    };
    size_t i;
    bool any_loaded = false;

    memset(snapshot, 0, sizeof(*snapshot));

    for (i = 0; i < ARRAY_SIZE(fields); ++i) {
        if (read_mem_field_value(fd, fields[i].field, use_mmap, fields[i].value_out) == 0) {
            *fields[i].valid_out = true;
            any_loaded = true;
            continue;
        }
        if (snapshot->failed_field == NULL) {
            snapshot->failed_field = fields[i].field->name;
            snapshot->failed_address = fields[i].field->address;
            snapshot->failed_errno = errno;
        }
    }

    snapshot->available = any_loaded;
    if (any_loaded) {
        return 0;
    }
    errno = snapshot->failed_errno != 0 ? snapshot->failed_errno : ENODEV;
    return -1;
}

static void dump_value_line_hex(const char *label, uint32_t value, bool valid)
{
    if (valid) {
        printf("  %-19s: 0x%08" PRIx32 "\n", label, value);
    } else {
        printf("  %-19s: unavailable\n", label);
    }
}

static void dump_value_line_hex_dec(const char *label, uint32_t value, bool valid)
{
    if (valid) {
        printf("  %-19s: 0x%08" PRIx32 " (%" PRIu32 ")\n", label, value, value);
    } else {
        printf("  %-19s: unavailable\n", label);
    }
}

static void dump_mem(const struct mem_snapshot *mem)
{
    printf("[mem]\n");

    dump_value_line_hex("board_id", mem->board_id, mem->board_id_valid);
    dump_value_line_hex("run_img_loc", mem->run_img_loc, mem->run_img_loc_valid);
    dump_value_line_hex("reset_src", mem->reset_src, mem->reset_src_valid);
    dump_value_line_hex("platform_info", mem->platform_info, mem->platform_info_valid);
    if (mem->force_boot_valid) {
        printf("  %-19s: 0x%08" PRIx32 " (%s)\n", "force_boot", mem->force_boot,
               force_boot_mode_name(mem->force_boot));
    } else {
        printf("  %-19s: unavailable\n", "force_boot");
    }
    dump_value_line_hex("usb_efuse", mem->usb_efuse, mem->usb_efuse_valid);
    dump_value_line_hex("pcie_boot_index", mem->pcie_boot_index, mem->pcie_boot_index_valid);
    if (mem->emmc_init_flag_valid) {
        printf("  %-19s: 0x%08" PRIx32 " (%s)\n", "emmc_init_flag", mem->emmc_init_flag,
               emmc_init_state_name(mem->emmc_init_flag));
    } else {
        printf("  %-19s: unavailable\n", "emmc_init_flag");
    }
    if (mem->os_verif_method_valid) {
        printf("  %-19s: 0x%08" PRIx32 " (%s, head_offset=0x%04" PRIx32 ")\n",
               "os_verif_method", mem->os_verif_method,
               os_verif_method_name(mem->os_verif_method),
               os_verif_head_offset(mem->os_verif_method));
    } else {
        printf("  %-19s: unavailable\n", "os_verif_method");
    }
    if (mem->hsm_ready_flag_valid) {
        printf("  %-19s: 0x%08" PRIx32 " (%s)\n", "hsm_ready_flag", mem->hsm_ready_flag,
               hsm_ready_state_name(mem->hsm_ready_flag));
    } else {
        printf("  %-19s: unavailable\n", "hsm_ready_flag");
    }
    dump_value_line_hex_dec("firmware_resetcnt", mem->firmware_resetcnt, mem->firmware_resetcnt_valid);
    dump_value_line_hex_dec("warmstart_flag", mem->warmstart_flag, mem->warmstart_flag_valid);
    dump_value_line_hex_dec("os_resetcnt", mem->os_resetcnt, mem->os_resetcnt_valid);
    dump_value_line_hex_dec("recovery_resetcnt", mem->recovery_resetcnt, mem->recovery_resetcnt_valid);
    print_reboot_reason_line("sw_excep_code7", mem->sw_excep_code7, mem->sw_excep_code7_valid, "os");
    if (mem->sw_excep_code8_valid) {
        printf("  %-19s: 0x%08" PRIx32 " (%s)\n", "sw_excep_code8", mem->sw_excep_code8,
               mem->sw_excep_code8 == 0 ? "clear" : "boot-stage marker / expected OS_START_POINT");
    } else {
        printf("  %-19s: unavailable\n", "sw_excep_code8");
    }
    print_reboot_reason_line("sw_excep_code9", mem->sw_excep_code9, mem->sw_excep_code9_valid, "low-power");
    print_reboot_reason_line("sw_excep_code10", mem->sw_excep_code10, mem->sw_excep_code10_valid,
                             "safetyisland-or-os");
    print_reboot_reason_line("sw_excep_code11", mem->sw_excep_code11, mem->sw_excep_code11_valid, "aos");
    if (mem->sw_excep_code12_valid) {
        if ((mem->sw_excep_code12 & 0xFFu) == ATF_SRAM_FATAL_FLAG) {
            printf("  %-19s: 0x%08" PRIx32 " (atf-sram-fatal)\n", "sw_excep_code12", mem->sw_excep_code12);
        } else if ((mem->sw_excep_code12 & 0xFFu) == 0) {
            printf("  %-19s: 0x%08" PRIx32 " (clear, atf)\n", "sw_excep_code12", mem->sw_excep_code12);
        } else {
            printf("  %-19s: 0x%08" PRIx32 " (%s, atf)\n", "sw_excep_code12", mem->sw_excep_code12,
                   reboot_reason_name(mem->sw_excep_code12));
        }
    } else {
        printf("  %-19s: unavailable\n", "sw_excep_code12");
    }
    if (mem->ver_ver_valid) {
        printf("  %-19s: 0x%08" PRIx32 " (platform=%s, version=0x%04" PRIx32 ")\n",
               "ver_ver", mem->ver_ver, platform_type_name(mem->ver_ver), platform_version_value(mem->ver_ver));
    } else {
        printf("  %-19s: unavailable\n", "ver_ver");
    }
    dump_value_line_hex("flash.upd_flag.a", mem->flash_update_flag_main, mem->flash_update_flag_main_valid);
    dump_value_line_hex("flash.upd_area.a", mem->flash_update_area_main, mem->flash_update_area_main_valid);
    dump_value_line_hex("flash.rec_force.a", mem->flash_recovery_force_main, mem->flash_recovery_force_main_valid);
    dump_value_line_hex("flash.upd_flag.b", mem->flash_update_flag_back, mem->flash_update_flag_back_valid);
    dump_value_line_hex("flash.upd_area.b", mem->flash_update_area_back, mem->flash_update_area_back_valid);
    dump_value_line_hex("flash.rec_force.b", mem->flash_recovery_force_back, mem->flash_recovery_force_back_valid);
    if (mem->sram_adapt_mode_valid) {
        printf("  %-19s: 0x%08" PRIx32 " (%s)\n", "sram.adapt_mode", mem->sram_adapt_mode,
               sram_adapt_mode_name(mem->sram_adapt_mode));
    } else {
        printf("  %-19s: unavailable\n", "sram.adapt_mode");
    }
    dump_value_line_hex_dec("sram.recovery_count", mem->sram_recovery_count, mem->sram_recovery_count_valid);
    if (mem->sram_recovery_init_flag_valid) {
        printf("  %-19s: 0x%08" PRIx32 " (raw; no semantic use found in hboot)\n",
               "sram.recov_init", mem->sram_recovery_init_flag);
    } else {
        printf("  %-19s: unavailable\n", "sram.recov_init");
    }
    printf("\n");
}

static void usage(FILE *stream)
{
    fprintf(stream,
            "Usage:\n"
            "  hboot2ctl dump [--mem-path PATH]\n"
            "  hboot2ctl get  [--mem-path PATH] <field>\n"
            "  hboot2ctl set  [-y] [--mem-path PATH] <field> <value>\n"
            "\n"
            "Field examples:\n"
            "  mem.force_boot\n"
            "  mem.os_resetcnt\n"
            "  mem.os_verif_method\n"
            "  mem.ver_ver\n"
            "  flash.update_flag.main\n"
            "  sram.adapt_mode\n"
            "  sram.recovery_count\n"
            "\n"
            "Notes:\n"
            "  /dev/mem is used for mem/flash/sram fields; root is usually required.\n"
            "  hboot2ctl only exposes the validated hboot register map.\n"
            "  metadata inspection and editing are handled by hboot2meta.\n"
            "  set asks for confirmation before writing; use -y to skip the prompt.\n");
}

int main(int argc, char **argv)
{
    enum command_kind cmd;
    const char *field = NULL;
    const char *value = NULL;
    const char *mem_path = NULL;
    bool assume_yes = false;
    bool mem_path_explicit = false;
    bool mem_use_mmap = false;
    int argi = 1;
    int mem_fd = -1;
    struct mem_snapshot mem;
    bool mem_loaded = false;

    memset(&mem, 0, sizeof(mem));

    if (argc < 2) {
        usage(stderr);
        return 1;
    }

    if (strcmp(argv[argi], "dump") == 0) {
        cmd = CMD_DUMP;
    } else if (strcmp(argv[argi], "get") == 0) {
        cmd = CMD_GET;
    } else if (strcmp(argv[argi], "set") == 0) {
        cmd = CMD_SET;
    } else {
        usage(stderr);
        return 1;
    }
    ++argi;

    while (argi < argc) {
        if (strcmp(argv[argi], "-y") == 0) {
            assume_yes = true;
            ++argi;
            continue;
        }
        if (strcmp(argv[argi], "--mem-path") == 0) {
            if (argi + 1 >= argc) {
                usage(stderr);
                return 1;
            }
            mem_path = argv[argi + 1];
            mem_path_explicit = true;
            argi += 2;
            continue;
        }
        break;
    }

    if ((cmd == CMD_DUMP && argc - argi != 0) ||
        (cmd == CMD_GET && argc - argi != 1) ||
        (cmd == CMD_SET && argc - argi != 2)) {
        usage(stderr);
        return 1;
    }

    if (cmd != CMD_DUMP) {
        field = argv[argi++];
    }
    if (cmd == CMD_SET) {
        value = argv[argi++];
    }

    if (mem_path == NULL && access("/dev/mem", R_OK) == 0) {
        mem_path = "/dev/mem";
    }

    if (mem_path != NULL) {
        mem_fd = open(mem_path, cmd == CMD_SET ? O_RDWR : O_RDONLY);
        if (mem_fd >= 0) {
            mem_use_mmap = mem_access_uses_mmap(mem_path, mem_fd);
            if (load_mem_snapshot(mem_fd, mem_use_mmap, &mem) == 0) {
                mem_loaded = true;
            } else if (mem_path_explicit) {
                perror("read mem snapshot");
                close(mem_fd);
                return 1;
            } else if (cmd == CMD_DUMP && mem.failed_field != NULL) {
                fprintf(stderr,
                        "warning: failed to read %s at 0x%08" PRIx64 " from %s: %s\n",
                        mem.failed_field, mem.failed_address, mem_path, strerror(mem.failed_errno));
            }
        } else if (mem_path_explicit) {
            perror(mem_path);
            return 1;
        }
    }

    if (cmd == CMD_DUMP) {
        printf("hboot2ctl\n");
        printf("  mem_path : %s\n\n", mem_path != NULL ? mem_path : "(unavailable)");
        if (mem_loaded) {
            dump_mem(&mem);
        } else {
            printf("[mem]\n  unavailable\n\n");
        }
        if (mem_fd >= 0) {
            close(mem_fd);
        }
        return 0;
    }

    if (cmd == CMD_GET) {
        const struct mem_field *mem_field = find_mem_field(field);
        uint32_t value_u32;

        if (mem_field == NULL) {
            fprintf(stderr, "unknown field: %s\n", field);
            goto fail;
        }
        if (!mem_loaded) {
            fprintf(stderr, "mem fields unavailable; use --mem-path or run on target with /dev/mem\n");
            goto fail;
        }
        if (read_mem_field_value(mem_fd, mem_field, mem_use_mmap, &value_u32) != 0) {
            if (errno == ENOTSUP) {
                fprintf(stderr, "field unavailable in hboot map: %s\n", field);
                goto fail;
            }
            perror("read mem field");
            goto fail;
        }
        print_mem_field_value(mem_field, value_u32);
        if (mem_fd >= 0) {
            close(mem_fd);
        }
        return 0;
    }

    if (cmd == CMD_SET) {
        const struct mem_field *mem_field = find_mem_field(field);
        uint64_t numeric_value;
        char prompt[256];

        if (mem_field == NULL) {
            fprintf(stderr, "field is unknown or read-only: %s\n", field);
            goto fail;
        }
        if (!assume_yes && !isatty(STDIN_FILENO)) {
            fprintf(stderr, "refusing to write without confirmation on non-interactive stdin; rerun with -y\n");
            goto fail;
        }
        if (!mem_loaded) {
            fprintf(stderr, "mem fields unavailable; use --mem-path or run on target with /dev/mem\n");
            goto fail;
        }
        if (!mem_field->writable) {
            fprintf(stderr, "field is read-only: %s\n", field);
            goto fail;
        }
        if (parse_integer_arg(value, &numeric_value) != 0 || numeric_value > UINT32_MAX) {
            fprintf(stderr, "invalid value for %s: %s\n", field, value);
            goto fail;
        }
        snprintf(prompt, sizeof(prompt), "Write %s = 0x%08" PRIx64 " to %s? [y/N] ",
                 field, numeric_value, mem_path != NULL ? mem_path : "(null)");
        if (!assume_yes && !prompt_yes_no(prompt)) {
            fprintf(stderr, "write cancelled\n");
            goto fail;
        }
        if (write_mem_field_value(mem_fd, mem_field, mem_use_mmap, (uint32_t)numeric_value) != 0) {
            if (errno == ENOTSUP) {
                fprintf(stderr, "field unavailable in hboot map: %s\n", field);
            } else {
                perror("write mem field");
            }
            goto fail;
        }
        if (!mem_use_mmap && fsync(mem_fd) != 0) {
            perror("write mem field");
            goto fail;
        }
        if (mem_fd >= 0) {
            close(mem_fd);
        }
        return 0;
    }

fail:
    if (mem_fd >= 0) {
        close(mem_fd);
    }
    return 1;
}
