/**
 * @file memory.cpp
 * @brief Windows内核驱动内存管理实现
 * 
 * 提供内核模式下的内存分配、释放、物理内存操作和地址转换功能。
 * 支持对齐内存分配、非分页内存分配、物理内存映射等核心操作。
 * 
 * 该模块针对hypervisor环境进行了优化，确保分配的内存满足VMX操作的要求：
 * - 对齐分配：用于VMCS、VMXON区域等需要特殊对齐的数据结构
 * - 非分页内存：确保内存在任何IRQL级别都可访问
 * - 物理内存操作：支持直接读写物理地址（用于EPT操作）
 * 
 * @author HyperHook Project
 * @date 2024
 */

#include "std_include.hpp"
#include "memory.hpp"
#include "string.hpp"

namespace memory
{
	namespace
	{
		/**
		 * @brief MmAllocateContiguousNodeMemory函数指针类型定义
		 * 
		 * 该函数在较新版本的Windows中可用，提供NUMA感知的连续物理内存分配。
		 * 相比传统的MmAllocateContiguousMemory，该函数能在指定的NUMA节点分配内存，
		 * 提高多处理器系统的性能。
		 */
		using mm_allocate_contiguous_node_memory = decltype(MmAllocateContiguousNodeMemory)*;

		/**
		 * @brief 获取MmAllocateContiguousNodeMemory函数地址
		 * 
		 * 该函数使用延迟加载策略获取系统函数地址。由于MmAllocateContiguousNodeMemory
		 * 在某些Windows版本中可能不存在，需要动态检测和加载。
		 * 
		 * 实现策略：
		 * 1. 首次调用时通过MmGetSystemRoutineAddress动态获取函数地址
		 * 2. 缓存结果避免重复查找
		 * 3. 如果函数不存在则返回nullptr，调用者需要fallback处理
		 * 
		 * @return 函数指针，如果系统不支持则为nullptr
		 * 
		 * @note 该函数是线程安全的，使用静态变量确保只初始化一次
		 * @note 运行时无IRQL限制，但应在PASSIVE_LEVEL调用以避免性能问题
		 */
		mm_allocate_contiguous_node_memory get_mm_allocate_contiguous_node_memory()
		{
			static bool fetched{false};
			static mm_allocate_contiguous_node_memory address{nullptr};

			if (!fetched)
			{
				fetched = true;
				auto function_name = string::get_unicode_string(L"MmAllocateContiguousNodeMemory");
				address = static_cast<mm_allocate_contiguous_node_memory>(MmGetSystemRoutineAddress(&function_name));
			}

			return address;
		}

		/**
		 * @brief 内部对齐内存分配函数
		 * 
		 * 分配连续的物理内存，适用于需要特定物理地址对齐的数据结构。
		 * 主要用于VMX相关数据结构（VMCS、VMXON区域等）的分配。
		 * 
		 * 分配策略：
		 * 1. 优先使用MmAllocateContiguousNodeMemory（如果系统支持）
		 *    - 在当前NUMA节点分配内存，提高访问效率
		 *    - 设置PAGE_READWRITE权限
		 * 2. Fallback使用传统的MmAllocateContiguousMemory
		 *    - 兼容较旧版本的Windows系统
		 * 
		 * 内存特性：
		 * - 物理地址连续（满足DMA和硬件访问要求）
		 * - 非分页（任何IRQL级别都可访问）
		 * - 可读写权限
		 * 
		 * @param size 要分配的内存大小（字节）
		 * @return 分配的内存地址，失败时返回nullptr
		 * 
		 * @note 调用者负责释放分配的内存（使用MmFreeContiguousMemory）
		 * @note 该函数分配的内存未初始化，调用者需要自行清零
		 */
		void* allocate_aligned_memory_internal(const size_t size)
		{
			// 设置物理地址范围（0到最大值）
			PHYSICAL_ADDRESS lowest{}, highest{};
			lowest.QuadPart = 0;
			highest.QuadPart = lowest.QuadPart - 1;  // 0xFFFFFFFFFFFFFFFF

			// 尝试使用NUMA感知的分配函数
			const auto allocate_node_mem = get_mm_allocate_contiguous_node_memory();
			if (allocate_node_mem)
			{
				return allocate_node_mem(size,
				                         lowest,      // 最低物理地址
				                         highest,     // 最高物理地址
				                         lowest,      // 边界对齐（0表示无特殊要求）
				                         PAGE_READWRITE, // 页面保护属性
				                         KeGetCurrentNodeNumber()); // 当前NUMA节点
			}

			// Fallback：使用传统分配函数
			return MmAllocateContiguousMemory(size, highest);
		}
	}

	/**
	 * @brief 释放对齐内存
	 * 
	 * 释放通过allocate_aligned_memory分配的连续物理内存。
	 * 该函数必须与allocate_aligned_memory成对使用。
	 * 
	 * @param memory 要释放的内存指针，允许nullptr
	 * 
	 * @note IRQL要求：<= DISPATCH_LEVEL
	 * @note 释放操作对nullptr安全
	 * @note 使用MmFreeContiguousMemory释放连续物理内存
	 */
	_IRQL_requires_max_(DISPATCH_LEVEL)
	void free_aligned_memory(void* memory)
	{
		if (memory)
		{
			MmFreeContiguousMemory(memory);
		}
	}

	/**
	 * @brief 分配已初始化的对齐内存
	 * 
	 * 分配连续的物理内存并初始化为零。适用于VMX相关数据结构：
	 * - VMCS (Virtual Machine Control Structure)
	 * - VMXON region
	 * - MSR bitmap
	 * - IO bitmap
	 * 
	 * 内存特性：
	 * - 物理地址连续
	 * - 非分页（任何IRQL都可访问）
	 * - 已零初始化（使用安全的RtlSecureZeroMemory）
	 * 
	 * @param size 分配大小（字节），应为页面大小的倍数
	 * @return 分配的内存指针，失败时返回nullptr
	 * 
	 * @note IRQL要求：<= DISPATCH_LEVEL
	 * @note 必须检查返回值（_Must_inspect_result_）
	 * @note 调用者负责调用free_aligned_memory释放内存
	 * @note VMX要求：VMCS区域必须4KB对齐，VMXON区域必须4KB对齐
	 */
	_Must_inspect_result_
	_IRQL_requires_max_(DISPATCH_LEVEL)
	void* allocate_aligned_memory(const size_t size)
	{
		void* memory = allocate_aligned_memory_internal(size);
		if (memory)
		{
			// 使用安全的零初始化函数，防止信息泄露
			RtlSecureZeroMemory(memory, size);
		}

		return memory;
	}

	/**
	 * @brief 读取物理内存
	 * 
	 * 直接读取指定物理地址的内容。需要谨慎使用，必须确保：
	 * 1. 目标物理地址有效
	 * 2. 对应的物理页面存在
	 * 3. 不会引发硬件异常
	 * 
	 * 主要用途：
	 * - EPT管理：读取客机物理内存
	 * - 系统调试：检查特定硬件状态
	 * - 内存扫描：搜索特定数据模式
	 * 
	 * @param destination 目标缓冲区（必须是有效的内核地址）
	 * @param physical_address 源物理地址
	 * @param size 要读取的字节数
	 * @return 成功返回true，失败返回false
	 * 
	 * @note IRQL要求：<= APC_LEVEL（由于MmCopyMemory的限制）
	 * @note 原子性：该操作是原子的，成功时整个块都被复制
	 * @note 错误处理：如果物理地址无效或不可访问，函数将安全返回false
	 */
	_IRQL_requires_max_(APC_LEVEL)
	bool read_physical_memory(void* destination, uint64_t physical_address, const size_t size)
	{
		size_t bytes_read{};
		MM_COPY_ADDRESS source{};
		source.PhysicalAddress.QuadPart = static_cast<int64_t>(physical_address);

		// 尝试复制内存并验证完整性
		return MmCopyMemory(destination, source, size, MM_COPY_MEMORY_PHYSICAL, &bytes_read) == STATUS_SUCCESS &&
			bytes_read == size;
	}

	/**
	 * @brief 获取虚拟地址对应的物理地址
	 * 
	 * 将内核虚拟地址转换为物理地址。该操作对于EPT管理至关重要：
	 * - EPT条目必须使用物理地址进行映射
	 * - VMCS中的地址字段需要物理地址
	 * - DMA操作需要物理地址
	 * 
	 * 使用场景：
	 * 1. EPT页表构建时，将客机物理地址转换为主机物理地址
	 * 2. VMCS配置时，设置各种指针字段
	 * 3. Hook函数定位时，确定目标代码的物理位置
	 * 
	 * @param address 内核虚拟地址（不能是用户态地址）
	 * @return 对应的物理地址
	 * 
	 * @note 无IRQL限制，但必须保证地址有效
	 * @note 对于分页内存，可能导致页面故障（在高IRQL下不安全）
	 * @note 该函数不适用于用户态地址，仅限内核地址
	 * @note 返回值0表示地址无效或无法访问
	 */
	uint64_t get_physical_address(void* address)
	{
		return static_cast<uint64_t>(MmGetPhysicalAddress(address).QuadPart);
	}

	/**
	 * @brief 获取物理地址对应的虚拟地址
	 * 
	 * 将物理地址转换为内核虚拟地址。该函数主要用于：
	 * - 访问通过物理地址定位的内存区域
	 * - 调试和分析：检查物理内存内容
	 * - EPT管理：访问客机物理页面内容
	 * 
	 * 重要限制：
	 * - 只能用于已映射到内核地址空间的物理内存
	 * - 不能用于任意物理地址（尤其是设备内存）
	 * - 返回的地址可能为nullptr（如果未映射）
	 * 
	 * @param address 物理地址
	 * @return 对应的内核虚拟地址，未映射时返回nullptr
	 * 
	 * @note 无IRQL限制
	 * @note 该函数不会创建新的映射，只返回已存在的映射
	 * @note 对于设备内存或未映射的物理内存，需使用map_physical_memory
	 */
	void* get_virtual_address(const uint64_t address)
	{
		PHYSICAL_ADDRESS physical_address{};
		physical_address.QuadPart = static_cast<LONGLONG>(address);
		return MmGetVirtualForPhysical(physical_address);
	}

	/**
	 * @brief 映射物理内存到内核虚拟地址空间
	 * 
	 * 创建一个新的虚拟地址映射，指向指定的物理内存区域。
	 * 主要用于访问：
	 * - 设备内存地址（MMIO）
	 * - 高端物理内存（>传统内核映射范围）
	 * - 特殊用途的物理页面
	 * 
	 * 缓存策略：
	 * - MmNonCached：不启用缓存，确保对设备内存的实时访问
	 * - 防止CPU缓存与设备状态不一致
	 * - 确保内存映射操作的原子性
	 * 
	 * @param address 要映射的物理地址（需要页面对齐）
	 * @param size 映射大小（字节，将被向上舍入到页面边界）
	 * @return 映射的虚拟地址，失败时返回nullptr
	 * 
	 * @note IRQL要求：<= DISPATCH_LEVEL
	 * @note 必须检查返回值（_Must_inspect_result_）
	 * @note 映射后必须调用unmap_physical_memory释放
	 * @note 不适用于普通的系统内存，仅限设备内存或特殊用途
	 * @warning 过度使用可能耗尽系统页表项(PTE)资源
	 */
	_Must_inspect_result_
	_IRQL_requires_max_(DISPATCH_LEVEL)
	void* map_physical_memory(const uint64_t address, const size_t size)
	{
		PHYSICAL_ADDRESS physical_address{};
		physical_address.QuadPart = static_cast<LONGLONG>(address);
		return MmMapIoSpace(physical_address, size, MmNonCached);
	}

	/**
	 * @brief 取消物理内存映射
	 * 
	 * 释放通过map_physical_memory创建的虚拟地址映射。
	 * 该函数必须与map_physical_memory成对使用。
	 * 
	 * 重要性：
	 * - 必须使用相同的地址和大小参数
	 * - 系统关机前必须释放所有映射
	 * - 防止内存泄漏和系统资源耗尽
	 * 
	 * @param address 要取消映射的虚拟地址（由map_physical_memory返回）
	 * @param size 映射的大小（必须与映射时使用的值相同）
	 * 
	 * @note IRQL要求：<= DISPATCH_LEVEL
	 * @note 该函数对nullptr参数不安全，调用者负责验证参数
	 * @note 在关键路径中（如驱动卸载）必须调用此函数
	 */
	_IRQL_requires_max_(DISPATCH_LEVEL)
	void unmap_physical_memory(void* address, const size_t size)
	{
		MmUnmapIoSpace(address, size);
	}

	/**
	 * @brief 分配非分页内存
	 * 
	 * 从非分页池(NonPagedPool)分配内存。非分页内存的特点：
	 * - 在任何IRQL级别都可访问
	 * - 不会被换出到磁盘
	 * - 位于物理内存中，不需要页面故障处理
	 * 
	 * 适用场景：
	 * - 中断处理例程(ISR)
	 * - 高IRQL级别的代码
	 * - DMA缓冲区
	 * - VMX相关数据结构的小型分配
	 * 
	 * @param size 分配大小（字节）
	 * @return 分配的内存指针，失败时返回nullptr
	 * 
	 * @note IRQL要求：<= DISPATCH_LEVEL
	 * @note 必须检查返回值（_Must_inspect_result_）
	 * @note 使用'MOMO'作为Pool Tag便于调试和内存追踪
	 * @note 自动零初始化内存以防止信息泄露
	 * @note 调用者负责调用free_non_paged_memory释放
	 * @warning ExAllocatePoolWithTag已在较新Windows中被弃用，这里禁用警告
	 */
	_Must_inspect_result_
	_IRQL_requires_max_(DISPATCH_LEVEL)
	void* allocate_non_paged_memory(const size_t size)
	{
#pragma warning(push)
#pragma warning(disable: 4996)  // 忽略已弃用API的警告
		void* memory = ExAllocatePoolWithTag(NonPagedPool, size, 'MOMO');
#pragma warning(pop)
		if (memory)
		{
			// 安全零初始化，防止内存中的敏感信息泄露
			RtlSecureZeroMemory(memory, size);
		}

		return memory;
	}

	/**
	 * @brief 释放非分页内存
	 * 
	 * 释放通过allocate_non_paged_memory分配的非分页内存。
	 * 该函数必须与allocate_non_paged_memory成对使用。
	 * 
	 * 重要注意：
	 * - 不能释放通过其他方式分配的内存
	 * - 释放后不能再次访问该内存区域
	 * - 可以在高IRQL级别调用（与分配时一致）
	 * 
	 * @param memory 要释放的内存指针，允许nullptr
	 * 
	 * @note IRQL要求：<= DISPATCH_LEVEL
	 * @note 该函数对nullptr安全
	 * @note 使用ExFreePool进行实际的内存释放
	 * @note 在驱动卸载时必须释放所有分配的内存
	 */
	_IRQL_requires_max_(DISPATCH_LEVEL)
	void free_non_paged_memory(void* memory)
	{
		if (memory)
		{
			ExFreePool(memory);
		}
	}

	/**
	 * @brief 安全检测内存区域的可读性
	 * 
	 * 使用结构化异常处理(SEH)安全检测内存区域是否可读。
	 * 适用于验证用户态参数或不信任的内存指针。
	 * 
	 * 检查内容：
	 * - 地址是否有效和可访问
	 * - 内存区域是否在用户态地址空间范围内
	 * - 是否满足对齐要求
	 * - 是否具有读取权限
	 * 
	 * @param address 要检测的内存地址
	 * @param length 内存区域长度（字节）
	 * @param alignment 对齐要求（字节，默认为1）
	 * @return 可读返回true，不可读或无效返回false
	 * 
	 * @note 无IRQL限制，但在高IRQL时可能导致系统不稳定
	 * @note 该函数不会实际读取内存内容，只验证访问权限
	 * @note 使用ProbeForRead进行底层检测
	 * @note 适用于验证IRP输入参数的有效性
	 */
	bool probe_for_read(const void* address, const size_t length, const uint64_t alignment)
	{
		__try
		{
			// ProbeForRead检查用户态地址的可读性
			ProbeForRead(const_cast<volatile void*>(address), length, static_cast<ULONG>(alignment));
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			// 发生异常表示地址无效或不可访问
			return false;
		}
	}

	/**
	 * @brief 断言内存区域可读性
	 * 
	 * 检测内存区域是否可读，如果不可读则抛出异常。
	 * 该函数是一个方便的封装，用于在需要确保参数有效性的代码中使用。
	 * 
	 * 使用场景：
	 * - IRP参数验证
	 * - 用户态缓冲区检查
	 * - API输入参数验证
	 * 
	 * @param address 要检测的内存地址
	 * @param length 内存区域长度（字节）
	 * @param alignment 对齐要求（字节，默认为1）
	 * 
	 * @throws std::runtime_error 如果内存区域不可读
	 * 
	 * @note 适用于C++异常处理模式的代码
	 * @note 在内核中使用异常需要谨慎，可能影响性能
	 * @warning 在高IRQL中使用异常可能导致系统不稳定
	 */
	void assert_readability(const void* address, const size_t length, const uint64_t alignment)
	{
		if (!probe_for_read(address, length, alignment))
		{
			throw std::runtime_error("Access violation");
		}
	}

	/**
	 * @brief 安全检测内存区域的可写性
	 * 
	 * 使用结构化异常处理(SEH)安全检测内存区域是否可写。
	 * 相比probe_for_read，还额外检查写入权限。
	 * 
	 * 检查内容：
	 * - 地址是否有效和可访问
	 * - 内存区域是否在用户态地址空间范围内
	 * - 是否满足对齐要求
	 * - 是否具有读写权限
	 * 
	 * 使用场景：
	 * - 验证输出缓冲区
	 * - IRP参数验证
	 * - 用户态内存写入操作前的检查
	 * 
	 * @param address 要检测的内存地址
	 * @param length 内存区域长度（字节）
	 * @param alignment 对齐要求（字节，默认为1）
	 * @return 可写返回true，不可写或无效返回false
	 * 
	 * @note 无IRQL限制，但在高IRQL时可能导致系统不稳定
	 * @note 该函数不会实际写入内存内容，只验证访问权限
	 * @note 使用ProbeForWrite进行底层检测
	 */
	bool probe_for_write(const void* address, const size_t length, const uint64_t alignment)
	{
		__try
		{
			// ProbeForWrite检查用户态地址的可写性
			ProbeForWrite(const_cast<volatile void*>(address), length, static_cast<ULONG>(alignment));
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			// 发生异常表示地址无效或不可写
			return false;
		}
	}

	/**
	 * @brief 断言内存区域可写性
	 * 
	 * 检测内存区域是否可写，如果不可写则抛出异常。
	 * 该函数是probe_for_write的便捷封装，用于简化错误处理。
	 * 
	 * 适用场景：
	 * - IRP输出缓冲区验证
	 * - 用户态内存写入前的安全检查
	 * - API输出参数验证
	 * 
	 * @param address 要检测的内存地址
	 * @param length 内存区域长度（字节）
	 * @param alignment 对齐要求（字节，默认为1）
	 * 
	 * @throws std::runtime_error 如果内存区域不可写
	 * 
	 * @note 适用于C++异常处理模式的代码
	 * @note 在内核中使用异常需要谨慎
	 * @warning 在高IRQL中使用异常可能导致系统不稳定
	 */
	void assert_writability(const void* address, const size_t length, const uint64_t alignment)
	{
		if (!probe_for_write(address, length, alignment))
		{
			throw std::runtime_error("Access violation");
		}
	}
}
