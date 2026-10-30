#pragma once

#include "io.hxx"

#include <cstddef>
#include <cstdint>

namespace memorymanager {

class process {
public:
    process() = default;
    ~process();

    process(process const&) = delete;
    process& operator=(process const&) = delete;

    auto open(std::uint32_t pid) -> bool;
    auto close() -> void;

    auto valid() const -> bool { return handle_ != nullptr; }
    auto is_local() const -> bool { return local_; }
    auto force_nt() const -> bool { return force_nt_; }
    auto streaming_wpm() const -> bool { return streaming_wpm_; }
    auto native_handle() const -> HANDLE { return handle_; }

    auto set_force_nt(bool value) -> void { force_nt_ = value; }
    auto set_streaming_wpm(bool value) -> void { streaming_wpm_ = value; }
    auto flush_region_cache() -> void { region_cache_clear(region_); }

    auto rpm(void* address, void* buffer, std::size_t size) -> bool;
    auto wpm(void* address, void const* buffer, std::size_t size) -> bool;

    auto rpm_hot(void* address, void* buffer, std::size_t size) -> bool;
    auto wpm_hot(void* address, void const* buffer, std::size_t size) -> bool;

    auto read(void* address, void* buffer, std::size_t size) -> bool { return rpm(address, buffer, size); }
    auto write(void* address, void const* buffer, std::size_t size) -> bool { return wpm(address, buffer, size); }

    template <typename T>
    auto rpm(void* address, T& out) -> bool {
        return rpm(address, &out, sizeof(T));
    }

    template <typename T>
    auto wpm(void* address, T const& value) -> bool {
        return wpm(address, &value, sizeof(T));
    }

private:
    HANDLE handle_{};
    bool local_{};
    bool force_nt_{};
    bool streaming_wpm_{};
    region_cache region_{};
};

auto main() -> int;

}
