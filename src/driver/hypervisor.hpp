#pragma once

#include "vmx.hpp"

// CPU 状态枚举 (基于 barehypervisor)
enum class cpu_status : uint32_t
{
	stopped = 0,    // CPU 未运行 VMX
	running = 1,    // CPU 正在运行 VMX  
	corrupt = 2     // CPU 状态损坏，需要重启
};

class hypervisor
{
public:
	hypervisor();
	~hypervisor();

	hypervisor(hypervisor&& obj) noexcept = delete;
	hypervisor& operator=(hypervisor&& obj) noexcept = delete;

	hypervisor(const hypervisor& obj) = delete;
	hypervisor& operator=(const hypervisor& obj) = delete;

	void enable();
	void disable();
	
	// 安全的启用方法，可以延迟调用
	bool try_enable_safely();

	bool is_enabled() const;

	bool install_ept_hook(const void* destination, const void* source, size_t length, process_id source_pid,
	                      process_id target_pid, const utils::list<vmx::ept_translation_hint>& hints = {});

	bool install_ept_code_watch_point(uint64_t physical_page, process_id source_pid, process_id target_pid,
	                                  bool invalidate = true) const;
	bool install_ept_code_watch_points(const uint64_t* physical_pages, size_t count, process_id source_pid,
	                                   process_id target_pid) const;

	void disable_all_ept_hooks() const;

	vmx::ept& get_ept() const;

	static hypervisor* get_instance();

	bool cleanup_process(process_id process);

private:
	uint32_t vm_state_count_{0};
	vmx::state** vm_states_{nullptr};
	vmx::ept* ept_{nullptr};
	
	// CPU 状态跟踪 (基于 barehypervisor 模式)
	cpu_status* cpu_status_array_{nullptr};

	// CPU 配置检查 (基于 barehypervisor)
	bool check_cpu_configuration() const;
	
	void enable_core(uint64_t system_directory_table_base);
	bool try_enable_core(uint64_t system_directory_table_base);
	void disable_core();
	
	// CPU 状态管理
	void set_cpu_status(uint32_t cpu_id, cpu_status status);
	cpu_status get_cpu_status(uint32_t cpu_id) const;

	void allocate_vm_states();
	void free_vm_states();

	void invalidate_cores() const;

	vmx::state* get_current_vm_state() const;
};
