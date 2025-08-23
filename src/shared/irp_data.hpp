/**
 * @file irp_data.hpp
 * @brief 定义用户态与内核态驱动通信的IRP(I/O Request Packet)数据结构和IOCTL控制码
 * 
 * 该文件包含了hypervisor驱动与用户态应用程序之间通信所需的所有数据结构定义。
 * 通过DeviceIoControl API使用这些结构体与驱动进行交互，实现EPT hook的安装、卸载等功能。
 */

#pragma once

/**
 * @brief IOCTL控制码定义 - 使用CTL_CODE宏生成标准的Windows设备控制码
 * 
 * 控制码格式说明：
 * - FILE_DEVICE_UNKNOWN: 设备类型，用于第三方驱动设备
 * - 0x800-0x803: 函数码，定义具体操作类型  
 * - METHOD_NEITHER: 缓冲区方法，直接传递用户缓冲区指针(需要手动验证地址有效性)
 * - FILE_ANY_ACCESS: 访问权限，任何访问权限都可以使用该IOCTL
 * 
 * 注意: METHOD_NEITHER要求驱动手动验证用户模式指针的有效性，
 * 使用MmIsAddressValid或ProbeForRead/ProbeForWrite进行验证
 */

/// 安装EPT hook - 在目标进程的指定地址安装内存hook
#define HOOK_DRV_IOCTL CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_NEITHER, FILE_ANY_ACCESS)

/// 卸载所有EPT hook - 移除当前安装的所有内存hook
#define UNHOOK_DRV_IOCTL CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_NEITHER, FILE_ANY_ACCESS)

/// 安装代码监视点 - 在指定内存区域设置执行监视，用于代码访问跟踪
#define WATCH_DRV_IOCTL CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_NEITHER, FILE_ANY_ACCESS)

/// 获取访问记录 - 检索被监视代码区域的访问历史记录
#define GET_RECORDS_DRV_IOCTL CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_NEITHER, FILE_ANY_ACCESS)

/**
 * @brief 确保在64位环境下运行
 * 该hypervisor仅支持64位架构，因为：
 * 1. EPT(Extended Page Tables)功能需要64位模式
 * 2. VMX操作需要64位寄存器支持
 * 3. 现代虚拟化技术主要针对64位平台设计
 */
static_assert(sizeof(void*) == 8);

/**
 * @brief EPT Hook安装请求结构体
 * 
 * 该结构体定义了安装EPT内存hook所需的所有参数。EPT hook通过修改
 * Extended Page Tables来拦截对特定内存页面的访问，实现隐蔽的内存修改。
 * 
 * EPT Hook工作原理：
 * 1. 将目标页面权限设置为只执行(execute-only)
 * 2. 当执行时，CPU访问包含hook代码的虚假页面
 * 3. 当读写时，触发EPT violation，切换到包含原始数据的真实页面
 * 4. 从而实现代码执行与数据访问的页面分离，达到隐蔽hook的效果
 */
struct hook_request
{
	/// 目标进程ID - 要安装hook的进程标识符
	uint32_t process_id{};
	
	/// 目标地址 - 在目标进程中要hook的虚拟内存地址
	const void* target_address{};
	
	/// 源数据 - 指向hook代码/数据的指针，该数据将替换目标地址的内容
	const void* source_data{};
	
	/// 源数据大小 - hook代码/数据的字节数，必须是页面大小的倍数或小于页面大小
	uint64_t source_data_size{};
};

/**
 * @brief 内存监视区域定义
 * 
 * 定义一个需要监视的连续内存区域。监视点会跟踪对该区域的所有
 * 执行访问，并记录访问时的指令指针(RIP)用于分析。
 */
struct watch_region
{
	/// 虚拟地址 - 要监视的内存区域起始地址
	const void* virtual_address{};
	
	/// 长度 - 要监视的内存区域大小(字节数)
	size_t length{};
};

/**
 * @brief 代码监视点安装请求结构体
 * 
 * 用于在指定进程的多个内存区域安装代码执行监视点。监视点通过
 * 设置EPT页面权限为execute-only来实现，当代码被执行时正常运行，
 * 当发生读写访问时触发EPT violation进行记录。
 * 
 * 监视点应用场景：
 * 1. 恶意软件分析 - 跟踪代码执行流程
 * 2. 逆向工程 - 理解程序执行路径  
 * 3. 安全研究 - 检测代码注入和ROP攻击
 */
struct watch_request
{
	/// 目标进程ID - 要安装监视点的进程标识符
	uint32_t process_id{};
	
	/// 监视区域数组 - 指向watch_region结构体数组的指针
	const watch_region* watch_regions{};
	
	/// 监视区域数量 - watch_regions数组中的元素个数
	uint64_t watch_region_count{};
};
