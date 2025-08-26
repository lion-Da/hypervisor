#include "thread.hpp"
#include "std_include.hpp"
#include "logging.hpp"
#include "exception.hpp"
#include "finally.hpp"

namespace thread
{
	namespace
	{
		struct dispatch_data
		{
			void (*callback)(void*){};
			void* data{};
		};

		void dispatch_callback(const void* context)
		{
			try
			{
				const auto* const data = static_cast<const dispatch_data*>(context);
				data->callback(data->data);
			}
			catch (std::exception& e)
			{
				debug_log("Exception during dpc on core %d: %s\n", get_processor_index(), e.what());
			}
			catch (...)
			{
				debug_log("Unknown exception during dpc on core %d\n", get_processor_index());
			}
		}


		_Function_class_(KDEFERRED_ROUTINE)

		void NTAPI callback_dispatcher(struct _KDPC* /*Dpc*/,
		                               const PVOID param,
		                               const PVOID arg1,
		                               const PVOID arg2)
		{
			dispatch_callback(param);

			KeSignalCallDpcSynchronize(arg2);
			KeSignalCallDpcDone(arg1);
		}

		_Function_class_(KDEFERRED_ROUTINE)

		void NTAPI sequential_callback_dispatcher(struct _KDPC* /*Dpc*/,
		                                          const PVOID param,
		                                          const PVOID arg1,
		                                          const PVOID arg2)
		{
			const auto cpu_count = get_processor_count();
			const auto current_cpu = get_processor_index();

			for (auto i = 0u; i < cpu_count; ++i)
			{
				if (i == current_cpu)
				{
					dispatch_callback(param);
				}

				KeSignalCallDpcSynchronize(arg2);
			}

			KeSignalCallDpcDone(arg1);
		}

		void thread_starter(void* context)
		{
			auto* function_ptr = static_cast<std::function<void()>*>(context);
			const auto function = std::move(*function_ptr);
			delete function_ptr;

			try
			{
				function();
			}
			catch (std::exception& e)
			{
				debug_log("Kernel thread threw an exception: %s\n", e.what());
			}
			catch (...)
			{
				debug_log("Kernel thread threw an unknown exception\n");
			}
		}
	}

	kernel_thread::kernel_thread(std::function<void()>&& callback)
	{
		auto* function_object = new std::function(std::move(callback));

		auto destructor = utils::finally([&function_object]()
		{
			delete function_object;
		});


		HANDLE handle{};
		const auto status = PsCreateSystemThread(&handle, 0, nullptr, nullptr, nullptr, thread_starter,
		                                         function_object);

		if (status != STATUS_SUCCESS)
		{
			throw std::runtime_error("Failed to create thread!");
		}

		ObReferenceObjectByHandle(handle, THREAD_ALL_ACCESS, nullptr, KernelMode,
		                          reinterpret_cast<void**>(&this->handle_), nullptr);

		ZwClose(handle);

		destructor.cancel();
	}

	kernel_thread::~kernel_thread()
	{
		this->join();
	}

	kernel_thread::kernel_thread(kernel_thread&& obj) noexcept
	{
		this->operator=(std::move(obj));
	}

	kernel_thread& kernel_thread::operator=(kernel_thread&& obj) noexcept
	{
		if (this != &obj)
		{
			this->join();
			this->handle_ = obj.handle_;
			obj.handle_ = nullptr;
		}

		return *this;
	}

	bool kernel_thread::joinable() const
	{
		return this->handle_ != nullptr;
	}

	void kernel_thread::join()
	{
		if (this->joinable())
		{
			KeWaitForSingleObject(this->handle_, Executive, KernelMode, FALSE, nullptr);
			this->detach();
		}
	}

	void kernel_thread::detach()
	{
		if (this->joinable())
		{
			ObDereferenceObject(this->handle_);
			this->handle_ = nullptr;
		}
	}

	uint32_t get_processor_count()
	{
		return static_cast<uint32_t>(KeQueryActiveProcessorCountEx(0));
	}

	uint32_t get_processor_index()
	{
		return static_cast<uint32_t>(KeGetCurrentProcessorNumberEx(nullptr));
	}

	bool sleep(const uint32_t milliseconds)
	{
		LARGE_INTEGER interval{};
		interval.QuadPart = -(10000ll * milliseconds);

		return STATUS_SUCCESS == KeDelayExecutionThread(KernelMode, FALSE, &interval);
	}

	void dispatch_on_specific_cpu(void (*callback)(void*), void* data, const uint32_t cpu_id)
	{
		dispatch_data callback_data{};
		callback_data.callback = callback;
		callback_data.data = data;

		// 创建 DPC 对象
		KDPC dpc;
		KeInitializeDpc(&dpc, [](struct _KDPC* /*dpc*/, PVOID context, PVOID /*arg1*/, PVOID /*arg2*/)
		{
			dispatch_callback(context);
		}, &callback_data);

		// 设置目标 CPU
		KeSetTargetProcessorDpc(&dpc, static_cast<CCHAR>(cpu_id));

		// 插入 DPC 并等待完成
		KeInsertQueueDpc(&dpc, nullptr, nullptr);

		// 等待 DPC 完成 (简单的忙等待)
		while (dpc.DpcData != nullptr) {
			LARGE_INTEGER interval;
			interval.QuadPart = -100; // 0.01ms
			KeDelayExecutionThread(KernelMode, FALSE, &interval);
		}
	}

	void dispatch_on_all_cores(void (*callback)(void*), void* data, const bool reverse_order)
	{
		const auto cpu_count = get_processor_count();
		
		if (reverse_order) {
			// 倒序：CPU N-1 -> N-2 -> ... -> 1 -> 0 (类似 barehypervisor 的 PLATFORM_REVERSE)
			for (uint32_t i = cpu_count; i > 0; --i) {
				const uint32_t cpu_id = i - 1;
				dispatch_on_specific_cpu(callback, data, cpu_id);
			}
		} else {
			// 正序：CPU 0 -> 1 -> 2 -> ... -> N-1 (类似 barehypervisor 的 PLATFORM_FORWARD)
			for (uint32_t cpu_id = 0; cpu_id < cpu_count; ++cpu_id) {
				dispatch_on_specific_cpu(callback, data, cpu_id);
			}
		}
	}
}
