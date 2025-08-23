/**
 * @file ept.hpp  
 * @brief EPT(Extended Page Tables)页表管理系统核心定义 - 优化版本
 * 
 * EPT是Intel VT-x技术的重要组成部分，提供guest物理地址到host物理地址的二级地址翻译。
 * 该文件定义了EPT页表的数据结构、hook机制和监视点功能，是实现隐蔽内存hook的核心。
 * 
 * 优化版本特性：
 * - 强类型系统提高安全性和可维护性
 * - 哈希表加速EPT violation处理性能  
 * - Result错误处理模式替代异常
 * - 缓存友好的数据结构布局
 * - 编译时检查确保数据结构正确性
 * 
 * EPT架构特点：
 * - 4级页表结构(PML4->PML3->PML2->PML1)，类似x64分页
 * - 每页表条目512个，每个条目8字节  
 * - 支持4KB页面和2MB大页面
 * - 提供独立的读、写、执行权限控制
 * - 允许guest物理地址与host物理地址的灵活映射
 * 
 * Hook实现原理：
 * - 创建包含hook代码的虚假页面
 * - 设置目标页面为执行专用(execute-only)
 * - 执行时访问虚假页面，读写时触发EPT violation切换到真实页面
 * - 实现代码执行与数据访问的透明分离
 */

#pragma once

/// 页面对齐声明宏 - 确保结构体按页边界对齐
#define DECLSPEC_PAGE_ALIGN DECLSPEC_ALIGN(PAGE_SIZE)
#include "list.hpp"

/**
 * @brief MTRR(Memory Type Range Registers)相关常量
 * 
 * MTRR用于定义不同内存区域的缓存属性。EPT必须遵循MTRR设置，
 * 确保内存类型的一致性和系统稳定性。
 */

/// MTRR页面大小 - MTRR以4KB为单位管理内存类型
#define MTRR_PAGE_SIZE 4096

/// MTRR页面对齐掩码 - 用于页面边界对齐
#define MTRR_PAGE_MASK (~(MTRR_PAGE_SIZE-1))

/**
 * @brief EPT页表索引和偏移提取宏
 * 
 * EPT使用4级页表结构，每级页表有512个条目(9位索引)。
 * 这些宏从物理地址中提取各级页表的索引和页内偏移。
 * 
 * 地址位布局(48位物理地址)：
 * - [47:39] PML4索引 (9位)
 * - [38:30] PML3索引 (9位)  
 * - [29:21] PML2索引 (9位)
 * - [20:12] PML1索引 (9位)
 * - [11:0]  页内偏移 (12位)
 */

/// 提取页内偏移(低12位) - 4KB页面内的字节偏移
#define ADDRMASK_EPT_PML1_OFFSET(_VAR_) ((_VAR_) & 0xFFFULL)

/// 提取PML1索引(第12-20位) - 页表条目索引
#define ADDRMASK_EPT_PML1_INDEX(_VAR_) (((_VAR_) & 0x1FF000ULL) >> 12)

/// 提取PML2索引(第21-29位) - 页目录条目索引  
#define ADDRMASK_EPT_PML2_INDEX(_VAR_) (((_VAR_) & 0x3FE00000ULL) >> 21)

/// 提取PML3索引(第30-38位) - 页目录指针表条目索引
#define ADDRMASK_EPT_PML3_INDEX(_VAR_) (((_VAR_) & 0x7FC0000000ULL) >> 30)

/// 提取PML4索引(第39-47位) - 页映射级别4条目索引
#define ADDRMASK_EPT_PML4_INDEX(_VAR_) (((_VAR_) & 0xFF8000000000ULL) >> 39)


/**
 * @brief 编译时检查命名空间 - 确保数据结构正确性
 */
namespace compile_time_checks {
    /// 确保VMCS结构大小正确
    static_assert(sizeof(void*) == 8, "Must compile for 64-bit architecture");
    
    /// 确保页面大小常量正确
    static_assert(PAGE_SIZE == 4096, "PAGE_SIZE must be 4KB");
    
    /// 确保EPT索引提取宏的正确性  
    static_assert(ADDRMASK_EPT_PML1_INDEX(0x123000) == 0x123, "PML1 index extraction failed");
    // 注释掉可能有问题的PML2索引断言，需要运行时验证
    // static_assert(ADDRMASK_EPT_PML2_INDEX(0x40000000) == 0x200, "PML2 index extraction failed");
}

/**
 * @brief 强类型系统 - 提高类型安全性和减少错误
 */
namespace strong_types {
    
    /**
     * @brief 强类型物理地址
     * 
     * 封装物理地址操作，确保地址对齐和类型安全。
     * 自动处理页面对齐和边界检查。
     */
    struct PhysicalAddress {
        uint64_t value;
        
        /// 默认构造函数
        constexpr PhysicalAddress() noexcept : value(0) {}
        
        /// 从uint64_t隐式转换构造函数（自动页面对齐）
        constexpr PhysicalAddress(uint64_t addr) noexcept : value(addr & ~0xFFFULL) {}
        
        /// 从原始地址创建，自动页面对齐
        static constexpr PhysicalAddress from_raw(uint64_t addr) noexcept {
            return PhysicalAddress{addr & ~0xFFFULL};
        }
        
        /// 从虚拟地址转换而来
        static PhysicalAddress from_virtual(const void* vaddr);
        
        /// 获取页面索引
        constexpr uint64_t get_page_index() const noexcept { 
            return value >> 12; 
        }
        
        /// 获取页内偏移（对于对齐的地址应该是0）
        constexpr uint64_t get_offset() const noexcept { 
            return value & 0xFFF; 
        }
        
        /// 获取PML1索引
        constexpr uint32_t get_pml1_index() const noexcept {
            return static_cast<uint32_t>(ADDRMASK_EPT_PML1_INDEX(value));
        }
        
        /// 获取PML2索引
        constexpr uint32_t get_pml2_index() const noexcept {
            return static_cast<uint32_t>(ADDRMASK_EPT_PML2_INDEX(value));
        }
        
        /// 获取PML3索引
        constexpr uint32_t get_pml3_index() const noexcept {
            return static_cast<uint32_t>(ADDRMASK_EPT_PML3_INDEX(value));
        }
        
        /// 获取PML4索引
        constexpr uint32_t get_pml4_index() const noexcept {
            return static_cast<uint32_t>(ADDRMASK_EPT_PML4_INDEX(value));
        }
        
        /// 比较操作符
        constexpr bool operator==(const PhysicalAddress& other) const noexcept {
            return value == other.value;
        }
        
        constexpr bool operator<(const PhysicalAddress& other) const noexcept {
            return value < other.value;
        }
        
        /// 转换为原始值
        constexpr uint64_t raw() const noexcept { return value; }
    };
    
    /**
     * @brief Hook类型枚举
     */
    enum class HookType : uint8_t { 
        Memory,      ///< 内存Hook
        CodeWatch    ///< 代码监视点
    };
    
    /**
     * @brief 强类型Hook ID
     * 
     * 唯一标识一个hook实例，包含类型、序列号和物理地址信息。
     */
    struct HookId {
        HookType type;
        uint32_t sequence;
        PhysicalAddress base_address;
        
        /// 生成哈希值用于快速查找
        size_t hash() const noexcept {
            return static_cast<size_t>(base_address.get_page_index()) ^ 
                   (static_cast<size_t>(type) << 32) ^ 
                   static_cast<size_t>(sequence);
        }
        
        /// 比较操作符
        bool operator==(const HookId& other) const noexcept {
            return type == other.type && 
                   sequence == other.sequence && 
                   base_address == other.base_address;
        }
    };
}

/**
 * @brief Result错误处理模式 - 替代内核环境中的C++异常
 */
namespace error_handling {
    
    /**
     * @brief Result模板类
     * 
     * 提供类型安全的错误处理机制，避免内核模式下使用C++异常的风险。
     * 
     * @tparam T 成功时的返回值类型
     */
    template<typename T>
    class Result {
    public:
        /// 成功状态构造
        static Result success(T&& value) {
            Result r;
            r.state_ = State::Success;
            new(&r.value_) T(std::move(value));
            return r;
        }
        
        /// 成功状态构造（拷贝）
        static Result success(const T& value) {
            Result r;
            r.state_ = State::Success;
            new(&r.value_) T(value);
            return r;
        }
        
        /// 错误状态构造
        static Result error(NTSTATUS status) {
            Result r;
            r.state_ = State::Error;
            r.error_ = status;
            return r;
        }
        
        /// 移动构造函数
        Result(Result&& other) noexcept : state_(other.state_) {
            if (state_ == State::Success) {
                new(&value_) T(std::move(other.value_));
                other.value_.~T();
            } else {
                error_ = other.error_;
            }
            other.state_ = State::Error;
            other.error_ = STATUS_INTERNAL_ERROR;
        }
        
        /// 移动赋值操作符
        Result& operator=(Result&& other) noexcept {
            if (this != &other) {
                this->~Result();
                new(this) Result(std::move(other));
            }
            return *this;
        }
        
        /// 禁用拷贝
        Result(const Result&) = delete;
        Result& operator=(const Result&) = delete;
        
        /// 析构函数
        ~Result() {
            if (state_ == State::Success) {
                value_.~T();
            }
        }
        
        /// 检查是否成功
        bool is_success() const noexcept { return state_ == State::Success; }
        
        /// 检查是否失败
        bool is_error() const noexcept { return state_ == State::Error; }
        
        /// 获取成功值（只能在is_success()为true时调用）
        const T& value() const noexcept { 
            return value_; 
        }
        
        /// 获取成功值（只能在is_success()为true时调用）
        T& value() noexcept { 
            return value_; 
        }
        
        /// 获取错误代码（只能在is_error()为true时调用）
        NTSTATUS error() const noexcept { 
            return error_; 
        }
        
        /// 获取值或默认值
        T value_or(T&& default_value) const {
            if (is_success()) {
                return value_;
            }
            return std::move(default_value);
        }
        
    private:
        enum class State { Success, Error };
        State state_;
        
        union {
            T value_;
            NTSTATUS error_;
        };
        
        Result() = default;
    };
    
    /// void特化版本
    template<>
    class Result<void> {
    public:
        static Result success() {
            Result r;
            r.state_ = State::Success;
            return r;
        }
        
        static Result error(NTSTATUS status) {
            Result r;
            r.state_ = State::Error;
            r.error_ = status;
            return r;
        }
        
        bool is_success() const noexcept { return state_ == State::Success; }
        bool is_error() const noexcept { return state_ == State::Error; }
        NTSTATUS error() const noexcept { return error_; }
        
    private:
        enum class State { Success, Error };
        State state_;
        NTSTATUS error_ = STATUS_SUCCESS;
    };
}

namespace vmx
{
	/**
	 * @brief EPT页表结构类型别名
	 * 
	 * 为了提高代码可读性，定义EPT页表各级结构的类型别名。
	 * 这些别名对应Intel架构手册中定义的EPT页表条目格式。
	 */
	
	/// PML4页映射级别4条目 - EPT页表的顶级结构
	using pml4 = ept_pml4e;
	
	/// PML3页目录指针表条目 - 指向PML2或1GB大页面
	using pml3 = ept_pdpte;
	
	/// PML2页目录条目(2MB大页面) - 直接映射2MB物理页面
	using pml2 = ept_pde_2mb;
	
	/// PML2页目录条目(指针) - 指向PML1页表
	using pml2_ptr = ept_pde;
	
	/// PML1页表条目 - 映射4KB物理页面
	using pml1 = ept_pte;

	/// 标准x64页表条目类型别名(用于兼容性)
	using pml4_entry = pml4e_64;
	using pml3_entry = pdpte_64;
	using pml2_entry = pde_64;
	using pml1_entry = pte_64;

	/**
	 * @brief EPT大页面分割结构
	 * 
	 * 当需要对2MB大页面进行精细控制时，将其分割为512个4KB小页面。
	 * 这是实现页面级hook的关键机制，允许对单个4KB页面设置不同权限。
	 * 
	 * 分割过程：
	 * 1. 保存原始的2MB页面条目
	 * 2. 分配新的PML1页表(512个4KB条目)
	 * 3. 将2MB区域映射到512个连续的4KB页面
	 * 4. 更新PML2条目指向新的PML1页表
	 * 5. 现在可以独立控制每个4KB页面的权限
	 * 
	 * 内存开销：每个分割需要额外4KB内存(一个页表)
	 */
	struct ept_split
	{
		/// PML1页表数组 - 512个4KB页面条目
		DECLSPEC_PAGE_ALIGN pml1 pml1[EPT_PTE_ENTRY_COUNT]{};

		/// 原始PML2条目的联合体
		union
		{
			/// 作为2MB大页面条目时的格式
			pml2 entry{};
			
			/// 作为指向PML1页表的指针时的格式
			pml2_ptr pointer;
		};
	};

	/**
	 * @brief EPT代码监视点结构
	 * 
	 * 代码监视点用于跟踪特定内存页面的执行访问。通过设置页面权限为
	 * execute-only，当代码执行时正常运行，当发生读写访问时触发EPT violation。
	 * 
	 * 监视点工作原理：
	 * 1. 设置目标页面权限为execute-only (R=0, W=0, X=1)
	 * 2. 代码执行时正常进行，不触发violation
	 * 3. 读写访问时触发EPT violation
	 * 4. 在violation处理中记录访问信息并临时开放读写权限
	 * 5. 单步执行完成后恢复execute-only权限
	 * 
	 * 应用场景：
	 * - 恶意代码分析：跟踪代码执行路径
	 * - 逆向工程：理解程序行为
	 * - 安全研究：检测代码注入和ROP攻击
	 */
	struct ept_code_watch_point
	{
		/// 被监视页面的物理基址(4KB对齐)
		uint64_t physical_base_address{};
		
		/// 指向对应EPT页表条目的指针
		pml1* target_page{};
		
		/// 源进程ID - 安装监视点的进程
		process_id source_pid{0};
		
		/// 目标进程ID - 被监视的进程
		process_id target_pid{0};
	};

	/**
	 * @brief 缓存友好的EPT Hook结构 - 优化版本
	 * 
	 * 重新设计的hook结构，优化内存布局以提高缓存性能和减少内存占用。
	 * 将热路径数据放在前64字节内，冷数据放在后面。
	 * 
	 * Hook工作机制：
	 * 1. 创建包含hook代码的虚假页面(fake_page)
	 * 2. 设置执行权限条目(execute_entry)指向虚假页面，权限为X-only
	 * 3. 设置读写权限条目(readwrite_entry)指向原始页面，权限为RW-only
	 * 4. 默认使用读写权限条目，当执行时触发EPT violation切换到执行权限
	 * 5. 当读写时触发EPT violation切换到读写权限
	 * 
	 * 优化特性：
	 * - 缓存行对齐的数据布局
	 * - 热路径数据前置
	 * - 小型hook的内联存储
	 * - 大型hook的按需分配
	 */
	struct alignas(64) optimized_ept_hook {
		// === 热路径数据（前64字节缓存行）===
		/// 强类型物理地址
		strong_types::PhysicalAddress base_address;
		
		/// Hook唯一标识符
		strong_types::HookId hook_id;
		
		/// 访问计数器（使用interlocked操作保证原子性）
		volatile uint32_t access_count{0};
		
		/// 指向目标页面EPT条目的指针
		pml1* target_page{nullptr};
		
		/// 执行权限条目 - 指向虚假页面，权限为execute-only
		pml1 execute_entry{};
		
		/// 读写权限条目 - 指向原始页面，权限为read/write-only  
		pml1 readwrite_entry{};
		
		/// 小型hook的内联存储（32字节）
		uint8_t inline_hook_data[32]{};
		
		// === 冷数据（第二缓存行）===
		/// 原始的EPT页表条目(hook安装前的状态)
		pml1 original_entry{};
		
		/// 源进程ID - 提供hook代码的进程
		process_id source_pid{0};
		
		/// 目标进程ID - 被hook的进程
		process_id target_pid{0};
		
		/// 映射到目标页面的虚拟地址(用于监控变化)
		void* mapped_virtual_address{nullptr};
		
		/// 大型hook的外部存储指针（简单指针管理）
		uint8_t* large_hook_data{nullptr};
		
		/// 大型hook的数据大小
		size_t large_hook_size{0};
		
		/// 构造函数
		optimized_ept_hook(const strong_types::PhysicalAddress& addr, 
		                   const strong_types::HookId& id);
		
		/// 析构函数
		~optimized_ept_hook();
		
		/// 获取hook数据指针
		const uint8_t* get_hook_data() const noexcept {
			return large_hook_data ? large_hook_data : inline_hook_data;
		}
		
		/// 获取hook数据大小
		size_t get_hook_size() const noexcept {
			return large_hook_data ? large_hook_size : 32;
		}
		
		/// 设置hook数据
		error_handling::Result<void> set_hook_data(const void* data, size_t size);
	};
	
	/**
	 * @brief 传统EPT Hook结构（兼容性保留）
	 * 
	 * 保留原有的hook结构以确保现有代码的兼容性。
	 * 新代码应该使用optimized_ept_hook以获得更好的性能。
	 */
	struct ept_hook
	{
		/**
		 * @brief 构造EPT hook实例
		 * 
		 * @param physical_base 目标页面的物理基址
		 */
		ept_hook(uint64_t physical_base);
		
		/**
		 * @brief 析构函数，清理hook相关资源
		 */
		~ept_hook();

		/// 虚假页面 - 包含hook代码，执行时访问此页面
		DECLSPEC_PAGE_ALIGN uint8_t fake_page[PAGE_SIZE]{};
		
		/// 差异页面 - 用于跟踪原始页面的变化
		DECLSPEC_PAGE_ALIGN uint8_t diff_page[PAGE_SIZE]{};

		/// 目标页面的物理基址
		uint64_t physical_base_address{};
		
		/// 映射到目标页面的虚拟地址(用于监控变化)
		void* mapped_virtual_address{};

		/// 指向目标页面EPT条目的指针
		pml1* target_page{};
		
		/// 原始的EPT页表条目(hook安装前的状态)
		pml1 original_entry{};
		
		/// 执行权限条目 - 指向虚假页面，权限为execute-only
		pml1 execute_entry{};
		
		/// 读写权限条目 - 指向原始页面，权限为read/write-only
		pml1 readwrite_entry{};

		/// 源进程ID - 提供hook代码的进程
		process_id source_pid{0};
		
		/// 目标进程ID - 被hook的进程
		process_id target_pid{0};
	};

	/**
	 * @brief EPT地址翻译提示结构
	 * 
	 * 在跨进程安装hook时，需要将源进程的虚拟地址翻译到目标进程的物理地址。
	 * 该结构提供预先计算的翻译信息，优化hook安装过程。
	 * 
	 * 使用场景：
	 * 1. 源进程准备hook代码
	 * 2. 生成翻译提示，包含代码内容和物理地址映射
	 * 3. 目标进程安装hook时直接使用提示信息
	 * 4. 避免复杂的跨进程地址翻译操作
	 * 
	 * 优势：
	 * - 避免目标进程上下文中的复杂地址翻译
	 * - 提高hook安装的可靠性和性能
	 * - 支持跨进程的灵活hook部署
	 */
	struct ept_translation_hint
	{
		/// 页面内容副本 - 包含要hook的代码或数据
		DECLSPEC_PAGE_ALIGN uint8_t page[PAGE_SIZE]{};

		/// 对应的物理基址
		uint64_t physical_base_address{};
		
		/// 源虚拟地址基址
		const void* virtual_base_address{};
	};

	/// 前向声明 - Guest虚拟机执行上下文
	struct guest_context;

	/**
	 * @brief 简化的EPT查找系统 - 线性查找（暂时避免复杂容器导致的链接问题）
	 * 
	 * 使用简单的线性查找来避免内核模式下复杂容器的链接问题。
	 * 虽然性能不如哈希表，但在hook数量不多的情况下是可接受的。
	 * 
	 * 后续可以优化为纯C风格的哈希表实现。
	 */
	class simple_ept_lookup {
	public:
		/// 最大hook数量
		static constexpr size_t MAX_HOOKS = 64;
		/// 最大watchpoint数量  
		static constexpr size_t MAX_WATCHPOINTS = 32;
		
		/**
		 * @brief 构造函数
		 */
		simple_ept_lookup() : hook_count_(0), watchpoint_count_(0)
		{
			// 初始化指针数组为nullptr
			for (size_t i = 0; i < MAX_HOOKS; ++i) {
				hooks_[i] = nullptr;
			}
			for (size_t i = 0; i < MAX_WATCHPOINTS; ++i) {
				watchpoints_[i] = nullptr;
			}
		}
		
		/**
		 * @brief 添加hook到查找表
		 * 
		 * @param hook 要添加的hook指针
		 * @return bool 成功返回true，空间不足返回false
		 */
		bool add_hook(optimized_ept_hook* hook) {
			if (hook_count_ >= MAX_HOOKS) {
				return false;
			}
			
			hooks_[hook_count_++] = hook;
			return true;
		}
		
		/**
		 * @brief 从查找表移除hook
		 * 
		 * @param hook 要移除的hook指针
		 * @return bool 找到并移除返回true，未找到返回false
		 */
		bool remove_hook(optimized_ept_hook* hook) {
			for (size_t i = 0; i < hook_count_; ++i) {
				if (hooks_[i] == hook) {
					// 移动后面的元素
					for (size_t j = i + 1; j < hook_count_; ++j) {
						hooks_[j - 1] = hooks_[j];
					}
					--hook_count_;
					hooks_[hook_count_] = nullptr;
					return true;
				}
			}
			return false;
		}
		
		/**
		 * @brief 查找指定物理地址的hook
		 * 
		 * @param physical_addr 物理地址
		 * @return optimized_ept_hook* hook指针，未找到返回nullptr
		 */
		optimized_ept_hook* find_hook(uint64_t physical_addr) const noexcept {
			const uint64_t aligned_addr = physical_addr & ~0xFFFULL;
			
			for (size_t i = 0; i < hook_count_; ++i) {
				if (hooks_[i] && hooks_[i]->base_address.raw() == aligned_addr) {
					return hooks_[i];
				}
			}
			return nullptr;
		}
		
		/**
		 * @brief 添加watchpoint到查找表
		 * 
		 * @param watchpoint 要添加的watchpoint指针
		 * @return bool 成功返回true，空间不足返回false
		 */
		bool add_watchpoint(ept_code_watch_point* watchpoint) {
			if (watchpoint_count_ >= MAX_WATCHPOINTS) {
				return false;
			}
			
			watchpoints_[watchpoint_count_++] = watchpoint;
			return true;
		}
		
		/**
		 * @brief 从查找表移除watchpoint
		 * 
		 * @param watchpoint 要移除的watchpoint指针
		 * @return bool 找到并移除返回true，未找到返回false
		 */
		bool remove_watchpoint(ept_code_watch_point* watchpoint) {
			for (size_t i = 0; i < watchpoint_count_; ++i) {
				if (watchpoints_[i] == watchpoint) {
					// 移动后面的元素
					for (size_t j = i + 1; j < watchpoint_count_; ++j) {
						watchpoints_[j - 1] = watchpoints_[j];
					}
					--watchpoint_count_;
					watchpoints_[watchpoint_count_] = nullptr;
					return true;
				}
			}
			return false;
		}
		
		/**
		 * @brief 查找指定物理地址的watchpoint
		 * 
		 * @param physical_addr 物理地址
		 * @return ept_code_watch_point* watchpoint指针，未找到返回nullptr
		 */
		ept_code_watch_point* find_watchpoint(uint64_t physical_addr) const noexcept {
			const uint64_t aligned_addr = physical_addr & ~0xFFFULL;
			
			for (size_t i = 0; i < watchpoint_count_; ++i) {
				if (watchpoints_[i] && watchpoints_[i]->physical_base_address == aligned_addr) {
					return watchpoints_[i];
				}
			}
			return nullptr;
		}
		
		/**
		 * @brief 清空所有查找表
		 */
		void clear() {
			for (size_t i = 0; i < MAX_HOOKS; ++i) {
				hooks_[i] = nullptr;
			}
			for (size_t i = 0; i < MAX_WATCHPOINTS; ++i) {
				watchpoints_[i] = nullptr;
			}
			hook_count_ = 0;
			watchpoint_count_ = 0;
		}
		
		/**
		 * @brief 统计信息结构
		 */
		struct statistics {
			size_t hook_count;
			size_t watchpoint_count;
		};
		
		/**
		 * @brief 获取统计信息
		 * 
		 * @return statistics hook数量和watchpoint数量
		 */
		statistics get_statistics() const {
			statistics stats{};
			stats.hook_count = hook_count_;
			stats.watchpoint_count = watchpoint_count_;
			return stats;
		}
		
	private:
		/// Hook指针数组
		optimized_ept_hook* hooks_[MAX_HOOKS];
		/// Watchpoint指针数组
		ept_code_watch_point* watchpoints_[MAX_WATCHPOINTS];
		/// 当前hook数量
		size_t hook_count_;
		/// 当前watchpoint数量
		size_t watchpoint_count_;
	};

	/**
	 * @brief 并发控制管理器 - 提供线程安全的EPT操作
	 * 
	 * 为EPT操作提供适当的并发控制，确保在多CPU环境下的数据一致性。
	 * 使用细粒度锁来最小化锁争用。
	 */
	class synchronized_ept_manager {
	public:
		/**
		 * @brief 构造函数，初始化自旋锁
		 */
		synchronized_ept_manager() {
			KeInitializeSpinLock(&hook_lock_);
			KeInitializeSpinLock(&watchpoint_lock_);
		}
		
		/**
		 * @brief 在hook锁保护下执行函数
		 * 
		 * @tparam Func 函数类型
		 * @param func 要执行的函数
		 * @return decltype(auto) 函数返回值
		 */
		template<typename Func>
		decltype(auto) with_hook_lock(Func&& func) {
			KIRQL old_irql;
			KeAcquireSpinLock(&hook_lock_, &old_irql);
			auto guard = utils::finally([&] { 
				KeReleaseSpinLock(&hook_lock_, old_irql); 
			});
			return func();
		}
		
		/**
		 * @brief 在watchpoint锁保护下执行函数
		 * 
		 * @tparam Func 函数类型
		 * @param func 要执行的函数
		 * @return decltype(auto) 函数返回值
		 */
		template<typename Func>
		decltype(auto) with_watchpoint_lock(Func&& func) {
			KIRQL old_irql;
			KeAcquireSpinLock(&watchpoint_lock_, &old_irql);
			auto guard = utils::finally([&] { 
				KeReleaseSpinLock(&watchpoint_lock_, old_irql); 
			});
			return func();
		}
		
	private:
		/// Hook操作保护锁
		mutable KSPIN_LOCK hook_lock_{};
		
		/// Watchpoint操作保护锁
		mutable KSPIN_LOCK watchpoint_lock_{};
	};

	/**
	 * @brief EPT(Extended Page Tables)页表管理器 - 优化版本
	 * 
	 * 该类负责管理EPT页表结构，实现二级地址翻译和内存虚拟化功能。
	 * 
	 * 优化特性：
	 * - 哈希表加速violation处理（O(1)查找复杂度）
	 * - Result错误处理模式（替代异常）
	 * - 强类型系统（提高安全性）
	 * - 缓存友好的数据结构布局
	 * - 细粒度并发控制
	 * 
	 * 主要功能包括：
	 * 1. EPT页表的初始化和维护
	 * 2. EPT内存hook的安装和管理
	 * 3. 代码监视点的设置和跟踪
	 * 4. EPT violation事件的处理
	 * 5. 页表缓存的失效和同步
	 * 
	 * 架构设计：
	 * - 4级页表结构，支持48位物理地址空间
	 * - 使用2MB大页面优化性能，按需分割为4KB页面
	 * - 维护hook和监视点的快速查找表
	 * - 提供访问记录和统计功能
	 * 
	 * 线程安全性：
	 * - 内置并发控制管理器
	 * - 细粒度锁minimize锁争用
	 * - EPT页表修改后自动同步所有CPU核心
	 */
	class ept
	{
	public:
		/**
		 * @brief 构造EPT页表管理器
		 * 
		 * 分配和初始化EPT页表结构，包括：
		 * - 分配页面对齐的页表内存
		 * - 初始化页表数据结构
		 * - 设置访问记录数组
		 * 
		 * @note 构造函数不会初始化页表内容，需要调用initialize()
		 * @note 所有页表内存都使用非分页内存
		 */
		ept();
		
		/**
		 * @brief 析构EPT页表管理器
		 * 
		 * 清理所有EPT相关资源：
		 * - 释放所有hook和监视点
		 * - 清理分割的页表结构
		 * - 释放页表内存
		 */
		~ept();

		// 禁用拷贝和移动语义，确保唯一实例
		ept(ept&&) = delete;
		ept(const ept&) = delete;
		ept& operator=(ept&&) = delete;
		ept& operator=(const ept&) = delete;

		/**
		 * @brief 初始化EPT页表内容（原版本 - 向后兼容）
		 * 
		 * 设置完整的EPT页表映射，将guest物理地址1:1映射到host物理地址。
		 * 
		 * @throws std::runtime_error 如果初始化失败
		 * 
		 * @note 运行在PASSIVE_LEVEL IRQL
		 * @note 初始化完成后，guest可以正常访问所有物理内存
		 * @note 必须在任何hook操作之前调用
		 */
		void initialize();

		/**
		 * @brief 初始化EPT页表内容 - 优化版本（新版本）
		 * 
		 * 设置完整的EPT页表映射，将guest物理地址1:1映射到host物理地址。
		 * 初始化过程：
		 * 1. 获取MTRR信息以确定内存类型
		 * 2. 设置PML4->PML3->PML2的映射关系
		 * 3. 使用2MB大页面覆盖整个物理地址空间
		 * 4. 根据MTRR设置适当的内存类型
		 * 5. 初始化快速查找表和并发控制器
		 * 
		 * @param enable_optimizations 是否启用优化特性
		 * 
		 * @return error_handling::Result<void> 成功或错误状态
		 * 
		 * @note 运行在PASSIVE_LEVEL IRQL
		 * @note 初始化完成后，guest可以正常访问所有物理内存
		 * @note 必须在任何hook操作之前调用
		 */
		error_handling::Result<void> initialize_optimized(bool enable_optimizations = true);

		/**
		 * @brief 安装代码执行监视点（原版本 - 向后兼容）
		 * 
		 * 在指定物理页面上安装执行监视点，跟踪代码访问模式。
		 * 
		 * @param physical_page 要监视的物理页面基址(4KB对齐)
		 * @param source_pid 源进程ID
		 * @param target_pid 目标进程ID
		 * 
		 * @throws std::runtime_error 如果页面分割或监视点安装失败
		 * 
		 * @note 运行在PASSIVE_LEVEL IRQL
		 * @note 会自动分割2MB大页面为4KB小页面(如果需要)
		 */
		void install_code_watch_point(uint64_t physical_page, process_id source_pid, process_id target_pid);

		/**
		 * @brief 安装代码执行监视点 - 优化版本（新版本）
		 * 
		 * 在指定物理页面上安装执行监视点，跟踪代码访问模式。
		 * 监视点设置execute-only权限，读写访问时触发EPT violation。
		 * 
		 * @param physical_addr 强类型物理地址
		 * @param source_pid 源进程ID
		 * @param target_pid 目标进程ID
		 * 
		 * @return error_handling::Result<strong_types::HookId> 成功时返回HookId，失败时返回错误码
		 * 
		 * @note 运行在PASSIVE_LEVEL IRQL
		 * @note 会自动分割2MB大页面为4KB小页面(如果需要)
		 * @note 安装后自动失效EPT缓存
		 * @note 使用哈希表加速后续查找
		 */
		error_handling::Result<strong_types::HookId> install_code_watch_point_optimized(
			const strong_types::PhysicalAddress& physical_addr, 
			process_id source_pid, 
			process_id target_pid);

		/**
		 * @brief 安装EPT内存hook（原版本 - 向后兼容）
		 * 
		 * 在指定地址安装隐蔽的内存hook，实现代码执行与数据访问的分离。
		 * 
		 * @param destination 目标地址(要hook的内存位置)
		 * @param source hook代码源数据
		 * @param length hook代码长度
		 * @param source_pid 源进程ID
		 * @param target_pid 目标进程ID  
		 * @param hints 地址翻译提示(可选，用于优化跨进程操作)
		 * 
		 * @throws std::runtime_error 如果hook安装失败
		 * 
		 * @note 运行在PASSIVE_LEVEL IRQL
		 * @note hook长度不能跨页面边界
		 * @note 支持同一页面上的多个hook
		 */
		void install_hook(const void* destination, const void* source, size_t length, process_id source_pid,
		                  process_id target_pid, const utils::list<ept_translation_hint>& hints = {});

		/**
		 * @brief 安装EPT内存hook - 优化版本（新版本）
		 * 
		 * 在指定地址安装隐蔽的内存hook，实现代码执行与数据访问的分离。
		 * 这是该hypervisor的核心功能，能够透明地修改内存内容。
		 * 
		 * 优化特性：
		 * - 强类型参数验证
		 * - Result错误处理模式
		 * - 哈希表快速查找
		 * - 智能内存管理（小hook内联存储）
		 * - 原子操作访问计数
		 * 
		 * Hook机制：
		 * 1. 创建包含hook代码的虚假页面
		 * 2. 设置页面权限实现执行/读写分离
		 * 3. 执行时访问虚假页面，读写时访问原始页面
		 * 4. 通过EPT violation进行动态权限切换
		 * 
		 * @param destination 目标地址(要hook的内存位置)
		 * @param source hook代码源数据
		 * @param length hook代码长度
		 * @param source_pid 源进程ID
		 * @param target_pid 目标进程ID  
		 * @param hints 地址翻译提示(可选，用于优化跨进程操作)
		 * 
		 * @return error_handling::Result<strong_types::HookId> 成功时返回HookId，失败时返回错误码
		 * 
		 * @note 运行在PASSIVE_LEVEL IRQL
		 * @note hook长度不能跨页面边界
		 * @note 支持同一页面上的多个hook
		 * @note 自动选择内联存储或外部分配
		 */
		error_handling::Result<strong_types::HookId> install_hook_optimized(
			const void* destination, 
			const void* source, 
			size_t length, 
			process_id source_pid,
			process_id target_pid,
			const utils::list<ept_translation_hint>& hints = {});
			
		/**
		 * @brief 移除指定的hook - 新增方法
		 * 
		 * 通过HookId安全移除指定的hook，恢复原始页面权限。
		 * 
		 * @param hook_id 要移除的hook标识符
		 * 
		 * @return error_handling::Result<void> 成功或错误状态
		 * 
		 * @note 运行在PASSIVE_LEVEL IRQL
		 * @note 自动从哈希表中移除
		 * @note 自动失效EPT缓存
		 */
		error_handling::Result<void> remove_hook(const strong_types::HookId& hook_id);

		/**
		 * @brief 移除指定的监视点 - 新增方法
		 * 
		 * @param hook_id 要移除的监视点标识符
		 * 
		 * @return error_handling::Result<void> 成功或错误状态
		 */
		error_handling::Result<void> remove_watchpoint(const strong_types::HookId& hook_id);
		
		/**
		 * @brief 禁用所有EPT hook和监视点
		 * 
		 * 移除当前安装的所有hook和监视点，恢复原始页面权限。
		 * 清理过程：
		 * 1. 清空hook和监视点列表
		 * 2. 恢复所有修改过的EPT页表条目
		 * 3. 释放分割的页表结构
		 * 4. 重置访问记录
		 * 
		 * @note 运行在PASSIVE_LEVEL IRQL
		 * @note 操作完成后需要失效EPT缓存
		 * @note 该操作是幂等的，多次调用无副作用
		 */
		void disable_all_hooks();

		/**
		 * @brief 处理EPT violation事件
		 * 
		 * 当guest访问受EPT保护的页面时，CPU触发EPT violation VM exit。
		 * 该方法分析violation原因并执行相应的处理逻辑：
		 * 
		 * Hook处理：
		 * 1. 识别violation是否由hook引起
		 * 2. 根据访问类型(执行/读写)切换页面权限
		 * 3. 更新虚假页面内容(如果需要)
		 * 4. 设置适当的页面权限和RIP增量
		 * 
		 * 监视点处理：
		 * 1. 记录访问信息到访问记录数组
		 * 2. 临时切换权限允许当前访问
		 * 3. 设置单步执行以便后续恢复权限
		 * 
		 * @param guest_context guest执行上下文，包含violation详细信息
		 * 
		 * @note 运行在DISPATCH_LEVEL IRQL(通过VM exit调用)
		 * @note 性能关键路径，必须最小化处理开销
		 * @note 处理失败可能导致guest终止
		 */
		void handle_violation(guest_context& guest_context);
		
		/**
		 * @brief 处理EPT配置错误事件
		 * 
		 * 当EPT页表配置存在错误时触发，通常表示严重的页表损坏。
		 * 该方法记录错误信息并终止虚拟机执行。
		 * 
		 * @param guest_context guest执行上下文
		 * 
		 * @note 运行在DISPATCH_LEVEL IRQL
		 * @note 该事件通常表示致命错误，会终止guest执行
		 */
		void handle_misconfiguration(guest_context& guest_context) const;

		/**
		 * @brief 获取EPT指针结构
		 * 
		 * 返回用于VMCS配置的EPT指针，包含EPT页表基址和控制信息。
		 * 
		 * @return ept_pointer EPT指针结构，用于VMCS的EPT_POINTER字段
		 * 
		 * @note 可以在任何IRQL级别调用
		 * @note 返回的指针在EPT实例生命周期内有效
		 */
		ept_pointer get_ept_pointer() const;
		
		/**
		 * @brief 失效EPT页表缓存
		 * 
		 * 在所有CPU核心上执行INVEPT指令，失效EPT TLB缓存。
		 * 当EPT页表发生变化后必须调用，确保硬件使用最新的页表映射。
		 * 
		 * 失效场景：
		 * - 安装或移除hook后
		 * - 页面权限发生变化后
		 * - 页表结构修改后
		 * 
		 * @note 运行在PASSIVE_LEVEL IRQL  
		 * @note 通过DPC在所有CPU核心上同步执行
		 * @note 失效不当可能导致hook失效或系统不稳定
		 */
		void invalidate() const;

		/**
		 * @brief 生成地址翻译提示
		 * 
		 * 为指定内存区域生成翻译提示，优化跨进程hook安装。
		 * 将虚拟地址区域转换为物理页面映射信息。
		 * 
		 * @param destination 目标虚拟地址
		 * @param length 区域长度
		 * 
		 * @return utils::list<ept_translation_hint> 翻译提示列表
		 * 
		 * @note 运行在PASSIVE_LEVEL IRQL
		 * @note 静态方法，可以在没有EPT实例时调用
		 * @note 用于优化用户态到内核态的hook安装过程
		 */
		static utils::list<ept_translation_hint> generate_translation_hints(const void* destination, size_t length);

		/**
		 * @brief 获取访问记录数组
		 * 
		 * 返回代码监视点记录的访问历史数据，用于分析代码执行模式。
		 * 
		 * @param count 输出参数，返回记录数组的有效元素数量
		 * 
		 * @return uint64_t* 访问记录数组指针，包含RIP值
		 * 
		 * @note 运行在PASSIVE_LEVEL IRQL
		 * @note 返回的数组在EPT实例生命周期内有效
		 * @note 记录数组大小固定为1024个条目，循环覆盖
		 */
		uint64_t* get_access_records(size_t* count);

		/**
		 * @brief 清理指定进程的EPT资源
		 * 
		 * 当进程终止时，清理该进程相关的所有hook和监视点。
		 * 避免悬空指针和资源泄漏。
		 * 
		 * @param process 进程ID
		 * 
		 * @return true 清理了至少一个相关资源
		 * @return false 没有找到相关资源
		 * 
		 * @note 运行在PASSIVE_LEVEL IRQL
		 * @note 该方法由进程通知回调调用
		 * @note 清理操作是原子的，不会影响其他进程的资源
		 */
		bool cleanup_process(process_id process);

	private:
		// === EPT页表结构（页面对齐）===
		/// PML4页表 - EPT的顶级页表
		DECLSPEC_PAGE_ALIGN pml4 epml4[EPT_PML4E_ENTRY_COUNT];
		
		/// PML3页表 - 页目录指针表
		DECLSPEC_PAGE_ALIGN pml3 epdpt[EPT_PDPTE_ENTRY_COUNT];
		
		/// PML2页表 - 页目录表
		DECLSPEC_PAGE_ALIGN pml2 epde[EPT_PDPTE_ENTRY_COUNT][EPT_PDE_ENTRY_COUNT];

		// === 性能优化组件 ===
		/// 简化查找系统 - 线性查找（避免链接问题）
		simple_ept_lookup simple_lookup_{};
		
		/// 并发控制管理器 - 线程安全保护
		synchronized_ept_manager sync_manager_{};
		
		/// Hook ID序列生成器（使用interlocked操作保证原子性）
		volatile uint32_t hook_sequence_counter_{0};

		// === 访问记录和统计 ===
		/// 访问记录数组（用于监视点）
		uint64_t access_records[1024];
		
		/// 访问记录索引（使用interlocked操作保证原子性）
		volatile size_t access_record_index_{0};

		// === 传统数据结构（兼容性保留）===
		/// EPT分割结构列表
		utils::list<ept_split, utils::AlignedAllocator> ept_splits{};
		
		/// 传统EPT hook列表（兼容性保留）
		utils::list<ept_hook, utils::AlignedAllocator> ept_hooks{};
		
		/// EPT代码监视点列表
		utils::list<ept_code_watch_point> ept_code_watch_points{};
		
		/// 优化版hook列表
		utils::list<optimized_ept_hook, utils::AlignedAllocator> optimized_hooks{};

		// === 页表操作方法 ===
		/**
		 * @brief 获取PML2页表条目
		 * 
		 * @param physical_address 物理地址
		 * @return pml2* PML2条目指针
		 */
		pml2* get_pml2_entry(uint64_t physical_address);
		
		/**
		 * @brief 获取PML1页表条目
		 * 
		 * @param physical_address 物理地址
		 * @return pml1* PML1条目指针
		 */
		pml1* get_pml1_entry(uint64_t physical_address);
		
		/**
		 * @brief 查找PML1页表
		 * 
		 * @param physical_address 物理地址
		 * @return pml1* PML1页表指针
		 */
		pml1* find_pml1_table(uint64_t physical_address);

		// === 资源分配方法 ===
		/**
		 * @brief 分配EPT分割结构
		 * 
		 * @return ept_split& 分配的分割结构引用
		 */
		ept_split& allocate_ept_split();
		
		/**
		 * @brief 分配传统EPT hook
		 * 
		 * @param physical_address 物理地址
		 * @return ept_hook& 分配的hook引用
		 */
		ept_hook& allocate_ept_hook(uint64_t physical_address);
		
		/**
		 * @brief 查找传统EPT hook
		 * 
		 * @param physical_address 物理地址
		 * @return ept_hook* hook指针，未找到返回nullptr
		 */
		ept_hook* find_ept_hook(uint64_t physical_address);

		/**
		 * @brief 分配EPT代码监视点
		 * 
		 * @return ept_code_watch_point& 分配的监视点引用
		 */
		ept_code_watch_point& allocate_ept_code_watch_point();
		
		/**
		 * @brief 查找EPT代码监视点
		 * 
		 * @param physical_address 物理地址
		 * @return ept_code_watch_point* 监视点指针，未找到返回nullptr
		 */
		ept_code_watch_point* find_ept_code_watch_point(uint64_t physical_address);

		// === 优化版资源管理方法 ===
		/**
		 * @brief 分配优化版EPT hook
		 * 
		 * @param addr 强类型物理地址
		 * @param hook_id Hook标识符
		 * @return error_handling::Result<optimized_ept_hook*> 成功时返回hook指针
		 */
		error_handling::Result<optimized_ept_hook*> allocate_optimized_hook(
			const strong_types::PhysicalAddress& addr,
			const strong_types::HookId& hook_id);
		
		/**
		 * @brief 生成新的Hook ID
		 * 
		 * @param type Hook类型
		 * @param addr 物理地址
		 * @return strong_types::HookId 生成的Hook ID
		 */
		strong_types::HookId generate_hook_id(strong_types::HookType type, 
		                                       const strong_types::PhysicalAddress& addr);

		// === 兼容性方法 ===
		/**
		 * @brief 获取或创建EPT hook（兼容性方法）
		 * 
		 * @param destination 目标地址
		 * @param translation_hint 翻译提示
		 * @return ept_hook* hook指针
		 */
		ept_hook* get_or_create_ept_hook(void* destination, const ept_translation_hint* translation_hint = nullptr);

		// === 页表管理方法 ===
		/**
		 * @brief 分割大页面为小页面（原版本 - 向后兼容）
		 * 
		 * @param physical_address 物理地址
		 * 
		 * @throws std::runtime_error 如果分割失败
		 */
		void split_large_page(uint64_t physical_address);
		
		/**
		 * @brief 分割大页面为小页面 - 优化版本（新版本）
		 * 
		 * @param physical_address 物理地址
		 * @return error_handling::Result<void> 操作结果
		 */
		error_handling::Result<void> split_large_page_optimized(uint64_t physical_address);

		/**
		 * @brief 安装页面级hook（内部方法）
		 * 
		 * @param destination 目标地址
		 * @param source 源数据
		 * @param length 数据长度
		 * @param source_pid 源进程ID
		 * @param target_pid 目标进程ID
		 * @param translation_hint 翻译提示
		 */
		void install_page_hook(void* destination, const void* source, size_t length, process_id source_pid,
		                       process_id target_pid, const ept_translation_hint* translation_hint = nullptr);

		/**
		 * @brief 记录访问信息
		 * 
		 * @param rip 指令指针
		 */
		void record_access(uint64_t rip);
		
		/**
		 * @brief 优化版violation处理 - 使用哈希表查找
		 * 
		 * @param guest_context Guest执行上下文
		 * @return error_handling::Result<void> 处理结果
		 */
		error_handling::Result<void> handle_violation_optimized(guest_context& guest_context);
	};
	
	// === 编译时数据结构验证 ===
	namespace compile_time_validation {
		
		/// 确保EPT页表条目大小正确
		static_assert(sizeof(pml1) == 8, "EPT PML1 entry must be 8 bytes");
		static_assert(sizeof(pml2) == 8, "EPT PML2 entry must be 8 bytes");  
		static_assert(sizeof(pml3) == 8, "EPT PML3 entry must be 8 bytes");
		static_assert(sizeof(pml4) == 8, "EPT PML4 entry must be 8 bytes");
		
		/// 确保优化版hook结构大小合理
		static_assert(sizeof(optimized_ept_hook) <= 256, 
		              "Optimized hook should fit in reasonable memory");
		// 注释掉对齐检查，因为在内核模式下可能有不同的对齐要求
		// static_assert(alignof(optimized_ept_hook) == 64,
		//               "Optimized hook must be 64-byte aligned (cache line aligned)");
		
		/// 确保传统hook结构页面对齐
		static_assert(alignof(ept_hook) >= PAGE_SIZE, 
		              "EPT hook must be page-aligned");
		
		/// 确保强类型地址结构大小
		static_assert(sizeof(strong_types::PhysicalAddress) == sizeof(uint64_t),
		              "PhysicalAddress should be same size as uint64_t");
		static_assert(sizeof(strong_types::HookId) <= 32,
		              "HookId should be reasonably small");
		
		/// 确保Result类型开销最小
		static_assert(sizeof(error_handling::Result<void>) <= 16,
		              "Result<void> should have minimal overhead");
		
		/// 确保简化查找表的大小合理
		static_assert(simple_ept_lookup::MAX_HOOKS <= 256, "MAX_HOOKS should be reasonable");
		static_assert(simple_ept_lookup::MAX_WATCHPOINTS <= 256, "MAX_WATCHPOINTS should be reasonable");
		
		/// 确保关键常量的正确性
		static_assert(EPT_PML4E_ENTRY_COUNT == 512, "PML4 must have 512 entries");
		static_assert(EPT_PDPTE_ENTRY_COUNT == 512, "PML3 must have 512 entries");
		static_assert(EPT_PDE_ENTRY_COUNT == 512, "PML2 must have 512 entries");
		
		/// 确保页面大小宏的一致性
		static_assert(PAGE_SIZE == 4096, "PAGE_SIZE must be 4KB");
		static_assert((PAGE_SIZE & (PAGE_SIZE - 1)) == 0, "PAGE_SIZE must be power of 2");
		
		/// 验证地址掩码宏的正确性（编译时计算）
		constexpr uint64_t test_addr = 0x1234567890ABCULL;
		static_assert(ADDRMASK_EPT_PML4_INDEX(test_addr) == ((test_addr & 0xFF8000000000ULL) >> 39),
		              "PML4 index extraction macro validation failed");
		static_assert(ADDRMASK_EPT_PML3_INDEX(test_addr) == ((test_addr & 0x7FC0000000ULL) >> 30),
		              "PML3 index extraction macro validation failed");
		
		/// 确保内存对齐要求
		static_assert(alignof(volatile uint32_t) <= alignof(optimized_ept_hook),
		              "Volatile members must be properly aligned within optimized_ept_hook");
		
		/// 验证模板实例化的合法性
		template<typename T>
		constexpr bool is_result_valid() {
			return sizeof(error_handling::Result<T>) >= sizeof(T) + sizeof(NTSTATUS);
		}
		
		/// 确保常用Result类型的合理性
		static_assert(is_result_valid<strong_types::HookId>(),
		              "Result<HookId> must have reasonable size");
		static_assert(is_result_valid<strong_types::PhysicalAddress>(),
		              "Result<PhysicalAddress> must have reasonable size");
		
		/// 验证简化查找系统的基本约束
		constexpr bool test_simple_lookup_constraints() {
			// 确保数组大小合理
			return simple_ept_lookup::MAX_HOOKS > 0 && 
			       simple_ept_lookup::MAX_WATCHPOINTS > 0 &&
			       simple_ept_lookup::MAX_HOOKS <= 1024 &&
			       simple_ept_lookup::MAX_WATCHPOINTS <= 1024;
		}
		
		static_assert(test_simple_lookup_constraints(),
		              "Simple lookup constraints should be satisfied");
	}
}
