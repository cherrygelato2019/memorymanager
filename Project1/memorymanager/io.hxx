#pragma once

#include "syscalls.hxx"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace memorymanager {

inline constexpr std::size_t k_page = 4096;
inline constexpr std::size_t k_max_transfer = 16 * 1024 * 1024;
inline constexpr std::size_t k_buffer_align = 64;

struct region_cache {
    std::uintptr_t base{};
    std::uintptr_t end{};
    std::size_t gran{k_page};
    bool large_mapping{};
};

class aligned_arena {
public:
    aligned_arena() = default;
    ~aligned_arena();

    aligned_arena(aligned_arena const&) = delete;
    aligned_arena& operator=(aligned_arena const&) = delete;
    aligned_arena(aligned_arena&& other) noexcept;
    aligned_arena& operator=(aligned_arena&& other) noexcept;

    auto alloc(std::size_t size, bool large_pages) -> bool;
    auto data() -> void* { return ptr_; }
    auto data() const -> void const* { return ptr_; }
    auto size() const -> std::size_t { return size_; }

private:
    void* ptr_{};
    void* raw_{};
    std::size_t size_{};
    bool large_{};
};

class buffer_pool {
public:
    auto init(std::size_t block_bytes, std::size_t blocks, bool large_pages) -> bool;
    auto acquire() -> void*;
    auto block_size() const -> std::size_t { return block_bytes_; }

private:
    std::vector<aligned_arena> arenas_{};
    std::size_t block_bytes_{};
    std::atomic<std::size_t> slot_{0};
};

namespace transfer {

auto init_pool(std::size_t block_bytes, std::size_t blocks, bool large_pages) -> bool;
auto acquire_buffer() -> void*;
auto pool_block_size() -> std::size_t;
auto pretouch(void* buffer, std::size_t bytes) -> void;

}

auto enable_large_page_privilege() -> bool;
auto pin_current_thread(std::uint32_t core_index) -> void;

auto region_cache_clear(region_cache& cache) -> void;

auto transfer_step(HANDLE process, region_cache& cache, void const* cursor, std::size_t left) -> std::size_t;

auto nt_ok(NTSTATUS status) -> bool;

auto copy_local(void* dst, void const* src, std::size_t bytes) -> void;

auto stream_copy(void* dst, void const* src, std::size_t bytes) -> void;

auto rpm_nt(HANDLE process, region_cache& cache, void* address, void* buffer, std::size_t size) -> bool;

auto wpm_nt(
    HANDLE process,
    region_cache& cache,
    void* address,
    void const* buffer,
    std::size_t size,
    bool streaming) -> bool;

}
