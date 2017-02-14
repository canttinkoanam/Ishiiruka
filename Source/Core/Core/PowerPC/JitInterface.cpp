// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <algorithm>
#include <cinttypes>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include "Common/PerformanceCounter.h"
#endif

#include "Core/Core.h"
#include "Core/PowerPC/CachedInterpreter/CachedInterpreter.h"
#include "Core/PowerPC/JitCommon/JitBase.h"
#include "Core/PowerPC/JitInterface.h"
#include "Core/PowerPC/PPCSymbolDB.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/PowerPC/Profiler.h"

#if _M_X86
#include "Core/PowerPC/Jit64/Jit.h"
#include "Core/PowerPC/Jit64/Jit64_Tables.h"
#include "Core/PowerPC/Jit64IL/JitIL.h"
#include "Core/PowerPC/Jit64IL/JitIL_Tables.h"
#endif

#if _M_ARM_64
#include "Core/PowerPC/JitArm64/Jit.h"
#include "Core/PowerPC/JitArm64/JitArm64_Tables.h"
#endif

namespace JitInterface
{
void DoState(PointerWrap& p)
{
	if (g_jit && p.GetMode() == PointerWrap::MODE_READ)
		g_jit->ClearCache();
}
CPUCoreBase* InitJitCore(int core)
{
	CPUCoreBase* ptr = nullptr;
	switch (core)
	{
#if _M_X86
	case PowerPC::CORE_JIT64:
		ptr = new Jit64();
		break;
	case PowerPC::CORE_JITIL64:
		ptr = new JitIL();
		break;
#endif
#if _M_ARM_64
	case PowerPC::CORE_JITARM64:
		ptr = new JitArm64();
		break;
#endif
	case PowerPC::CORE_CACHEDINTERPRETER:
		ptr = new CachedInterpreter();
		break;

	default:
		PanicAlert("Unrecognizable cpu_core: %d", core);
		g_jit = nullptr;
		return nullptr;
	}
	g_jit = static_cast<JitBase*>(ptr);
	g_jit->Init();
	return ptr;
}
void InitTables(int core)
{
	switch (core)
	{
#if _M_X86
	case PowerPC::CORE_JIT64:
		Jit64Tables::InitTables();
		break;
	case PowerPC::CORE_JITIL64:
		JitILTables::InitTables();
		break;
#endif
#if _M_ARM_64
	case PowerPC::CORE_JITARM64:
		JitArm64Tables::InitTables();
		break;
#endif
	case PowerPC::CORE_CACHEDINTERPRETER:
		// has no tables
		break;
	default:
		PanicAlert("Unrecognizable cpu_core: %d", core);
		break;
	}
}
CPUCoreBase* GetCore()
{
	return g_jit;
}

void WriteProfileResults(const std::string& filename)
{
	ProfileStats prof_stats;
	GetProfileResults(&prof_stats);

	File::IOFile f(filename, "w");
	if (!f)
	{
		PanicAlert("Failed to open %s", filename.c_str());
		return;
	}
	fprintf(f.GetHandle(), "origAddr\tblkName\trunCount\tcost\ttimeCost\tpercent\ttimePercent\tOvAlli"
		"nBlkTime(ms)\tblkCodeSize\n");
	for (auto& stat : prof_stats.block_stats)
	{
		std::string name = g_symbolDB.GetDescription(stat.addr);
		double percent = 100.0 * (double)stat.cost / (double)prof_stats.cost_sum;
		double timePercent = 100.0 * (double)stat.tick_counter / (double)prof_stats.timecost_sum;
		fprintf(f.GetHandle(),
			"%08x\t%s\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%.2f\t%.2f\t%.2f\t%i\n", stat.addr,
			name.c_str(), stat.run_count, stat.cost, stat.tick_counter, percent, timePercent,
			(double)stat.tick_counter * 1000.0 / (double)prof_stats.countsPerSec, stat.block_size);
	}
}

void GetProfileResults(ProfileStats* prof_stats)
{
	// Can't really do this with no g_jit core available
	if (!g_jit)
		return;

	prof_stats->cost_sum = 0;
	prof_stats->timecost_sum = 0;
	prof_stats->block_stats.clear();

	Core::State old_state = Core::GetState();
	if (old_state == Core::State::Running)
		Core::SetState(Core::State::Paused);

	QueryPerformanceFrequency((LARGE_INTEGER*)&prof_stats->countsPerSec);
	g_jit->GetBlockCache()->RunOnBlocks([&prof_stats](const JitBlock& block) {
		// Rough heuristic.  Mem instructions should cost more.
		u64 cost = block.originalSize * (block.runCount / 4);
		u64 timecost = block.ticCounter;
		// Todo: tweak.
		if (block.runCount >= 1)
			prof_stats->block_stats.emplace_back(block.effectiveAddress, cost, timecost, block.runCount,
				block.codeSize);
		prof_stats->cost_sum += cost;
		prof_stats->timecost_sum += timecost;
	});

	sort(prof_stats->block_stats.begin(), prof_stats->block_stats.end());
	if (old_state == Core::State::Running)
		Core::SetState(Core::State::Running);
}

int GetHostCode(u32* address, const u8** code, u32* code_size)
{
	if (!g_jit)
	{
		*code_size = 0;
		return 1;
	}

	JitBlock* block = g_jit->GetBlockCache()->GetBlockFromStartAddress(*address, MSR);
	if (!block)
	{
		for (int i = 0; i < 500; i++)
		{
			block = g_jit->GetBlockCache()->GetBlockFromStartAddress(*address - 4 * i, MSR);
			if (block)
				break;
		}

		if (block)
		{
			if (!(block->effectiveAddress <= *address &&
				block->originalSize + block->effectiveAddress >= *address))
				block = nullptr;
		}

		// Do not merge this "if" with the above - block_num changes inside it.
		if (!block)
		{
			*code_size = 0;
			return 2;
		}
	}

	*code = block->checkedEntry;
	*code_size = block->codeSize;
	*address = block->effectiveAddress;
	return 0;
}

bool HandleFault(uintptr_t access_address, SContext* ctx)
{
	// Prevent nullptr dereference on a crash with no JIT present
	if (!g_jit)
	{
		return false;
	}

	return g_jit->HandleFault(access_address, ctx);
}

bool HandleStackFault()
{
	if (!g_jit)
	{
		return false;
	}

	return g_jit->HandleStackFault();
}

void ClearCache()
{
	if (g_jit)
		g_jit->ClearCache();
}
void ClearSafe()
{
	// This clear is "safe" in the sense that it's okay to run from
	// inside a JIT'ed block: it clears the instruction cache, but not
	// the JIT'ed code.
	// TODO: There's probably a better way to handle this situation.
	if (g_jit)
		g_jit->GetBlockCache()->Clear();
}

void InvalidateICache(u32 address, u32 size, bool forced)
{
	if (g_jit)
		g_jit->GetBlockCache()->InvalidateICache(address, size, forced);
}

void CompileExceptionCheck(ExceptionType type)
{
	if (!g_jit)
		return;

	std::unordered_set<u32>* exception_addresses = nullptr;

	switch (type)
	{
	case ExceptionType::EXCEPTIONS_FIFO_WRITE:
		exception_addresses = &g_jit->js.fifoWriteAddresses;
		break;
	case ExceptionType::EXCEPTIONS_PAIRED_QUANTIZE:
		exception_addresses = &g_jit->js.pairedQuantizeAddresses;
		break;
	case ExceptionType::EXCEPTIONS_SPECULATIVE_CONSTANTS:
		exception_addresses = &g_jit->js.noSpeculativeConstantsAddresses;
		break;
	}

	if (PC != 0 && (exception_addresses->find(PC)) == (exception_addresses->end()))
	{
		if (type == ExceptionType::EXCEPTIONS_FIFO_WRITE)
		{
			// Check in case the code has been replaced since: do we need to do this?
			int optype = GetOpInfo(PowerPC::HostRead_U32(PC))->type;
			if (optype != OPTYPE_STORE && optype != OPTYPE_STOREFP && (optype != OPTYPE_STOREPS))
				return;
		}
		exception_addresses->insert(PC);

		// Invalidate the JIT block so that it gets recompiled with the external exception check
		// included.
		g_jit->GetBlockCache()->InvalidateICache(PC, 4, true);
	}
}

void Shutdown()
{
	if (g_jit)
	{
		g_jit->Shutdown();
		delete g_jit;
		g_jit = nullptr;
	}
}
}
