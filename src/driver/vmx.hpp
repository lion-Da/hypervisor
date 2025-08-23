/**
 * @file vmx.hpp
 * @brief Intel VMX(Virtual Machine Extensions)数据结构和常量定义
 * 
 * 该文件定义了实现VMX虚拟化所需的所有关键数据结构，包括：
 * 1. VMCS(Virtual Machine Control Structure) - VMX控制结构
 * 2. VMX状态管理结构 - 每个CPU核心的虚拟化状态
 * 3. Guest执行上下文 - VM exit时的CPU状态信息
 * 4. 段描述符和特殊寄存器结构
 * 
 * 这些结构体直接对应Intel架构手册中定义的硬件规格，
 * 必须严格遵循硬件要求的内存布局和对齐方式。
 */

#pragma once
#include "ept.hpp"

/**
 * @brief Hyper-V兼容性标识位
 * 
 * 设置CPUID leaf 1的ECX寄存器bit 31，表示存在hypervisor。
 * 这是Microsoft Hyper-V定义的标准，用于向guest OS表明运行在虚拟化环境中。
 * 
 * 用途：
 * - 让guest OS知道自己运行在hypervisor之上
 * - 某些系统软件会检查此位来调整行为
 * - 提供与主流hypervisor的兼容性
 */
#define HYPERV_HYPERVISOR_PRESENT_BIT           0x80000000

/**
 * @brief Hyper-V CPUID接口leaf编号
 * 
 * 定义hypervisor标识查询的CPUID leaf编号。
 * 当guest执行CPUID(0x40000001)时，hypervisor返回标识信息。
 */
#define HYPERV_CPUID_INTERFACE                  0x40000001

namespace vmx
{
	/**
	 * @brief VMCS (Virtual Machine Control Structure) 数据结构
	 * 
	 * VMCS是VMX架构的核心数据结构，控制虚拟机的行为和状态。
	 * Intel硬件要求：
	 * - 必须页面对齐(4KB边界)
	 * - 大小固定为4KB(PAGE_SIZE)
	 * - 前8字节包含revision ID和abort指示器
	 * - 其余部分由CPU硬件管理，软件不应直接访问
	 * 
	 * 用途分类：
	 * - Guest状态字段：保存guest CPU状态
	 * - Host状态字段：保存host CPU状态  
	 * - VM执行控制字段：控制VM behavior
	 * - VM退出信息字段：记录VM exit原因和信息
	 */
	struct vmcs
	{
		/// VMCS修订标识符 - 必须与CPU支持的版本匹配
		uint32_t revision_id;
		
		/// VM abort指示器 - 非零值表示VM abort发生
		uint32_t abort_indicator;
		
		/// 硬件管理的VMCS数据区域 - 软件不应直接访问
		uint8_t data[PAGE_SIZE - 8];
	};

	/**
	 * @brief 特殊寄存器快照结构
	 * 
	 * 保存CPU进入VMX根模式前的关键系统寄存器状态。
	 * 这些寄存器控制CPU的操作模式、内存管理和调试功能。
	 * 必须在虚拟化环境启动时精确捕获，在退出时正确恢复。
	 * 
	 * 寄存器分类：
	 * - 控制寄存器(CR0,CR3,CR4)：控制CPU操作模式和内存管理
	 * - 段寄存器(TR,LDTR)：段式内存管理
	 * - 描述符表寄存器(GDTR,IDTR)：系统表位置
	 * - MSR寄存器：模型特定功能控制
	 * - 调试寄存器：调试和性能监控
	 */
	struct special_registers
	{
		/// CR0控制寄存器 - 控制CPU操作模式(保护模式、分页等)
		uint64_t cr0;
		
		/// CR3页目录基址寄存器 - 指向页目录表物理地址  
		uint64_t cr3;
		
		/// CR4控制寄存器 - 控制扩展CPU特性(VMX、SMEP、SMAP等)
		uint64_t cr4;
		
		/// GS段基址MSR - 内核GS段基址(用于快速系统调用)
		uint64_t msr_gs_base;
		
		/// TR任务寄存器 - 指向当前TSS段选择子
		uint16_t tr;
		
		/// LDTR局部描述符表寄存器 - 通常在64位模式下为0
		uint16_t ldtr;
		
		/// 调试控制MSR - 控制分支跟踪和性能监控
		uint64_t debug_control;
		
		/// 内核调试寄存器DR7 - 硬件断点控制
		uint64_t kernel_dr7;
		
		/// 中断描述符表寄存器 - IDT的基址和长度
		segment_descriptor_register_64 idtr;
		
		/// 全局描述符表寄存器 - GDT的基址和长度
		segment_descriptor_register_64 gdtr;
	};

	/**
	 * @brief VMX启动上下文结构
	 * 
	 * 包含启动VMX虚拟化环境所需的所有信息和状态。
	 * 该结构在hypervisor启动时填充，包含：
	 * 1. CPU状态快照 - 进入VMX前的完整CPU状态
	 * 2. 物理地址映射 - 关键数据结构的物理地址
	 * 3. VMX能力信息 - 从MSR寄存器读取的VMX特性
	 * 4. 控制字段配置 - EPT和其他VMX功能的控制位
	 * 
	 * 内存布局要求：
	 * - 整个结构必须页面对齐
	 * - 与stack_buffer共享内存空间(union)
	 * - 大小不超过KERNEL_STACK_SIZE
	 */
	struct launch_context
	{
		/// 特殊寄存器状态快照
		special_registers special_registers;
		
		/// Windows执行上下文 - 包含通用寄存器和浮点状态
		CONTEXT context_frame;
		
		/// 系统页目录表基址 - System进程的CR3值
		uint64_t system_directory_table_base;
		
		/// VMX能力MSR数据 - 17个MSR寄存器的值，定义VMX功能限制
		ULARGE_INTEGER msr_data[17];
		
		/// VMXON区域物理地址 - 用于VMXON指令
		uint64_t vmx_on_physical_address;
		
		/// VMCS物理地址 - 当前VMCS的物理地址
		uint64_t vmcs_physical_address;
		
		/// MSR位图物理地址 - 控制guest对MSR的访问权限
		uint64_t msr_bitmap_physical_address;
		
		/// EPT控制配置 - 启用EPT和VPID的控制位
		ia32_vmx_procbased_ctls2_register ept_controls;
		
		/// 启动状态标志 - 标识VMX是否已成功启动
		bool launched;
	};

	/**
	 * @brief 每个CPU核心的VMX状态管理结构
	 * 
	 * 该结构包含单个CPU核心运行VMX虚拟化所需的所有数据：
	 * 1. 启动上下文和栈空间
	 * 2. MSR访问控制位图
	 * 3. VMXON和VMCS区域
	 * 4. EPT页表管理器指针
	 * 
	 * 内存布局要求：
	 * - 整个结构和所有成员都必须页面对齐(4KB边界)
	 * - 每个CPU核心有独立的state实例
	 * - 总大小可达数百KB，必须使用非分页内存
	 * 
	 * union设计：
	 * - launch_context和stack_buffer共享同一内存区域
	 * - launch_context用于VMX初始化
	 * - stack_buffer用于hypervisor运行时栈空间
	 */
	struct state
	{
		/// 启动上下文与栈缓冲区的联合体(节省内存)
		union
		{
			/// 内核栈缓冲区 - hypervisor运行时使用
			DECLSPEC_PAGE_ALIGN uint8_t stack_buffer[KERNEL_STACK_SIZE]{};
			
			/// VMX启动上下文 - 初始化时使用，与栈缓冲区互斥
			DECLSPEC_PAGE_ALIGN launch_context launch_context;
		};

		/**
		 * @brief MSR访问控制位图
		 * 
		 * 4KB位图，控制guest对MSR寄存器的访问权限：
		 * - 每个MSR对应2个bit(读权限位和写权限位)
		 * - bit=0: 允许guest直接访问MSR
		 * - bit=1: guest访问MSR时触发VM exit
		 * 
		 * 用途：
		 * - 拦截guest对关键MSR的访问
		 * - 实现MSR虚拟化和安全控制
		 * - 隐藏hypervisor的存在
		 */
		DECLSPEC_PAGE_ALIGN uint8_t msr_bitmap[PAGE_SIZE]{};

		/**
		 * @brief VMXON区域
		 * 
		 * VMXON指令所需的4KB对齐数据结构：
		 * - 存储VMX根模式的初始化信息
		 * - CPU硬件管理，软件只需设置revision ID
		 * - 每个逻辑处理器需要独立的VMXON区域
		 */
		DECLSPEC_PAGE_ALIGN vmcs vmx_on{};
		
		/**
		 * @brief 当前活动的VMCS
		 * 
		 * 控制当前虚拟机执行的VMCS结构：
		 * - 包含guest和host的完整状态信息
		 * - 定义VM执行控制策略
		 * - CPU通过VMREAD/VMWRITE指令访问
		 */
		DECLSPEC_PAGE_ALIGN vmcs vmcs{};

		/**
		 * @brief EPT页表管理器指针
		 * 
		 * 指向全局EPT页表管理器实例。
		 * 所有CPU核心共享同一个EPT页表结构，
		 * 但每个核心有独立的EPT指针字段。
		 * 
		 * @note 该指针在所有CPU核心间共享，
		 *       指向同一个EPT页表管理实例
		 */
		DECLSPEC_PAGE_ALIGN ept* ept{};
	};

	/**
	 * @brief GDT(Global Descriptor Table)条目信息结构
	 * 
	 * 表示一个GDT段描述符的完整信息，用于：
	 * 1. 在VMCS中设置guest和host段寄存器字段
	 * 2. 保存和恢复段描述符状态
	 * 3. 实现段虚拟化(如果需要)
	 * 
	 * 64位模式下的段管理：
	 * - 代码和数据段大多被平坦化(base=0, limit=0xFFFFFFFF)
	 * - TSS和LDT段仍然使用传统的段机制
	 * - 访问权限控制DPL、类型等属性
	 */
	struct gdt_entry
	{
		/// 段基址 - 64位线性基地址(32位模式下截断为32位)
		uint64_t base;
		
		/// 段长度限制 - 定义段的最大偏移量
		uint32_t limit;
		
		/// 段访问权限 - DPL、段类型、存在位等控制信息
		vmx_segment_access_rights access_rights;
		
		/// 段选择子 - 包含段索引、TI位和RPL字段
		segment_selector selector;
	};

	/**
	 * @brief Guest虚拟机执行上下文
	 * 
	 * 在VM exit发生时，该结构保存guest的执行状态和exit信息。
	 * VM exit处理程序使用此信息来：
	 * 1. 分析exit原因和相关信息
	 * 2. 执行相应的虚拟化处理逻辑  
	 * 3. 决定是否返回guest或终止虚拟机
	 * 4. 修改guest状态(如寄存器值)
	 * 
	 * 处理流程：
	 * 1. VM exit触发时，CPU自动保存guest状态到VMCS
	 * 2. Hypervisor从VMCS读取相关字段填充此结构
	 * 3. Exit处理程序根据exit_reason分发到具体处理函数
	 * 4. 处理完成后，根据increment_rip决定是否更新RIP
	 */
	struct guest_context
	{
		/// 指向guest寄存器上下文的指针 - 包含通用寄存器状态
		PCONTEXT vp_regs;
		
		/// Guest指令指针 - VM exit时的RIP值
		uintptr_t guest_rip;
		
		/// Guest栈指针 - VM exit时的RSP值  
		uintptr_t guest_rsp;
		
		/// Guest标志寄存器 - VM exit时的RFLAGS值
		uintptr_t guest_e_flags;
		
		/// Guest物理地址 - 导致EPT violation的物理地址(仅EPT exit有效)
		uintptr_t guest_physical_address;
		
		/// VM exit原因码 - 标识导致VM exit的具体原因
		uint16_t exit_reason;
		
		/// Exit限定信息 - 提供exit原因的额外详细信息
		uintptr_t exit_qualification;
		
		/// 退出VM标志 - 设置为true时将终止虚拟机执行
		bool exit_vm;
		
		/// RIP增量标志 - 是否在返回guest前自动增加RIP
		bool increment_rip;
	};
}
