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
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define HEAD_MAGIC_NUM 0x55AA55AAu
#define MAX_IMAGE_COUNT 14

#define REGION_BASE_DEFAULT    0x0ULL
#define RAW_BOOT_DISK_OFFSET   0x100000ULL

#define PART_INFO_HEAD_OFFSET_MAIN 0x100000ULL
#define PART_CTRL_HEAD_OFFSET_MAIN 0x100200ULL
#define PART_INFO_HEAD_OFFSET_BACK 0x110000ULL
#define PART_CTRL_HEAD_OFFSET_BACK 0x110200ULL
#define BOOT_IMAGE_OFFSET_MAIN     0x120000ULL
#define BOOT_IMAGE_OFFSET_BACK     0x120400ULL
#define RECOV_IMAGE_OFFSET_MAIN    0x120800ULL
#define RECOV_IMAGE_OFFSET_BACK    0x120C00ULL

#define PART_INFO_REGION_SIZE  0x200U
#define PART_CTRL_REGION_SIZE  0x200U
#define IMAGE_INFO_REGION_SIZE 0x400U

#define PART_INFO_STRUCT_SIZE 56U
#define PART_CTRL_STRUCT_SIZE 104U
#define PART_IMAGE_STRUCT_SIZE 980U

enum section_kind {
    SECTION_PART_INFO,
    SECTION_PART_CTRL,
    SECTION_BOOT_IMAGE,
    SECTION_RECOVERY_IMAGE,
};

enum command_kind {
    CMD_DUMP,
    CMD_GET,
    CMD_SET,
};

enum layout_mode_kind {
    LAYOUT_AUTO,
    LAYOUT_WHOLE_DISK,
    LAYOUT_RAW_BOOT,
};

struct blob_region {
    const char *label;
    enum section_kind kind;
    uint64_t relative_offset;
    uint64_t absolute_offset;
    size_t raw_size;
    uint8_t raw[IMAGE_INFO_REGION_SIZE];
};

struct layout_image {
    struct blob_region part_info[2];
    struct blob_region part_ctrl[2];
    struct blob_region boot[2];
    struct blob_region recovery[2];
    uint64_t base_offset;
};

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t read_le64(const uint8_t *p)
{
    return (uint64_t)read_le32(p) | ((uint64_t)read_le32(p + 4) << 32);
}

static void write_le32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value & 0xFFu);
    p[1] = (uint8_t)((value >> 8) & 0xFFu);
    p[2] = (uint8_t)((value >> 16) & 0xFFu);
    p[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static void write_le64(uint8_t *p, uint64_t value)
{
    write_le32(p, (uint32_t)(value & 0xFFFFFFFFu));
    write_le32(p + 4, (uint32_t)(value >> 32));
}

static double bytes_to_mib(uint64_t value)
{
    return (double)value / (1024.0 * 1024.0);
}

static uint16_t crc16_ccitt(uint16_t start_crc, const uint8_t *data, size_t length)
{
    uint16_t crc = start_crc;
    size_t i;

    for (i = 0; i < length; ++i) {
        int bit;

        crc ^= (uint16_t)data[i] << 8;
        for (bit = 0; bit < 8; ++bit) {
            if ((crc & 0x8000u) != 0) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }

    return crc;
}

static void trim_ascii_copy(char *dst, size_t dst_size, const uint8_t *src, size_t src_size)
{
    size_t n = 0;

    if (dst_size == 0) {
        return;
    }

    while (n < src_size && n + 1 < dst_size && src[n] != '\0') {
        dst[n] = (char)src[n];
        ++n;
    }
    while (n > 0 && dst[n - 1] == ' ') {
        --n;
    }
    dst[n] = '\0';
}

static void copy_padded_ascii(uint8_t *dst, size_t dst_size, const char *src)
{
    size_t src_len = strlen(src);
    size_t copy_len = src_len < dst_size ? src_len : dst_size;

    memset(dst, 0, dst_size);
    memcpy(dst, src, copy_len);
}

static void init_region(struct blob_region *region, const char *label, enum section_kind kind,
                        uint64_t base_offset, uint64_t relative_offset, size_t raw_size)
{
    region->label = label;
    region->kind = kind;
    region->relative_offset = relative_offset;
    region->absolute_offset = base_offset + relative_offset;
    region->raw_size = raw_size;
    memset(region->raw, 0, sizeof(region->raw));
}

static uint64_t layout_relative_offset(enum layout_mode_kind layout_mode, uint64_t disk_offset)
{
    if (layout_mode == LAYOUT_RAW_BOOT) {
        return disk_offset - RAW_BOOT_DISK_OFFSET;
    }
    return disk_offset;
}

static void init_layout(struct layout_image *layout, enum layout_mode_kind layout_mode, uint64_t base_offset)
{
    layout->base_offset = base_offset;

    init_region(&layout->part_info[0], "part_info.main", SECTION_PART_INFO,
                base_offset, layout_relative_offset(layout_mode, PART_INFO_HEAD_OFFSET_MAIN), PART_INFO_REGION_SIZE);
    init_region(&layout->part_info[1], "part_info.back", SECTION_PART_INFO,
                base_offset, layout_relative_offset(layout_mode, PART_INFO_HEAD_OFFSET_BACK), PART_INFO_REGION_SIZE);

    init_region(&layout->part_ctrl[0], "part_ctrl.main", SECTION_PART_CTRL,
                base_offset, layout_relative_offset(layout_mode, PART_CTRL_HEAD_OFFSET_MAIN), PART_CTRL_REGION_SIZE);
    init_region(&layout->part_ctrl[1], "part_ctrl.back", SECTION_PART_CTRL,
                base_offset, layout_relative_offset(layout_mode, PART_CTRL_HEAD_OFFSET_BACK), PART_CTRL_REGION_SIZE);

    init_region(&layout->boot[0], "boot.main", SECTION_BOOT_IMAGE,
                base_offset, layout_relative_offset(layout_mode, BOOT_IMAGE_OFFSET_MAIN), IMAGE_INFO_REGION_SIZE);
    init_region(&layout->boot[1], "boot.back", SECTION_BOOT_IMAGE,
                base_offset, layout_relative_offset(layout_mode, BOOT_IMAGE_OFFSET_BACK), IMAGE_INFO_REGION_SIZE);

    init_region(&layout->recovery[0], "recovery.main", SECTION_RECOVERY_IMAGE,
                base_offset, layout_relative_offset(layout_mode, RECOV_IMAGE_OFFSET_MAIN), IMAGE_INFO_REGION_SIZE);
    init_region(&layout->recovery[1], "recovery.back", SECTION_RECOVERY_IMAGE,
                base_offset, layout_relative_offset(layout_mode, RECOV_IMAGE_OFFSET_BACK), IMAGE_INFO_REGION_SIZE);
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

static int load_region(int fd, struct blob_region *region)
{
    return read_full_at(fd, region->raw, region->raw_size, region->absolute_offset);
}

static int load_layout_regions(int fd, struct layout_image *layout)
{
    size_t i;

    for (i = 0; i < ARRAY_SIZE(layout->part_info); ++i) {
        if (load_region(fd, &layout->part_info[i]) != 0) {
            return -1;
        }
        if (load_region(fd, &layout->part_ctrl[i]) != 0) {
            return -1;
        }
        if (load_region(fd, &layout->boot[i]) != 0) {
            return -1;
        }
        if (load_region(fd, &layout->recovery[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

static size_t region_crc_offset(enum section_kind kind);
static size_t region_min_struct_size(enum section_kind kind);
static uint32_t region_size_field(const struct blob_region *region);
static uint32_t region_crc_field(const struct blob_region *region);
static bool region_crc_valid(const struct blob_region *region, uint32_t *computed_out);
static bool region_crc_is_hboot_verified(enum section_kind kind);

static int region_probe_score(const struct blob_region *region)
{
    int score = 0;
    uint32_t magic = read_le32(region->raw);

    if (magic == HEAD_MAGIC_NUM) {
        score += 4;
    }
    if (region_crc_valid(region, NULL)) {
        score += 2;
    }
    if (region_size_field(region) >= region_min_struct_size(region->kind) &&
        region_size_field(region) <= region->raw_size) {
        score += 1;
    }
    return score;
}

static int layout_probe_score(const struct layout_image *layout)
{
    int score = 0;
    size_t i;

    for (i = 0; i < ARRAY_SIZE(layout->part_info); ++i) {
        score += region_probe_score(&layout->part_info[i]);
        score += region_probe_score(&layout->part_ctrl[i]);
        score += region_probe_score(&layout->boot[i]);
        score += region_probe_score(&layout->recovery[i]);
    }
    return score;
}

static int probe_layout_choice(int fd, enum layout_mode_kind *layout_mode_out, uint64_t *base_offset_out)
{
    const struct {
        enum layout_mode_kind layout_mode;
        uint64_t base_offset;
    } candidates[] = {
        {LAYOUT_WHOLE_DISK, 0},
        {LAYOUT_RAW_BOOT, 0},
        {LAYOUT_WHOLE_DISK, RAW_BOOT_DISK_OFFSET},
        {LAYOUT_RAW_BOOT, RAW_BOOT_DISK_OFFSET},
    };
    int best_score = -1;
    enum layout_mode_kind best_mode = candidates[0].layout_mode;
    uint64_t best_base = candidates[0].base_offset;
    size_t i;

    for (i = 0; i < ARRAY_SIZE(candidates); ++i) {
        struct layout_image candidate;
        int score;

        init_layout(&candidate, candidates[i].layout_mode, candidates[i].base_offset);
        if (load_layout_regions(fd, &candidate) != 0) {
            continue;
        }
        score = layout_probe_score(&candidate);
        if (score > best_score) {
            best_score = score;
            best_mode = candidates[i].layout_mode;
            best_base = candidates[i].base_offset;
        }
    }

    if (best_score < 0) {
        return -1;
    }
    *layout_mode_out = best_mode;
    *base_offset_out = best_base;
    return best_score;
}

static size_t region_crc_offset(enum section_kind kind)
{
    switch (kind) {
    case SECTION_PART_INFO:
        return 52U;
    case SECTION_PART_CTRL:
        return 100U;
    case SECTION_BOOT_IMAGE:
    case SECTION_RECOVERY_IMAGE:
        return 976U;
    }
    return 0U;
}

static size_t region_min_struct_size(enum section_kind kind)
{
    switch (kind) {
    case SECTION_PART_INFO:
        return PART_INFO_STRUCT_SIZE;
    case SECTION_PART_CTRL:
        return PART_CTRL_STRUCT_SIZE;
    case SECTION_BOOT_IMAGE:
    case SECTION_RECOVERY_IMAGE:
        return PART_IMAGE_STRUCT_SIZE;
    }
    return 0U;
}

static uint32_t region_size_field(const struct blob_region *region)
{
    return read_le32(region->raw + 8);
}

static uint32_t region_crc_field(const struct blob_region *region)
{
    return read_le32(region->raw + region_crc_offset(region->kind));
}

static bool region_crc_is_hboot_verified(enum section_kind kind)
{
    return kind == SECTION_PART_INFO || kind == SECTION_PART_CTRL;
}

static bool region_crc_valid(const struct blob_region *region, uint32_t *computed_out)
{
    size_t crc_off;
    uint32_t computed;
    uint32_t stored;

    if (!region_crc_is_hboot_verified(region->kind)) {
        if (computed_out != NULL) {
            *computed_out = 0;
        }
        return false;
    }

    crc_off = region_crc_offset(region->kind);
    if (crc_off + 4 > region->raw_size) {
        if (computed_out != NULL) {
            *computed_out = 0;
        }
        return false;
    }

    computed = (uint32_t)crc16_ccitt(0, region->raw, crc_off);
    stored = region_crc_field(region) & 0xFFFFu;
    if (computed_out != NULL) {
        *computed_out = computed;
    }
    return computed == stored;
}

static const char *region_crc_kind_name(enum section_kind kind)
{
    if (kind == SECTION_PART_INFO || kind == SECTION_PART_CTRL) {
        return "CRC16-CCITT(start=0)";
    }
    return "opaque/not checked by hboot";
}

static uint32_t image_entry_count_to_dump(const struct blob_region *region)
{
    uint32_t count = read_le32(region->raw + 76);
    if (count > MAX_IMAGE_COUNT) {
        count = MAX_IMAGE_COUNT;
    }
    return count;
}

static const char *yesno(bool value)
{
    return value ? "yes" : "no";
}

static void print_component_map(uint32_t component_map)
{
    printf("    component_map bits : 0x%08" PRIx32 "\n", component_map);
    printf("      RAWDATA_A=%u RAWDATA_B=%u RECOVER_A=%u RECOVER_B=%u\n",
           !!(component_map & (1u << 0)), !!(component_map & (1u << 1)),
           !!(component_map & (1u << 2)), !!(component_map & (1u << 3)));
    printf("      RECOVER_DATA_A=%u RECOVER_DATA_B=%u ROOTFS_A=%u ROOTFS_B=%u\n",
           !!(component_map & (1u << 4)), !!(component_map & (1u << 5)),
           !!(component_map & (1u << 6)), !!(component_map & (1u << 7)));
}

static void dump_part_info(const struct blob_region *region)
{
    uint32_t computed_crc = 0;
    bool crc_ok = region_crc_valid(region, &computed_crc);
    uint32_t i;

    printf("[%s]\n", region->label);
    printf("  absolute_offset : 0x%016" PRIx64 " (%.6f MiB)\n", region->absolute_offset,
           bytes_to_mib(region->absolute_offset));
    printf("  relative_offset : 0x%08" PRIx64 " (%.6f MiB)\n", region->relative_offset,
           bytes_to_mib(region->relative_offset));
    printf("  head_magic      : 0x%08" PRIx32 " (%s)\n", read_le32(region->raw + 0),
           read_le32(region->raw + 0) == HEAD_MAGIC_NUM ? "ok" : "unexpected");
    printf("  version         : %" PRIu32 "\n", read_le32(region->raw + 4));
    printf("  size            : %" PRIu32 " bytes (%.6f MiB)\n", read_le32(region->raw + 8),
           bytes_to_mib(read_le32(region->raw + 8)));
    printf("  partition_count : %" PRIu32 "\n", read_le32(region->raw + 12));
    print_component_map(read_le32(region->raw + 16));
    for (i = 0; i < 8; ++i) {
        printf("  resv[%u]        : 0x%08" PRIx32 "\n", i, read_le32(region->raw + 20 + i * 4));
    }
    printf("  crc_kind        : %s\n", region_crc_kind_name(region->kind));
    printf("  crc(stored)     : 0x%08" PRIx32 "\n", region_crc_field(region));
    printf("  crc(computed)   : 0x%08" PRIx32 "\n", computed_crc);
    printf("  crc_valid       : %s\n\n", yesno(crc_ok));
}

static void dump_part_ctrl(const struct blob_region *region)
{
    uint32_t computed_crc = 0;
    bool crc_ok = region_crc_valid(region, &computed_crc);
    uint32_t i;

    printf("[%s]\n", region->label);
    printf("  absolute_offset      : 0x%016" PRIx64 " (%.6f MiB)\n", region->absolute_offset,
           bytes_to_mib(region->absolute_offset));
    printf("  relative_offset      : 0x%08" PRIx64 " (%.6f MiB)\n", region->relative_offset,
           bytes_to_mib(region->relative_offset));
    printf("  head_magic           : 0x%08" PRIx32 " (%s)\n", read_le32(region->raw + 0),
           read_le32(region->raw + 0) == HEAD_MAGIC_NUM ? "ok" : "unexpected");
    printf("  version              : %" PRIu32 "\n", read_le32(region->raw + 4));
    printf("  size                 : %" PRIu32 " bytes (%.6f MiB)\n", read_le32(region->raw + 8),
           bytes_to_mib(read_le32(region->raw + 8)));
    printf("  force_recovery_flag  : 0x%08" PRIx32 "\n", read_le32(region->raw + 12));
    printf("  upgrade_part_count   : %" PRIu32 "\n", read_le32(region->raw + 16));
    for (i = 0; i < 4; ++i) {
        size_t base = 20 + i * 12;
        printf("  upgrade[%u].type     : 0x%08" PRIx32 "\n", i, read_le32(region->raw + base + 0));
        printf("  upgrade[%u].status   : 0x%08" PRIx32 "\n", i, read_le32(region->raw + base + 4));
        printf("  upgrade[%u].partflag : 0x%08" PRIx32 "\n", i, read_le32(region->raw + base + 8));
    }
    for (i = 0; i < 8; ++i) {
        printf("  resv[%u]             : 0x%08" PRIx32 "\n", i, read_le32(region->raw + 68 + i * 4));
    }
    printf("  crc_kind             : %s\n", region_crc_kind_name(region->kind));
    printf("  crc(stored)          : 0x%08" PRIx32 "\n", region_crc_field(region));
    printf("  crc(computed)        : 0x%08" PRIx32 "\n", computed_crc);
    printf("  crc_valid            : %s\n\n", yesno(crc_ok));
}

static void dump_image_header(const struct blob_region *region)
{
    uint32_t count = read_le32(region->raw + 76);
    uint32_t dump_count = image_entry_count_to_dump(region);
    uint32_t i;
    char partition_name[65];

    trim_ascii_copy(partition_name, sizeof(partition_name), region->raw + 12, 64);

    printf("[%s]\n", region->label);
    printf("  absolute_offset : 0x%016" PRIx64 " (%.6f MiB)\n", region->absolute_offset,
           bytes_to_mib(region->absolute_offset));
    printf("  relative_offset : 0x%08" PRIx64 " (%.6f MiB)\n", region->relative_offset,
           bytes_to_mib(region->relative_offset));
    printf("  head_magic      : 0x%08" PRIx32 " (%s)\n", read_le32(region->raw + 0),
           read_le32(region->raw + 0) == HEAD_MAGIC_NUM ? "ok" : "unexpected");
    printf("  version         : %" PRIu32 "\n", read_le32(region->raw + 4));
    printf("  size            : %" PRIu32 " bytes (%.6f MiB)\n", read_le32(region->raw + 8),
           bytes_to_mib(read_le32(region->raw + 8)));
    printf("  partition_name  : \"%s\"\n", partition_name);
    printf("  component_count : %" PRIu32 "%s\n", count, count > MAX_IMAGE_COUNT ? " (invalid)" : "");
    for (i = 0; i < dump_count; ++i) {
        size_t base = 80 + i * 64;
        char component_name[21];

        trim_ascii_copy(component_name, sizeof(component_name), region->raw + base + 4, 20);
        printf("  image[%u]:\n", i);
        printf("    component_type : 0x%08" PRIx32 "\n", read_le32(region->raw + base + 0));
        printf("    component_name : \"%s\"\n", component_name);
        printf("    offset         : 0x%016" PRIx64 " (%" PRIu64 ", %.6f MiB)\n",
               read_le64(region->raw + base + 24), read_le64(region->raw + base + 24),
               bytes_to_mib(read_le64(region->raw + base + 24)));
        printf("    data_size      : 0x%016" PRIx64 " (%" PRIu64 ", %.6f MiB)\n",
               read_le64(region->raw + base + 32), read_le64(region->raw + base + 32),
               bytes_to_mib(read_le64(region->raw + base + 32)));
        printf("    max_size       : 0x%016" PRIx64 " (%" PRIu64 ", %.6f MiB)\n",
               read_le64(region->raw + base + 40), read_le64(region->raw + base + 40),
               bytes_to_mib(read_le64(region->raw + base + 40)));
        printf("    rec[0]         : 0x%016" PRIx64 "\n", read_le64(region->raw + base + 48));
        printf("    rec[1]         : 0x%016" PRIx64 "\n", read_le64(region->raw + base + 56));
    }
    if (dump_count < count) {
        printf("  parsed_entries  : %" PRIu32 " (clamped)\n", dump_count);
    }
    printf("  crc_kind        : %s\n", region_crc_kind_name(region->kind));
    printf("  crc_field@0x%zx  : 0x%08" PRIx32 "\n", region_crc_offset(region->kind), region_crc_field(region));
    printf("  crc_valid       : n/a\n\n");
}

static void dump_layout(const struct layout_image *layout)
{
    size_t i;

    printf("base_offset: 0x%016" PRIx64 " (%.6f MiB)\n\n", layout->base_offset,
           bytes_to_mib(layout->base_offset));

    for (i = 0; i < 2; ++i) {
        dump_part_info(&layout->part_info[i]);
        dump_part_ctrl(&layout->part_ctrl[i]);
    }
    for (i = 0; i < 2; ++i) {
        dump_image_header(&layout->boot[i]);
    }
    for (i = 0; i < 2; ++i) {
        dump_image_header(&layout->recovery[i]);
    }
}

static bool parse_index_expr(const char *text, const char *prefix, uint32_t max_exclusive,
                             uint32_t *index_out, const char **rest_out)
{
    char *endptr;
    unsigned long index;

    if (strncmp(text, prefix, strlen(prefix)) != 0) {
        return false;
    }
    text += strlen(prefix);
    errno = 0;
    index = strtoul(text, &endptr, 10);
    if (errno != 0 || endptr == text || *endptr != ']' || index >= max_exclusive) {
        return false;
    }
    if (index_out != NULL) {
        *index_out = (uint32_t)index;
    }
    if (rest_out != NULL) {
        *rest_out = endptr + 1;
    }
    return true;
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

static int locate_field(struct blob_region *region, const char *field,
                        size_t *offset_out, size_t *width_out, size_t *str_width_out)
{
    *offset_out = 0;
    *width_out = 0;
    *str_width_out = 0;

    if (region->kind == SECTION_PART_INFO) {
        uint32_t idx;
        const char *rest;

        if (strcmp(field, "head_magic") == 0) {
            *offset_out = 0; *width_out = 4; return 0;
        }
        if (strcmp(field, "version") == 0) {
            *offset_out = 4; *width_out = 4; return 0;
        }
        if (strcmp(field, "size") == 0) {
            *offset_out = 8; *width_out = 4; return 0;
        }
        if (strcmp(field, "partition_count") == 0) {
            *offset_out = 12; *width_out = 4; return 0;
        }
        if (strcmp(field, "component_map") == 0) {
            *offset_out = 16; *width_out = 4; return 0;
        }
        if (parse_index_expr(field, "resv[", 8, &idx, &rest) && *rest == '\0') {
            *offset_out = 20 + idx * 4; *width_out = 4; return 0;
        }
        if (strcmp(field, "crc") == 0) {
            *offset_out = 52; *width_out = 4; return 0;
        }
        return -1;
    }

    if (region->kind == SECTION_PART_CTRL) {
        uint32_t idx;
        const char *rest;

        if (strcmp(field, "head_magic") == 0) {
            *offset_out = 0; *width_out = 4; return 0;
        }
        if (strcmp(field, "version") == 0) {
            *offset_out = 4; *width_out = 4; return 0;
        }
        if (strcmp(field, "size") == 0) {
            *offset_out = 8; *width_out = 4; return 0;
        }
        if (strcmp(field, "force_recovery_flag") == 0) {
            *offset_out = 12; *width_out = 4; return 0;
        }
        if (strcmp(field, "upgrade_part_count") == 0) {
            *offset_out = 16; *width_out = 4; return 0;
        }
        if (parse_index_expr(field, "upgrade[", 4, &idx, &rest)) {
            size_t base = 20 + idx * 12;
            if (strcmp(rest, ".upgrade_type") == 0) {
                *offset_out = base + 0; *width_out = 4; return 0;
            }
            if (strcmp(rest, ".upgrade_status") == 0) {
                *offset_out = base + 4; *width_out = 4; return 0;
            }
            if (strcmp(rest, ".upgrade_part_flag") == 0) {
                *offset_out = base + 8; *width_out = 4; return 0;
            }
        }
        if (parse_index_expr(field, "resv[", 8, &idx, &rest) && *rest == '\0') {
            *offset_out = 68 + idx * 4; *width_out = 4; return 0;
        }
        if (strcmp(field, "crc") == 0) {
            *offset_out = 100; *width_out = 4; return 0;
        }
        return -1;
    }

    if (region->kind == SECTION_BOOT_IMAGE || region->kind == SECTION_RECOVERY_IMAGE) {
        uint32_t idx;
        const char *rest;

        if (strcmp(field, "head_magic") == 0) {
            *offset_out = 0; *width_out = 4; return 0;
        }
        if (strcmp(field, "version") == 0) {
            *offset_out = 4; *width_out = 4; return 0;
        }
        if (strcmp(field, "size") == 0) {
            *offset_out = 8; *width_out = 4; return 0;
        }
        if (strcmp(field, "partition_name") == 0) {
            *offset_out = 12; *str_width_out = 64; return 0;
        }
        if (strcmp(field, "component_count") == 0) {
            *offset_out = 76; *width_out = 4; return 0;
        }
        if (parse_index_expr(field, "image[", MAX_IMAGE_COUNT, &idx, &rest)) {
            size_t base = 80 + idx * 64;
            if (strcmp(rest, ".component_type") == 0) {
                *offset_out = base + 0; *width_out = 4; return 0;
            }
            if (strcmp(rest, ".component_name") == 0) {
                *offset_out = base + 4; *str_width_out = 20; return 0;
            }
            if (strcmp(rest, ".offset") == 0) {
                *offset_out = base + 24; *width_out = 8; return 0;
            }
            if (strcmp(rest, ".data_size") == 0) {
                *offset_out = base + 32; *width_out = 8; return 0;
            }
            if (strcmp(rest, ".max_size") == 0) {
                *offset_out = base + 40; *width_out = 8; return 0;
            }
            if (strcmp(rest, ".rec[0]") == 0) {
                *offset_out = base + 48; *width_out = 8; return 0;
            }
            if (strcmp(rest, ".rec[1]") == 0) {
                *offset_out = base + 56; *width_out = 8; return 0;
            }
        }
        if (strcmp(field, "crc") == 0) {
            *offset_out = 976; *width_out = 4; return 0;
        }
        return -1;
    }

    return -1;
}

static struct blob_region *resolve_field_region(struct layout_image *layout, const char *field_path,
                                                const char **field_suffix_out)
{
    struct blob_region *candidates[] = {
        &layout->part_info[0], &layout->part_info[1],
        &layout->part_ctrl[0], &layout->part_ctrl[1],
        &layout->boot[0], &layout->boot[1],
        &layout->recovery[0], &layout->recovery[1],
    };
    size_t i;

    for (i = 0; i < ARRAY_SIZE(candidates); ++i) {
        size_t len = strlen(candidates[i]->label);
        if (strncmp(field_path, candidates[i]->label, len) == 0 && field_path[len] == '.') {
            if (field_suffix_out != NULL) {
                *field_suffix_out = field_path + len + 1;
            }
            return candidates[i];
        }
    }
    return NULL;
}

static void print_field_value(const struct blob_region *region, size_t offset, size_t width, size_t str_width)
{
    if (str_width != 0) {
        char text[65];
        size_t buf_size = str_width + 1;
        char *dynamic_text = NULL;
        char *out = text;

        if (buf_size > sizeof(text)) {
            dynamic_text = calloc(1, buf_size);
            if (dynamic_text == NULL) {
                fprintf(stderr, "allocation failed\n");
                return;
            }
            out = dynamic_text;
        }
        trim_ascii_copy(out, buf_size, region->raw + offset, str_width);
        printf("%s\n", out);
        free(dynamic_text);
        return;
    }

    if (width == 4) {
        uint32_t value = read_le32(region->raw + offset);
        printf("0x%08" PRIx32 " (%" PRIu32 ")\n", value, value);
        return;
    }
    if (width == 8) {
        uint64_t value = read_le64(region->raw + offset);
        printf("0x%016" PRIx64 " (%" PRIu64 ")\n", value, value);
        return;
    }
    fprintf(stderr, "unsupported field width %zu\n", width);
}

static int update_region_crc(struct blob_region *region)
{
    size_t crc_off = region_crc_offset(region->kind);
    uint16_t crc16;

    if (!region_crc_is_hboot_verified(region->kind)) {
        return 0;
    }

    if (crc_off + 4 > region->raw_size) {
        fprintf(stderr, "%s: invalid crc field offset\n", region->label);
        return -1;
    }

    write_le32(region->raw + crc_off, 0);
    crc16 = crc16_ccitt(0, region->raw, crc_off);
    write_le32(region->raw + crc_off, (uint32_t)crc16);
    return 0;
}

static int set_field_value(struct blob_region *region, const char *field_suffix, const char *value_text)
{
    size_t offset;
    size_t width;
    size_t str_width;
    uint64_t value;

    if (locate_field(region, field_suffix, &offset, &width, &str_width) != 0) {
        fprintf(stderr, "unknown field path: %s.%s\n", region->label, field_suffix);
        return -1;
    }

    if (str_width != 0) {
        size_t len = strlen(value_text);
        if (len > str_width) {
            fprintf(stderr, "value too long for %s.%s: max %zu bytes\n",
                    region->label, field_suffix, str_width);
            return -1;
        }
        copy_padded_ascii(region->raw + offset, str_width, value_text);
        return update_region_crc(region);
    }

    if (parse_integer_arg(value_text, &value) != 0) {
        fprintf(stderr, "invalid numeric value: %s\n", value_text);
        return -1;
    }

    if (width == 4) {
        if (value > UINT32_MAX) {
            fprintf(stderr, "value out of range for 32-bit field: %s\n", value_text);
            return -1;
        }
        write_le32(region->raw + offset, (uint32_t)value);
    } else if (width == 8) {
        write_le64(region->raw + offset, value);
    } else {
        fprintf(stderr, "unsupported writable width %zu\n", width);
        return -1;
    }

    return update_region_crc(region);
}

static int flush_region(int fd, const struct blob_region *region)
{
    return write_full_at(fd, region->raw, region->raw_size, region->absolute_offset);
}

static void usage(FILE *stream)
{
    fprintf(stream,
            "Usage:\n"
            "  bootmeta_tool dump [--layout auto|whole-disk|raw-boot] [--base-offset N] <path>\n"
            "  bootmeta_tool get  [--layout auto|whole-disk|raw-boot] [--base-offset N] <path> <field>\n"
            "  bootmeta_tool set  [--layout auto|whole-disk|raw-boot] [--base-offset N] <path> <field> <value>\n"
            "\n"
            "Field path examples:\n"
            "  part_info.main.partition_count\n"
            "  part_ctrl.back.upgrade[0].upgrade_status\n"
            "  boot.main.partition_name\n"
            "  boot.back.image[1].offset\n"
            "  recovery.main.image[2].max_size\n"
            "\n"
            "Notes:\n"
            "  auto probes both whole-disk and raw-boot interpretations.\n"
            "  whole-disk reads metadata at absolute offsets 0x100000/0x120000/...\n"
            "  raw-boot treats the blob start as metadata base, so offsets become 0x0/0x200/0x20000/...\n"
            "  part_info/part_ctrl CRC is CRC16-CCITT(start=0) over bytes before the Crc field.\n"
            "  boot/recovery trailing crc field is opaque in hboot and is left unchanged unless explicitly set.\n");
}

static int parse_layout_mode(const char *mode, enum layout_mode_kind *layout_mode,
                             uint64_t *base_offset, bool *base_explicit)
{
    if (strcmp(mode, "auto") == 0) {
        *layout_mode = LAYOUT_AUTO;
        if (!*base_explicit) {
            *base_offset = REGION_BASE_DEFAULT;
        }
        return 0;
    }
    if (strcmp(mode, "whole-disk") == 0) {
        *layout_mode = LAYOUT_WHOLE_DISK;
        if (!*base_explicit) {
            *base_offset = REGION_BASE_DEFAULT;
        }
        return 0;
    }
    if (strcmp(mode, "raw-boot") == 0) {
        *layout_mode = LAYOUT_RAW_BOOT;
        if (!*base_explicit) {
            *base_offset = REGION_BASE_DEFAULT;
        }
        return 0;
    }
    return -1;
}

int main(int argc, char **argv)
{
    enum command_kind cmd;
    const char *path;
    const char *field = NULL;
    const char *value = NULL;
    const char *layout_mode_name = "auto";
    enum layout_mode_kind layout_mode = LAYOUT_AUTO;
    uint64_t base_offset = REGION_BASE_DEFAULT;
    bool base_explicit = false;
    int fd;
    int open_flags;
    struct layout_image layout;
    int argi = 1;

    if (argc < 3) {
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

    while (argi < argc && strncmp(argv[argi], "--", 2) == 0) {
        if (strcmp(argv[argi], "--layout") == 0) {
            if (argi + 1 >= argc ||
                parse_layout_mode(argv[argi + 1], &layout_mode, &base_offset, &base_explicit) != 0) {
                fprintf(stderr, "invalid --layout value\n");
                return 1;
            }
            layout_mode_name = argv[argi + 1];
            argi += 2;
            continue;
        }
        if (strcmp(argv[argi], "--base-offset") == 0) {
            if (argi + 1 >= argc || parse_integer_arg(argv[argi + 1], &base_offset) != 0) {
                fprintf(stderr, "invalid --base-offset value\n");
                return 1;
            }
            base_explicit = true;
            argi += 2;
            continue;
        }
        fprintf(stderr, "unknown option: %s\n", argv[argi]);
        return 1;
    }

    if ((cmd == CMD_DUMP && argc - argi != 1) ||
        (cmd == CMD_GET && argc - argi != 2) ||
        (cmd == CMD_SET && argc - argi != 3)) {
        usage(stderr);
        return 1;
    }

    path = argv[argi++];
    if (cmd != CMD_DUMP) {
        field = argv[argi++];
    }
    if (cmd == CMD_SET) {
        value = argv[argi++];
    }

    open_flags = (cmd == CMD_SET) ? O_RDWR : O_RDONLY;
    fd = open(path, open_flags);
    if (fd < 0) {
        perror(path);
        return 1;
    }

    if (!base_explicit && layout_mode == LAYOUT_AUTO) {
        int probe_score = probe_layout_choice(fd, &layout_mode, &base_offset);
        if (probe_score < 0) {
            perror("probe metadata");
            close(fd);
            return 1;
        }
        fprintf(stderr, "auto-detected layout=%s base_offset=0x%llx (probe score=%d)\n",
                layout_mode == LAYOUT_RAW_BOOT ? "raw-boot" : "whole-disk",
                (unsigned long long)base_offset, probe_score);
    }

    (void)layout_mode_name;
    init_layout(&layout, layout_mode, base_offset);
    if (load_layout_regions(fd, &layout) != 0) {
        perror("read metadata");
        close(fd);
        return 1;
    }

    if (cmd == CMD_DUMP) {
        dump_layout(&layout);
        close(fd);
        return 0;
    }

    if (cmd == CMD_GET) {
        struct blob_region *region;
        size_t offset = 0;
        size_t width = 0;
        size_t str_width = 0;
        const char *field_suffix = NULL;

        region = resolve_field_region(&layout, field, &field_suffix);
        if (region == NULL) {
            fprintf(stderr, "unknown region in field path: %s\n", field);
            close(fd);
            return 1;
        }
        if (locate_field(region, field_suffix, &offset, &width, &str_width) != 0) {
            fprintf(stderr, "unknown field path: %s\n", field);
            close(fd);
            return 1;
        }
        print_field_value(region, offset, width, str_width);
        close(fd);
        return 0;
    }

    if (cmd == CMD_SET) {
        struct blob_region *region;
        const char *field_suffix = NULL;

        region = resolve_field_region(&layout, field, &field_suffix);
        if (region == NULL) {
            fprintf(stderr, "unknown region in field path: %s\n", field);
            close(fd);
            return 1;
        }
        if (set_field_value(region, field_suffix, value) != 0) {
            close(fd);
            return 1;
        }
        if (flush_region(fd, region) != 0) {
            perror("write metadata");
            close(fd);
            return 1;
        }
        if (fsync(fd) != 0) {
            perror("fsync");
            close(fd);
            return 1;
        }
        close(fd);
        return 0;
    }

    close(fd);
    return 1;
}
