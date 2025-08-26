/**
 * @file msr_handler.hpp
 * @brief MSR (Model Specific Register) 处理模块头文件
 */

#pragma once
#include "std_include.hpp"
#include "vmx.hpp"

// MSR 枚举定义
enum class MSR : uint32_t {
    MsrSysenterCs = 0x174,
    MsrSysenterEsp = 0x175,
    MsrSysenterEip = 0x176,
    MsrDebugctl = 0x1D9,
    MsrPat = 0x277,
    MsrEfer = 0xC0000080,
    MsrStar = 0xC0000081,
    MsrLstar = 0xC0000082,
    MsrCstar = 0xC0000083,
    MsrFmask = 0xC0000084,
    MsrFsBase = 0xC0000100,
    MsrGsBase = 0xC0000101,
    MsrKernelGsBase = 0xC0000102
};

// VMCS 字段枚举定义
enum class VmcsField : uint32_t {
    GuestIa32SYSENTERCS = 0x0000482A,
    GuestIa32SYSENTERESP = 0x00006824,
    GuestIa32SYSENTEREIP = 0x00006826,
    GuestIa32DebugCtl = 0x00002802,
    GuestFsBase = 0x0000680E,
    GuestGsBase = 0x00006810
};

// Guest 寄存器结构体定义
struct GpRegisters {
	uint64_t r15;
	uint64_t r14;
	uint64_t r13;
	uint64_t r12;
	uint64_t r11;
	uint64_t r10;
	uint64_t r9;
	uint64_t r8;
	uint64_t di;
	uint64_t si;
	uint64_t bp;
	uint64_t sp;
	uint64_t bx;
	uint64_t dx;
	uint64_t cx;
	uint64_t ax;
    // 其他寄存器可根据需要添加
};

// MSR 处理函数声明
extern "C" {
    /**
     * @brief 初始化 MSR 处理器
     * @param msr_bitmap MSR bitmap 指针 (4KB 页面)
     */
    void initialize_msr_handler(uint8_t* msr_bitmap);

    /**
     * @brief 处理 MSR 相关的 VM exit
     * @param guest_context Guest 上下文
     * @param is_write true = MSR write, false = MSR read
     * @return true = 成功处理, false = 需要注入异常
     */
    bool handle_msr_access(vmx::guest_context& guest_context, bool is_write);

    /**
     * @brief MSR 读写处理函数（按照用户提供的正确方式）
     * @param pGuestRegisters Guest 寄存器指针
     * @param isRead true = MSR read, false = MSR write
     */
    void ReadWriteMsrHandle(GpRegisters* pGuestRegisters, bool isRead);
}

