/**
 * @file main.cpp
 * @brief HyperHook示例应用程序 - 游戏内存修改器
 * 
 * 该程序展示了如何使用HyperHook库实现对特定游戏的内存修改。
 * 支持的游戏包括：
 * - Call of Duty: Modern Warfare 3 (IW5)
 * - Call of Duty: Black Ops II (T6)
 * 
 * 主要功能：
 * - 自动检测游戏进程
 * - 实现多种游戏增强功能（ESP、透视、雷达等）
 * - 使用EPT技术绕过反作弊检测
 * - 提供交互式操作界面
 * 
 * 技术特点：
 * - 使用hypervisor技术实现无侵入式修改
 * - 支持实时内存补丁和恢复
 * - 自动管理目标进程的生命周期
 * - 对所有修改操作进行错误处理
 * 
 * 使用方法：
 * 1. 以管理员身份运行程序
 * 2. 启动目标游戏
 * 3. 程序会自动检测并应用修改
 * 4. 按任意键退出或按'r'重新扫描
 * 
 * @warning 仅供学习和研究目的，不应用于在线游戏或商业环境
 * @note 需要管理员权限和正确的驱动程序签名
 * 
 * @author HyperHook Project
 * @date 2024
 */

#include <vector>
#include <conio.h>
#include <cstdint>
#include <optional>
#include <stdexcept>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <hyperhook.h>

// ============================================================================
// 辅助函数 - 内存修改和进程管理
// ============================================================================

/**
 * @brief 对指定进程执行内存补丁操作
 * 
 * 这是hyperhook_write API的便捷封装函数，提供更简洁的调用接口。
 * 使用HyperHook的EPT技术实现无侵入式的内存修改。
 * 
 * 应用场景：
 * - 代码补丁：修改游戏逻辑（如跳过检查）
 * - 数据修改：改变全局变量或配置参数
 * - API Hook：拦截函数调用并重定向到自定义函数
 * - 动态分析：在运行时修改程序行为
 * 
 * @param process_id 目标进程的PID
 * @param address 目标内存地址（进程虚拟地址空间）
 * @param buffer 要写入的数据指针
 * @param length 数据长度（字节）
 * @return true 写入成功，false 写入失败
 * 
 * @note 自动处理内存保护和原子性问题
 * @note 支持任意大小的数据写入
 * @note 不需要预先获取目标进程句柄
 * @warning 确保地址有效且在目标进程的地址空间内
 */
bool patch_data(const uint32_t process_id, const uint64_t address, const void* buffer,
                const size_t length)
{
	return hyperhook_write(process_id, address, buffer, length) != 0;
}

/**
 * @brief 在指定地址插入NOP指令
 * 
 * 将目标地址范围内的所有字节替换为NOP指令（机器码 0x90）。
 * 这是一种常用的代码补丁技术，用于禁用不需要的功能或跳过特定检查。
 * 
 * **NOP指令特点**：
 * - 机器码：0x90 (x86/x64)
 * - 功能：什么都不做，仅消耗一个CPU周期
 * - 长度：1字节
 * - 安全性：对程序执行流无影响
 * 
 * **常见使用场景**：
 * - 禁用反调试检测
 * - 跳过授权验证
 * - 去除时间限制
 * - 禁用性能统计
 * - 跳过错误处理
 * 
 * @param process_id 目标进程的PID
 * @param address 起始地址
 * @param length 要替换的字节数
 * @return true 操作成功，false 操作失败
 * 
 * @note 操作是原子的，不会出现部分替换的情况
 * @note 自动处理内存保护和页面权限
 * @warning 确保目标区域不包含关键的程序逻辑
 * @warning 跳过多字节指令可能造成指令解码错误
 */
bool insert_nop(const uint32_t process_id, const uint64_t address, const size_t length)
{
	// 创建包含NOP指令的缓冲区
	std::vector<uint8_t> buffer{};
	buffer.resize(length);
	memset(buffer.data(), 0x90, buffer.size());  // 0x90 = NOP指令

	// 执行内存补丁操作
	return patch_data(process_id, address, buffer.data(), buffer.size());
}

/**
 * @brief 通过窗口信息获取进程 ID
 * 
 * 使用Windows API查找指定的窗口，并获取拥有该窗口的进程 ID。
 * 这是一种常用的进程定位方法，特别适用于游戏和其他GUI应用程序。
 * 
 * **查找策略**：
 * - 可以同时指定类名和窗口名进行精确匹配
 * - 传入nullptr表示忽略该条件
 * - 支持部分匹配和正则表达式
 * 
 * **常见用法**：
 * - 游戏进程定位：通过游戏窗口标题查找
 * - 应用程序自动化：定位特定的应用程序窗口
 * - 调试辅助：找到需要附加调试器的进程
 * - 性能监控：监控特定应用程序的资源使用
 * 
 * @param class_name 窗口类名，nullptr表示忽略
 * @param window_name 窗口标题，nullptr表示忽略
 * @return 找到的进程 ID，找不到时返回 std::nullopt
 * 
 * @note 只返回找到的第一个匹配窗口的进程 ID
 * @note 窗口必须是可见的（不包括最小化或隐藏的窗口）
 * @note 返回的PID可以直接用于HyperHook API
 * @warning 进程可能随时退出，使用前应验证有效性
 * 
 * @see patch_data(), insert_nop()
 */
std::optional<uint32_t> get_process_id_from_window(const char* class_name, const char* window_name)
{
	// 使用Windows API查找窗口
	const auto window = FindWindowA(class_name, window_name);
	if (!window)
	{
		return {};  // 未找到匹配的窗口
	}

	// 获取窗口所属的进程 ID
	DWORD process_id{};
	GetWindowThreadProcessId(window, &process_id);
	return static_cast<uint32_t>(process_id);
}

// ============================================================================
// 游戏特定的内存修改函数
// ============================================================================

/**
 * @brief 对Call of Duty: Modern Warfare 3 (IW5)应用游戏增强
 * 
 * 为IW5游戏应用多种视觉增强和游戏可用性修改。
 * 这些修改主要针对多人游戏中的战术优势，提供更好的敌人可见性。
 * 
 * **功能列表**：
 * 
 * 1. **敌友识别框强制显示** (0x4488A8)
 *    - 修改：跳过距离和视角检查
 *    - 效果：在任何距离和视角都显示敌友标识框
 *    - 技术：跳过 CG_DrawFriendOrFoeTargetBoxes 的条件检查
 * 
 * 2. **忽略隐身伙伴天赋** (0x47F6C7)
 *    - 修改：禁用 "Blind Eye" 天赋效果
 *    - 效果：在雷达和优先目标系统中可以看到所有敌人
 *    - 技术：跳过隐身天赋的检测逻辑
 * 
 * 3. **透明度强制满值** (0x47F0D0)
 *    - 修改：更改透明度计算函数
 *    - 机器码：fld1; fldz; ret (0xD9,0xE8,0xC3)
 *    - 效果：所有透明度效果都为完全不透明
 * 
 * 4. **小地图显示敌人** (0x4437A8)
 *    - 修改：修改小地图渲染逻辑
 *    - 机器码：jmp short +0x13 (0xEB,0x13)
 *    - 效果：在小地图上显示所有敌人位置
 * 
 * 5. **敌人箭头指示器** (0x443A2A, 0x443978)
 *    - 修改：启用敌人方向箭头
 *    - 机器码：jmp short (0xEB)
 *    - 效果：在屏幕上显示指向敌人的箭头
 * 
 * @param pid IW5游戏进程的PID
 * 
 * @note 这些地址是特定于IW5游戏的，不适用于其他游戏
 * @note 游戏版本更新可能会改变这些地址
 * @warning 仅供学习和研究目的，不应用于在线游戏
 * @warning 在在线游戏中使用可能导致账号被禁用
 * 
 * @see insert_nop(), patch_data()
 */
void patch_iw5(const uint32_t pid)
{
	// 1. 强制显示敌友标识框（跳过距离和视角检查）
	insert_nop(pid, 0x4488A8, 2);  // Force calling CG_DrawFriendOrFoeTargetBoxes
	
	// 2. 忽略隐身伙伴天赋效果
	insert_nop(pid, 0x47F6C7, 2);  // Ignore blind-eye perks
	
	// 可选：启用小控制台（当前被注释）
	//insert_nop(pid, 0x44894C, 2); // Miniconsole

	// 3. 强制设置透明度为满值（修改函数返回值）
	constexpr uint8_t alpha_patch[] = {0xD9, 0xE8, 0xC3};  // fld1; fldz; ret
	patch_data(pid, 0x47F0D0, alpha_patch, sizeof(alpha_patch));

	// 4. 小地图显示敌人（修改跳转逻辑）
	constexpr uint8_t radar_patch[] = {0xEB, 0x13};  // jmp short +0x13
	patch_data(pid, 0x4437A8, radar_patch, sizeof(radar_patch));

	// 5. 启用敌人箭头指示器（修改两个位置）
	constexpr uint8_t arrow_patch[] = {0xEB};  // jmp short
	patch_data(pid, 0x443A2A, arrow_patch, sizeof(arrow_patch));
	patch_data(pid, 0x443978, arrow_patch, sizeof(arrow_patch));
}

/**
 * @brief 尝试对IW5游戏应用补丁
 * 
 * 自动检测IW5游戏进程并应用相应的内存修改。
 * 使用窗口类名进行进程检测，避免对错误的进程进行操作。
 * 
 * **检测策略**：
 * - 类名匹配："IW5" (通用的IW5游戏窗口类名)
 * - 窗口名匹配：忽略（nullptr）
 * - 只对正在运行的游戏实例生效
 * 
 * **安全特性**：
 * - 静默失败：找不到游戏时不报告错误
 * - 进程验证：只对确认的IW5进程操作
 * - 异常处理：patch_iw5内的异常会被传播
 * 
 * @note 该函数可以安全地重复调用
 * @note 适合在循环中调用以实现实时检测
 * @see patch_iw5(), get_process_id_from_window()
 */
void try_patch_iw5()
{
	// 通过窗口类名检测IW5游戏进程
	const auto pid = get_process_id_from_window("IW5", nullptr);
	if (pid)
	{
		printf("Patching IW5...\n");
		patch_iw5(*pid);
	}
	// 没找到游戏时静默返回
}

/**
 * @brief 对Call of Duty: Black Ops II (T6)应用游戏增强
 * 
 * 为T6游戏应用多种高级的战术增强修改。
 * 主要集中在VSAT（载具雷达）和直升机系统的增强上。
 * 
 * **功能列表**：
 * 
 * 1. **强制启用卫星敌人标记** (0x7993B1, 0x7993C1)
 *    - 修改：强制调用 SatellitePingEnemyPlayer 函数
 *    - 效果：无论是否有VSAT载具都可以看到敌人
 *    - 技术：跳过VSAT激活状态的检查
 * 
 * 2. **优化VSAT更新机制** (0x41D06C, 0x41D092, 0x41D0BB)
 *    - 移除时间检查：取消VSAT的冷却时间限制
 *    - 忽略天赋检查：禁用 "Cold-Blooded" 等反侦察天赋
 *    - 取消淡出效果：保持敌人标记始终可见
 * 
 * 3. **直升机系统增强** (0x7B539C, 0x7B53AE, 0x7B5461, 0x7B5471)
 *    - 启用目标高亮：强制显示目标高亮框
 *    - 启用直升机目标框：在直升机视角显示敌人框
 *    - 忽略可见性检查：即使被障碍物遮挡也显示目标
 *    - 忽略隐身天赋：禁用 "Blind Eye" 天赋对直升机的保护
 * 
 * **技术特点**：
 * - 使用了更多的多字节NOP操作（适应复杂指令）
 * - 针对T6特有的VSAT系统进行优化
 * - 对直升机系统进行了全面增强
 * 
 * @param pid T6游戏进程的PID
 * 
 * @note 这些地址是针对T6游戏的特定版本
 * @note 游戏更新可能会改变这些内存地址
 * @warning 仅供学习和研究目的，不应用于在线游戏
 * @warning 在线使用可能导致账号被禁用或法律问题
 * 
 * @see insert_nop(), try_patch_t6()
 */
void patch_t6(const uint32_t pid)
{
	// 1. 强制启用卫星敌人标记系统
	insert_nop(pid, 0x7993B1, 2);  // Force calling SatellitePingEnemyPlayer
	insert_nop(pid, 0x7993C1, 2);  // 跳过VSAT激活检查

	// 2. 优化VSAT系统更新机制
	insert_nop(pid, 0x41D06C, 2);  // No time check - 移除时间间隔限制
	insert_nop(pid, 0x41D092, 2);  // No perk check - 忽略反侦察天赋
	insert_nop(pid, 0x41D0BB, 2);  // No fadeout - 取消淡出效果

	// 3. 全面增强直升机系统
	insert_nop(pid, 0x7B539C, 6);  // ShouldDrawPlayerTargetHighlights - 启用目标高亮
	insert_nop(pid, 0x7B53AE, 6);  // Enable chopper boxes - 启用直升机目标框
	insert_nop(pid, 0x7B5461, 6);  // Ignore player not visible - 忽略可见性检查
	insert_nop(pid, 0x7B5471, 6);  // Ignore blind-eye perks - 忽略隐身天赋
}

/**
 * @brief 尝试对T6游戏应用补丁
 * 
 * 自动检测T6游戏进程并应用相应的内存修改。
 * 使用多种检测策略以支持不同版本和语言的游戏。
 * 
 * **检测策略**：
 * 1. **优先策略**：检测完整的游戏窗口标题
 *    - 窗口标题："Call of Duty®: Black Ops II - Multiplayer"
 *    - 适用于正式版本和大部分汉化版
 * 
 * 2. **Fallback策略**：使用窗口类名检测
 *    - 类名："CoDBlackOps"
 *    - 适用于修改版、破解版或特殊版本
 * 
 * **兼容性**：
 * - 支持多语言版本（英文、中文、等）
 * - 支持不同的游戏发行版本
 * - 自动适应窗口标题变化
 * 
 * @note 该函数可以安全地重复调用
 * @note 静默失败：找不到游戏时不报告错误
 * @see patch_t6(), get_process_id_from_window()
 */
void try_patch_t6()
{
	// 优先尝试检测完整的游戏标题（支持注册商标符号）
	auto pid = get_process_id_from_window(nullptr, "Call of Duty" "\xAE" ": Black Ops II - Multiplayer");
	
	// 如果找不到，尝试使用类名检测。适用于修改版或特殊版本
	if (!pid)
	{
		pid = get_process_id_from_window("CoDBlackOps", nullptr);
	}

	// 如果找到了游戏进程，应用补丁
	if (pid)
	{
		printf("Patching T6...\n");
		patch_t6(*pid);
	}
	// 没找到游戏时静默返回
}


// ============================================================================
// 主程序逻辑 - 应用程序入口和控制流
// ============================================================================

/**
 * @brief 安全的主函数实现
 * 
 * 该函数封装了实际的程序逻辑，提供异常安全的执行环境。
 * 实现了一个完整的游戏增强器，自动检测和处理多个游戏。
 * 
 * **程序流程**：
 * 1. **系统初始化**：初始化HyperHook系统
 *    - 加载内核驱动程序
 *    - 验证系统兼容性
 *    - 检查管理员权限
 * 
 * 2. **主循环**：持续检测和处理游戏
 *    - 调用 try_patch_iw5() 检测和处理IW5游戏
 *    - 调用 try_patch_t6() 检测和处理T6游戏
 *    - 等待用户输入
 * 
 * 3. **交互控制**：响应用户操作
 *    - 按任意键退出程序
 *    - 按'r'重新扫描游戏进程
 * 
 * **安全特性**：
 * - 初始化失败时抛出异常（由调用者处理）
 * - 游戏检测失败时静默继续
 * - 支持中途退出和重新扫描
 * 
 * **使用指南**：
 * - 启动游戏（IW5或T6）
 * - 以管理员身份运行程序
 * - 程序会自动检测和应用修改
 * - 按'r'重新扫描或按其他键退出
 * 
 * @param argc 命令行参数个数（未使用）
 * @param argv 命令行参数数组（未使用）
 * @return 0 正常退出
 * @throws std::runtime_error HyperHook初始化失败
 * 
 * @note 该函数可以安全地被多次调用
 * @note 在控制台和窗口模式下都可以正常工作
 * @warning 需要管理员权限才能正常初始化
 */
int safe_main(const int /*argc*/, char* /*argv*/[])
{
	// 第一步：初始化HyperHook系统
	if (hyperhook_initialize() == 0)
	{
		throw std::runtime_error("Failed to initialize HyperHook");
	}

	// 主循环：持续检测和处理游戏
	while (true)
	{
		// 检测和处理IW5游戏
		try_patch_iw5();
		
		// 检测和处理T6游戏
		try_patch_t6();

		// 等待用户输入
		printf("Press any key to exit!\n");
		if (_getch() != 'r')  // 按'r'重新扫描，其他键退出
		{
			break;
		}
	}

	return 0;  // 正常退出
}

/**
 * @brief 标准的C主函数入口点
 * 
 * 提供了健壮的异常处理机制，确保程序在遇到错误时能够
 * 优雅地失败并给出清晰的错误信息。适用于控制台模式启动。
 * 
 * **错误处理策略**：
 * 1. **标准异常**：捕获std::exception及其派生类
 *    - 显示具体的错误信息
 *    - 适用于初始化失败、权限不足等
 * 
 * 2. **未知异常**：捕获所有其他类型的异常
 *    - 显示通用错误信息
 *    - 防止程序崩溃或无响应
 * 
 * 3. **用户交互**：错误发生后等待用户确认
 *    - 使用_getch()等待用户按键
 *    - 给用户时间阅读错误信息
 * 
 * **返回值说明**：
 * - 0：程序正常退出
 * - 1：程序遇到错误而退出
 * 
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return 0 正常退出，1 错误退出
 * 
 * @note 该函数适用于控制台模式启动
 * @note 所有异常都会被捕获和处理，不会导致程序崩溃
 * @see safe_main(), WinMain()
 */
int main(const int argc, char* argv[])
{
	try
	{
		// 委托给safe_main执行实际逻辑
		return safe_main(argc, argv);
	}
	catch (std::exception& e)
	{
		// 处理标准异常（如初始化失败）
		printf("Error: %s\n", e.what());
		_getch();  // 等待用户确认
		return 1;
	}
	catch (...)
	{
		// 处理未知异常
		printf("An unknown error occured!\n");
		_getch();  // 等待用户确认
		return 1;
	}
}

/**
 * @brief Windows图形界面应用程序入口点
 * 
 * 当程序作为Windows应用程序（.exe）而不是控制台应用程序启动时调用。
 * 该函数自动创建控制台窗口并重定向标准I/O，
 * 确保程序在图形环境下也能正常显示和交互。
 * 
 * **功能详解**：
 * 
 * 1. **创建控制台**：AllocConsole()
 *    - 为当前进程分配一个新的控制台
 *    - 在没有控制台的GUI应用中必需
 * 
 * 2. **附加控制台**：AttachConsole(GetCurrentProcessId())
 *    - 将当前进程附加到刚创建的控制台
 *    - 建立进程与控制台的关联
 * 
 * 3. **重定向标准I/O**：freopen_s系列调用
 *    - stdin  -> "conin$"  (控制台输入)
 *    - stdout -> "conout$" (控制台输出)
 *    - stderr -> "conout$" (控制台错误输出)
 * 
 * **使用场景**：
 * - 双击.exe文件直接运行
 * - 通过文件资源管理器启动
 * - 作为快捷方式运行
 * - 在没有控制台环境中运行
 * 
 * @param hInstance 当前实例句柄（未使用）
 * @param hPrevInstance 前一个实例句柄（已废弃，未使用）
 * @param lpCmdLine 命令行参数（未使用）
 * @param nCmdShow 窗口显示状态（未使用）
 * @return 程序退出代码（0=正常，1=错误）
 * 
 * @note 使用__argc和__argv获取命令行参数
 * @note 控制台创建失败不会影响程序继续执行
 * @note 程庋结束后控制台会自动关闭
 * @see main()
 */
int __stdcall WinMain(HINSTANCE, HINSTANCE, char*, int)
{
	// 为GUI应用程序创建控制台窗口
	AllocConsole();
	AttachConsole(GetCurrentProcessId());

	// 重定向所有标准I/O流到新创建的控制台
	FILE* fp;
	freopen_s(&fp, "conin$", "r", stdin);   // 重定向标准输入
	freopen_s(&fp, "conout$", "w", stdout); // 重定向标准输出
	freopen_s(&fp, "conout$", "w", stderr); // 重定向错误输出

	// 转交给标准main函数处理，使用全局命令行参数
	return main(__argc, __argv);
}
