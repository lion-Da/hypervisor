#pragma once
#include "ept.hpp"

#define HYPERV_HYPERVISOR_PRESENT_BIT           0x80000000
#define HYPERV_CPUID_INTERFACE                  0x40000001

namespace vmx
{
	struct vmcs
	{
		uint32_t revision_id;
		uint32_t abort_indicator;
		uint8_t data[PAGE_SIZE - 8];
	};

	struct special_registers
	{
		uint64_t cr0;
		uint64_t cr3;
		uint64_t cr4;
		uint64_t msr_gs_base;
		uint16_t tr;
		uint16_t ldtr;
		uint64_t debug_control;
		uint64_t kernel_dr7;
		segment_descriptor_register_64 idtr;
		segment_descriptor_register_64 gdtr;
	};

	// 增强的上下文结构 (基于 barehypervisor 最佳实践)
	struct enhanced_state_save
	{
		// 通用寄存器 (保持Windows兼容性)
		CONTEXT context_frame;
		
		// 控制寄存器 (barehypervisor 风格完整保存)
		uint64_t cr0, cr2, cr3, cr4, cr8;
		uint64_t xcr0;
		
		// 调试寄存器 (barehypervisor 完整保存)
		uint64_t dr0, dr1, dr2, dr3, dr6, dr7;
		
		// 关键MSR (barehypervisor 风格)
		uint64_t msr_efer;
		uint64_t msr_star;
		uint64_t msr_lstar;
		uint64_t msr_cstar;
		uint64_t msr_fmask;
		uint64_t msr_fs_base;
		uint64_t msr_gs_base;
		uint64_t msr_kernel_gs_base;
		uint64_t msr_sysenter_cs;
		uint64_t msr_sysenter_esp;
		uint64_t msr_sysenter_eip;
		uint64_t msr_pat;
		uint64_t msr_debugctl;
		
		// VMX 相关 (保留现有)
		ULARGE_INTEGER vmx_msr_data[17];
		
		// 状态标志
		bool context_saved;
		bool launched;
	};

	struct launch_context
	{
		special_registers special_registers;
		enhanced_state_save state_save;
		uint64_t system_directory_table_base;
		uint64_t vmx_on_physical_address;
		uint64_t vmcs_physical_address;
		uint64_t msr_bitmap_physical_address;
		ia32_vmx_procbased_ctls2_register ept_controls;
		bool launched;
	};

	struct state
	{
		union
		{
			DECLSPEC_PAGE_ALIGN uint8_t stack_buffer[KERNEL_STACK_SIZE]{};
			DECLSPEC_PAGE_ALIGN launch_context launch_context;
		};

		DECLSPEC_PAGE_ALIGN uint8_t msr_bitmap[PAGE_SIZE]{};

		DECLSPEC_PAGE_ALIGN vmcs vmx_on{};
		DECLSPEC_PAGE_ALIGN vmcs vmcs{};

		DECLSPEC_PAGE_ALIGN ept* ept{};
	};

	struct gdt_entry
	{
		uint64_t base;
		uint32_t limit;
		vmx_segment_access_rights access_rights;
		segment_selector selector;
	};

	struct guest_context
	{
		PCONTEXT vp_regs;
		uintptr_t guest_rip;
		uintptr_t guest_rsp;
		uintptr_t guest_e_flags;
		uintptr_t guest_physical_address;
		uint16_t exit_reason;
		uintptr_t exit_qualification;
		bool exit_vm;
		bool increment_rip;
	};
}
