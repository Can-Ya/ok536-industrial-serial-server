# 基于OK536开发板的串口服务器
## 项目简介
本项目以OK536开发板为硬件核心，实现一款具备多路并发、协议转换能力的工业级串口服务器，支持串口数据与网络数据（TCP）双向透传，集成Modbus RTU协议解析、心跳保活、分级日志等核心能力，后续还将拓展Web可视化管理、守护进程+看门狗等工业级特性，适用于工业现场多串口设备的网络接入与协议转换场景。

## 项目进度
### 已完成
#### 第一阶段：基础框架搭建与数据通路验证
- 硬件层：定位并验证OK536开发板串口设备节点，完成硬件通路连通性测试，确认串口物理层收发正常；
- 功能层：编写双路串口回环测试程序，全覆盖验证串口数据收发的完整性与稳定性；
- 核心通路：实现单路串口→TCP Server简易透传工具，打通“串口-网络”基础数据转发链路。

#### 第二阶段：多路并发采集与核心业务逻辑实现
- 架构重构：基于epoll/多线程模型重构程序，支持多路串口并发数据采集，解决单线程串行处理的性能瓶颈；
- 配置模块：开发配置文件解析模块，支持每个串口的波特率、数据位、校验位、停止位等参数独立配置；
- 协议解析：编写Modbus RTU帧解析状态机，完成串口Modbus RTU数据到网络TCP数据的核心协议转换功能。

#### 第三阶段：系统健壮性建设与基础管理功能
- 网络保活：实现TCP心跳保活机制，自动检测连接状态，避免无效长连接占用资源；
- 日志体系：集成分级日志系统（DEBUG/INFO/WARN/ERROR/FATAL），记录串口收发、网络连接、协议解析等关键事件及异常信息，便于问题定位；
- 管理界面：开发命令行管理界面（CLI），支持串口状态查询、实时参数修改、连接状态监控等基础管理操作。

### 待完成（可优化）
#### 1：嵌入式Web图形化管理界面开发
- 技术选型：基于轻量级C语言库（mongoose）或BoA+CGI搭建Web服务；
- 核心功能：实现浏览器可视化配置（串口参数、协议模式切换）、状态监控（串口连接状态、数据流量统计）、数据透传实时查看；
- 目标：完成简洁实用的Web界面开发，支持面试现场演示，体现可视化与易用性设计能力。

#### 2：守护进程+看门狗机制实现
- 进程改造：将主程序改造为Linux守护进程，脱离终端后台稳定运行；
- 异常兜底：编写简易看门狗脚本，实时监控主进程运行状态，若进程异常崩溃则自动重启，符合工业级设备高可用要求。

#### 3：性能优化
- 技术落地：尝试集成Linux「零拷贝」技术（splice系统调用），优化串口→网络的数据搬运流程；
- 优化目标：降低CPU资源占用率，提升大流量场景下的数据转发效率，体现底层性能调优能力。

## 技术栈/硬件&软件环境
### 硬件
- 核心板：OK536开发板（ARM Cortex-A55架构）
- 外设：OK536配套串口扩展模块、网线、电源适配器（12V/2A）
- 辅助硬件：PC机（用于交叉编译、调试）、串口调试助手、网络调试助手、Modbus RTU从站设备

### 软件
- 开发环境：Ubuntu 20.04（交叉编译主机）、ARM-Linux-GCC 7.5.0交叉编译器
- 系统：OK536开发板适配Linux 4.19内核
- 开发语言：C
- 核心技术：epoll/多线程并发、socket网络编程、Modbus RTU协议解析、Linux命令行编程
- 调试工具：minicom、（串口调试）、SerialStudio（网络抓包）、gdb（远程调试）

## 快速开始
### 编译与部署
#### 1. 克隆代码
```bash
# 交叉编译主机执行
git clone https://github.com/Can-Ya/ok536-industrial-serial-server.git
cd ok536-industrial-serial-server
```

#### 2. 交叉编译项目
```bash
# 执行Makefile（已配置交叉编译器路径）
make
# 编译完成后，生成可执行文件serial_server
```

#### 3. 部署至OK536开发板
```bash
# 方式1：通过scp传输（开发板需开启SSH）
scp bin/serial_server root@[开发板IP]:/root/

# 方式2：通过U盘拷贝
# 1. 将serial_server复制到U盘，插入开发板
# 2. 登录开发板，挂载U盘并拷贝文件
mount /dev/sda1 /mnt/usb
cp /mnt/usb/serial_server /root/
umount /mnt/usb
```

### 运行与配置
#### 1. 登录OK536开发板
```bash
# 通过SSH登录
ssh root@[开发板IP]

# 或通过串口登录
minicom -s  # 配置串口参数后登录
```

#### 2. 配置串口与网络参数
```bash
# 编辑多路串口配置文件（每个串口独立配置）
vi serial_server.conf
# 示例配置（多路串口）：
#  - idx: 0
#    dev_path: "/dev/ttyAS0"
#    baudrate: 115200
#    databit: 8
#    stopbit: 1
#    parity: "N"
#    flow_ctrl: 0
#    enable: false
#    modbus_enable: false
#  - idx: 1
#    dev_path: "/dev/ttyAS1"
#    baudrate: 115200
#    databit: 8
#    stopbit: 1
#    parity: "N"
#    flow_ctrl: 0
#    enable: false
#    modbus_enable: false
```

#### 3. 启动串口服务器
```bash
# 赋予可执行权限
chmod +x /root/serial_server

# 前台运行（调试用，查看分级日志）
/root/serial_server -c /root/serial_server.log

# 后台运行（临时使用）
nohup /root/serial_server -c /root/serial_server.conf > /root/serial_server.log 2>&1 &
```

#### 4. 命令行管理操作（CLI）
```bash
# 查看串口1状态
serial_server > uart_status 1

# 修改指定串口波特率
serial_server > uart_uart_set -i 1 -b 115200
...
```

#### 5. 测试数据透传与协议转换
- 网络侧：PC机打开网络调试助手，以TCP Client模式分别连接开发板IP:192.168.1.232:8888；
- 串口侧：Modbus RTU传感器发送数据，网络调试助手可接收解析后的协议数据；
- 反向测试：网络调试助手发送Modbus RTU指令，串口传感器可响应并回传数据；
- 心跳验证：断开网络连接后，日志中可查看到心跳超时提示，重连后自动恢复。

## 核心功能说明
### 1. 基础数据透传
- 单/多路串口→TCP Server：支持多路串口并发采集，数据实时转发至对应TCP端口；
- TCP→串口：接收网络端指令，校验后分发至指定串口，完成双向透传。

### 2. 串口参数配置
支持每个串口独立配置波特率（9600/19200/38400/115200/230400）、数据位（5/6/7/8）、校验位（NONE/ODD/EVEN）、停止位（1/2）、数据串流（on/off）、Modbus协议(on/off)，适配不同工业串口设备。

### 3. 系统健壮性能力
- TCP心跳保活：定时发送心跳包检测连接状态，超时则主动断开并重新监听；
- 分级日志：按DEBUG/INFO/WARN/ERROR分级记录事件，支持问题快速定位；
- CLI管理：支持串口状态查询、参数在线修改，无需重启程序。

## 目录结构
```
ok536-serial-server/
├── config/             # 配置文件目录
│   └── uart_config.yaml  # 多路串口配置示例
├── src/              # 源码目录
│   ├── uart/       # 串口模块
│   │   ├── uart_mgr.c  # 串口打开/配置/读写
│   │   ├── uart_mgr.h
│   ├── net/      # 网络模块
│   │   ├── net_mgr.c     # TCP通信+心跳保活
│   │   └── net_mgr.h
│   ├── modbus/     # 协议解析模块
│   │   ├── modbus_core.c # Modbus RTU帧解析
│   │   └── modbus_core.h
│   ├── cli/          # 命令行管理模块
│   │   ├── cli_mgr.c     # CLI交互逻辑
│   │   └── cli_mgr.h
│   ├── log/          # 分级日志模块
│   │   ├── log.c     # 日志打印/分级
│   │   └── log.h
│   └── main.c        # 主程序（流程调度）
└── README.md         # 项目说明文档
```
## 联系方式
- 开发者：Can-Ya
- 邮箱：19154547736@163.com