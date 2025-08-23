/**
 * @file memory.hpp
 * @brief Windows内核驱动内存管理头文件
 * 
 * 定义了内核模式下的内存分配、释放、映射和检查操作。
 * 为hypervisor环境提供了专用的内存管理功能：
 * 
 * 主要功能模块：
 * - 对齐内存分配：用于VMCS、VMXON区域等VMX数据结构
 * - 非分页内存分配：用于高IRQL环境下的对象管理
 * - 物理内存操作：直接访问物理地址，用于EPT管理
 * - 地址转换：虚拟地址与物理地址间的转换
 * - 内存检查：安全验证用户态地址的有效性
 * 
 * IRQL要求说明：
 * - PASSIVE_LEVEL：不限制，所有函数都可使用
 * - APC_LEVEL：限制部分物理内存操作
 * - DISPATCH_LEVEL：限制大部分分配/释放操作
 * - 更高IRQL：仅允许有限的无限制函数
 * 
 * @author HyperHook Project
 * @date 2024
 */

#pragma once

/**
 * @namespace memory
 * @brief 内核内存管理功能集合
 * 
 * 包含了适用于内核模式的内存分配、释放、映射和检查操作。
 * 所有操作都经过针对hypervisor环境的优化，特别支持VMX相关需求。
 */
namespace memory
{
	// ============================================================================
	// 对齐内存分配 API - 用于VMX数据结构
	// ============================================================================

	/**
	 * @brief 释放对齐分配的连续物理内存
	 * @param memory 要释放的内存指针，可为nullptr
	 * @note IRQL <= DISPATCH_LEVEL
	 */
	_IRQL_requires_max_(DISPATCH_LEVEL)
	void free_aligned_memory(void* memory);

	/**
	 * @brief 分配连续的物理内存并零初始化
	 * @param size 分配大小（字节）
	 * @return 内存指针，失败时为nullptr
	 * @note IRQL <= DISPATCH_LEVEL
	 * @note 用于VMCS、VMXON region等VMX数据结构
	 */
	_Must_inspect_result_
	_IRQL_requires_max_(DISPATCH_LEVEL)
	void* allocate_aligned_memory(size_t size);

	// ============================================================================
	// 物理内存访问 API - 用于EPT管理
	// ============================================================================

	/**
	 * @brief 读取物理地址的内容
	 * @param destination 目标缓冲区
	 * @param physical_address 源物理地址
	 * @param size 读取大小
	 * @return 成功返回true
	 * @note IRQL <= APC_LEVEL
	 */
	_IRQL_requires_max_(APC_LEVEL)
	bool read_physical_memory(void* destination, uint64_t physical_address, size_t size);

	// ============================================================================
	// 地址转换 API - 用于虚拟化管理
	// ============================================================================

	/**
	 * @brief 获取虚拟地址对应的物理地址
	 * @param address 内核虚拟地址
	 * @return 物理地址
	 * @note 无IRQL限制
	 */
	uint64_t get_physical_address(void* address);

	/**
	 * @brief 获取物理地址对应的虚拟地址
	 * @param address 物理地址
	 * @return 内核虚拟地址，未映射时为nullptr
	 * @note 无IRQL限制
	 */
	void* get_virtual_address(uint64_t address);

	// ============================================================================
	// 物理内存映射 API - 用于设备内存访问
	// ============================================================================

	/**
	 * @brief 映射物理内存到虚拟地址空间
	 * @param address 物理地址
	 * @param size 映射大小
	 * @return 映射的虚拟地址，失败时为nullptr
	 * @note IRQL <= DISPATCH_LEVEL
	 */
	_Must_inspect_result_
	_IRQL_requires_max_(DISPATCH_LEVEL)
	void* map_physical_memory(const uint64_t address, const size_t size);

	/**
	 * @brief 取消物理内存映射
	 * @param address 要取消映射的虚拟地址
	 * @param size 映射大小
	 * @note IRQL <= DISPATCH_LEVEL
	 */
	_IRQL_requires_max_(DISPATCH_LEVEL)
	void unmap_physical_memory(void* address, const size_t size);

	// ============================================================================
	// 非分页内存分配 API - 用于高IRQL环境
	// ============================================================================

	/**
	 * @brief 分配非分页内存并零初始化
	 * @param size 分配大小（字节）
	 * @return 内存指针，失败时为nullptr
	 * @note IRQL <= DISPATCH_LEVEL
	 * @note 在任何IRQL下都可访问的内存
	 */
	_Must_inspect_result_
	_IRQL_requires_max_(DISPATCH_LEVEL)
	void* allocate_non_paged_memory(size_t size);

	/**
	 * @brief 释放非分页内存
	 * @param memory 要释放的内存指针，可为nullptr
	 * @note IRQL <= DISPATCH_LEVEL
	 */
	_IRQL_requires_max_(DISPATCH_LEVEL)
	void free_non_paged_memory(void* memory);

	// ============================================================================
	// 内存检查 API - 用于参数验证
	// ============================================================================

	/**
	 * @brief 安全检测内存区域的可读性
	 * @param address 要检测的地址
	 * @param length 内存区域大小
	 * @param alignment 对齐要求（默认1字节）
	 * @return 可读返回true
	 * @note 适用于验证用户态参数
	 */
	bool probe_for_read(const void* address, size_t length, uint64_t alignment = 1);

	/**
	 * @brief 断言内存区域可读性，失败时抛出异常
	 * @param address 要检测的地址
	 * @param length 内存区域大小
	 * @param alignment 对齐要求（默认1字节）
	 * @throws std::runtime_error 如果不可读
	 */
	void assert_readability(const void* address, size_t length, uint64_t alignment = 1);

	/**
	 * @brief 安全检测内存区域的可写性
	 * @param address 要检测的地址
	 * @param length 内存区域大小
	 * @param alignment 对齐要求（默认1字节）
	 * @return 可写返回true
	 * @note 适用于验证用户态参数
	 */
	bool probe_for_write(const void* address, size_t length, uint64_t alignment = 1);

	/**
	 * @brief 断言内存区域可写性，失败时抛出异常
	 * @param address 要检测的地址
	 * @param length 内存区域大小
	 * @param alignment 对齐要求（默认1字节）
	 * @throws std::runtime_error 如果不可写
	 */
	void assert_writability(const void* address, size_t length, uint64_t alignment = 1);

	// ============================================================================
	// 对象级内存管理 API - C++对象的RAII管理
	// ============================================================================

	/**
	 * @brief 在非分页内存中分配并构造C++对象
	 * 
	 * 使用placement new在预先分配的内存中构造对象。适用于：
	 * - 需要在高IRQL下访问的C++对象
	 * - 中断处理例程中使用的数据结构
	 * - VMX相关的小型管理对象
	 * 
	 * @tparam T 要分配的对象类型
	 * @tparam Args 构造函数参数类型
	 * @param args 传递给构造函数的参数
	 * @return 构造完成的对象指针，失败时为nullptr
	 * 
	 * @note IRQL <= DISPATCH_LEVEL
	 * @note 使用完美转发(perfect forwarding)传递构造参数
	 * @note 必须使用free_non_paged_object释放
	 * @warning 构造函数异常可能导致内存泄漏
	 */
	template <typename T, typename... Args>
	T* allocate_non_paged_object(Args ... args)
	{
		auto* object = static_cast<T*>(allocate_non_paged_memory(sizeof(T)));
		if (object)
		{
			// 使用placement new在已分配内存中构造对象
			new(object) T(std::forward<Args>(args)...);
		}

		return object;
	}

	/**
	 * @brief 销毁并释放非分页对象
	 * 
	 * 先调用对象的析构函数，然后释放内存。
	 * 确保正确的RAII资源管理和对象生命周期。
	 * 
	 * @tparam T 要释放的对象类型
	 * @param object 要释放的对象指针，可为nullptr
	 * 
	 * @note IRQL <= DISPATCH_LEVEL
	 * @note 该函数对nullptr安全
	 * @note 必须与allocate_non_paged_object成对使用
	 */
	template <typename T>
	void free_non_paged_object(T* object)
	{
		if (object)
		{
			// 手动调用析构函数
			object->~T();
			// 释放底层内存
			free_non_paged_memory(object);
		}
	}

	/**
	 * @brief 在对齐内存中分配并构造C++对象
	 * 
	 * 使用连续物理内存分配对象，主要用于VMX相关数据结构：
	 * - VMCS (Virtual Machine Control Structure)
	 * - VMXON region
	 * - MSR/IO bitmaps
	 * - 其他需要特殊对齐要求的结构
	 * 
	 * @tparam T 要分配的对象类型
	 * @tparam Args 构造函数参数类型
	 * @param args 传递给构造函数的参数
	 * @return 构造完成的对象指针，失败时为nullptr
	 * 
	 * @note IRQL <= DISPATCH_LEVEL
	 * @note 分配的内存具有连续物理地址
	 * @note 必须使用free_aligned_object释放
	 * @warning 构造函数异常可能导致内存泄漏
	 */
	template <typename T, typename... Args>
	T* allocate_aligned_object(Args ... args)
	{
		auto* object = static_cast<T*>(allocate_aligned_memory(sizeof(T)));
		if (object)
		{
			// 使用placement new在已分配内存中构造对象
			new(object) T(std::forward<Args>(args)...);
		}

		return object;
	}

	/**
	 * @brief 销毁并释放对齐对象
	 * 
	 * 先调用对象的析构函数，然后释放连续物理内存。
	 * 确保正确的VMX资源管理和对象生命周期。
	 * 
	 * @tparam T 要释放的对象类型
	 * @param object 要释放的对象指针，可为nullptr
	 * 
	 * @note IRQL <= DISPATCH_LEVEL
	 * @note 该函数对nullptr安全
	 * @note 必须与allocate_aligned_object成对使用
	 * @note 特别重要的是VMX相关资源在系统关机前必须释放
	 */
	template <typename T>
	void free_aligned_object(T* object)
	{
		if (object)
		{
			// 手动调用析构函数
			object->~T();
			// 释放连续物理内存
			free_aligned_memory(object);
		}
	}
}

// ============================================================================
// 内存大小单位转换 - 用户定义字面量操作符
// ============================================================================

/**
 * @brief KB单位字面量操作符
 * 
 * 将整数字面量转换为对应的字节数。
 * 使用示例：4_kb 返回 4096
 * 
 * @param size KB数量
 * @return 对应的字节数
 * 
 * @note 用于提高内存大小计算的可读性
 * @note 1KB = 1024字节（二进制标准）
 */
inline uint64_t operator""_kb(const uint64_t size)
{
	return size * 1024;
}

/**
 * @brief MB单位字面量操作符
 * 
 * 将整数字面量转换为对应的字节数。
 * 使用示例：2_mb 返回 2097152
 * 
 * @param size MB数量
 * @return 对应的字节数
 * 
 * @note 用于提高大型内存分配的可读性
 * @note 1MB = 1024KB = 1048576字节
 */
inline uint64_t operator""_mb(const uint64_t size)
{
	return size * 1024_kb;
}

/**
 * @brief GB单位字面量操作符
 * 
 * 将整数字面量转换为对应的字节数。
 * 使用示例：1_gb 返回 1073741824
 * 
 * @param size GB数量
 * @return 对应的字节数
 * 
 * @note 主要用于表示大型内存区域（如虚拟机内存）
 * @note 1GB = 1024MB = 1073741824字节
 * @warning 注意64位整数溢出，最大值约18EB
 */
inline uint64_t operator""_gb(const uint64_t size)
{
	return size * 1024_mb;
}
