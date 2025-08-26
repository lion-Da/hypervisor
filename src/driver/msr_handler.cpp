/**
 * @file msr_handler.cpp
 * @brief MSR (Model Specific Register) 处理模块
 * 
 * 处理 VMX 环境下的 MSR 访问，包括：
 * 1. MSR bitmap 配置
 * 2. MSR read/write VM exit 处理
 * 3. 安全的 MSR 白名单管理
 */

#include <ntddk.h>
#include "logging.hpp"
#include "vmx.hpp"
#include "msr_handler.hpp"

namespace msr_handler {

/**
 * @brief 配置MSR bitmap（基于TinyVT的简洁实现）
 * 简单地将MSR bitmap清零，允许大部分MSR直接访问
 */
void configure_msr_bitmap(uint8_t* msr_bitmap)
{
    if (!msr_bitmap) {
        // debug_log("Error: MSR bitmap pointer is NULL\n");
        return;
    }
    
    // debug_log("Configuring MSR bitmap (TinyVT style)\n");
    
    // 将整个MSR bitmap清零，允许所有MSR直接访问（不触发VM exit）
    // 使用简单的memset替代RtlZeroMemory以避免头文件依赖问题
    for (size_t i = 0; i < 4096; ++i) {
        msr_bitmap[i] = 0;
    }
    // debug_log("MSR bitmap configured: all MSRs allowed direct access\n");
}

} // namespace msr_handler

/**
 * @brief MSR 读写处理函数（正确的VMX实现）
 * 按照用户提供的正确代码实现，与TinyVT完全一致
 */
void ReadWriteMsrHandle(GpRegisters* pGuestRegisters, bool isRead)
{
    if (!pGuestRegisters) {
        // debug_log("Error: pGuestRegisters is NULL\n");
        return;
    }

    MSR msr = static_cast<MSR>(pGuestRegisters->cx);
    // debug_log("MSR %s: 0x%X\n", isRead ? "read" : "write", static_cast<uint32_t>(msr));

    bool transfer_to_vmcs = false;
    VmcsField vmcs_field = {};
    
    switch (msr) {
    case MSR::MsrSysenterCs:
        vmcs_field = VmcsField::GuestIa32SYSENTERCS;
        transfer_to_vmcs = true;
        break;
    case MSR::MsrSysenterEsp:
        vmcs_field = VmcsField::GuestIa32SYSENTERESP;
        transfer_to_vmcs = true;
        break;
    case MSR::MsrSysenterEip:
        vmcs_field = VmcsField::GuestIa32SYSENTEREIP;
        transfer_to_vmcs = true;
        break;
    case MSR::MsrDebugctl:
        vmcs_field = VmcsField::GuestIa32DebugCtl;
        transfer_to_vmcs = true;
        break;
    case MSR::MsrGsBase:
        vmcs_field = VmcsField::GuestGsBase;
        transfer_to_vmcs = true;
        break;
    case MSR::MsrFsBase:
        vmcs_field = VmcsField::GuestFsBase;
        transfer_to_vmcs = true;
        break;
    default:
        transfer_to_vmcs = false;
        break;
    }

    uint64_t msr_value = 0;
    if (isRead) {
        if (transfer_to_vmcs) {
            __vmx_vmread(static_cast<uint32_t>(vmcs_field), reinterpret_cast<uintptr_t*>(&msr_value));
            // debug_log("Read MSR 0x%X from VMCS field 0x%X: 0x%llX\n", 
            //          static_cast<uint32_t>(msr), static_cast<uint32_t>(vmcs_field), msr_value);
        } else {
            msr_value = __readmsr(static_cast<uint32_t>(msr));
            // debug_log("Read MSR 0x%X directly: 0x%llX\n", static_cast<uint32_t>(msr), msr_value);
        }

        pGuestRegisters->ax = msr_value & 0xFFFFFFFF;
        pGuestRegisters->dx = (msr_value >> 32) & 0xFFFFFFFF;
    } else {
        msr_value = (pGuestRegisters->dx << 32) | (pGuestRegisters->ax & 0xFFFFFFFF);
        
        if (transfer_to_vmcs) {
            __vmx_vmwrite(static_cast<uint32_t>(vmcs_field), static_cast<uintptr_t>(msr_value));
            // debug_log("Write MSR 0x%X to VMCS field 0x%X: 0x%llX\n", 
            //          static_cast<uint32_t>(msr), static_cast<uint32_t>(vmcs_field), msr_value);
        } else {
            __writemsr(static_cast<uint32_t>(msr), msr_value);
            // debug_log("Write MSR 0x%X directly: 0x%llX\n", static_cast<uint32_t>(msr), msr_value);
        }
    }
}


/**
 * @brief 处理 MSR read VM exit（更新为使用正确的处理函数）
 */
bool handle_msr_read(vmx::guest_context& guest_context)
{
    // debug_log("MSR read VM exit\n");
    
    GpRegisters gp_regs = {};
    gp_regs.cx = guest_context.vp_regs->Rcx;
    
    ::ReadWriteMsrHandle(&gp_regs, true);
    
    guest_context.vp_regs->Rax = gp_regs.ax;
    guest_context.vp_regs->Rdx = gp_regs.dx;
    
    return true;
}

/**
 * @brief 处理 MSR write VM exit（更新为使用正确的处理函数）
 */
bool handle_msr_write(vmx::guest_context& guest_context)
{
    // debug_log("MSR write VM exit\n");
    
    GpRegisters gp_regs = {};
    gp_regs.cx = guest_context.vp_regs->Rcx;
    gp_regs.ax = guest_context.vp_regs->Rax;
    gp_regs.dx = guest_context.vp_regs->Rdx;
    
    ::ReadWriteMsrHandle(&gp_regs, false);
    
    return true;
}

/**
 * @brief 初始化 MSR 处理器
 * 在 VMCS 设置期间调用
 */
extern "C" void initialize_msr_handler(uint8_t* msr_bitmap)
{
    if (msr_bitmap) {
        msr_handler::configure_msr_bitmap(msr_bitmap);
    }
}

/**
 * @brief 处理 MSR 相关的 VM exit
 * 在 VM exit 处理器中调用
 */
extern "C" bool handle_msr_access(vmx::guest_context& guest_context, bool is_write)
{
    if (is_write) {
        return handle_msr_write(guest_context);
    } else {
        return handle_msr_read(guest_context);
    }
}
