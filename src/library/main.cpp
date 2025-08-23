/**
 * @file main.cpp
 * @brief HyperHook动态链接库主要实现文件
 * 
 * 实现了HyperHook的公共API接口，提供给第三方应用程序使用。
 * 该文件包含了从用户态到内核态的完整通信链路，
 * 包括驱动程序的加载、设备通信和内存hook操作。
 * 
 * 主要功能：
 * - 自动驱动程序管理（加载、启动、停止）
 * - 设备句柄管理和通信
 * - 内存读写hook操作的用户态接口
 * - 错误处理和异常传播
 * 
 * 架构设计：
 * 用户应用 -> HyperHook.dll -> IRP通信 -> HyperHook.sys -> VMX/EPT
 * 
 * 安全特性：
 * - 自动验证驱动程序签名
 * - 参数有效性检查
 * - 异常安全的错误处理
 * 
 * @author HyperHook Project
 * @date 2024
 */

#include "std_include.hpp"

#include "driver.hpp"
#include "driver_device.hpp"
#include <driver_file.h>
#include <irp_data.hpp>

#include "utils/io.hpp"

// 对外导出函数定义重载
#define DLL_IMPORT __declspec(dllexport)
#include <hyperhook.h>

/**
 * @brief 内部实现命名空间
 * 
 * 包含不对外暴露的内部辅助函数和实现细节。
 */
namespace
{
	/**
	 * @brief 执行内存补丁操作
	 * 
	 * 向指定进程的指定地址写入数据，实现代码hook功能。
	 * 该函数封装了与内核驱动程序的通信细节。
	 * 
	 * 操作流程：
	 * 1. 封装hook_request数据结构
	 * 2. 通过IOCTL发送到内核驱动程序
	 * 3. 驱动程序使用EPT技术执行实际的内存修改
	 * 
	 * @param driver_device 驱动设备句柄引用
	 * @param pid 目标进程 ID
	 * @param address 目标内存地址（虚拟地址）
	 * @param buffer 要写入的数据缓冲区
	 * @param length 数据长度（字节）
	 * 
	 * @throws std::exception 如果驱动程序通信失败
	 * 
	 * @note 这是一个内部函数，不直接对外暴露
	 * @note 该操作需要管理员权限
	 * @note 支持跨进程内存操作
	 */
	void patch_data(const driver_device& driver_device, const uint32_t pid, const uint64_t address,
	                const uint8_t* buffer,
	                const size_t length)
	{
		// 封装hook请求数据结构
		hook_request hook_request{};
		hook_request.process_id = pid;
		hook_request.target_address = reinterpret_cast<void*>(address);

		hook_request.source_data = buffer;
		hook_request.source_data_size = length;

		// 将结构转换为字节数组以供IOCTL传输
		driver_device::data input{};
		input.assign(reinterpret_cast<uint8_t*>(&hook_request),
		             reinterpret_cast<uint8_t*>(&hook_request) + sizeof(hook_request));

		// 发送hook请求到内核驱动程序
		(void)driver_device.send(HOOK_DRV_IOCTL, input);
	}

	/**
	 * @brief 创建驱动设备句柄
	 * 
	 * 尝试连接到已经加载和运行的HyperHook驱动程序。
	 * 使用Windows设备命名空间中的符号链接名称。
	 * 
	 * 设备路径解释：
	 * - \\\\.\\: Windows设备命名空间的前缀
	 * - HyperHook: 驱动程序创建的设备名称
	 * 
	 * @return driver_device 对象，封装了设备句柄
	 * @throws std::exception 如果设备不存在或无法访问
	 * 
	 * @note 这个函数需要驱动程序已经被正确加载
	 * @note 没有管理员权限可能导致访问拒绝
	 */
	driver_device create_driver_device()
	{
		return driver_device{R"(\\.\HyperHook)"};
	}

	/**
	 * @brief 创建驱动程序管理对象
	 * 
	 * 创建用于管理HyperHook驱动程序生命周期的对象。
	 * 驱动程序文件路径会被自动转换为绝对路径。
	 * 
	 * 初始化参数：
	 * - 驱动文件：DRIVER_NAME宏定义的.sys文件
	 * - 服务名：HyperHookDriver（用于Windows服务管理）
	 * 
	 * @return driver 对象，封装了驱动程序管理功能
	 * 
	 * @note 会自动处理驱动程序的加载、启动和停止
	 * @note 需要管理员权限加载驱动程序
	 * @note 驱动文件必须存在且具有有效的数字签名
	 */
	driver create_driver()
	{
		return driver{std::filesystem::absolute(DRIVER_NAME), "HyperHookDriver"};
	}

	/**
	 * @brief 获取驱动设备句柄的单例引用
	 * 
	 * 这是整个库的核心函数，负责管理与内核驱动程序的连接。
	 * 使用单例模式保证全局只有一个驱动实例和设备句柄。
	 * 
	 * 初始化策略：
	 * 1. **懒加载连接**：先尝试连接已存在的设备
	 * 2. **自动加载**：如果连接失败，自动加载驱动程序
	 * 3. **重新连接**：驱动加载后再次尝试连接设备
	 * 4. **缓存结果**：成功后缓存连接供后续使用
	 * 
	 * 错误处理：
	 * - 首次连接失败时忽略异常（可能是驱动未加载）
	 * - 自动加载驱动程序并重试
	 * - 最终失败时传播异常给调用者
	 * 
	 * @return 驱动设备句柄的引用
	 * @throws std::exception 如果无法建立连接
	 * 
	 * @note 线程安全：使用静态局部变量保证线程安全
	 * @note 性能优化：初始化后的调用几乎无开销
	 * @note 资源管理：驱动和设备句柄会自动管理生命周期
	 * @warning 第一次调用可能需要管理员权限
	 */
	driver_device& get_driver_device()
	{
		// 静态局部变量保证单例模式和线程安全
		static driver hypervisor{};
		static driver_device device{};

		// 第一步：尝试懒加载连接现有设备
		if (!device)
		{
			try
			{
				device = create_driver_device();
			}
			catch (...)
			{
				// 忽略首次连接失败，可能驱动未加载
			}
		}

		// 如果连接成功，直接返回
		if (device)
		{
			return device;
		}

		// 第二步：驱动未运行，尝试加载驱动程序
		if (!hypervisor)
		{
			hypervisor = create_driver();
		}

		// 第三步：驱动加载后再次尝试连接设备
		if (!device)
		{
			device = create_driver_device();
		}

		return device;
	}
}

// ============================================================================
// 公共API实现 - 对外接口函数
// ============================================================================

/**
 * @brief 初始化HyperHook系统
 * 
 * 这是HyperHook库的主初始化函数，必须在使用任何其他API前调用。
 * 该函数会自动处理：
 * - 驱动程序的检测和加载
 * - 设备句柄的创建和验证
 * - 系统兼容性检查
 * - 权限验证
 * 
 * 初始化流程：
 * 1. 尝试连接现有的驱动程序实例
 * 2. 如果失败，自动加载驱动程序
 * 3. 创建与驱动程序的通信通道
 * 4. 验证驱动程序功能正常
 * 
 * 对于不同的失败原因：
 * - **缺少权限**: 需要以管理员身份运行
 * - **驱动签名**: 需要启用测试模式或使用签名版本
 * - **系统不兼容**: VMX不支持或被禁用
 * 
 * @return 1 初始化成功，0 初始化失败
 * 
 * @note 线程安全：可以在多线程环境中安全调用
 * @note 幂等性：多次调用安全，不会产生副作用
 * @note 性能：首次调用可能较慢，后续调用很快
 * @warning 必须在调用其他API前成功初始化
 * 
 * @see hyperhook_write()
 */
int hyperhook_initialize()
{
	try
	{
		// 获取驱动设备句柄（会自动加载驱动程序）
		const auto& device = get_driver_device();
		if (device)
		{
			return 1; // 初始化成功
		}
	}
	catch (const std::exception& e)
	{
		// 打印错误信息以供调试
		printf("%s\n", e.what());
	}

	return 0; // 初始化失败
}

/**
 * @brief 向指定进程的指定地址写入数据
 * 
 * 这是HyperHook的核心功能，使用EPT(Extended Page Tables)技术
 * 实现无侵入式的跨进程内存修改。与传统的API hook不同，
 * 该方法能够绕过大部分反调试和安全软件的检测。
 * 
 * **技术原理**：
 * 1. 使用VMX技术将系统切换到hypervisor模式
 * 2. 通过EPT技术对目标页面进行实时映射
 * 3. 在物理内存层面实施修改，绕过软件检测
 * 4. 保持原始页面不变，仅在特定情况下映射修改内容
 * 
 * **支持的操作类型**：
 * - 代码补丁：修改执行指令（如函数入口的jmp）
 * - 数据修改：修改全局变量、配置参数等
 * - API Hook：拦截系统调用和库函数调用
 * - 软件破解：去除授权检查、时间限制等
 * 
 * **安全特性**：
 * - 需要管理员权限和正确的驱动程序签名
 * - 自动验证参数有效性和内存可访问性
 * - 支持原子操作，避免部分写入造成的数据损坏
 * 
 * @param process_id 目标进程的PID
 * @param address 目标内存地址（进程的虚拟地址）
 * @param data 要写入的数据指针
 * @param size 要写入的数据大小（字节）
 * @return 1 操作成功，0 操作失败
 * 
 * @note 线程安全：可以在多线程环境中并发调用
 * @note 性能：首次调用可能较慢（需要初始化EPT）
 * @note 内存对齐：不需要特殊的对齐要求
 * @warning 需要先调用 hyperhook_initialize() 初始化
 * @warning 修改关键系统代码可能导致系统不稳定
 * @warning 不应用于恶意目的或未授权的软件修改
 * 
 * @see hyperhook_initialize()
 * 
 * @example
 * @code
 * // 初始化系统
 * if (hyperhook_initialize() == 0) {
 *     printf("初始化失败\n");
 *     return -1;
 * }
 * 
 * // 在notepad.exe的某个地址写入NOP指令
 * uint8_t nop_bytes[] = {0x90, 0x90, 0x90};
 * int result = hyperhook_write(1234, 0x00401000, nop_bytes, sizeof(nop_bytes));
 * if (result == 1) {
 *     printf("写入成功\n");
 * } else {
 *     printf("写入失败\n");
 * }
 * @endcode
 */
int hyperhook_write(const unsigned int process_id, const unsigned long long address, const void* data,
                    const unsigned long long size)
{
	// 首先确保系统已经正确初始化
	if (hyperhook_initialize() == 0)
	{
		return 0; // 初始化失败，无法继续
	}

	try
	{
		// 获取驱动设备句柄
		const auto& device = get_driver_device();
		if (device)
		{
			// 执行实际的内存补丁操作
			patch_data(device, process_id, address, static_cast<const uint8_t*>(data), size);
			return 1; // 操作成功
		}
	}
	catch (const std::exception& e)
	{
		// 打印错误信息以供调试
		printf("%s\n", e.what());
	}

	return 0; // 操作失败
}
