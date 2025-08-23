/**
 * @file hypervisor.hpp
 * @brief Intel VT-x Hypervisor核心类定义
 * 
 * 该文件定义了基于Intel VT-x技术的type-1 hypervisor的主要接口。
 * 该hypervisor专门为EPT(Extended Page Tables) hooking设计，提供：
 * 1. VMX虚拟化环境的管理
 * 2. EPT页表的操作接口
 * 3. 内存hook和代码监视点功能
 * 4. 跨CPU核心的虚拟化状态同步
 */

#pragma once

#include "vmx.hpp"

/**
 * @brief Intel VT-x Hypervisor主控制类
 * 
 * 该类实现了一个轻量级的type-1 hypervisor，专门用于内存hooking和代码监视。
 * 核心特性：
 * - 基于Intel VMX(Virtual Machine Extensions)技术
 * - 使用EPT(Extended Page Tables)实现隐蔽的内存hook
 * - 支持多核CPU环境下的同步操作
 * - 提供进程级别的hook管理和清理
 * 
 * 设计原则：
 * - 单例模式：整个系统中只允许存在一个hypervisor实例
 * - RAII资源管理：构造时初始化，析构时自动清理
 * - 异常安全：所有操作都有适当的错误处理
 * 
 * @note 该hypervisor运行在VMX根模式(root mode)中，被虚拟化的系统运行在
 *       VMX非根模式(non-root mode)中
 */
class hypervisor
{
public:
	/**
	 * @brief 构造hypervisor实例并初始化VMX环境
	 * 
	 * 执行以下初始化步骤：
	 * 1. 检查CPU是否支持VMX特性
	 * 2. 验证VMX是否在BIOS中启用
	 * 3. 为每个CPU核心分配VMX状态结构
	 * 4. 在所有CPU核心上启用VMX根模式
	 * 5. 初始化EPT页表结构
	 * 
	 * @throws std::runtime_error 如果VMX不支持、不可用或初始化失败
	 * 
	 * @note 运行在PASSIVE_LEVEL IRQL
	 * @note 构造函数会自动调用enable()方法启动虚拟化
	 */
	hypervisor();
	
	/**
	 * @brief 析构hypervisor实例并清理所有资源
	 * 
	 * 清理序列：
	 * 1. 禁用所有EPT hook
	 * 2. 在所有CPU核心上退出VMX根模式
	 * 3. 释放VMX状态结构内存
	 * 4. 清理EPT页表
	 * 
	 * @note 析构函数不抛出异常，所有错误都被内部处理
	 * @note 运行在PASSIVE_LEVEL IRQL
	 */
	~hypervisor();

	// 禁用移动和拷贝语义，确保单例模式
	hypervisor(hypervisor&& obj) noexcept = delete;
	hypervisor& operator=(hypervisor&& obj) noexcept = delete;
	hypervisor(const hypervisor& obj) = delete;
	hypervisor& operator=(const hypervisor& obj) = delete;

	/**
	 * @brief 在所有CPU核心上启用VMX虚拟化环境
	 * 
	 * 该方法会在每个CPU核心上执行以下操作：
	 * 1. 捕获当前CPU状态(寄存器、段描述符等)
	 * 2. 初始化VMCS(Virtual Machine Control Structure)
	 * 3. 配置VMX控制字段和EPT指针
	 * 4. 执行VMLAUNCH指令进入VMX根模式
	 * 
	 * @note 运行在PASSIVE_LEVEL IRQL
	 * @note 使用KeGenericCallDpc在每个CPU核心上同步执行
	 * @note 如果任何核心上的启用失败，整个操作将失败
	 */
	void enable();
	
	/**
	 * @brief 在所有CPU核心上禁用VMX虚拟化环境
	 * 
	 * 通过特殊的CPUID指令向hypervisor发送关闭信号，
	 * 触发VM exit并执行VMXOFF指令退出VMX根模式。
	 * 
	 * @note 运行在PASSIVE_LEVEL IRQL
	 * @note 禁用操作必须在所有核心上同步执行
	 * @note 如果禁用失败，将触发内核panic以确保系统安全
	 */
	void disable();

	/**
	 * @brief 检查hypervisor是否处于激活状态
	 * 
	 * 通过执行CPUID指令检查虚拟化环境是否正常运行。
	 * 
	 * @return true hypervisor正在运行
	 * @return false hypervisor未运行或已停止
	 * 
	 * @note 该方法可以在任何IRQL级别安全调用
	 */
	bool is_enabled() const;

	/**
	 * @brief 安装EPT内存hook
	 * 
	 * 在指定进程的目标地址安装隐蔽的内存hook。EPT hook的工作原理：
	 * 1. 创建包含hook代码的虚假页面(fake page)
	 * 2. 设置目标页面为执行专用权限(execute-only)
	 * 3. 当执行时，CPU访问虚假页面中的hook代码
	 * 4. 当读写时，触发EPT violation切换到真实页面
	 * 
	 * @param destination 目标进程中要hook的虚拟地址
	 * @param source 包含hook代码的源数据指针
	 * @param length hook代码的长度(字节)
	 * @param source_pid 源进程ID(提供hook代码的进程)
	 * @param target_pid 目标进程ID(被hook的进程)
	 * @param hints 可选的地址翻译提示，用于优化跨进程地址翻译
	 * 
	 * @return true hook安装成功
	 * @return false hook安装失败
	 * 
	 * @note 运行在PASSIVE_LEVEL IRQL
	 * @note hook长度必须在页面边界内
	 * @note 安装后会在所有CPU核心上失效EPT TLB
	 */
	bool install_ept_hook(const void* destination, const void* source, size_t length, process_id source_pid,
	                      process_id target_pid, const utils::list<vmx::ept_translation_hint>& hints = {});

	/**
	 * @brief 安装单个EPT代码监视点
	 * 
	 * 在指定的物理页面上安装代码执行监视点。监视点会记录所有对该页面的执行访问，
	 * 包括访问时的指令指针(RIP)，用于代码执行流程分析。
	 * 
	 * @param physical_page 要监视的物理页面基址
	 * @param source_pid 源进程ID
	 * @param target_pid 目标进程ID
	 * @param invalidate 是否立即失效EPT缓存
	 * 
	 * @return true 监视点安装成功
	 * @return false 监视点安装失败
	 * 
	 * @note 运行在PASSIVE_LEVEL IRQL
	 * @note 监视点通过设置页面权限为execute-only实现
	 */
	bool install_ept_code_watch_point(uint64_t physical_page, process_id source_pid, process_id target_pid,
	                                  bool invalidate = true) const;
	                                  
	/**
	 * @brief 批量安装EPT代码监视点
	 * 
	 * 在多个物理页面上同时安装代码监视点，相比单独安装更高效。
	 * 
	 * @param physical_pages 物理页面基址数组
	 * @param count 数组中的页面数量
	 * @param source_pid 源进程ID
	 * @param target_pid 目标进程ID
	 * 
	 * @return true 所有监视点安装成功
	 * @return false 至少一个监视点安装失败
	 * 
	 * @note 运行在PASSIVE_LEVEL IRQL
	 * @note 批量操作完成后统一失效EPT缓存
	 */
	bool install_ept_code_watch_points(const uint64_t* physical_pages, size_t count, process_id source_pid,
	                                   process_id target_pid) const;

	/**
	 * @brief 禁用所有EPT hook
	 * 
	 * 移除当前安装的所有EPT内存hook和代码监视点，恢复原始页面权限。
	 * 
	 * @note 运行在PASSIVE_LEVEL IRQL
	 * @note 操作完成后会失效所有CPU核心上的EPT缓存
	 */
	void disable_all_ept_hooks() const;

	/**
	 * @brief 获取EPT页表管理器的引用
	 * 
	 * @return vmx::ept& EPT管理器对象引用
	 * 
	 * @note 返回的引用在hypervisor生命周期内有效
	 */
	vmx::ept& get_ept() const;

	/**
	 * @brief 获取hypervisor单例实例
	 * 
	 * @return hypervisor* 实例指针，如果未初始化则返回nullptr
	 * 
	 * @note 线程安全的单例访问方法
	 */
	static hypervisor* get_instance();

	/**
	 * @brief 清理指定进程相关的所有EPT hook和监视点
	 * 
	 * 当进程即将终止时调用，清理该进程相关的所有虚拟化资源：
	 * 1. 移除该进程的所有EPT hook
	 * 2. 清理该进程的所有代码监视点
	 * 3. 释放相关的内存资源
	 * 
	 * @param process 进程ID
	 * 
	 * @return true 清理了至少一个hook或监视点
	 * @return false 没有找到该进程相关的资源
	 * 
	 * @note 运行在PASSIVE_LEVEL IRQL
	 * @note 该方法通常由进程通知回调调用
	 */
	bool cleanup_process(process_id process);

private:
	/// 系统中CPU核心的数量
	uint32_t vm_state_count_{0};
	
	/// VMX状态结构数组 - 每个CPU核心一个状态结构
	vmx::state** vm_states_{nullptr};
	
	/// EPT页表管理器实例 - 负责所有EPT相关操作
	vmx::ept* ept_{nullptr};

	/**
	 * @brief 在当前CPU核心上启用VMX虚拟化
	 * 
	 * 该方法在特定CPU核心上执行VMX初始化序列：
	 * 1. 捕获当前CPU的执行上下文
	 * 2. 配置和初始化VMCS
	 * 3. 设置Guest和Host状态字段
	 * 4. 配置VMX控制字段和EPT指针
	 * 5. 执行VMLAUNCH进入VMX根模式
	 * 
	 * @param system_directory_table_base 系统页目录表基址(CR3值)
	 * 
	 * @throws std::runtime_error 如果VMX启用失败
	 * 
	 * @note 运行在DISPATCH_LEVEL IRQL(通过DPC调用)
	 * @note 该方法必须在目标CPU核心上执行
	 * @note 使用内联汇编进行关键的CPU状态捕获
	 */
	void enable_core(uint64_t system_directory_table_base);
	
	/**
	 * @brief 尝试在当前CPU核心上启用VMX(异常安全版本)
	 * 
	 * 与enable_core功能相同，但不抛出异常，而是返回成功状态。
	 * 用于需要错误处理但不希望异常传播的场景。
	 * 
	 * @param system_directory_table_base 系统页目录表基址
	 * 
	 * @return true VMX启用成功
	 * @return false VMX启用失败
	 * 
	 * @note 运行在DISPATCH_LEVEL IRQL
	 * @note 内部捕获所有异常并转换为返回值
	 */
	bool try_enable_core(uint64_t system_directory_table_base);
	
	/**
	 * @brief 在当前CPU核心上禁用VMX虚拟化
	 * 
	 * 通过特殊的CPUID指令触发VM exit，然后在exit处理程序中：
	 * 1. 恢复原始的CPU执行上下文
	 * 2. 执行VMXOFF指令退出VMX根模式
	 * 3. 清理VMCS和相关数据结构
	 * 
	 * @note 运行在DISPATCH_LEVEL IRQL(通过DPC调用)
	 * @note 如果禁用失败，会触发内核panic确保系统安全
	 * @note 必须在目标CPU核心上执行
	 */
	void disable_core();

	/**
	 * @brief 为所有CPU核心分配VMX状态结构
	 * 
	 * 根据系统中的CPU核心数量分配对应的vmx::state结构：
	 * 1. 获取系统CPU核心数量
	 * 2. 分配状态结构指针数组
	 * 3. 为每个核心分配页面对齐的状态结构
	 * 4. 初始化EPT页表管理器
	 * 
	 * @throws std::runtime_error 如果内存分配失败
	 * 
	 * @note 运行在PASSIVE_LEVEL IRQL
	 * @note 状态结构必须页面对齐，因为包含VMCS等硬件结构
	 */
	void allocate_vm_states();
	
	/**
	 * @brief 释放所有VMX状态结构的内存
	 * 
	 * 清理序列：
	 * 1. 释放EPT页表管理器
	 * 2. 释放每个CPU核心的状态结构
	 * 3. 释放状态结构指针数组
	 * 
	 * @note 运行在PASSIVE_LEVEL IRQL
	 * @note 该方法是异常安全的，即使部分释放失败也会继续
	 */
	void free_vm_states();

	/**
	 * @brief 在所有CPU核心上失效EPT缓存
	 * 
	 * 当EPT页表结构发生变化后，必须失效所有CPU核心上的EPT TLB缓存
	 * 以确保硬件使用最新的页表映射。使用INVEPT指令实现。
	 * 
	 * @note 运行在PASSIVE_LEVEL IRQL
	 * @note 通过DPC在每个CPU核心上同步执行INVEPT指令
	 * @note EPT TLB失效是确保hook正确性的关键操作
	 */
	void invalidate_cores() const;

	/**
	 * @brief 获取当前CPU核心对应的VMX状态结构
	 * 
	 * 根据当前CPU核心索引返回对应的vmx::state结构指针。
	 * 
	 * @return vmx::state* 当前CPU核心的VMX状态结构指针
	 * 
	 * @note 可以在任何IRQL级别调用
	 * @note 返回的指针在hypervisor生命周期内有效
	 * @note 每个CPU核心有独立的VMX状态结构
	 */
	vmx::state* get_current_vm_state() const;
};
