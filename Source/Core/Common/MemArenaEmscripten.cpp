// Copyright 2026 Dolphin Emulator Project / Slippi
// SPDX-License-Identifier: GPL-2.0-or-later

// wasm32 has one flat linear memory: no shm, no aliased mappings, no page
// protection. The arena is a single heap allocation; every "view" is just a
// pointer into it, so views of the same offset alias exactly as the multi-map
// platforms guarantee. ReserveMemoryRegion always fails, which keeps Dolphin
// on its slowmem (non-fastmem) path — the fastmem arena is only ever requested
// by the x86-64/AArch64 JITs, which don't exist in this build.

#include "Common/MemArena.h"

#include <cstdlib>
#include <cstring>

#include "Common/Assert.h"
#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"

namespace Common
{
MemArena::MemArena() = default;

MemArena::~MemArena()
{
  ReleaseSHMSegment();
}

namespace
{
// Native backends hand out mmap/VirtualAlloc memory, which is always
// page-aligned; SIMD paths and (later) JIT-emitted code assume that guarantee
// holds for guest RAM too. Plain calloc only promises malloc alignment
// (8-16 bytes), so match the other backends explicitly instead of relying on
// the allocator's default.
constexpr size_t ARENA_PAGE_SIZE = 4096;
size_t RoundUpToPage(size_t size)
{
  return (size + ARENA_PAGE_SIZE - 1) & ~(ARENA_PAGE_SIZE - 1);
}
}  // namespace

void MemArena::GrabSHMSegment(size_t size, std::string_view base_name)
{
  const size_t aligned_size = RoundUpToPage(size);
  m_backing = static_cast<u8*>(std::aligned_alloc(ARENA_PAGE_SIZE, aligned_size));
  if (!m_backing)
  {
    ERROR_LOG_FMT(MEMMAP, "MemArenaEmscripten: failed to allocate {} bytes", size);
    return;
  }
  std::memset(m_backing, 0, aligned_size);
  m_backing_size = size;
}

void MemArena::ReleaseSHMSegment()
{
  std::free(m_backing);
  m_backing = nullptr;
  m_backing_size = 0;
}

void* MemArena::CreateView(s64 offset, size_t size)
{
  if (!m_backing || static_cast<size_t>(offset) + size > m_backing_size)
  {
    ERROR_LOG_FMT(MEMMAP, "MemArenaEmscripten: bad view request offset {:#x} size {:#x}", offset,
                  size);
    return nullptr;
  }
  return m_backing + offset;
}

void MemArena::ReleaseView(void* view, size_t size)
{
  // Views are borrowed pointers into m_backing; nothing to do.
}

u8* MemArena::ReserveMemoryRegion(size_t memory_size)
{
  // No virtual-memory tricks on wasm: report failure so callers use slowmem.
  return nullptr;
}

void MemArena::ReleaseMemoryRegion()
{
}

void* MemArena::MapInMemoryRegion(s64 offset, size_t size, void* base)
{
  // Only reachable if ReserveMemoryRegion succeeded — it never does here.
  ASSERT_MSG(MEMMAP, false, "MapInMemoryRegion is unsupported on Emscripten");
  return nullptr;
}

void MemArena::UnmapFromMemoryRegion(void* view, size_t size)
{
}

LazyMemoryRegion::LazyMemoryRegion() = default;

LazyMemoryRegion::~LazyMemoryRegion()
{
  Release();
}

void* LazyMemoryRegion::Create(size_t size)
{
  // The only current user (JitBaseBlockCache's fast block map) asks for a
  // 64 GiB lazily-committed region, which cannot exist in a 4 GiB linear
  // memory. Failing here routes it onto its fallback lookup table.
  return nullptr;
}

void LazyMemoryRegion::Clear()
{
}

void LazyMemoryRegion::Release()
{
}
}  // namespace Common
