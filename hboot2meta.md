# hboot2meta 使用说明与字段解析

## 1. 文档目的

`hboot2meta` 用来直接读取或修改 Ascend 310B `hboot` 使用的启动元数据。它面向两类对象：

- 整块磁盘设备，例如 `/dev/mmcblk1`
- 裁剪出来的 raw boot 区镜像，例如只包含 `0x100000 ~ 0x121000` 一段的 `boot_region.bin`

如果命令里没有显式给出 `<path>`，`hboot2meta` 会优先根据 `/proc/cmdline` 里的 `root=` 参数推断当前启动磁盘；如果推断失败，再回退到常见设备名探测。

这份文档做两件事：

- 说明 `hboot2meta` 的命令行用法
- 结合 `hboot` 源码说明每个字段在实际启动流程里的作用

源码依据主要来自：

- `SdEmmcPartition.h`
- `BootLoaderLib.c`
- `CommonLib.c`

## 2. hboot 眼中的磁盘布局

`hboot` 里对这几块元数据的定义见：

- `PART_INFO_HEAD_OFFSET_MAIN = 0x100000`
- `PART_CTRL_HEAD_OFFSET_MAIN = 0x100200`
- `PART_INFO_HEAD_OFFSET_BACK = 0x110000`
- `PART_CTRL_HEAD_OFFSET_BACK = 0x110200`
- `BOOT_IMAGE_OFFSET_MAIN = 0x120000`
- `BOOT_IMAGE_OFFSET_BACK = 0x120400`
- `RECOV_IMAGE_OFFSET_MAIN = 0x120800`
- `RECOV_IMAGE_OFFSET_BACK = 0x120C00`

对应源码：

- `/mnt/sda1/yihao/opiai/Ascend310B-hboot2-source/drivers/firmware/bios/HwPkg/UEFI/Products/as310b/Common/Include/Library/SdEmmcPartition.h`

从布局注释看，`hboot` 认为：

- `0x100000 ~ 0x100200` 是主分区信息头
- `0x100200 ~ 0x100400` 是主分区控制头
- `0x110000 ~ 0x110200` 是备分区信息头
- `0x110200 ~ 0x110400` 是备分区控制头
- `0x120000 ~ 0x120400` 是 Boot Slot A 目录
- `0x120400 ~ 0x120800` 是 Boot Slot B 目录
- `0x120800 ~ 0x120C00` 是 Recovery A 目录
- `0x120C00 ~ 0x121000` 是 Recovery B 目录
- `0x220000 ~ 0x2A0000` 是 CRL

也就是说，对整盘设备来说，这些偏移本身就是磁盘绝对偏移，不需要额外再加 1 MiB 基址。

## 3. `hboot2meta` 的布局模式

### 3.1 `whole-disk`

用于直接读整盘设备或整盘 `.img`。

此时工具按绝对偏移访问元数据：

- `part_info.main` 读 `0x100000`
- `boot.main` 读 `0x120000`

示例：

```bash
./hboot2meta dump --layout whole-disk /dev/mmcblk1
```

### 3.2 `raw-boot`

用于只裁剪出 boot 元数据区的 blob。

此时 blob 起点视为原盘 `0x100000`，所以工具内部会把：

- 原盘 `0x100000` 映射为 blob `0x000000`
- 原盘 `0x100200` 映射为 blob `0x000200`
- 原盘 `0x120000` 映射为 blob `0x020000`

示例：

```bash
./hboot2meta dump --layout raw-boot boot_region.bin
```

### 3.3 `auto`

自动在 `whole-disk` 和 `raw-boot` 两种解释之间探测。

示例：

```bash
./hboot2meta dump --layout auto /dev/mmcblk1
./hboot2meta dump
```

## 4. 命令行用法

### 4.1 `dump`

打印所有元数据块。

```bash
./hboot2meta dump [--layout auto|whole-disk|raw-boot] [--base-offset N] [path]
```

示例：

```bash
./hboot2meta dump
./hboot2meta dump /dev/mmcblk1
./hboot2meta dump --layout whole-disk disk.img
./hboot2meta dump --layout raw-boot boot_region.bin
```

### 4.2 `get`

读取单个字段。

```bash
./hboot2meta get [--layout auto|whole-disk|raw-boot] [--base-offset N] [path] <field>
```

示例：

```bash
./hboot2meta get part_info.main.partition_count
./hboot2meta get /dev/mmcblk1 part_info.main.partition_count
./hboot2meta get /dev/mmcblk1 part_ctrl.main.upgrade[0].upgrade_status
./hboot2meta get /dev/mmcblk1 boot.main.image[0].offset
```

### 4.3 `set`

修改单个字段，并只回写对应 metadata block。

```bash
./hboot2meta set [--layout auto|whole-disk|raw-boot] [--base-offset N] [path] <field> <value>
```

示例：

```bash
./hboot2meta set boot.main.image[1].offset 0x3800000
./hboot2meta set /dev/mmcblk1 boot.main.partition_name PARTUUID=xxxx
./hboot2meta set /dev/mmcblk1 boot.main.image[1].offset 0x3800000
./hboot2meta set /dev/mmcblk1 part_ctrl.main.force_recovery_flag 0x55AA55AA
```

### 4.4 `flash`

把一个实际镜像文件写入某个 `boot.*.image[i]` 或 `recovery.*.image[i]` 描述的盘上区域。

```bash
./hboot2meta flash [-y] [--layout auto|whole-disk|raw-boot] [--base-offset N] [path] <image-target> <image-file>
```

示例：

```bash
./hboot2meta flash boot.main.image[0] Image
./hboot2meta flash /dev/mmcblk1 boot.main.image[0] Image
./hboot2meta flash -y /dev/mmcblk1 boot.main.image[0] Image
./hboot2meta flash /dev/mmcblk1 boot.back.image[1] dt.img
./hboot2meta flash /dev/mmcblk1 recovery.main.image[3] initrd.img
./hboot2meta flash --layout raw-boot boot_region.bin boot.main.image[2] itrustee.img
```

`flash` 的行为是：

- 先读取目标目录项里的 `ImageInfo[i].Offset`
- 再读取 `ImageInfo[i].MaxSize`
- 检查输入文件大小不能超过 `MaxSize`
- 默认在真正写入前要求人工确认
- 带 `-y` 时跳过确认，适合脚本或 CI
- 把文件原样写到该 `Offset` 对应的位置
- 写完后把该目录项的 `ImageInfo[i].DataSize` 更新为本次文件大小
- 最后打印一个等效的 `dd` 命令，方便你手工复现

## 5. 字段路径规则

### 5.1 分区信息头 `part_info.*`

支持：

- `part_info.main.head_magic`
- `part_info.main.version`
- `part_info.main.size`
- `part_info.main.partition_count`
- `part_info.main.component_map`
- `part_info.main.resv[0]` 到 `resv[7]`
- `part_info.main.crc`

`back` 同理。

### 5.2 分区控制头 `part_ctrl.*`

支持：

- `part_ctrl.main.head_magic`
- `part_ctrl.main.version`
- `part_ctrl.main.size`
- `part_ctrl.main.force_recovery_flag`
- `part_ctrl.main.upgrade_part_count`
- `part_ctrl.main.upgrade[0].upgrade_type`
- `part_ctrl.main.upgrade[0].upgrade_status`
- `part_ctrl.main.upgrade[0].upgrade_part_flag`
- `part_ctrl.main.resv[0]` 到 `resv[7]`
- `part_ctrl.main.crc`

`back` 同理。

### 5.3 启动目录 `boot.*` / `recovery.*`

支持：

- `boot.main.head_magic`
- `boot.main.version`
- `boot.main.size`
- `boot.main.partition_name`
- `boot.main.component_count`
- `boot.main.image[0].component_type`
- `boot.main.image[0].component_name`
- `boot.main.image[0].offset`
- `boot.main.image[0].data_size`
- `boot.main.image[0].max_size`
- `boot.main.image[0].rec[0]`
- `boot.main.image[0].rec[1]`
- `boot.main.crc`

`boot.back`、`recovery.main`、`recovery.back` 同理。

`flash` 的 `<image-target>` 只接受镜像项级别路径，例如：

- `boot.main.image[0]`
- `boot.back.image[2]`
- `recovery.main.image[3]`

## 6. dump 输出中每个“工具字段”的含义

### 6.1 `base_offset`

工具最终采用的文件基址。

- `whole-disk` 通常是 `0`
- `raw-boot` 通常也是 `0`

区别不在 `base_offset` 本身，而在 `layout_relative_offset()` 对磁盘偏移的解释。

### 6.2 `absolute_offset`

本次实际执行 `pread/pwrite` 的文件偏移。

这决定工具真正读写的是文件里的哪一段。

### 6.3 `relative_offset`

相对当前布局起点的偏移。

- 在 `whole-disk` 下，它等于源码里的磁盘偏移
- 在 `raw-boot` 下，它等于“磁盘偏移减去 `0x100000`”

## 7. 三类元数据头的字段说明

---

## 7.1 `tagPartInfoHead`

源码定义见：

- `/mnt/sda1/yihao/opiai/Ascend310B-hboot2-source/drivers/firmware/bios/HwPkg/UEFI/Products/as310b/Common/Include/Library/SdEmmcPartition.h`

字段如下。

### `HeadMagic`

固定值 `0x55AA55AA`。

`hboot` 读取主/备头时只硬性检查这个字段，见 `GetPartInfoHead()`：

- `/mnt/sda1/yihao/opiai/Ascend310B-hboot2-source/drivers/firmware/bios/HwPkg/UEFI/Products/as310b/Common/Library/BootLoaderLib/BootLoaderLib.c`

如果主头魔数不对，就尝试备头。

### `Version`

格式版本号。注释写的是“100 表示 v1.00”，但你盘上的真实值可能是 `2`。

当前 `hboot` 启动路径里并没有按这个字段分支。

### `Size`

结构体长度，包含 `Crc`。

对 `part_info` 来说，真实结构大小与这个字段是一致的，通常是 `56`。

### `PartitionCount`

表示逻辑分区数。

启动流程里主要用于调试输出，不直接控制镜像搬运数量。

### `ComponentMap`

位图，表示哪些逻辑分区存在。

定义如下：

- bit0: `RAWDATA_A`
- bit1: `RAWDATA_B`
- bit2: `RECOVER_A`
- bit3: `RECOVER_B`
- bit4: `RECOVER_DATA_A`
- bit5: `RECOVER_DATA_B`
- bit6: `ROOTFS_A`
- bit7: `ROOTFS_B`

它更多是一个全局布局描述，不是实际搬运镜像时的直接索引来源。

### `Resv[8]`

保留字段。当前源码里没有消费这些值。

### `Crc`

对 `part_info` 来说，这是有效校验字段。

当前仓里的算法不是 CRC32，而是 `CRC16-CCITT(start=0)`，实现见：

- `/mnt/sda1/yihao/opiai/Ascend310B-hboot2-source/drivers/firmware/bios/HwPkg/UEFI/Common/Library/CommonLib/CommonLib.c`

覆盖范围是 `Crc` 字段之前的字节。

`hboot2meta` 现在已经按这个算法重算。

---

## 7.2 `tagPartCtrlHead`

字段如下。

### `HeadMagic`

同样是 `0x55AA55AA`。

`hboot` 在 `GetPartCtrlHead()` 中只检查这个字段，不检查版本兼容，不检查 CRC。

### `Version`

格式版本号。

当前启动链路没有按它分支。

### `Size`

结构体长度，包含 `Crc`。

通常是 `104`。

### `ForceRecoveryFlag`

强制进入 recovery 的标记。

`CheckBootOsImgOrRecovImg()` 中会检查：

- 若它等于 `RECOV_FORCE_DC_FLAG`，则直接走 recovery
- 否则还会看恢复配置节点和 GPIO 条件

也就是说，它是“正常启动”和“recovery 启动”的一级开关之一。

### `UpgradePartCount`

表示 `UpgradeCtrl[]` 中有效的升级控制项数量。

当前注释默认是 4，对应：

- 0: `RAWDATA`
- 1: `RECOVER`
- 2: `RECOVER_DATA`
- 3: `ROOTFS`

### `UpgradeCtrl[i].UpgradeType`

升级类型字段。

在当前 `BootLoaderLib.c` 里几乎没有直接消费这个值。

### `UpgradeCtrl[i].UpgradeStatus`

升级状态。

对启动最关键的是 `UpgradeCtrl[RAWDATA_IDX].UpgradeStatus`，它会在 `GetPartitionInfo()` 后写入 `gUpdateInfo[0]`。

`LoadOsImgEntry()` -> `GetFirstTryPos()` / `UpdateCaseImgPos()` 会据此判断：

- 当前是不是“OS 正在升级后的首次/多次尝试启动”
- A/B 槽位优先试哪一边
- 在 Flash 固件回滚时，OS 是否需要跟着回滚槽位

常见魔数：

- `0xD6C55BC1`: 升级了新版本

### `UpgradeCtrl[i].UpgradePartFlag`

指示升级写到了哪一边。

对 A/B 启动最关键的是 `UpgradeCtrl[RAWDATA_IDX].UpgradePartFlag`，它会写入 `gUpdateInfo[1]`。

常见魔数：

- `0xC11CB55B`: 升级了主区
- `0x3443DAAD`: 升级了备区

### `Resv[8]`

保留字段，当前启动路径不使用。

### `Crc`

和 `part_info` 一样，实际是 `CRC16-CCITT(start=0)`，覆盖 `Crc` 之前的字节。

---

## 7.3 `tagPartImageHead`

这是 `boot.main/back` 和 `recovery.main/back` 对应的目录头。

### `HeadMagic`

目录头魔数，通常也是 `0x55AA55AA`。

注意：当前 `GetBootImgInfo()` / `GetRecoveryImgInfo()` 读出目录后，并没有像 `part_info/part_ctrl` 那样再显式检查这个魔数。

### `Version`

目录版本号。

当前启动链路没有按它分支。

### `Size`

这个字段在实际盘上经常是 `144`，但这不代表整个目录实际只占 144 字节。

这里要特别注意：

- `hboot` 每次是按固定 `0x400` 字节把目录区读出来
- 然后按 `sizeof(tagPartImageHead)` 拷进内存结构
- 真正决定遍历多少条镜像的是 `ComponentCount`

也就是说，在当前 `hboot` 路径里，`boot/recovery` 的 `Size` 更像一个信息性字段，而不是实际解析边界。

### `PartitionName`

分区名字符串。

这个字段非常关键，因为它会进入 Linux 启动参数链路。

`StorePartitionName()` 会把当前选中的 `boot` 或 `recovery` 目录里的 `PartitionName` 复制到全局 `gPartitionName`，之后：

- `UpdateBootargs()` 调 `EfiFdtUpdateBootargs()`
- 把这个分区名写进 DTB/bootargs

所以这里通常会看到：

- 正常启动是 `PARTUUID=...`
- recovery 可能是 `/dev/ram0`

### `ComponentCount`

这是 `boot/recovery` 目录里最重要的控制字段之一。

`ReadOsImage()` 和 `ReadRecovImage()` 都直接用它决定要加载多少个组件：

- `boot.main/back` 常见是 3
- `recovery.main/back` 常见是 4

如果 `ComponentCount > MAX_OS_IMAGE_CNT`，`hboot` 会直接报错。

### `ImageInfo[i].ComponentType`

组件类型。

在 `SdEmmcPartition.h` 里有定义，但当前 `BootLoaderLib.c` 的主加载路径并没有按这个字段做分支，实际更依赖数组顺序和 `ComponentCount`。

结合当前代码和常见盘内容，通常约定是：

- 0: kernel
- 1: dtb
- 2: tee
- 3: initrd

更准确地说，它是“目录自描述字段”，但当前实现真正依赖的是索引顺序。

### `ImageInfo[i].ComponentName`

组件名称字符串。

常见值：

- `kernel`
- `dtb`
- `tee`
- `initrd`

当前启动代码不靠它查找组件，只是对目录可读性很有帮助。

### `ImageInfo[i].Offset`

这是启动最核心的字段之一。

它表示镜像在存储介质上的偏移。后续真正加载镜像时，`hboot` 就是直接用它：

- `ReadOsImageFromSdEmmc()`
- `ReadOsImageFromSsdUsb()`
- `ReadRecovImageFromSsdUsb()`

例如普通启动：

- `boot.main.image[0].offset = kernel 在盘上的偏移`
- `boot.main.image[1].offset = dt.img 在盘上的偏移`
- `boot.main.image[2].offset = tee 在盘上的偏移`

`hboot2meta flash` 最终也是把输入文件写到这个 `Offset` 指向的位置。所以：

- 改 `Offset` 是在改目录指针
- 执行 `flash` 是往目录指向的位置写内容

### `ImageInfo[i].DataSize`

目录记录的实际数据大小。

当前 `hboot` 主加载路径里，普通 eMMC/SD 启动并不完全依赖它，而是会先读 secure header，再根据安全头里的 `UwLCodeLen` 算真实装载长度。

但这个字段对分析盘内容、构建镜像、做一致性检查仍然很有价值。

`hboot2meta flash` 在写完镜像后，会自动把对应条目的 `DataSize` 更新成这次输入文件的真实大小。

### `ImageInfo[i].MaxSize`

目录记录的组件最大空间。

当前启动里主要用于：

- 对 SSD/USB 按最大空间整块读取
- 为越界检查、尾部清零提供上限语义

`hboot2meta flash` 会把它当成硬上限：

- 输入文件大小大于 `MaxSize` 时，直接拒绝写入

这是最重要的防越界保护，因为这些镜像槽位在盘上通常是连续排布的，超写很容易覆盖后续组件。

### `ImageInfo[i].Rec[0]` / `Rec[1]`

保留字段。

当前主启动路径没有使用。

### `Crc`

这一点最容易误判。

对 `boot/recovery` 目录来说，当前 `hboot` 并没有像 `part_info/part_ctrl` 那样校验这个字段。你在真实盘上经常会看到它是：

- `0xffffffff`

所以：

- 工具不会把它解释成“有效 CRC32”
- `set` 修改 `boot/recovery` 时，也不会擅自生成一个“看起来标准、但源码并不依赖”的校验值

## 8. 这些字段如何驱动启动流程

下面按 `LoadAndStartOS()` 主线说明。

### 8.1 先读元数据

`GetPartitionInfo()` 做了三件事：

1. 先读主区 `part_info/part_ctrl`
2. 主区无效则回退读备区 `part_info/part_ctrl`
3. 无论主备哪个信息头有效，都会把 `boot.main/back` 和 `recovery.main/back` 两份目录都读进来

这里真正决定“目录区在哪”的就是这些固定偏移：

- `0x100000`
- `0x100200`
- `0x120000`
- `0x120400`
- `0x120800`
- `0x120C00`

### 8.2 决定启动 OS 还是 recovery

`CheckBootOsImgOrRecovImg()` 主要看：

- `PartCtrlHead.ForceRecoveryFlag`
- 恢复配置节点里的 `RecoveryEnable`
- 相关 GPIO 状态

如果命中，就走 recovery；否则走正常 OS。

因此：

- `part_ctrl.*.force_recovery_flag` 直接影响启动模式

### 8.3 决定主槽还是备槽

正常 OS 路径：

- `GetFirstTryPos()`
- `UpdateCaseImgPos()`

关键输入来自：

- `part_ctrl.*.upgrade[0].upgrade_status`
- `part_ctrl.*.upgrade[0].upgrade_part_flag`

也就是 RAWDATA 的升级信息。它们决定：

- 是不是升级场景
- 优先试主区还是备区
- 固件回滚时 OS 是否跟随切槽

recovery 路径则主要依赖 recovery reset count。

### 8.4 决定加载多少个组件

正常 OS：

- `ImageCnt = BootImageInfo[PartIdx].ComponentCount`

recovery：

- `ImageCnt = RecoveryImageInfo[PartIdx].ComponentCount`

所以：

- `boot.main.component_count`
- `boot.back.component_count`
- `recovery.main.component_count`
- `recovery.back.component_count`

都会直接影响加载循环次数。

`flash` 也沿用这条边界：如果你指定的 `image[i]` 已经超出当前 `ComponentCount`，工具会拒绝写入。

### 8.5 决定从盘上哪里搬运镜像

真正的读盘地址来自：

- `BootImageInfo[PartIdx].ImageInfo[ImgIdx].Offset`
- `RecoveryImageInfo[PartIdx].ImageInfo[ImgIdx].Offset`

这就是目录项 `offset` 字段的核心意义。

`flash` 命令的设计完全复用了这套规则。比如：

- `flash /dev/mmcblk1 boot.main.image[0] Image`

本质上就是把 `Image` 写到后续 `ReadOsImageFromSdEmmc()` / `ReadOsImageFromSsdUsb()` 会读取的那个盘上偏移。

### 8.6 决定 Linux bootargs 指向哪个根文件系统

加载完镜像后，`StorePartitionName()` 会把目录头里的 `PartitionName` 保存起来，后续：

- `UpdateBootargs()`
- `EfiFdtUpdateBootargs()`

会把它写进设备树启动参数。

所以：

- `boot.*.partition_name`
- `recovery.*.partition_name`

会直接影响 Linux 看到的 root/rootfs 启动参数。

## 9. 组件目录与 DDR 目标地址的关系

对于普通 `as310b` 路径，当前代码里组件装载目标大致是：

- kernel: `0x29000000`
- dtb: `0x2B000000`
- tee: `0x28A40000`
- initrd: `0x2BA00000`

对应 `gImgAddrInfo[]`：

- index 0 -> kernel
- index 1 -> dtb
- index 2 -> tee
- index 3 -> initrd

这进一步说明：当前 `hboot` 主实现主要依赖“数组索引顺序”而不是 `ComponentType`。

## 10. 校验与回写策略

### 10.1 `part_info` / `part_ctrl`

工具会在修改后自动重算：

- `CRC16-CCITT(start=0)`

覆盖范围是 `Crc` 字段之前的字节。

### 10.2 `boot` / `recovery`

工具默认不重算其尾部 `crc` 字段，因为当前 `hboot` 主路径并不依赖它，而且真实盘上这个字段常见为 `0xffffffff`。

这是一种刻意保守的策略，避免生成一个“看起来正确，但并不一定符合厂商生成链”的值。

`flash` 修改 `DataSize` 时也遵循这条规则：

- 会回写目录块
- 但不会擅自生成 `boot/recovery` 尾部 `crc`

## 11. 常见操作示例

### 11.1 看当前正常启动槽位的 kernel 偏移

```bash
./hboot2meta get /dev/mmcblk1 boot.main.image[0].offset
./hboot2meta get /dev/mmcblk1 boot.back.image[0].offset
```

### 11.2 看 recovery 是否被强制打开

```bash
./hboot2meta get /dev/mmcblk1 part_ctrl.main.force_recovery_flag
```

### 11.3 看 RAWDATA 升级状态

```bash
./hboot2meta get /dev/mmcblk1 part_ctrl.main.upgrade[0].upgrade_status
./hboot2meta get /dev/mmcblk1 part_ctrl.main.upgrade[0].upgrade_part_flag
```

### 11.4 修改 boot 分区名

```bash
./hboot2meta set /dev/mmcblk1 boot.main.partition_name PARTUUID=xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
```

### 11.5 修改 dtb 偏移

```bash
./hboot2meta set /dev/mmcblk1 boot.main.image[1].offset 0x3800000
```

### 11.6 把 kernel 写入 boot.main 的第 0 个组件

```bash
./hboot2meta flash /dev/mmcblk1 boot.main.image[0] Image
```

### 11.7 把 recovery initrd 写到 recovery.main 的第 3 个组件

```bash
./hboot2meta flash /dev/mmcblk1 recovery.main.image[3] initrd.img
```

### 11.8 在脚本里跳过确认直接写入

```bash
./hboot2meta flash -y /dev/mmcblk1 boot.back.image[2] itrustee.img
```

## 12. 风险提示

### 12.1 不要随意改 `component_count`

这个字段直接决定加载循环次数。改大可能让 `hboot` 读到未定义目录项，改小会导致应加载的组件没被搬运。

### 12.2 不要把 `ComponentType` 当成当前实现的唯一真相

当前 `hboot` 代码更依赖索引顺序。就算 `component_type` 写成了别的值，只要顺序和偏移还是 kernel/dtb/tee/initrd，很多路径仍会继续跑。

### 12.3 `boot/recovery.size` 不要按常规 header 理解

真实盘上常见 `size=144`，但目录区物理上是固定 `0x400` 字节，`hboot` 也不是按这个 `size` 截断解析。

### 12.4 修改整盘前建议先备份

例如：

```bash
dd if=/dev/mmcblk1 of=bootmeta-backup.bin bs=1M count=2
```

### 12.5 `flash` 打印的 `dd` 只覆盖“写内容”动作

工具会打印等效 `dd` 命令，便于你手工复现写镜像本体。

但如果你直接手工跑 `dd`：

- 不会自动做 `MaxSize` 检查
- 不会自动更新目录项里的 `DataSize`
- 也不会帮你做额外的元数据一致性审计

### 12.6 `flash` 默认会要求确认

这是为了避免误写真实设备。

- 交互式终端下，工具会在打印写入摘要后要求你输入 `y/yes`
- 非交互环境下，如果没有 `-y`，工具会直接拒绝执行
- 自动化脚本、批处理或 CI 请显式加 `-y`

## 13. 总结

如果只记住三点，可以记这三条：

- `part_info/part_ctrl` 主要决定“系统状态”和“A/B 选择”
- `boot/recovery` 目录主要决定“从哪里读镜像”和“Linux 最终拿什么 bootargs 启动”
- 当前 `hboot` 真正依赖的是 `HeadMagic`、`ForceRecoveryFlag`、`UpgradeCtrl[]`、`ComponentCount`、`ImageInfo[].Offset`、`PartitionName`

相对地，`boot/recovery` 目录里的 `Size` 和尾部 `Crc` 在当前主启动路径里都不是关键控制项。
