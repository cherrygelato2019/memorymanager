#include "io.hxx"

#include <Windows.h>

#include <cstring>
#include <immintrin.h>
#include <utility>

namespace memorymanager {

aligned_arena::~aligned_arena() {
    if (raw_) {
        VirtualFree(raw_, 0, MEM_RELEASE);
        raw_ = nullptr;
        ptr_ = nullptr;
        size_ = 0;
    }
}

aligned_arena::aligned_arena(aligned_arena&& other) noexcept
    : ptr_{other.ptr_}, raw_{other.raw_}, size_{other.size_}, large_{other.large_} {
    other.ptr_ = nullptr;
    other.raw_ = nullptr;
    other.size_ = 0;
}

auto aligned_arena::operator=(aligned_arena&& other) noexcept -> aligned_arena& {
    if (this == &other) {
        return *this;
    }
    if (raw_) {
        VirtualFree(raw_, 0, MEM_RELEASE);
    }
    ptr_ = other.ptr_;
    raw_ = other.raw_;
    size_ = other.size_;
    large_ = other.large_;
    other.ptr_ = nullptr;
    other.raw_ = nullptr;
    other.size_ = 0;
    return *this;
}

auto aligned_arena::alloc(std::size_t size, bool large_pages) -> bool {
    if (raw_) {
        VirtualFree(raw_, 0, MEM_RELEASE);
        raw_ = nullptr;
        ptr_ = nullptr;
        size_ = 0;
    }

    large_ = large_pages;
    size_ = size;

    if (large_pages) {
        auto const gran = GetLargePageMinimum();
        auto const padded = (size + gran - 1) / gran * gran;
        raw_ = VirtualAlloc(nullptr, padded, MEM_COMMIT | MEM_RESERVE | MEM_LARGE_PAGES, PAGE_READWRITE);
        if (raw_) {
            ptr_ = raw_;
            size_ = padded;
            return true;
        }
    }

    auto const padded = size + k_buffer_align + k_page;
    raw_ = VirtualAlloc(nullptr, padded, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!raw_) {
        return false;
    }

    auto const base = reinterpret_cast<std::uintptr_t>(raw_);
    auto const aligned = (base + k_buffer_align - 1) & ~(k_buffer_align - 1);
    ptr_ = reinterpret_cast<void*>(aligned);
    return true;
}

auto buffer_pool::init(std::size_t block_bytes, std::size_t blocks, bool large_pages) -> bool {
    arenas_.clear();
    block_bytes_ = block_bytes;
    slot_.store(0, std::memory_order_relaxed);

    arenas_.reserve(blocks);
    for (std::size_t i = 0; i < blocks; ++i) {
        aligned_arena block{};
        if (!block.alloc(block_bytes, large_pages)) {
            arenas_.clear();
            return false;
        }
        arenas_.push_back(std::move(block));
    }

    return true;
}

auto buffer_pool::acquire() -> void* {
    if (arenas_.empty()) {
        return nullptr;
    }
    auto const i = slot_.fetch_add(1, std::memory_order_relaxed) % arenas_.size();
    return arenas_[i].data();
}

auto enable_large_page_privilege() -> bool {
    HANDLE token{};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        return false;
    }

    LUID luid{};
    if (!LookupPrivilegeValueW(nullptr, SE_LOCK_MEMORY_NAME, &luid)) {
        CloseHandle(token);
        return false;
    }

    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    CloseHandle(token);
    return GetLastError() != ERROR_NOT_ALL_ASSIGNED;
}

namespace {

auto page_tail(std::uintptr_t address, std::size_t gran) -> std::size_t {
    return gran - (address & (gran - 1));
}

auto cache_refresh(HANDLE process, region_cache& cache, std::uintptr_t address) -> bool {
    if (address >= cache.base && address < cache.end) {
        return true;
    }

    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQueryEx(process, reinterpret_cast<void*>(address), &mbi, sizeof(mbi)) != sizeof(mbi)) {
        cache.base = 0;
        cache.end = 0;
        return false;
    }

    if (mbi.State != MEM_COMMIT) {
        region_cache_clear(cache);
        return false;
    }

    cache.base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
    cache.end = cache.base + mbi.RegionSize;

    auto const large_gran = static_cast<std::size_t>(GetLargePageMinimum());
    cache.large_mapping = mbi.RegionSize >= large_gran
        && cache.base % large_gran == 0
        && mbi.RegionSize % large_gran == 0;
    cache.gran = cache.large_mapping ? large_gran : k_page;

    return address >= cache.base && address < cache.end;
}

auto region_bytes_at(region_cache const& cache, std::uintptr_t address) -> std::size_t {
    if (address >= cache.end || address < cache.base) {
        return 0;
    }
    return cache.end - address;
}

auto clamp_step(std::uintptr_t address, std::size_t left, std::size_t region, region_cache const& cache) -> std::size_t {
    if (region == 0 || left == 0) {
        return 0;
    }

    auto const gran = cache.gran;
    auto step = left;
    if (step > k_max_transfer) {
        step = k_max_transfer;
    }
    if (step > region) {
        step = region;
    }

    if (cache.large_mapping && address % gran == 0 && step >= gran) {
        step &= ~(gran - 1);
    } else if ((address & (k_page - 1)) != 0) {
        auto const tail = page_tail(address, k_page);
        if (step > tail) {
            step = tail;
        }
    } else if (step >= k_page) {
        step &= ~(k_page - 1);
    }

    if (step > left) {
        step = left;
    }

    return step;
}

}

auto region_cache_clear(region_cache& cache) -> void {
    cache.base = 0;
    cache.end = 0;
    cache.gran = k_page;
    cache.large_mapping = false;
}

auto pin_current_thread(std::uint32_t core_index) -> void {
    auto const mask = 1ull << (core_index % 64);
    SetThreadAffinityMask(GetCurrentThread(), mask);
}

auto copy_local(void* dst, void const* src, std::size_t bytes) -> void {
    std::memcpy(dst, src, bytes);
}

auto stream_copy(void* dst, void const* src, std::size_t bytes) -> void {
    auto* out = static_cast<std::uint8_t*>(dst);
    auto const* in = static_cast<std::uint8_t const*>(src);

    if (reinterpret_cast<std::uintptr_t>(out) % 16 == 0 && reinterpret_cast<std::uintptr_t>(in) % 16 == 0) {
        std::size_t i = 0;
        for (; i + 64 <= bytes; i += 64) {
            auto const v0 = _mm_load_si128(reinterpret_cast<__m128i const*>(in + i));
            auto const v1 = _mm_load_si128(reinterpret_cast<__m128i const*>(in + i + 16));
            auto const v2 = _mm_load_si128(reinterpret_cast<__m128i const*>(in + i + 32));
            auto const v3 = _mm_load_si128(reinterpret_cast<__m128i const*>(in + i + 48));
            _mm_stream_si128(reinterpret_cast<__m128i*>(out + i), v0);
            _mm_stream_si128(reinterpret_cast<__m128i*>(out + i + 16), v1);
            _mm_stream_si128(reinterpret_cast<__m128i*>(out + i + 32), v2);
            _mm_stream_si128(reinterpret_cast<__m128i*>(out + i + 48), v3);
        }
        _mm_sfence();
        if (i < bytes) {
            std::memcpy(out + i, in + i, bytes - i);
        }
        return;
    }

    std::memcpy(out, in, bytes);
}

auto transfer_step(HANDLE process, region_cache& cache, void const* cursor, std::size_t left) -> std::size_t {
    auto const address = reinterpret_cast<std::uintptr_t>(cursor);
    if (!cache_refresh(process, cache, address)) {
        return 0;
    }

    auto const region = region_bytes_at(cache, address);
    return clamp_step(address, left, region, cache);
}

auto nt_ok(NTSTATUS status) -> bool {
    return status == STATUS_SUCCESS || status == STATUS_PARTIAL_COPY;
}

auto rpm_nt(HANDLE process, region_cache& cache, void* address, void* buffer, std::size_t size) -> bool {
    auto* dst = static_cast<std::uint8_t*>(buffer);
    auto* src = static_cast<std::uint8_t*>(address);
    auto left = size;

    if (left <= k_max_transfer) {
        auto const step = transfer_step(process, cache, src, left);
        if (step == left) {
            SIZE_T got = 0;
            auto const status = syscalls::read_virtual_memory(process, src, dst, step, &got);
            return nt_ok(status) && got == left;
        }
    }

    while (left > 0) {
        auto const step = transfer_step(process, cache, src, left);
        if (step == 0) {
            return false;
        }

        SIZE_T got = 0;
        auto const status = syscalls::read_virtual_memory(process, src, dst, step, &got);
        if (!nt_ok(status) || got == 0) {
            return false;
        }

        dst += got;
        src += got;
        left -= got;

        if (got < step && status != STATUS_PARTIAL_COPY) {
            return false;
        }

        if (left > 0 && reinterpret_cast<std::uintptr_t>(src) >= cache.end) {
            region_cache_clear(cache);
        }
    }

    return true;
}

auto wpm_nt(
    HANDLE process,
    region_cache& cache,
    void* address,
    void const* buffer,
    std::size_t size,
    bool streaming) -> bool {
    auto* dst = static_cast<std::uint8_t*>(address);
    auto const* src = static_cast<std::uint8_t const*>(buffer);
    auto left = size;

    if (left <= k_max_transfer) {
        auto const step = transfer_step(process, cache, dst, left);
        if (step == left) {
            SIZE_T wrote = 0;
            auto const status = syscalls::write_virtual_memory(
                process,
                dst,
                const_cast<std::uint8_t*>(src),
                step,
                &wrote);
            return nt_ok(status) && wrote == left;
        }
    }

    while (left > 0) {
        auto const step = transfer_step(process, cache, dst, left);
        if (step == 0) {
            return false;
        }

        SIZE_T wrote = 0;
        auto const status = syscalls::write_virtual_memory(
            process,
            dst,
            const_cast<std::uint8_t*>(src),
            step,
            &wrote);
        if (!nt_ok(status) || wrote == 0) {
            return false;
        }

        dst += wrote;
        src += wrote;
        left -= wrote;

        if (wrote < step && status != STATUS_PARTIAL_COPY) {
            return false;
        }

        if (left > 0 && reinterpret_cast<std::uintptr_t>(dst) >= cache.end) {
            region_cache_clear(cache);
        }
    }

    (void)streaming;
    return true;
}

}
