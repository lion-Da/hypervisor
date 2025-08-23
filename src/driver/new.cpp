/**
 * @file new.cpp
 * @brief 内核模式C++内存分配操作符重载实现
 * 
 * 实现了适用于Windows内核环境的C++ new/delete操作符。
 * 所有分配都使用非分页内存池，确保在任何IRQL下都可访问。
 * 
 * 安全特性：
 * - 所有分配都进行零初始化以防止信息泄露
 * - 分配失败时抛出C++异常而非返回nullptr
 * - 使用'MOMO' Pool Tag便于内存调试和追踪
 * 
 * 错误处理：
 * - 内存分配失败抛出 std::runtime_error
 * - C++异常未被处理时触发BSOD（通过__std_terminate）
 * 
 * @author HyperHook Project
 * @date 2024
 */

#include "std_include.hpp"
#include "new.hpp"
#include "exception.hpp"
#include "memory.hpp"

/**
 * @brief 内部实现命名空间
 * 
 * 包含不对外暴露的辅助函数。
 */
namespace
{
	/**
	 * @brief 执行有检查的非分页内存分配
	 * 
	 * 封装了内存分配和错误检查逻辑，确保分配失败时抛出C++异常。
	 * 该函数由所有 new 操作符实现共享使用。
	 * 
	 * 安全特性：
	 * - 分配的内存自动零初始化
	 * - 使用非分页内存池确保高IRQL可用性
	 * - 分配失败时主动抛出异常
	 * 
	 * @param size 要分配的字节数
	 * @return 分配的内存指针（保证不为nullptr）
	 * @throws std::runtime_error 如果内存分配失败
	 * 
	 * @note IRQL <= DISPATCH_LEVEL
	 * @note 这是唯一可能抛出异常的内存分配函数
	 * @warning 在高IRQL下使用异常需要谨慎
	 */
	void* perform_checked_non_paged_allocation(const size_t size)
	{
		auto* memory = memory::allocate_non_paged_memory(size);
		if (!memory)
		{
			// 内存分配失败，抛出统一的错误信息
			throw std::runtime_error("Memory allocation failed");
		}

		return memory;
	}
}

// ============================================================================
// new 操作符实现 - 在非分页内存池中分配对象
// ============================================================================

/**
 * @brief 全局 new 操作符实现（单个对象）
 * 
 * 为单个对象分配内存。该实现会：
 * - 使用非分页内存池以支持高IRQL环境
 * - 自动零初始化分配的内存
 * - 分配失败时抛出异常而非返回nullptr
 * 
 * 使用示例：
 * @code
 * auto* obj = new MyClass(param1, param2);
 * // 使用obj...
 * delete obj;
 * @endcode
 * 
 * @param size 要分配的字节数（通常是sizeof(T)）
 * @return 分配的内存指针（保证不为nullptr）
 * @throws std::runtime_error 内存分配失败
 * 
 * @note IRQL <= DISPATCH_LEVEL
 * @note 对应的 delete 操作符会自动调用对象析构函数
 * @note 适用于所有C++对象的动态分配
 */
void* operator new(const size_t size)
{
	return perform_checked_non_paged_allocation(size);
}

/**
 * @brief 全局 new[] 操作符实现（数组）
 * 
 * 为对象数组分配内存。该实现会：
 * - 使用非分页内存池以支持高IRQL环境
 * - 自动零初始化分配的内存
 * - 分配失败时抛出异常而非返回nullptr
 * 
 * 使用示例：
 * @code
 * auto* array = new MyClass[10];
 * // 使用array...
 * delete[] array;
 * @endcode
 * 
 * @param size 要分配的字节数（数组所有元素的总大小）
 * @return 分配的内存指针（保证不为nullptr）
 * @throws std::runtime_error 内存分配失败
 * 
 * @note IRQL <= DISPATCH_LEVEL
 * @note 必须使用 delete[] 释放，不能使用 delete
 * @note 适用于动态大小的对象数组
 */
void* operator new[](const size_t size)
{
	return perform_checked_non_paged_allocation(size);
}

// ============================================================================
// Placement new 操作符实现 - 在指定地址构造对象
// ============================================================================

/**
 * @brief Placement new 操作符实现
 * 
 * 在指定的内存地址构造对象，不进行实际的内存分配。
 * 该操作符的主要用途：
 * 
 * 1. **内存池管理**：在预先分配的缓冲区中构造对象
 * 2. **高性能场景**：避免频繁分配/释放的开销
 * 3. **对象重用**：在同一内存位置重新构造对象
 * 4. **内部实现**：内存管理器的底层实现
 * 
 * 使用示例：
 * @code
 * alignas(MyClass) char buffer[sizeof(MyClass)];
 * auto* obj = new(buffer) MyClass(param1, param2);
 * // 使用obj...
 * obj->~MyClass(); // 手动调用析构函数
 * @endcode
 * 
 * @param size 对象大小（被忽略，编译器自动传递）
 * @param where 目标内存地址（必须有效且空间足够）
 * @return where 参数的原值
 * 
 * @note 无IRQL限制（不涉及内存分配）
 * @note 不会抛出异常
 * @note 调用者负责确保目标地址的有效性和对齐
 * @note 必须手动调用析构函数，不能使用 delete
 * @warning 使用不当可能导致内存损坏或系统崩溃
 */
void* operator new(size_t, void* where)
{
	return where;
}

// ============================================================================
// delete 操作符实现 - 释放非分页内存
// ============================================================================

/**
 * @brief 全局 delete 操作符实现（带尺寸参数）
 * 
 * 释放通过 new 操作符分配的单个对象内存。
 * 该函数会在以下情况下被调用：
 * - C++14及更高版本的编译器默认使用
 * - 编译器能够推导对象大小时的优化路径
 * 
 * @param ptr 要释放的内存指针（可为nullptr）
 * @param size 对象大小（未使用，由编译器传递）
 * 
 * @note IRQL <= DISPATCH_LEVEL
 * @note 该函数对nullptr安全
 * @note 与 new 操作符成对使用
 * @note 会自动调用对象的析构函数（由编译器处理）
 */
void operator delete(void* ptr, size_t) noexcept
{
	memory::free_non_paged_memory(ptr);
}

/**
 * @brief 全局 delete 操作符实现（不带尺寸参数）
 * 
 * 释放通过 new 操作符分配的单个对象内存。
 * 这是C++11之前的传统版本，为了兼容性保留。
 * 
 * 使用场景：
 * - C++11之前的代码兼容
 * - 编译器无法推导对象大小时的fallback
 * - 显式调用 delete 时的默认行为
 * 
 * @param ptr 要释放的内存指针（可为nullptr）
 * 
 * @note IRQL <= DISPATCH_LEVEL
 * @note 该函数对nullptr安全
 * @note 与 new 操作符成对使用
 * @note 会自动调用对象的析构函数（由编译器处理）
 */
void operator delete(void* ptr) noexcept
{
	memory::free_non_paged_memory(ptr);
}

/**
 * @brief 全局 delete[] 操作符实现（带尺寸参数）
 * 
 * 释放通过 new[] 操作符分配的数组内存。
 * 该函数会：
 * - 自动调用数组中所有对象的析构函数（由编译器处理）
 * - 释放底层的内存区域
 * - 处理数组元数的元数据（如有）
 * 
 * @param ptr 要释放的数组指针（可为nullptr）
 * @param size 数组大小（未使用，由编译器传递）
 * 
 * @note IRQL <= DISPATCH_LEVEL
 * @note 该函数对nullptr安全
 * @note 必须与 new[] 操作符成对使用
 * @note 不能用于释放通过 new 分配的单个对象
 */
void operator delete[](void* ptr, size_t) noexcept
{
	memory::free_non_paged_memory(ptr);
}

/**
 * @brief 全局 delete[] 操作符实现（不带尺寸参数）
 * 
 * 释放通过 new[] 操作符分配的数组内存。
 * 这是C++11之前的传统版本，为了兼容性保留。
 * 
 * 行为特性：
 * - 自动调用数组中每个对象的析构函数
 * - 按照构造的相反顺序调用析构函数
 * - 处理编译器生成的数组元数据
 * 
 * @param ptr 要释放的数组指针（可为nullptr）
 * 
 * @note IRQL <= DISPATCH_LEVEL
 * @note 该函数对nullptr安全
 * @note 必须与 new[] 操作符成对使用
 * @note 不能用于释放通过 new 分配的单个对象
 */
void operator delete[](void* ptr) noexcept
{
	memory::free_non_paged_memory(ptr);
}

// ============================================================================
// C++17 对齐感知的 delete 操作符 - 空实现用于兼容性
// ============================================================================

/**
 * @brief C++17对齐感知的 delete 操作符（单个对象）
 * 
 * C++17引入的新特性，用于处理具有特殊对齐要求的对象。
 * 在内核环境中，我们不实际支持对齐感知的分配，
 * 但必须提供这些函数以满足C++17标准的要求。
 * 
 * 实现策略：
 * - 空实现：不执行任何操作
 * - 依赖编译器优化：不会实际调用这些函数
 * - 兼容性保证：避免链接错误
 * 
 * @param ptr 要释放的内存指针（未使用）
 * @param size 对象大小（未使用）
 * @param alignment 对齐要求（未使用）
 * 
 * @note 空实现，仅用于编译器兼容性
 * @note 不应该被实际调用
 * @warning 如果被调用，表示编译器配置错误
 */
void operator delete(void*, size_t, std::align_val_t) noexcept
{
	// 空实现 - 仅用于满足C++17编译器的需求
}

/**
 * @brief C++17对齐感知的 delete[] 操作符（数组）
 * 
 * 与上面的函数类似，用于处理具有特殊对齐要求的对象数组。
 * 同样为空实现，仅用于兼容性。
 * 
 * @param ptr 要释放的内存指针（未使用）
 * @param size 数组大小（未使用）
 * @param alignment 对齐要求（未使用）
 * 
 * @note 空实现，仅用于编译器兼容性
 * @note 不应该被实际调用
 * @warning 如果被调用，表示编译器配置错误
 */
void operator delete[](void*, size_t, std::align_val_t) noexcept
{
	// 空实现 - 仅用于满足C++17编译器的需求
}

// ============================================================================
// C++异常处理终止函数 - 用于未捕获异常的处理
// ============================================================================

/**
 * @brief C++标准终止函数实现
 * 
 * 当C++程序中发生未被捕获的异常时，运行时会调用此函数。
 * 在内核环境中，这通常意味着程序逻辑错误或资源耗尽，
 * 必须立即停止系统操作以防止进一步损坏。
 * 
 * 触发情况：
 * - 未被捕获的C++异常
 * - std::terminate()被显式调用
 * - 析构函数中抛出异常（C++11）
 * - noexcept函数中抛出异常（C++11）
 * 
 * 处理策略：
 * - 使用KeBugCheckEx触发BSOD
 * - 错误码DRIVER_VIOLATION指示驱动程序违例
 * - 参数14指示C++异常处理问题
 * 
 * @note 此函数不会返回，系统将立即崩溃
 * @note 在内核中使用C++异常需要极其谨慎
 * @note BSOD信息将包含调用堆栈和错误参数
 * @warning 此函数被调用表示程序存在严重错误
 */
extern "C" void __std_terminate()
{
	// 使用KeBugCheckEx触发蓝屏，提供调试信息
	KeBugCheckEx(
		DRIVER_VIOLATION,           // Bug Check码：驱动程序违例
		14,                         // 参数1：C++异常处理问题
		0,                          // 参数2：保留
		0,                          // 参数3：保留
		0                           // 参数4：保留
	);
}
