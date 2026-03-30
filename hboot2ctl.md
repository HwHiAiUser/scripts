# hboot2ctl 寄存器速查

## 基本用法

查看全部：

```bash
sudo ./hboot2ctl dump
```

查看单个字段：

```bash
sudo ./hboot2ctl get mem.force_boot
```

写单个字段：

```bash
sudo ./hboot2ctl set mem.force_boot 0x10
```

说明：

- 输入既支持十进制，也支持十六进制。
## 常用目标速查

强制下次从 OS 主区启动：

```bash
sudo ./hboot2ctl set mem.force_boot 0x10
```

强制下次从 OS 备区启动：

```bash
sudo ./hboot2ctl set mem.force_boot 0x14
```

强制下次从 recovery 主区启动：

```bash
sudo ./hboot2ctl set mem.force_boot 0x18
```

强制下次从 recovery 备区启动：

```bash
sudo ./hboot2ctl set mem.force_boot 0x1c
```

取消强制启动，恢复正常启动决策：

```bash
sudo ./hboot2ctl set mem.force_boot 0x0
```

切到 customer header 验证方式：

```bash
sudo ./hboot2ctl set mem.os_verif_method 0x39593593
```

切回 Huawei header 验证方式：

```bash
sudo ./hboot2ctl set mem.os_verif_method 0x35933595
```

让下一次 OS warm reset 更偏向主区：

```bash
sudo ./hboot2ctl set mem.warmstart_flag 0x1
sudo ./hboot2ctl set mem.os_resetcnt 0x0
```

让下一次 OS warm reset 更偏向备区：

```bash
sudo ./hboot2ctl set mem.warmstart_flag 0x1
sudo ./hboot2ctl set mem.os_resetcnt 0x3
```

让下一次 recovery 更偏向主区：

```bash
sudo ./hboot2ctl set mem.recovery_resetcnt 0x0
```

让下一次 recovery 更偏向备区：

```bash
sudo ./hboot2ctl set mem.recovery_resetcnt 0x3
```

打开 MDC adaptive 模式：

```bash
sudo ./hboot2ctl set sram.adapt_mode 0xb2
```

恢复 normal package 模式：

```bash
sudo ./hboot2ctl set sram.adapt_mode 0x0
```

注入一个 OS panic 原因，方便 blackbox 验证：

```bash
sudo ./hboot2ctl set mem.sw_excep_code7 0x24
```

## 一、板卡与平台识别相关

### `mem.board_id` 对应 `SC_BAK_DATA0`

作用：

- 保存板卡 ID。
- hboot 会用它去匹配 DTB。

源码依据：

- `GetBoardid()` 直接读 `SC_BAK_DATA0`
- DTB 选择逻辑会按 board id 查表

常见理解：

- 这是“这块板是谁”的核心身份值。
- 对 Orange Pi AI Pro 20T 这种场景，它会直接影响 dtb 选择。

补充：

- 某些 MDC / equip 场景里，源码还会把它按 `BOARD_ID_MOD = 1000` 拆成 module id 和 ft id。
- 但在普通路径里，就是直接用原始值。

能不能改：

- 可以改，但不建议日常改。

改了会怎样：

- 可能让 hboot 选择另一套 dtb。
- 如果 dtb 不匹配，后果可能是 GPIO、供电、存储、PCIe、风扇、时钟等全部异常。

推荐用途：

- 只做只读观察。
- 或者在非常明确的 dtb 兼容性测试里短暂改。

### `mem.platform_info` 对应 `SC_BAK_DATA3`

作用：

- 保存 hboot 汇总出来的平台基础信息。
- 源码注释写得很清楚：它用来记录 `DC/MDC`、`1P/2P`、`SOCA/SOCB`。

源码生成方式：

- 低 2 bit：`GetDcMdcMode()`
- `bits[4:3]`：`GetChipCnt() << 3`
- `bit[8]`：`CheckABSoc() << 8`

大致含义：

- `bits[1:0] = 0x0`：DC
- `bits[1:0] = 0x2`：MDC
- `0x8` 一般可理解为 1P
- `0x10` 一般可理解为 2P
- `0x0` in bit 8：SOC A
- `0x100` in bit 8：SOC B

能不能改：

- 可以，但更像“改缓存结果”，不是改底层 strap / GPIO 本身。

改了会怎样：

- 某些依赖这个寄存器的后续逻辑会看到被伪造的平台信息。

推荐用途：

- 诊断时读取。
- 非必要不要手改。

### `mem.ver_ver` 对应 `SC_VER_VER_REG_ADDR`

作用：

- 保存平台类型与版本号。
- hboot 用它区分 ASIC / EMU / ESL / FPGA。

源码语义：

- 若寄存器值为 `0`，平台类型视为 `ASIC`
- 高 16 位为 `0x1`，视为 `EMU`
- 高 16 位为 `0x2`，视为 `ESL`
- 其他非零高 16 位，视为 `FPGA`
- 低 16 位是版本号

例子：

- `0x00000000`
  - `platform=asic`
  - `version=0x0000`
- `0x00006002`
  - `platform=fpga`
  - `version=0x6002`
- `0x00016002`
  - `platform=emu`
  - `version=0x6002`
- `0x00026002`
  - `platform=esl`
  - `version=0x6002`

能不能改：

- 可以，但非常不建议在正常板上改。

改了会怎样：

- hboot 可能走到另一套平台分支。
- 会影响很多和平台类型相关的路径判断。

## 二、启动介质与启动位置记录

### `mem.run_img_loc` 对应 `SC_BAK_DATA1`

作用：

- 保存“这次镜像是从哪里启动的”。
- 最终还会被拼到 kernel cmdline 的 `runImgLocation=...` 参数里。

源码里目前能明确确认的位：

- `0x20`：eMMC 启动
- `0x80`：SSD 启动

低位的含义：

- hboot 在 `ReportBootArea()` 里会用低位来记录 main/back 区选择。
- 注释写的是：`kernel, initrd, dtb and tee boot region flag`
- 但当前源码没有一处把这些低位逐位枚举成完整名字，所以我们现在只能稳定解出“介质”和“粗粒度 main/back”。

能不能改：

- 可以，但不推荐把它当成主控开关。

改了会怎样：

- 它更多像“结果记录”。
- 单独改它，不等于真的改了 hboot 的启动来源。

推荐用途：

- 用来确认上一轮 hboot 最终判定的启动介质和区位。

### `mem.pcie_boot_index` 对应 `SC_BAK_DATA9`

作用：

- PCIe mailbox 启动流程里使用的文件索引。

源码行为：

- hboot 读取 `SC_BAK_DATA9`
- 以它作为 `FileIndex` 开始等待 mailbox 里的文件就绪

能不能改：

- 能改，但只适合 PCIe 启动调试。

改了会怎样：

- 会让 hboot 从不同 mailbox 文件序号开始等。
- 如果索引不对，容易直接卡在加载流程。

## 三、强制启动与启动策略

### `mem.force_boot` 对应 `SC_BAK_DATA4`

作用：

- 这是最重要的启动覆盖寄存器之一。
- hboot 会取它的低 5 bit 决定是否强制走某种启动路径。

已确认取值：

| 值 | 含义 |
| --- | --- |
| `0x10` | 强制正常启动 main |
| `0x14` | 强制正常启动 back |
| `0x18` | 强制 recovery 启动 main |
| `0x1c` | 强制 recovery 启动 back |
| `0x1e` | PXE 测试模式 |
| 其他值 | 非强制启动 |

源码上的影响：

- 它会影响“启动 OS 还是 recovery”
- 也会影响“从 main 还是 back 启动”

这是最推荐人工调试的控制位：

- 因为它语义清楚
- 覆盖效果直接
- 不用碰 metadata

典型玩法：

强制 OS 主区：

```bash
sudo ./hboot2ctl set mem.force_boot 0x10
```

强制 OS 备区：

```bash
sudo ./hboot2ctl set mem.force_boot 0x14
```

强制 recovery 主区：

```bash
sudo ./hboot2ctl set mem.force_boot 0x18
```

强制 recovery 备区：

```bash
sudo ./hboot2ctl set mem.force_boot 0x1c
```

恢复默认决策：

```bash
sudo ./hboot2ctl set mem.force_boot 0x0
```

推荐操作习惯：

- 写入强制值
- 重启观察效果
- 测完后清回 `0`

## 四、镜像头验证与安全握手

### `mem.os_verif_method` 对应 `OS_VERIF_METHOD`

作用：

- 决定 kernel / dtb 等镜像按哪种 header 格式做验证。

源码里已经明确的值：

| 值 | 含义 | header 偏移 |
| --- | --- | --- |
| `0x35933595` | Huawei header | `0x0000` |
| `0x39593593` | Customer header | `0x1000` |
| 其他非 HW 值 | 按 customer-like 处理 | `0x1000` |

源码语义重点：

- `HEAD_OFFSET(usrType)` 只有在 `HW` 时返回 `0`
- 其他情况都返回 `0x1000`
- 这会直接影响 hboot 读镜像头的位置

能不能改：

- 可以改，而且这是一个很有价值的实验开关。

改了会怎样：

- 如果镜像实际是 Huawei 格式，你却设成 customer，hboot 可能在偏移 `0x1000` 处找头，从而校验失败。
- 如果镜像实际是 customer 格式，反过来也可能失败。

典型用途：

- 验证你手上的 kernel / dtb 镜像到底是哪种头格式。

例子：

```bash
sudo ./hboot2ctl set mem.os_verif_method 0x35933595
sudo ./hboot2ctl set mem.os_verif_method 0x39593593
```

### `mem.hsm_ready_flag` 对应 `SC_HSM_READY_ADDR`

作用：

- 这是 TEE/HSM 加载时的握手标志。

源码明确行为：

- hboot 会循环等待 `(SC_HSM_READY_ADDR & 0xFF) == 0xDC`
- 一旦满足，就继续执行，并把该寄存器清零

常见状态：

| 值特征 | 含义 |
| --- | --- |
| 低 8 bit = `0xdc` | ready |
| 低 8 bit = `0x00` | cleared |
| 其他 | waiting / 中间状态 |

能不能改：

- 理论上可以改。
- 但它更像“另一个处理器 / 固件模块写给 hboot 的握手状态”。

推荐用途：

- 观察 TEE/HSM 卡住时是否一直等不到 ready。

### `mem.usb_efuse` 对应 `SC_BAK_DATA6`

作用：

- 参与 USB 启动模式判断。

源码里当前能确认的点：

- hboot 会看 bit1
- 当 `bit1` 不满足某条件，且 pad update mode 也不满足时，会进入 USB boot mode

能不能改：

- 不建议随便改。

改了会怎样：

- 可能把板子引导到 USB 启动路径。

## 五、eMMC / SD 启动状态

### `mem.emmc_init_flag` 对应 `SC_EMMC_INIT_ADDR`

作用：

- 给 eMMC 初始化代码一个“要不要走完整初始化”的提示。

源码语义：

- 如果值不是 `0x5A5A5A5A`，hboot 会走完整初始化路径
- 初始化后又会把这个寄存器写回 `0`

常见值：

| 值 | 含义 |
| --- | --- |
| `0x5a5a5a5a` | init flag 已置位 |
| `0x00000000` | 已清零 |

能不能改：

- 可以，但属于存储启动调试开关。

建议怎么改：

- 想让 hboot 下次更像“完整重新 init”，写 `0` 更合理。
- 写 `0x5A5A5A5A` 可能改变原本的初始化路径，不建议在不清楚上下文时使用。

## 六、reset source 与异常信息

### `mem.reset_src` 对应 `SC_BAK_DATA2`

作用：

- 记录 reset source。
- 在 AS610 这套 blackbox 路径里，也被用来判断上次是不是 cold boot。

当前已确认的最稳妥语义：

- 对 AS610 代码路径，低 8 bit 为 `0x00` 时，blackbox 视为 cold boot

目前还不能完全确认的部分：

- 更完整的 reset source 编码表在这份源码里没有完整展开。

所以当前建议：

- 把它当“诊断寄存器”
- 不要依赖它去做复杂控制

### `mem.sw_excep_code7` 到 `mem.sw_excep_code12`

作用：

- 这是 blackbox 相关的软件异常 / 重启原因寄存器。

角色分工：

| 字段 | 作用 |
| --- | --- |
| `mem.sw_excep_code7` | OS 异常原因 |
| `mem.sw_excep_code8` | OS 启动打点 |
| `mem.sw_excep_code9` | 低功耗异常原因 |
| `mem.sw_excep_code10` | SafetyIsland 或 OS 异常原因 |
| `mem.sw_excep_code11` | AOS 异常原因 |
| `mem.sw_excep_code12` | ATF 异常原因 |

常见值：

| 值 | 含义 |
| --- | --- |
| `0x00` | clear |
| `0x24` | panic |
| `0x26` | soft-lockup |
| `0x29` | os-coredump |
| `0x2a` | oom |
| `0x38` | tee-exception |
| `0x3b` | hsm-exception |
| `0x3c` | atf-exception |
| `0x8a` | device-load-timeout |
| `0xff` | invalid |

特别说明：

- `mem.sw_excep_code12 = 0x39`
  - 在 ATF 路径里被单独解释成 `ATF_SRAM_FATAL_FLAG`
  - 不只是普通的 `lpfw-exception`

关于 `mem.sw_excep_code8`：

- 源码里只看到它被拿来和 `OS_START_POINT` 比较
- 也看到 hboot 在某些路径里会主动写 `OS_START_POINT`
- 但这次代码搜索没把 `OS_START_POINT` 的具体数值稳定找出来
- 所以文档里把它描述成“OS 启动打点 / boot-stage marker”，不去硬写一个可能错的常量值

这些寄存器能不能改：

- 可以，而且很适合做 blackbox 测试

改了会怎样：

- 会影响 hboot / blackbox 认为“上次系统是因为什么原因异常”
- 但它们不是主启动选择开关，更多是诊断痕迹

举例：

注入 OS panic：

```bash
sudo ./hboot2ctl set mem.sw_excep_code7 0x24
```

注入低功耗异常：

```bash
sudo ./hboot2ctl set mem.sw_excep_code9 0x32
```

注入 ATF SRAM fatal：

```bash
sudo ./hboot2ctl set mem.sw_excep_code12 0x39
```

注意：

- cold boot 时，blackbox 会把这些寄存器清零
- blackbox 在消费掉异常信息后，也会清零

## 七、avoid-brick 计数器与 warm-start 逻辑

### `mem.warmstart_flag` 对应 `SC_POR_REG1`

作用：

- 用来区分当前 OS 启动决策是不是“第一次进入”路径。

已确认取值：

| 值 | 含义 |
| --- | --- |
| `0x0` | 冷启动 / 初次进入路径 |
| `0x1` | warm-start 路径 |

源码行为：

- 如果 `warmstart_flag == 0`
  - hboot 会先把它写成 `1`
  - 然后根据 update 场景或默认规则重置 OS 计数器基线

这意味着：

- 如果你想手动设置 `mem.os_resetcnt` 来控制下次去哪一边
- 最好同时确保 `mem.warmstart_flag = 1`
- 否则 hboot 可能先把计数逻辑重新初始化，覆盖你的预期

### `mem.os_resetcnt` 对应 `SC_POR_REG2`

作用：

- OS 镜像 main/back 轮转计数器。

源码决策逻辑：

- 读取当前值
- `+1`
- `% 8`
- 如果结果 `< 4`，走 main
- 否则走 back

因此可以推导出“下一次”的方向：

| 当前写入值 | 下次 OS 尝试 |
| --- | --- |
| `0x0` | main |
| `0x1` | main |
| `0x2` | main |
| `0x3` | back |
| `0x4` | back |
| `0x5` | back |
| `0x6` | back |
| `0x7` | main |

典型玩法：

让下次偏向 main：

```bash
sudo ./hboot2ctl set mem.warmstart_flag 0x1
sudo ./hboot2ctl set mem.os_resetcnt 0x0
```

让下次偏向 back：

```bash
sudo ./hboot2ctl set mem.warmstart_flag 0x1
sudo ./hboot2ctl set mem.os_resetcnt 0x3
```

### `mem.recovery_resetcnt` 对应 `SC_POR_REG3`

作用：

- recovery 镜像 main/back 轮转计数器。

源码行为：

- 逻辑与 `os_resetcnt` 类似
- 也是 `+1` 后 `% 8`
- `< 4` 走 main
- `>= 4` 走 back

常见用途：

让下次 recovery 偏向 main：

```bash
sudo ./hboot2ctl set mem.recovery_resetcnt 0x0
```

让下次 recovery 偏向 back：

```bash
sudo ./hboot2ctl set mem.recovery_resetcnt 0x3
```

### `mem.firmware_resetcnt` 对应 `SC_POR_REG0`

作用：

- flash 固件镜像 reset counter
- 会参与 flash 升级 / 回滚判定

源码行为：

- hboot 会用 `firmware_resetcnt % 8`
- 再结合 `flash.update_flag.*` 和 `flash.update_area.*`
- 判定当前是否发生 firmware rollback

大致规则：

- 如果升级目标是 main
  - `< 4` 视为未回滚
  - `>= 4` 视为回滚
- 如果升级目标是 back
  - `< 4` 视为回滚
  - `>= 4` 视为未回滚

能不能改：

- 能改，但只适合升级 / 回滚逻辑测试。

## 八、flash 升级与回滚状态

### `flash.update_flag.main` / `flash.update_flag.back`

作用：

- 表示 flash firmware 是否处于升级场景。

已确认值：

| 值 | 含义 |
| --- | --- |
| `0xD6C55BC1` | 升级场景有效 |
| 其他 | 当前逻辑里视作非升级场景 |

### `flash.update_area.main` / `flash.update_area.back`

作用：

- 表示升级目标区域。

已确认值：

| 值 | 含义 |
| --- | --- |
| `0xC11CB55B` | main / A 区 |
| 其他 | 当前逻辑里大多按 back / B 区处理 |

### `flash.recovery_force.main` / `flash.recovery_force.back`

作用：

- `Resource.h` 里定义成 force recovery flag。

当前源码现状：

- 能看到它们被打印出来
- 但在主启动决策路径里，没有看到明确参与核心选择逻辑

因此当前结论：

- 可以观察
- 不建议作为主要控制手段

### 这些 flash 字段到底适合怎么用

适合：

- 还原某次升级失败现场
- 做回滚逻辑验证

不适合：

- 日常切主备区
- 想“快速试一下启动哪边”

如果你只是想切主备区，优先改：

- `mem.force_boot`
- `mem.os_resetcnt`
- `mem.recovery_resetcnt`

如果你真的要模拟 flash update main 场景：

```bash
sudo ./hboot2ctl set flash.update_flag.main 0xd6c55bc1
sudo ./hboot2ctl set flash.update_area.main 0xc11cb55b
```

清除升级标志：

```bash
sudo ./hboot2ctl set flash.update_flag.main 0x0
sudo ./hboot2ctl set flash.update_flag.back 0x0
```

## 九、SRAM 启动状态

### `sram.adapt_mode` 对应 `SRAM_ADAPT_MOD_ADDR`

作用：

- 区分 normal package 和 MDC adaptive package。

已确认值：

| 值 | 含义 |
| --- | --- |
| `0xB2` | MDC adaptive |
| `0x0` | normal |

源码影响：

- 在 MDC adaptive 模式下
  - DTB 选择会切到 GPIO 获取 slot id 的逻辑
  - 某些 FDT update 路径也会特殊处理

能不能改：

- 可以，且这是一个比较适合做实验的开关。

例子：

```bash
sudo ./hboot2ctl set sram.adapt_mode 0xb2
sudo ./hboot2ctl set sram.adapt_mode 0x0
```

### `sram.recovery_count` 对应 `SRAM_RECOV_CNT_ADDR`

作用：

- MDC 启动模式里用来选择 OS / recovery / PXE 的计数器。

源码里明确的范围：

| 区间 | 含义 |
| --- | --- |
| `0..7` | 正常 OS 范围 |
| `8..11` | recovery main |
| `12..15` | recovery back |
| `14..15` | test build 下可触发 PXE |

注意：

- hboot 用的是 `RecovCnt % 16`

典型玩法：

让 MDC 下次更偏向 recovery main：

```bash
sudo ./hboot2ctl set sram.recovery_count 0x8
```

让 MDC 下次更偏向 recovery back：

```bash
sudo ./hboot2ctl set sram.recovery_count 0xc
```

### `sram.recovery_init_flag` 对应 `SRAM_RECOV_INIT_FLAG`

作用：

- 在 `Resource.h` 里有定义。

当前源码状态：

- 这次搜索里没有找到非常明确的消费者逻辑。

当前建议：

- 当作“原始状态值”看待。
- 暂时不建议把它当成一个可控开关。

## 每个字段适不适合 `set`

| 字段 | 适不适合手改 | 推荐用途 |
| --- | --- | --- |
| `mem.board_id` | 不建议 | DTB 兼容性实验 |
| `mem.run_img_loc` | 一般不建议 | 看 hboot 记录的启动结果 |
| `mem.reset_src` | 一般不建议 | 看 reset 来源，blackbox 调试 |
| `mem.platform_info` | 一般不建议 | 看平台缓存状态 |
| `mem.force_boot` | 推荐 | 强制切 OS/recovery 主备 |
| `mem.usb_efuse` | 不建议 | USB 启动相关诊断 |
| `mem.pcie_boot_index` | 不建议 | PCIe mailbox 启动调试 |
| `mem.emmc_init_flag` | 可实验 | eMMC init 路径测试 |
| `mem.os_verif_method` | 推荐谨慎使用 | Huawei/customer header 切换 |
| `mem.hsm_ready_flag` | 仅实验室 | HSM/TEE 握手调试 |
| `mem.firmware_resetcnt` | 仅实验室 | flash rollback 测试 |
| `mem.warmstart_flag` | 推荐谨慎使用 | 控制 OS 计数逻辑是否重置 |
| `mem.os_resetcnt` | 推荐谨慎使用 | 控制下次 OS 主备倾向 |
| `mem.recovery_resetcnt` | 推荐谨慎使用 | 控制下次 recovery 主备倾向 |
| `mem.sw_excep_code7..12` | 推荐用于 debug | blackbox 异常注入 |
| `mem.ver_ver` | 不建议 | 平台类型实验 |
| `flash.*` | 高风险，不建议 | 升级 / 回滚现场重建 |
| `sram.adapt_mode` | 推荐用于实验 | MDC adaptive 模式验证 |
| `sram.recovery_count` | 推荐用于实验 | MDC recovery / PXE 测试 |
| `sram.recovery_init_flag` | 不建议 | 目前语义不够明确 |

## 目前还不能完全写死语义的字段

这些字段我在 `hboot2ctl` 里也保持了保守解释：

- `mem.reset_src`
  - 目前只稳定确认了 AS610 路径里的 cold boot 判断
- `mem.run_img_loc`
  - 介质位清楚，低位逐组件映射还不够完整
- `mem.pcie_boot_index`
  - 确认它是 mailbox 启动文件索引，但完整枚举不完整
- `flash.recovery_force.main/back`
  - 定义和打印都看到了，但主决策链路里没看到明确消费
- `sram.recovery_init_flag`
  - 目前只适合读原始值

## 推荐调试顺序

如果你的目标是“想控制下一次启动去哪边”，推荐优先级是：

1. `mem.force_boot`
2. `mem.warmstart_flag + mem.os_resetcnt`
3. `mem.recovery_resetcnt`
4. `sram.recovery_count`
5. 最后才碰 `flash.*`

如果你的目标是“判断上次为什么挂了”，推荐优先看：

1. `mem.sw_excep_code7..12`
2. `mem.reset_src`
3. `mem.run_img_loc`
4. `mem.firmware_resetcnt`
5. `sram.recovery_count`

如果你的目标是“镜像头格式或签名验证有问题”，优先看：

1. `mem.os_verif_method`
2. `mem.ver_ver`
3. `mem.board_id`

## 后续还可以继续增强的地方

如果你愿意，后面可以继续把工具做得更顺手：

- 给 `hboot2ctl` 增加 `help <field>`
  - 直接在命令行里显示某个字段的简版说明
- 给 `hboot2ctl` 增加 `decode <field> <value>`
  - 不写寄存器，只看某个值会被解释成什么
- 给 `dump` 增加“风险级别”和“推荐动作”
  - 比如显示 `safe-to-set`、`debug-only`、`dangerous`

这会比单纯看十六进制值更接近真正的调试工具。
