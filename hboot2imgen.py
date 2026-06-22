#!/usr/bin/env python3
"""
Generate an Ascend310B hboot2 metadata + payload raw image from JSON/JSONC.

The output file starts at disk offset 0x100000 by default. Write it with:

    sudo dd if=hboot2.raw of=/dev/sdX bs=1M seek=1 conv=fsync,notrunc

Offsets stored in JSON/JSONC and in metadata are hboot2 disk-absolute offsets.
Payload bytes are placed in the output file at offset - base_offset.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


HEAD_MAGIC_NUM = 0x55AA55AA
MAX_IMAGE_COUNT = 14

DEFAULT_BASE_OFFSET = 0x100000
DEFAULT_OUTPUT_SIZE = 0x320000 - DEFAULT_BASE_OFFSET
DEFAULT_PAYLOAD_START_MIN = 0x320000
DEFAULT_PAYLOAD_ALIGNMENT = 0x100000
FIXED_CRL_OFFSET = 0x220000
FIXED_CRL_MAX_SIZE = 0x80000

PART_INFO_HEAD_OFFSET_MAIN = 0x100000
PART_CTRL_HEAD_OFFSET_MAIN = 0x100200
PART_INFO_HEAD_OFFSET_BACK = 0x110000
PART_CTRL_HEAD_OFFSET_BACK = 0x110200
BOOT_IMAGE_OFFSET_MAIN = 0x120000
BOOT_IMAGE_OFFSET_BACK = 0x120400
RECOV_IMAGE_OFFSET_MAIN = 0x120800
RECOV_IMAGE_OFFSET_BACK = 0x120C00

PART_INFO_REGION_SIZE = 0x200
PART_CTRL_REGION_SIZE = 0x200
IMAGE_INFO_REGION_SIZE = 0x400

PART_INFO_STRUCT_SIZE = 56
PART_CTRL_STRUCT_SIZE = 104
PART_IMAGE_STRUCT_SIZE = 980

CRC_PART_INFO = 52
CRC_PART_CTRL = 100
CRC_PART_IMAGE = 976


class ConfigError(Exception):
    pass


@dataclass(frozen=True)
class WriteRegion:
    label: str
    disk_offset: int
    data: bytes
    kind: str


@dataclass(frozen=True)
class Payload:
    label: str
    path: Path
    disk_offset: int
    size: int
    reserved_size: int
    max_size: int | None


@dataclass
class LayoutAllocator:
    cursor: int
    alignment: int

    def allocate(self, label: str, reserved_size: int, alignment: int | None = None) -> int:
        effective_alignment = alignment if alignment is not None else self.alignment
        offset = align_up(self.cursor, effective_alignment, f"{label}.alignment")
        self.cursor = offset + reserved_size
        return offset


NO_DEFAULT = object()
MISSING = object()


def warn(message: str) -> None:
    print(f"warning: {message}", file=sys.stderr)


def is_auto(value: Any) -> bool:
    return isinstance(value, str) and value.strip().lower() in {"auto", "file-size", "file_size"}


def align_up(value: int, alignment: int, field: str = "alignment") -> int:
    if alignment <= 0:
        raise ConfigError(f"{field}: alignment must be greater than zero")
    return ((value + alignment - 1) // alignment) * alignment


def parse_int(value: Any, field: str) -> int:
    if isinstance(value, bool):
        raise ConfigError(f"{field}: boolean is not a valid integer")
    if isinstance(value, int):
        if value < 0:
            raise ConfigError(f"{field}: negative integer is not allowed")
        return value
    if not isinstance(value, str):
        raise ConfigError(f"{field}: expected integer or numeric string")

    text = value.strip().replace("_", "")
    if not text:
        raise ConfigError(f"{field}: empty numeric string")

    lower = text.lower()
    multiplier = 1
    suffixes = (
        ("gib", 1024**3), ("gb", 1024**3), ("g", 1024**3),
        ("mib", 1024**2), ("mb", 1024**2), ("m", 1024**2),
        ("kib", 1024), ("kb", 1024), ("k", 1024),
    )
    for suffix, factor in suffixes:
        if lower.endswith(suffix):
            multiplier = factor
            text = text[: -len(suffix)]
            break

    try:
        number = int(text, 0)
    except ValueError as exc:
        raise ConfigError(f"{field}: invalid integer {value!r}") from exc

    if number < 0:
        raise ConfigError(f"{field}: negative integer is not allowed")
    return number * multiplier


def parse_int_default(obj: dict[str, Any], names: Iterable[str], default: int, field: str) -> int:
    value = get_any(obj, names, default)
    if is_auto(value):
        raise ConfigError(f"{field}: auto is not valid here")
    return parse_int(value, field)


def parse_optional_int_auto(
    obj: dict[str, Any],
    names: Iterable[str],
    field: str,
) -> int | None:
    value = get_any(obj, names, MISSING)
    if value is MISSING or value is None or is_auto(value):
        return None
    return parse_int(value, field)


def get_any(obj: dict[str, Any], names: Iterable[str], default: Any = NO_DEFAULT) -> Any:
    for name in names:
        if name in obj:
            return obj[name]
    if default is NO_DEFAULT:
        raise ConfigError(f"missing required field: {'/'.join(names)}")
    return default


def get_section(config: dict[str, Any], *names: str) -> dict[str, Any]:
    value = get_any(config, names, {})
    if value is None:
        return {}
    if not isinstance(value, dict):
        raise ConfigError(f"{'/'.join(names)}: expected object")
    return value


def get_side(container: dict[str, Any], side: str) -> dict[str, Any]:
    aliases = {
        "main": ("main", "Main", "MAIN"),
        "back": ("back", "Back", "BACK"),
    }[side]
    value = get_any(container, aliases, {})
    if value is None:
        return {}
    if not isinstance(value, dict):
        raise ConfigError(f"{side}: expected object")
    return value


def get_path_value(obj: dict[str, Any]) -> Any:
    return get_any(obj, ("path", "img_path", "image_path", "img", "file", "Path"), None)


def crc16_ccitt(start_crc: int, data: bytes | bytearray) -> int:
    crc = start_crc & 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def write_u32(buf: bytearray, offset: int, value: int, field: str) -> None:
    if value > 0xFFFFFFFF:
        raise ConfigError(f"{field}: value 0x{value:x} does not fit in UINT32")
    buf[offset:offset + 4] = value.to_bytes(4, "little")


def write_u64(buf: bytearray, offset: int, value: int, field: str) -> None:
    if value > 0xFFFFFFFFFFFFFFFF:
        raise ConfigError(f"{field}: value 0x{value:x} does not fit in UINT64")
    buf[offset:offset + 8] = value.to_bytes(8, "little")


def write_ascii(buf: bytearray, offset: int, width: int, value: Any, field: str) -> None:
    if value is None:
        value = ""
    if not isinstance(value, str):
        raise ConfigError(f"{field}: expected string")
    try:
        raw = value.encode("ascii")
    except UnicodeEncodeError as exc:
        raise ConfigError(f"{field}: only ASCII strings are supported") from exc
    if len(raw) > width:
        raise ConfigError(f"{field}: string is {len(raw)} bytes, max {width}")
    buf[offset:offset + width] = b"\x00" * width
    buf[offset:offset + len(raw)] = raw


def write_crc_field(buf: bytearray, crc_offset: int, value: Any, default_auto: bool, label: str) -> None:
    if value is MISSING:
        value = "auto" if default_auto else 0

    if is_auto(value):
        write_u32(buf, crc_offset, 0, f"{label}.crc")
        write_u32(buf, crc_offset, crc16_ccitt(0, buf[:crc_offset]), f"{label}.crc")
    else:
        write_u32(buf, crc_offset, parse_int(value, f"{label}.crc"), f"{label}.crc")


def normalize_resv(value: Any, count: int, field: str) -> list[int]:
    if value is None:
        return [0] * count
    if not isinstance(value, list):
        raise ConfigError(f"{field}: expected array with {count} integers")
    if len(value) > count:
        raise ConfigError(f"{field}: has {len(value)} entries, max {count}")
    out = [parse_int(v, f"{field}[{idx}]") for idx, v in enumerate(value)]
    out.extend([0] * (count - len(out)))
    return out


def normalize_entries(value: Any, max_count: int, field: str) -> list[dict[str, Any]]:
    entries: list[dict[str, Any]] = [{} for _ in range(max_count)]
    if value is None:
        return entries

    if isinstance(value, list):
        if len(value) > max_count:
            raise ConfigError(f"{field}: has {len(value)} entries, max {max_count}")
        for idx, item in enumerate(value):
            if item is None:
                item = {}
            if not isinstance(item, dict):
                raise ConfigError(f"{field}[{idx}]: expected object")
            entries[idx] = item
        return entries

    if isinstance(value, dict):
        for key, item in value.items():
            try:
                idx = int(str(key), 0)
            except ValueError as exc:
                raise ConfigError(f"{field}: invalid index key {key!r}") from exc
            if idx < 0 or idx >= max_count:
                raise ConfigError(f"{field}: index {idx} outside 0..{max_count - 1}")
            if item is None:
                item = {}
            if not isinstance(item, dict):
                raise ConfigError(f"{field}[{idx}]: expected object")
            entries[idx] = item
        return entries

    raise ConfigError(f"{field}: expected array or object")


def resolve_input_path(config_dir: Path, value: Any, field: str) -> Path | None:
    if value in (None, ""):
        return None
    if not isinstance(value, str):
        raise ConfigError(f"{field}: expected path string")
    path = Path(value)
    if not path.is_absolute():
        path = config_dir / path
    return path


def pack_part_info(cfg: dict[str, Any], label: str) -> bytes:
    buf = bytearray(PART_INFO_REGION_SIZE)
    write_u32(buf, 0, parse_int_default(cfg, ("head_magic", "HeadMagic"), HEAD_MAGIC_NUM, f"{label}.head_magic"),
              f"{label}.head_magic")
    write_u32(buf, 4, parse_int_default(cfg, ("version", "Version"), 100, f"{label}.version"), f"{label}.version")
    write_u32(buf, 8, parse_int_default(cfg, ("size", "Size"), PART_INFO_STRUCT_SIZE, f"{label}.size"),
              f"{label}.size")
    write_u32(buf, 12, parse_int_default(cfg, ("partition_count", "PartitionCount"), 0, f"{label}.partition_count"),
              f"{label}.partition_count")
    write_u32(buf, 16, parse_int_default(cfg, ("component_map", "ComponentMap"), 0, f"{label}.component_map"),
              f"{label}.component_map")

    for idx, value in enumerate(normalize_resv(get_any(cfg, ("resv", "Resv"), None), 8, f"{label}.resv")):
        write_u32(buf, 20 + idx * 4, value, f"{label}.resv[{idx}]")

    write_crc_field(buf, CRC_PART_INFO, get_any(cfg, ("crc", "Crc"), MISSING), True, label)
    return bytes(buf)


def pack_part_ctrl(cfg: dict[str, Any], label: str) -> bytes:
    buf = bytearray(PART_CTRL_REGION_SIZE)
    write_u32(buf, 0, parse_int_default(cfg, ("head_magic", "HeadMagic"), HEAD_MAGIC_NUM, f"{label}.head_magic"),
              f"{label}.head_magic")
    write_u32(buf, 4, parse_int_default(cfg, ("version", "Version"), 100, f"{label}.version"), f"{label}.version")
    write_u32(buf, 8, parse_int_default(cfg, ("size", "Size"), PART_CTRL_STRUCT_SIZE, f"{label}.size"),
              f"{label}.size")
    write_u32(buf, 12,
              parse_int_default(cfg, ("force_recovery_flag", "ForceRecoveryFlag"), 0, f"{label}.force_recovery_flag"),
              f"{label}.force_recovery_flag")
    write_u32(buf, 16,
              parse_int_default(cfg, ("upgrade_part_count", "UpgradePartCount"), 4, f"{label}.upgrade_part_count"),
              f"{label}.upgrade_part_count")

    upgrades = normalize_entries(get_any(cfg, ("upgrade", "UpgradeCtrl"), None), 4, f"{label}.upgrade")
    for idx, entry in enumerate(upgrades):
        base = 20 + idx * 12
        write_u32(buf, base + 0,
                  parse_int_default(entry, ("upgrade_type", "UpgradeType"), 0,
                                    f"{label}.upgrade[{idx}].upgrade_type"),
                  f"{label}.upgrade[{idx}].upgrade_type")
        write_u32(buf, base + 4,
                  parse_int_default(entry, ("upgrade_status", "UpgradeStatus"), 0,
                                    f"{label}.upgrade[{idx}].upgrade_status"),
                  f"{label}.upgrade[{idx}].upgrade_status")
        write_u32(buf, base + 8,
                  parse_int_default(entry, ("upgrade_part_flag", "UpgradePartFlag"), 0,
                                    f"{label}.upgrade[{idx}].upgrade_part_flag"),
                  f"{label}.upgrade[{idx}].upgrade_part_flag")

    for idx, value in enumerate(normalize_resv(get_any(cfg, ("resv", "Resv"), None), 8, f"{label}.resv")):
        write_u32(buf, 68 + idx * 4, value, f"{label}.resv[{idx}]")

    write_crc_field(buf, CRC_PART_CTRL, get_any(cfg, ("crc", "Crc"), MISSING), True, label)
    return bytes(buf)


def component_count_default(images: list[dict[str, Any]]) -> int:
    highest = -1
    for idx, entry in enumerate(images):
        if entry:
            highest = idx
    return highest + 1


def pack_image_header(
    cfg: dict[str, Any],
    label: str,
    config_dir: Path,
    allocator: LayoutAllocator,
    payloads: list[Payload],
) -> bytes:
    buf = bytearray(IMAGE_INFO_REGION_SIZE)
    images = normalize_entries(get_any(cfg, ("image", "images", "ImageInfo"), None), MAX_IMAGE_COUNT, f"{label}.image")
    component_count = parse_int_default(
        cfg,
        ("component_count", "ComponentCount"),
        component_count_default(images),
        f"{label}.component_count",
    )
    if component_count > MAX_IMAGE_COUNT:
        raise ConfigError(f"{label}.component_count: {component_count} exceeds {MAX_IMAGE_COUNT}")

    write_u32(buf, 0, parse_int_default(cfg, ("head_magic", "HeadMagic"), HEAD_MAGIC_NUM, f"{label}.head_magic"),
              f"{label}.head_magic")
    write_u32(buf, 4, parse_int_default(cfg, ("version", "Version"), 100, f"{label}.version"), f"{label}.version")
    write_u32(buf, 8, parse_int_default(cfg, ("size", "Size"), PART_IMAGE_STRUCT_SIZE, f"{label}.size"),
              f"{label}.size")
    write_ascii(buf, 12, 64, get_any(cfg, ("partition_name", "PartitionName"), ""), f"{label}.partition_name")
    write_u32(buf, 76, component_count, f"{label}.component_count")

    for idx, entry in enumerate(images):
        base = 80 + idx * 64
        entry_label = f"{label}.image[{idx}]"
        image_path = resolve_input_path(config_dir, get_path_value(entry), f"{entry_label}.path")
        image_size: int | None = None

        if image_path is not None:
            if not image_path.is_file():
                raise ConfigError(f"{entry_label}.path: file does not exist: {image_path}")
            image_size = image_path.stat().st_size

        offset = parse_optional_int_auto(entry, ("offset", "Offset"), f"{entry_label}.offset")
        max_size = parse_optional_int_auto(entry, ("max_size", "MaxSize"), f"{entry_label}.max_size")
        entry_alignment = parse_optional_int_auto(
            entry,
            ("alignment", "align", "Alignment", "Align"),
            f"{entry_label}.alignment",
        )

        data_size_value = get_any(entry, ("data_size", "DataSize"), MISSING)
        if image_size is not None and (data_size_value is MISSING or is_auto(data_size_value)):
            data_size = image_size
        elif data_size_value is MISSING:
            data_size = 0
        else:
            data_size = parse_int(data_size_value, f"{entry_label}.data_size")
            if image_size is not None and data_size != image_size:
                warn(f"{entry_label}.data_size is {data_size}, but file size is {image_size}")

        reserved_size = 0
        if image_path is not None:
            if idx >= component_count:
                raise ConfigError(f"{entry_label}.path is outside component_count={component_count}")
            if image_size is None:
                raise AssertionError("image_size not set")
            if max_size is None:
                align = entry_alignment if entry_alignment is not None else allocator.alignment
                max_size = align_up(image_size, align, f"{entry_label}.max_size")
            if max_size == 0:
                raise ConfigError(f"{entry_label}.max_size is zero but path is set")
            if image_size > max_size:
                raise ConfigError(
                    f"{entry_label}.path is too large: {image_size} bytes > max_size {max_size}"
                )
            reserved_size = max_size
            if offset is None:
                offset = allocator.allocate(entry_label, reserved_size, entry_alignment)
        else:
            if offset is None:
                offset = 0
            if max_size is None:
                max_size = 0

        write_u32(buf, base + 0,
                  parse_int_default(entry, ("component_type", "ComponentType"), 0,
                                    f"{entry_label}.component_type"),
                  f"{entry_label}.component_type")
        write_ascii(buf, base + 4, 20, get_any(entry, ("component_name", "ComponentName"), ""),
                    f"{entry_label}.component_name")
        write_u64(buf, base + 24, offset, f"{entry_label}.offset")
        write_u64(buf, base + 32, data_size, f"{entry_label}.data_size")
        write_u64(buf, base + 40, max_size, f"{entry_label}.max_size")

        rec_values = normalize_resv(get_any(entry, ("rec", "Rec"), None), 2, f"{entry_label}.rec")
        write_u64(buf, base + 48, rec_values[0], f"{entry_label}.rec[0]")
        write_u64(buf, base + 56, rec_values[1], f"{entry_label}.rec[1]")

        if image_path is not None:
            if offset == 0:
                raise ConfigError(f"{entry_label}.offset is zero but path is set")
            payloads.append(Payload(entry_label, image_path, offset, image_size, reserved_size, max_size))

    write_crc_field(buf, CRC_PART_IMAGE, get_any(cfg, ("crc", "Crc"), MISSING), False, label)
    return bytes(buf)


def payloads_from_extra(config: dict[str, Any], config_dir: Path, allocator: LayoutAllocator) -> list[Payload]:
    raw = get_any(config, ("payloads", "extra_payloads", "Payloads"), [])
    if raw is None:
        return []
    if not isinstance(raw, list):
        raise ConfigError("payloads: expected array")

    out: list[Payload] = []
    for idx, entry in enumerate(raw):
        label = f"payloads[{idx}]"
        if not isinstance(entry, dict):
            raise ConfigError(f"{label}: expected object")
        name = get_any(entry, ("name", "Name"), label)
        if not isinstance(name, str):
            raise ConfigError(f"{label}.name: expected string")
        path = resolve_input_path(config_dir, get_any(entry, ("path", "Path", "file"), None), f"{label}.path")
        if path is None:
            raise ConfigError(f"{label}.path: required")
        if not path.is_file():
            raise ConfigError(f"{label}.path: file does not exist: {path}")
        offset = parse_optional_int_auto(entry, ("offset", "Offset"), f"{label}.offset")
        max_size = parse_optional_int_auto(entry, ("max_size", "MaxSize"), f"{label}.max_size")
        entry_alignment = parse_optional_int_auto(
            entry,
            ("alignment", "align", "Alignment", "Align"),
            f"{label}.alignment",
        )
        size = path.stat().st_size
        if name.lower() == "crl":
            if offset is None:
                offset = FIXED_CRL_OFFSET
            if max_size is None:
                max_size = FIXED_CRL_MAX_SIZE
        if max_size is None:
            align = entry_alignment if entry_alignment is not None else allocator.alignment
            max_size = align_up(size, align, f"{label}.max_size")
        if max_size is not None and size > max_size:
            raise ConfigError(f"{label}.path is too large: {size} bytes > max_size {max_size}")
        if offset is None:
            offset = allocator.allocate(name, max_size, entry_alignment)
        if offset == 0:
            raise ConfigError(f"{label}.offset is zero but path is set")
        out.append(Payload(name, path, offset, size, max_size, max_size))
    return out


def disk_to_output_offset(disk_offset: int, base_offset: int, label: str) -> int:
    if disk_offset < base_offset:
        raise ConfigError(
            f"{label}: disk offset 0x{disk_offset:x} is before output base 0x{base_offset:x}"
        )
    return disk_offset - base_offset


def add_region(
    writes: list[WriteRegion],
    label: str,
    disk_offset: int,
    data: bytes,
) -> None:
    writes.append(WriteRegion(label, disk_offset, data, "metadata"))


def build_image(
    config: dict[str, Any],
    config_dir: Path,
    allocator: LayoutAllocator,
) -> tuple[list[WriteRegion], list[Payload]]:
    writes: list[WriteRegion] = []
    payloads: list[Payload] = []

    part_info = get_section(config, "part_info", "PartInfo")
    part_ctrl = get_section(config, "part_ctrl", "PartCtrl")
    boot = get_section(config, "boot", "Boot")
    recovery = get_section(config, "recovery", "Recovery")

    add_region(writes, "part_info.main", PART_INFO_HEAD_OFFSET_MAIN,
               pack_part_info(get_side(part_info, "main"), "part_info.main"))
    add_region(writes, "part_ctrl.main", PART_CTRL_HEAD_OFFSET_MAIN,
               pack_part_ctrl(get_side(part_ctrl, "main"), "part_ctrl.main"))
    add_region(writes, "part_info.back", PART_INFO_HEAD_OFFSET_BACK,
               pack_part_info(get_side(part_info, "back"), "part_info.back"))
    add_region(writes, "part_ctrl.back", PART_CTRL_HEAD_OFFSET_BACK,
               pack_part_ctrl(get_side(part_ctrl, "back"), "part_ctrl.back"))

    add_region(writes, "boot.main", BOOT_IMAGE_OFFSET_MAIN,
               pack_image_header(get_side(boot, "main"), "boot.main", config_dir, allocator, payloads))
    add_region(writes, "boot.back", BOOT_IMAGE_OFFSET_BACK,
               pack_image_header(get_side(boot, "back"), "boot.back", config_dir, allocator, payloads))
    add_region(writes, "recovery.main", RECOV_IMAGE_OFFSET_MAIN,
               pack_image_header(get_side(recovery, "main"), "recovery.main", config_dir, allocator, payloads))
    add_region(writes, "recovery.back", RECOV_IMAGE_OFFSET_BACK,
               pack_image_header(get_side(recovery, "back"), "recovery.back", config_dir, allocator, payloads))

    payloads.extend(payloads_from_extra(config, config_dir, allocator))
    return writes, payloads


def interval_list(
    writes: list[WriteRegion],
    payloads: list[Payload],
    base_offset: int,
) -> list[tuple[int, int, str, str]]:
    intervals: list[tuple[int, int, str, str]] = []
    for write in writes:
        start = disk_to_output_offset(write.disk_offset, base_offset, write.label)
        intervals.append((start, start + len(write.data), write.label, write.kind))
    for payload in payloads:
        start = disk_to_output_offset(payload.disk_offset, base_offset, payload.label)
        intervals.append((start, start + payload.reserved_size, payload.label, "payload"))
    return intervals


def validate_intervals(intervals: list[tuple[int, int, str, str]], allow_overlap: bool) -> None:
    if allow_overlap:
        return

    ordered = sorted(intervals)
    for prev, cur in zip(ordered, ordered[1:]):
        prev_start, prev_end, prev_label, prev_kind = prev
        cur_start, cur_end, cur_label, cur_kind = cur
        if cur_start < prev_end:
            raise ConfigError(
                f"overlap: {cur_label} ({cur_kind}) 0x{cur_start:x}..0x{cur_end:x} overlaps "
                f"{prev_label} ({prev_kind}) 0x{prev_start:x}..0x{prev_end:x}"
            )


def copy_file_at(src: Path, dst, offset: int) -> None:
    with src.open("rb") as in_file:
        dst.seek(offset)
        shutil.copyfileobj(in_file, dst, length=1024 * 1024)


def write_output(
    output: Path,
    writes: list[WriteRegion],
    payloads: list[Payload],
    base_offset: int,
    output_size: int,
) -> None:
    with output.open("wb") as out_file:
        out_file.truncate(output_size)
        for write in writes:
            out_file.seek(disk_to_output_offset(write.disk_offset, base_offset, write.label))
            out_file.write(write.data)
        for payload in payloads:
            copy_file_at(payload.path, out_file, disk_to_output_offset(payload.disk_offset, base_offset, payload.label))
        out_file.flush()
        os.fsync(out_file.fileno())


def print_plan(
    writes: list[WriteRegion],
    payloads: list[Payload],
    base_offset: int,
    output_size: int,
) -> None:
    print(f"base_offset : 0x{base_offset:x} ({base_offset} bytes)")
    print(f"output_size : 0x{output_size:x} ({output_size} bytes)")
    print("")
    print("metadata:")
    for write in sorted(writes, key=lambda item: item.disk_offset):
        rel = disk_to_output_offset(write.disk_offset, base_offset, write.label)
        print(f"  {write.label:18s} disk=0x{write.disk_offset:08x} rel=0x{rel:08x} size=0x{len(write.data):x}")
    print("")
    print("payloads:")
    if not payloads:
        print("  none")
    for payload in sorted(payloads, key=lambda item: item.disk_offset):
        rel = disk_to_output_offset(payload.disk_offset, base_offset, payload.label)
        max_text = "none" if payload.max_size is None else f"0x{payload.max_size:x}"
        print(
            f"  {payload.label:18s} disk=0x{payload.disk_offset:08x} "
            f"rel=0x{rel:08x} size=0x{payload.size:x} "
            f"reserved=0x{payload.reserved_size:x} max={max_text} path={payload.path}"
        )


EXAMPLE_CONFIG: dict[str, Any] = {
    "base_offset": "0x100000",
    "output_size": "0x220000",
    "layout": {
        "payload_start": "auto",
        "payload_alignment": "1M",
    },
    "part_info": {
        "main": {
            "head_magic": "0x55aa55aa",
            "version": 100,
            "size": 56,
            "partition_count": 8,
            "component_map": "0xff",
            "resv": [0, 0, 0, 0, 0, 0, 0, 0],
            "crc": "auto",
        },
        "back": {
            "head_magic": "0x55aa55aa",
            "version": 100,
            "size": 56,
            "partition_count": 8,
            "component_map": "0xff",
            "resv": [0, 0, 0, 0, 0, 0, 0, 0],
            "crc": "auto",
        },
    },
    "part_ctrl": {
        "main": {
            "head_magic": "0x55aa55aa",
            "version": 100,
            "size": 104,
            "force_recovery_flag": 0,
            "upgrade_part_count": 4,
            "upgrade": [
                {"upgrade_type": 0, "upgrade_status": 0, "upgrade_part_flag": 0},
                {"upgrade_type": 0, "upgrade_status": 0, "upgrade_part_flag": 0},
                {"upgrade_type": 0, "upgrade_status": 0, "upgrade_part_flag": 0},
                {"upgrade_type": 0, "upgrade_status": 0, "upgrade_part_flag": 0},
            ],
            "resv": [0, 0, 0, 0, 0, 0, 0, 0],
            "crc": "auto",
        },
        "back": {
            "head_magic": "0x55aa55aa",
            "version": 100,
            "size": 104,
            "force_recovery_flag": 0,
            "upgrade_part_count": 4,
            "upgrade": [
                {"upgrade_type": 0, "upgrade_status": 0, "upgrade_part_flag": 0},
                {"upgrade_type": 0, "upgrade_status": 0, "upgrade_part_flag": 0},
                {"upgrade_type": 0, "upgrade_status": 0, "upgrade_part_flag": 0},
                {"upgrade_type": 0, "upgrade_status": 0, "upgrade_part_flag": 0},
            ],
            "resv": [0, 0, 0, 0, 0, 0, 0, 0],
            "crc": "auto",
        },
    },
    "boot": {
        "main": {
            "head_magic": "0x55aa55aa",
            "version": 100,
            "size": 980,
            "partition_name": "rootfs_a",
            "component_count": 4,
            "image": [
                {
                    "component_type": 0,
                    "component_name": "kernel",
                    "offset": "auto",
                    "data_size": "auto",
                    "max_size": "auto",
                    "rec": [0, 0],
                    "path": "Image.hboot2",
                },
                {
                    "component_type": 0,
                    "component_name": "dtb",
                    "offset": "auto",
                    "data_size": "auto",
                    "max_size": "auto",
                    "rec": [0, 0],
                    "path": "board.dtb",
                },
                {
                    "component_type": 0,
                    "component_name": "tee",
                    "offset": "auto",
                    "data_size": "auto",
                    "max_size": "auto",
                    "rec": [0, 0],
                    "path": "tee.img",
                },
                {
                    "component_type": 0,
                    "component_name": "initrd",
                    "offset": "auto",
                    "data_size": "auto",
                    "max_size": "auto",
                    "rec": [0, 0],
                    "path": "uInitrd",
                },
            ],
            "crc": 0,
        },
        "back": {
            "head_magic": "0x55aa55aa",
            "version": 100,
            "size": 980,
            "partition_name": "rootfs_b",
            "component_count": 4,
            "image": [],
            "crc": 0,
        },
    },
    "recovery": {
        "main": {
            "head_magic": "0x55aa55aa",
            "version": 100,
            "size": 980,
            "partition_name": "recovery_a",
            "component_count": 0,
            "image": [],
            "crc": 0,
        },
        "back": {
            "head_magic": "0x55aa55aa",
            "version": 100,
            "size": 980,
            "partition_name": "recovery_b",
            "component_count": 0,
            "image": [],
            "crc": 0,
        },
    },
    "payloads": [
        {
            "name": "crl",
            "offset": "auto",
            "max_size": "auto",
            "path": "crl.bin",
        }
    ],
}


def strip_json_line_comments(text: str) -> str:
    out: list[str] = []
    in_string = False
    escaped = False
    idx = 0

    while idx < len(text):
        ch = text[idx]
        nxt = text[idx + 1] if idx + 1 < len(text) else ""

        if in_string:
            out.append(ch)
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == "\"":
                in_string = False
            idx += 1
            continue

        if ch == "\"":
            in_string = True
            out.append(ch)
            idx += 1
            continue

        if ch == "/" and nxt == "/":
            while idx < len(text) and text[idx] not in "\r\n":
                idx += 1
            continue

        out.append(ch)
        idx += 1

    return "".join(out)


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as in_file:
        data = json.loads(strip_json_line_comments(in_file.read()))
    if not isinstance(data, dict):
        raise ConfigError("top-level JSON value must be an object")
    return data


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Generate an hboot2 raw metadata + payload image from JSON/JSONC.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "The output starts at --base-offset, 0x100000 by default.\n"
            "Use: sudo dd if=OUTPUT of=/dev/sdX bs=1M seek=1 conv=fsync,notrunc"
        ),
    )
    parser.add_argument("config", nargs="?", help="JSON/JSONC config path")
    parser.add_argument("output", nargs="?", help="output raw image path")
    parser.add_argument("--base-offset", help="disk offset represented by output byte 0; default from config or 0x100000")
    parser.add_argument("--output-size", help="minimum output file size; default from config or 0x220000")
    parser.add_argument("--payload-start", help="first auto-allocated payload disk offset; default 0x400000")
    parser.add_argument("--payload-alignment", help="auto layout alignment; default 1M")
    parser.add_argument("--allow-overlap", action="store_true", help="allow metadata/payload interval overlaps")
    parser.add_argument("--dry-run", action="store_true", help="validate and print plan without writing output")
    parser.add_argument("--example", action="store_true", help="print an example JSON config and exit")
    args = parser.parse_args(argv)

    if args.example:
        print(json.dumps(EXAMPLE_CONFIG, indent=2))
        return 0

    if args.config is None or args.output is None:
        parser.error("config and output are required unless --example is used")

    config_path = Path(args.config).resolve()
    output_path = Path(args.output)
    config = load_json(config_path)
    config_dir = config_path.parent
    layout_config = get_section(config, "layout", "Layout")

    base_value = args.base_offset if args.base_offset is not None else get_any(config, ("base_offset", "BaseOffset"), DEFAULT_BASE_OFFSET)
    base_offset = parse_int(base_value, "base_offset")

    size_value = args.output_size if args.output_size is not None else get_any(config, ("output_size", "OutputSize"), DEFAULT_OUTPUT_SIZE)
    min_output_size = DEFAULT_OUTPUT_SIZE if is_auto(size_value) else parse_int(size_value, "output_size")

    alignment_value = (
        args.payload_alignment if args.payload_alignment is not None
        else get_any(layout_config, ("payload_alignment", "alignment", "PayloadAlignment", "Alignment"),
                     get_any(config, ("payload_alignment", "PayloadAlignment"), DEFAULT_PAYLOAD_ALIGNMENT))
    )
    payload_alignment = parse_int(alignment_value, "payload_alignment")

    default_payload_start = align_up(DEFAULT_PAYLOAD_START_MIN, payload_alignment, "payload_alignment")
    start_value = (
        args.payload_start if args.payload_start is not None
        else get_any(layout_config, ("payload_start", "PayloadStart"),
                     get_any(config, ("payload_start", "PayloadStart"), default_payload_start))
    )
    payload_start = default_payload_start if is_auto(start_value) else parse_int(start_value, "payload_start")
    payload_start = align_up(payload_start, payload_alignment, "payload_alignment")
    allocator = LayoutAllocator(payload_start, payload_alignment)

    writes, payloads = build_image(config, config_dir, allocator)
    intervals = interval_list(writes, payloads, base_offset)
    validate_intervals(intervals, args.allow_overlap)

    required_size = max([min_output_size] + [end for _, end, _, _ in intervals])
    print_plan(writes, payloads, base_offset, required_size)

    if args.dry_run:
        return 0

    write_output(output_path, writes, payloads, base_offset, required_size)
    print(f"\nwrote {output_path} ({required_size} bytes)")
    print(f"dd command: sudo dd if={output_path} of=/dev/sdX bs=1M seek=1 conv=fsync,notrunc")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except ConfigError as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
