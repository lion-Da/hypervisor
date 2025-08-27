#pragma once
#include "stdint.hpp"
#include "functional.hpp"

namespace thread
{
	// CPU 调度顺序常量
	constexpr bool CPU_ORDER_FORWARD = false;  // 正序：CPU 0 -> 1 -> 2 -> ...
	constexpr bool CPU_ORDER_REVERSE = true;   // 倒序：CPU N -> N-1 -> ... -> 0
	uint32_t get_processor_count();
	uint32_t get_processor_index();

	_IRQL_requires_min_(PASSIVE_LEVEL)
	_IRQL_requires_max_(APC_LEVEL)
	bool sleep(uint32_t milliseconds);

	_IRQL_requires_max_(APC_LEVEL)
	_IRQL_requires_min_(PASSIVE_LEVEL)
	_IRQL_requires_same_
	void dispatch_on_specific_cpu(void (*callback)(void*), void* data, uint32_t cpu_id);

	_IRQL_requires_max_(APC_LEVEL)
	_IRQL_requires_min_(PASSIVE_LEVEL)
	_IRQL_requires_same_
	void dispatch_on_all_cores(void (*callback)(void*), void* data, bool reverse_order = CPU_ORDER_FORWARD);

	_IRQL_requires_max_(APC_LEVEL)
	_IRQL_requires_min_(PASSIVE_LEVEL)
	_IRQL_requires_same_

	template <typename F>
	void dispatch_on_all_cores(F&& callback, bool reverse_order = CPU_ORDER_FORWARD)
	{
		dispatch_on_all_cores([](void* data)
		{
			(*static_cast<F*>(data))();
		}, &callback, reverse_order);
	}
	
	class kernel_thread
	{
	public:
		kernel_thread() = default;
		kernel_thread(std::function<void()>&& callback);
		~kernel_thread();

		kernel_thread(kernel_thread&& obj) noexcept;
		kernel_thread& operator=(kernel_thread&& obj) noexcept;

		kernel_thread(const kernel_thread& obj) = delete;
		kernel_thread& operator=(const kernel_thread& obj) = delete;

		bool joinable() const;
		void join();
		void detach();

	private:
		PETHREAD handle_{nullptr};
	};
}
