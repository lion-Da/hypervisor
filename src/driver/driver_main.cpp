#include "std_include.hpp"
#include "logging.hpp"
#include "sleep_callback.hpp"
#include "irp.hpp"
#include "exception.hpp"
#include "hypervisor.hpp"
#include "globals.hpp"
#include "process.hpp"
#include "process_callback.hpp"

#define DOS_DEV_NAME L"\\DosDevices\\HyperHook"
#define DEV_NAME L"\\Device\\HyperHook"

class global_driver
{
public:
	global_driver(const PDRIVER_OBJECT driver_object)
		: sleep_callback_([this](const sleep_callback::type type)
		  {
			  this->sleep_notification(type);
		  }),
		  process_callback_(
			  [this](const process_id parent_id, const process_id process_id, const process_callback::type type)
			  {
				  this->process_notification(parent_id, process_id, type);
			  }),
		  irp_(driver_object, DEV_NAME, DOS_DEV_NAME)
	{
		debug_log("Driver started\n");
		
		// 延迟启用hypervisor，使用修复后的DPC插入机制
		if (!this->hypervisor_.try_enable_safely())
		{
			debug_log("Warning: Failed to enable hypervisor during driver initialization\n");
		}
	}

	~global_driver()
	{
		debug_log("Unloading driver\n");
	}

	global_driver(global_driver&&) noexcept = delete;
	global_driver& operator=(global_driver&&) noexcept = delete;

	global_driver(const global_driver&) = delete;
	global_driver& operator=(const global_driver&) = delete;

	void pre_destroy(const PDRIVER_OBJECT /*driver_object*/)
	{
	}

private:
	bool hypervisor_was_enabled_{false};
	hypervisor hypervisor_{};
	sleep_callback sleep_callback_{};
	process_callback::scoped_process_callback process_callback_{};
	irp irp_{};

	void sleep_notification(const sleep_callback::type type)
	{
		if (type == sleep_callback::type::sleep)
		{
			debug_log("Going to sleep...\n");
			this->hypervisor_was_enabled_ = this->hypervisor_.is_enabled();
			this->hypervisor_.disable();
		}

		if (type == sleep_callback::type::wakeup && this->hypervisor_was_enabled_)
		{
			debug_log("Waking up...\n");
			this->hypervisor_.enable();
		}
	}

	void process_notification(process_id /*parent_id*/, const process_id process_id, const process_callback::type type)
	{
		if (type == process_callback::type::destroy)
		{
			if (this->hypervisor_.cleanup_process(process_id))
			{
				const auto proc = process::find_process_by_id(process_id);
				if(proc)
				{
					debug_log("Handled termination of %s\n", proc.get_image_filename());
				}
			}
		}
	}
};

global_driver* global_driver_instance{nullptr};

_Function_class_(DRIVER_UNLOAD) void unload(const PDRIVER_OBJECT driver_object)
{
	try
	{
		if (global_driver_instance)
		{
			global_driver_instance->pre_destroy(driver_object);
			delete global_driver_instance;
			global_driver_instance = nullptr;
		}

		globals::run_destructors();
	}
	catch (std::exception& e)
	{
		debug_log("Destruction error occured: %s\n", e.what());
	}
	catch (...)
	{
		debug_log("Unknown destruction error occured. This should not happen!");
	}
}

extern "C" NTSTATUS DriverEntry(const PDRIVER_OBJECT driver_object, PUNICODE_STRING /*registry_path*/)
{
	try
	{
		driver_object->DriverUnload = unload;
		globals::run_constructors();
		global_driver_instance = new global_driver(driver_object);
	}
	catch (std::exception& e)
	{
		debug_log("Error: %s\n", e.what());
		return STATUS_INTERNAL_ERROR;
	}
	catch (...)
	{
		debug_log("Unknown initialization error occured");
		return STATUS_INTERNAL_ERROR;
	}

	return STATUS_SUCCESS;
}
