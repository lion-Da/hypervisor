/**
 * @file assembly.hpp
 * @brief 汇编函数接口声明 - VMX和系统级操作的底层接口
 * 
 * 该文件声明了实现hypervisor所需的关键汇编函数。这些函数直接操作：
 * 1. CPU特权指令 - 段寄存器操作、页表管理等
 * 2. VMX指令 - INVEPT等虚拟化专用指令  
 * 3. 上下文切换 - CPU状态的精确保存和恢复
 * 4. 控制流转换 - VM entry/exit的关键路径
 * 
 * 所有函数都使用C调用约定，但内部使用汇编实现以：
 * - 精确控制CPU状态和寄存器操作
 * - 执行特权指令和系统级操作
 * - 实现无法用C语言表达的底层操作
 * - 确保关键代码路径的性能和正确性
 */

#pragma once

extern "C"
{
/**
 * @brief 存储局部描述符表寄存器(SLDT指令)
 * 
 * 读取当前CPU的LDTR(Local Descriptor Table Register)值。
 * 64位模式下LDTR通常为0，因为局部描述符表很少使用。
 * 
 * @param ldtr 输出参数，存储LDTR的值
 * 
 * @note 运行在任何IRQL级别
 * @note SLDT是非特权指令，但只在系统代码中使用
 */
void _sldt(uint16_t* ldtr);

/**
 * @brief 加载任务寄存器(LTR指令)
 * 
 * 设置CPU的TR(Task Register)，指向新的TSS段。
 * TR寄存器在任务切换和系统调用中起关键作用。
 * 
 * @param tr 要加载的任务段选择子
 * 
 * @note 需要CPL=0特权级别
 * @note 会影响系统调用和中断处理
 * @note 必须在禁用中断的情况下调用
 */
void _ltr(uint16_t tr);

/**
 * @brief 存储任务寄存器(STR指令)
 * 
 * 读取当前CPU的TR(Task Register)值。
 * 用于保存CPU状态以便后续恢复。
 * 
 * @param tr 输出参数，存储TR的值
 * 
 * @note 运行在任何IRQL级别
 * @note STR是非特权指令
 */
void _str(uint16_t* tr);

/**
 * @brief 加载全局描述符表寄存器(LGDT指令)
 * 
 * 设置CPU的GDTR(Global Descriptor Table Register)，
 * 指向新的GDT基址和长度。这是系统初始化的关键操作。
 * 
 * @param gdtr 指向GDTR结构的指针(包含base和limit)
 * 
 * @note 需要CPL=0特权级别
 * @note 会影响所有段寄存器的行为
 * @note 必须在禁用中断的情况下调用
 * @note 调用后需要重新加载段寄存器
 */
void __lgdt(void* gdtr);

/**
 * @brief 存储全局描述符表寄存器(SGDT指令)
 * 
 * 读取当前CPU的GDTR值，包含GDT的基址和长度。
 * 用于保存CPU状态以便VMX初始化和恢复。
 * 
 * @param gdtr 输出缓冲区，存储GDTR结构
 * 
 * @note 运行在任何IRQL级别
 * @note SGDT是非特权指令
 */
void _sgdt(void*);

/**
 * @brief 无效化EPT缓存(INVEPT指令)
 * 
 * 使CPU上的EPT(Extended Page Tables)缓存失效。
 * 当EPT页表结构发生变化时必须调用，确保CPU使用最新的页表映射。
 * 
 * @param type 失效类型：
 *             - 1: 单上下文失效(指定EPTP)
 *             - 2: 全局失效(所有EPTP)
 * @param descriptor 指向INVEPT描述符的指针，包含EPTP和保留字段
 * 
 * @note 只能在VMX根模式下调用
 * @note 需要CPU支持EPT功能
 * @note 失败时会设置RFLAGS.CF标志
 * 
 * INVEPT指令的重要性：
 * - EPT TLB缓存可能包含过时的页表映射
 * - 不正确的缓存失效会导致EPT hook失效
 * - 必须在所有相关CPU核心上执行
 */
void __invept(size_t type, invept_descriptor* descriptor);

/**
 * @brief VMX虚拟机启动入口点
 * 
 * 执行VMLAUNCH指令启动虚拟机执行。如果成功，控制权转移到guest；
 * 如果失败，函数返回到调用者。该函数是VMX虚拟化的关键入口点。
 * 
 * 启动序列：
 * 1. 保存当前CPU状态到栈
 * 2. 调用vm_launch_handler进行最后的准备
 * 3. 执行VMLAUNCH进入VMX非根模式
 * 4. 如果失败，通过异常处理恢复执行
 * 
 * @note 该函数从不正常返回(noreturn)
 * @note 只能在VMX根模式下调用，且VMCS已配置
 * @note 失败时通过异常机制返回到调用者
 * @note 成功时控制权转移到guest代码
 */
[[ noreturn ]] void vm_launch();

/**
 * @brief VMX虚拟机退出处理入口点
 * 
 * VM exit发生时的汇编入口点。负责：
 * 1. 保存guest的CPU状态到CONTEXT结构
 * 2. 调用C语言的exit处理程序
 * 3. 根据处理结果决定返回guest或终止VM
 * 
 * 处理流程：
 * 1. 使用RtlCaptureContext保存寄存器状态
 * 2. 修正栈指针以反映真实的guest状态  
 * 3. 调用vm_exit_handler进行高级处理
 * 4. 根据处理结果执行相应的恢复操作
 * 
 * @note 该函数从不正常返回(noreturn)
 * @note 只能从VMX硬件调用(VM exit时)
 * @note 必须精确保存和恢复guest状态
 * @note 性能关键路径，最小化开销
 */
[[ noreturn ]] void vm_exit();

/**
 * @brief 恢复CPU执行上下文并跳转
 * 
 * 从CONTEXT结构完全恢复CPU状态并跳转到指定地址。
 * 用于从hypervisor返回到guest执行，或实现精确的上下文切换。
 * 
 * 恢复序列：
 * 1. 恢复所有XMM浮点寄存器
 * 2. 恢复MXCSR浮点控制字
 * 3. 恢复所有通用寄存器  
 * 4. 恢复标志寄存器(RFLAGS)
 * 5. 恢复栈指针并跳转到目标地址
 * 
 * @param context 指向包含目标CPU状态的CONTEXT结构
 * 
 * @note 该函数从不返回(noreturn)
 * @note 必须精确恢复所有CPU状态
 * @note 用于实现hypervisor的透明性
 * @note 恢复后的执行就好像从未进入hypervisor
 */
[[ noreturn ]] void restore_context(CONTEXT* context);
}
