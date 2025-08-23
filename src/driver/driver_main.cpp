/**
 * @file driver_main.cpp
 * @brief Hypervisor内核驱动的主入口点和全局管理类
 * 
 * 该文件实现了Windows内核驱动的标准入口点(DriverEntry)和卸载例程，
 * 以及管理hypervisor生命周期的核心逻辑。驱动负责：
 * 1. 初始化VMX虚拟化环境
 * 2. 设置设备对象供用户态通信
 * 3. 注册系统回调以处理进程生命周期和电源管理事件
 * 4. 维护EPT hook的一致性
 */

#include "std_include.hpp"
#include "logging.hpp"
#include "sleep_callback.hpp"
#include "irp.hpp"
//#include "exception.hpp"
#include "hypervisor.hpp"
#include "globals.hpp"
#include "process.hpp"
#include "process_callback.hpp"

/// DOS设备名称 - 用户态应用程序可见的符号链接名称
#define DOS_DEV_NAME L"\\DosDevices\\HyperHook"

/// NT设备名称 - 内核中的实际设备对象名称
#define DEV_NAME L"\\Device\\HyperHook"

/**
 * @brief 全局驱动管理类
 * 
 * 该类负责整个hypervisor驱动的生命周期管理，包括：
 * 1. hypervisor实例的创建和销毁
 * 2. 电源管理事件的处理(休眠/唤醒时的VMX状态管理)
 * 3. 进程生命周期监控(清理进程相关的EPT hook)
 * 4. IRP处理器的初始化(用户态通信接口)
 * 
 * 设计为单例模式，整个驱动生命周期中只存在一个实例。
 * 
 * @note 该类使用RAII模式管理资源，构造时初始化所有组件，
 *       析构时自动清理，确保即使发生异常也能正确释放资源。
 */
class global_driver
{
public:
	/**
	 * @brief 构造全局驱动实例
	 * 
	 * 初始化顺序：
	 * 1. 注册电源管理回调 - 处理系统休眠/唤醒事件
	 * 2. 注册进程回调 - 监控进程创建/销毁事件
	 * 3. 创建设备对象和IRP处理器 - 建立用户态通信通道
	 * 4. 初始化hypervisor实例 - 启动VMX虚拟化环境
	 * 
	 * @param driver_object Windows驱动对象指针，用于创建设备和注册回调
	 * 
	 * @throws std::exception 如果VMX不支持或初始化失败
	 * 
	 * @note 运行在PASSIVE_LEVEL IRQL，可以分页内存访问
	 */
	global_driver(const PDRIVER_OBJECT driver_object)
		: sleep_callback_([this](const sleep_callback::type type)
		  {
			  this->sleep_notification(type);
		  }),
		  process_callback_(
			  [this](const process_id parent_id, const process_id process_id, const process_callback::type type)
			  {
				  this->process_notification(parent_id, process_id, type);
			  }),
		  irp_(driver_object, DEV_NAME, DOS_DEV_NAME)
	{
		debug_log("Driver started\n");
	}

	/**
	 * @brief 析构函数 - 清理所有资源
	 * 
	 * 清理顺序(遵循构造的逆序)：
	 * 1. 自动清理hypervisor实例(调用hypervisor析构函数)
	 * 2. 自动注销进程回调
	 * 3. 自动注销电源管理回调  
	 * 4. 自动删除设备对象和符号链接
	 * 
	 * @note 析构函数不应抛出异常，所有异常都被捕获并记录
	 */
	~global_driver()
	{
		debug_log("Unloading driver\n");
	}

	// 禁用移动和拷贝语义，确保单例模式
	global_driver(global_driver&&) noexcept = delete;
	global_driver& operator=(global_driver&&) noexcept = delete;
	global_driver(const global_driver&) = delete;
	global_driver& operator=(const global_driver&) = delete;

	/**
	 * @brief 驱动卸载前的预处理
	 * 
	 * 在驱动卸载例程中调用，用于执行需要在成员析构之前完成的清理工作。
	 * 当前实现为空，所有清理工作由析构函数处理。
	 * 
	 * @param driver_object 驱动对象指针(未使用)
	 * 
	 * @note 运行在PASSIVE_LEVEL IRQL
	 */
	void pre_destroy(const PDRIVER_OBJECT /*driver_object*/)
	{
	}

private:
	/// 记录休眠前hypervisor的启用状态，用于唤醒时恢复
	bool hypervisor_was_enabled_{false};
	
	/// hypervisor实例 - 负责VMX虚拟化和EPT管理的核心组件
	hypervisor hypervisor_{};
	
	/// 电源管理回调处理器 - 监听系统休眠/唤醒事件
	sleep_callback sleep_callback_{};
	
	/// 进程生命周期回调处理器 - 监听进程创建/销毁事件  
	process_callback::scoped_process_callback process_callback_{};
	
	/// IRP处理器 - 处理来自用户态的设备控制请求
	irp irp_{};

	/**
	 * @brief 电源状态变化通知处理函数
	 * 
	 * 当系统即将进入休眠状态或从休眠状态唤醒时被调用。
	 * VMX虚拟化环境无法在系统休眠时保持，因此需要：
	 * 1. 休眠前：禁用所有CPU核心上的VMX，保存状态
	 * 2. 唤醒后：如果之前是启用状态，重新启用VMX环境
	 * 
	 * @param type 电源状态变化类型(sleep/wakeup)
	 * 
	 * @note 运行在PASSIVE_LEVEL IRQL
	 * @note 必须在所有CPU核心上同步执行VMX状态变化
	 * 
	 * 技术说明：
	 * - VMX根模式状态不能跨越系统休眠周期保持
	 * - VMCS(Virtual Machine Control Structure)在休眠后会失效
	 * - 必须重新执行VMXON指令来重新进入VMX根模式
	 */
	void sleep_notification(const sleep_callback::type type)
	{
		if (type == sleep_callback::type::sleep)
		{
			debug_log("Going to sleep...\n");
			this->hypervisor_was_enabled_ = this->hypervisor_.is_enabled();
			this->hypervisor_.disable();
		}

		if (type == sleep_callback::type::wakeup && this->hypervisor_was_enabled_)
		{
			debug_log("Waking up...\n");
			this->hypervisor_.enable();
		}
	}

	/**
	 * @brief 进程生命周期事件通知处理函数
	 * 
	 * 当系统中有进程创建或销毁时被调用。主要用于：
	 * 1. 进程销毁时：清理该进程相关的所有EPT hook
	 * 2. 避免内存泄漏和悬空指针引用
	 * 3. 维护EPT页表的一致性
	 * 
	 * @param parent_id 父进程ID(未使用)
	 * @param process_id 目标进程ID
	 * @param type 进程事件类型(create/destroy)
	 * 
	 * @note 运行在PASSIVE_LEVEL IRQL
	 * @note 进程销毁时，该进程的虚拟地址空间即将失效
	 * 
	 * 清理必要性：
	 * - EPT hook通常与特定进程的虚拟地址空间关联
	 * - 进程销毁后，相关的物理页面可能被重新分配给其他用途
	 * - 必须及时清理避免hook错误的内存区域
	 */
	void process_notification(process_id /*parent_id*/, const process_id process_id, const process_callback::type type)
	{
		if (type == process_callback::type::destroy)
		{
			if (this->hypervisor_.cleanup_process(process_id))
			{
				const auto proc = process::find_process_by_id(process_id);
				if(proc)
				{
					debug_log("Handled termination of %s\n", proc.get_image_filename());
				}
			}
		}
	}
};

/// 全局驱动实例指针 - 整个驱动生命周期中的单例对象
global_driver* global_driver_instance{nullptr};

/**
 * @brief Windows内核驱动标准卸载例程
 * 
 * 当驱动被卸载时由操作系统调用。负责：
 * 1. 有序清理全局驱动实例及其所有资源
 * 2. 执行全局析构函数清理静态对象
 * 3. 确保所有VMX操作完全停止
 * 4. 释放所有分配的内存和系统资源
 * 
 * @param driver_object 驱动对象指针
 * 
 * @note 运行在PASSIVE_LEVEL IRQL
 * @note 卸载过程必须是幂等的，多次调用不应产生副作用
 * @note 所有异常都被捕获，防止在卸载过程中导致系统不稳定
 * 
 * 卸载顺序至关重要：
 * 1. 首先调用pre_destroy进行预清理
 * 2. 然后销毁驱动实例(触发所有析构函数)
 * 3. 最后清理全局静态对象
 * 
 * 错误处理策略：
 * - 捕获所有异常，记录但不重新抛出
 * - 即使发生错误，也尽可能完成清理工作
 * - 避免在卸载过程中导致蓝屏
 */
_Function_class_(DRIVER_UNLOAD) void unload(const PDRIVER_OBJECT driver_object)
{
	try
	{
		if (global_driver_instance)
		{
			global_driver_instance->pre_destroy(driver_object);
			delete global_driver_instance;
			global_driver_instance = nullptr;
		}

		globals::run_destructors();
	}
	catch (std::exception& e)
	{
		debug_log("Destruction error occured: %s\n", e.what());
	}
	catch (...)
	{
		debug_log("Unknown destruction error occured. This should not happen!");
	}
}

/**
 * @brief Windows内核驱动标准入口点
 * 
 * 驱动被加载时由操作系统调用的第一个函数。负责：
 * 1. 设置驱动卸载例程
 * 2. 初始化全局构造函数
 * 3. 创建全局驱动管理实例
 * 4. 启动hypervisor虚拟化环境
 * 
 * @param driver_object Windows驱动对象，包含驱动的基本信息和回调函数指针
 * @param registry_path 驱动在注册表中的路径(未使用)
 * 
 * @return NTSTATUS 返回状态码
 * @retval STATUS_SUCCESS 驱动加载成功
 * @retval STATUS_INTERNAL_ERROR 驱动加载失败
 * 
 * @note 运行在PASSIVE_LEVEL IRQL
 * @note 如果返回失败状态，操作系统将自动调用卸载例程
 * 
 * 初始化序列：
 * 1. 注册卸载例程(确保异常时能正确清理)
 * 2. 运行全局构造函数(初始化静态对象)
 * 3. 创建驱动管理实例(启动所有服务)
 * 
 * 错误处理：
 * - 任何异常都会导致驱动加载失败
 * - 返回STATUS_INTERNAL_ERROR通知系统加载失败
 * - 系统会自动调用卸载例程进行清理
 */
extern "C" NTSTATUS DriverEntry(const PDRIVER_OBJECT driver_object, PUNICODE_STRING /*registry_path*/)
{
	try
	{
		driver_object->DriverUnload = unload;
		globals::run_constructors();
		global_driver_instance = new global_driver(driver_object);
	}
	catch (std::exception& e)
	{
		debug_log("Error: %s\n", e.what());
		return STATUS_INTERNAL_ERROR;
	}
	catch (...)
	{
		debug_log("Unknown initialization error occured");
		return STATUS_INTERNAL_ERROR;
	}

	return STATUS_SUCCESS;
}
