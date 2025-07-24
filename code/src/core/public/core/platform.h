#pragma once

#include <core/slice_types.h>
#include <core/string_types.h>
#include <core/memory_types.h>
#include <core/math_types.h>

typedef void JobQueueFunc(void* data);

struct JobDecl
{
	StringView8 name = PAW_STR("Unknown Job");
	void* data = nullptr;
	JobQueueFunc* func = nullptr;
};

void ScheduleJobs(Slice<JobDecl const>&& jobs);

Byte* PlatformReserveAddressSpace(PtrSize size_bytes);
void PlatformCommitAddressSpace(Byte* start_ptr, PtrSize size_bytes);
void PlatformDecommitAddressSpace(Byte* start_ptr, PtrSize size_bytes);

namespace Platform
{
	void Init(IAllocator* static_allocator, IAllocator* static_debug_allocator);
	bool PumpEvents();
	Int2 GetViewportSize();
	MemorySlice LoadFileBlocking(char const* path, IAllocator* allocator);
};

//
// void job_queue_start();
// void job_queue_deinit();
// void job_queue_schedule(Slice<JobQueueDecl const> const&& jobs);
// u32 job_queue_push_wait_job(PlatformWaitHandle const& wait_handle, JobQueueDecl const&& decl);
//// Returns true if switch from idle to load happened, returns false if triggered
// bool job_queue_load_wait_if_idle(u32 wait_index);
// void job_queue_set_wait_to_idle(u32 wait_index);
// usize job_queue_get_worker_count();
// u32 job_queue_get_worker_index();