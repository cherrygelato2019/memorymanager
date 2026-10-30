#include "memory.hxx"

#include <Windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace memorymanager {

namespace {

auto log(char const* fmt, ...) -> void {
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stdout, fmt, args);
    va_end(args);
    std::fflush(stdout);
}

auto log_err(char const* fmt, ...) -> void {
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
    std::fflush(stderr);
}

auto qpc_freq() -> double {
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    return static_cast<double>(f.QuadPart);
}

auto qpc_now() -> std::uint64_t {
    LARGE_INTEGER t{};
    QueryPerformanceCounter(&t);
    return static_cast<std::uint64_t>(t.QuadPart);
}

struct bench_result {
    double read_mib_s{};
    double write_mib_s{};
    std::size_t read_ok{};
    std::size_t write_ok{};
};

struct bench_config {
    process* proc{};
    void* target{};
    void* read_buf{};
    void* write_buf{};
    std::size_t bytes{};
    std::size_t iterations{};
    std::size_t thread_index{};
    std::size_t thread_count{};
    std::uint32_t core_base{};
};

auto stripe_offset(bench_config const& cfg) -> std::size_t {
    return (cfg.bytes / cfg.thread_count) * cfg.thread_index;
}

auto stripe_bytes(bench_config const& cfg) -> std::size_t {
    if (cfg.thread_index + 1 == cfg.thread_count) {
        return cfg.bytes - stripe_offset(cfg);
    }
    return cfg.bytes / cfg.thread_count;
}

auto run_stripe_rpm(bench_config const& cfg) -> std::size_t {
    auto* base = static_cast<std::uint8_t*>(cfg.target) + stripe_offset(cfg);
    auto* buf = static_cast<std::uint8_t*>(cfg.read_buf) + stripe_offset(cfg);
    auto const n = stripe_bytes(cfg);
    std::size_t ok = 0;

    pin_current_thread(cfg.core_base + static_cast<std::uint32_t>(cfg.thread_index));

    for (std::size_t i = 0; i < cfg.iterations; ++i) {
        if (cfg.proc->rpm(base, buf, n)) {
            ++ok;
        }
    }

    return ok;
}

auto run_stripe_wpm(bench_config const& cfg) -> std::size_t {
    auto* base = static_cast<std::uint8_t*>(cfg.target) + stripe_offset(cfg);
    auto* buf = static_cast<std::uint8_t*>(cfg.write_buf) + stripe_offset(cfg);
    auto const n = stripe_bytes(cfg);
    std::size_t ok = 0;

    pin_current_thread(cfg.core_base + static_cast<std::uint32_t>(cfg.thread_index));

    for (std::size_t i = 0; i < cfg.iterations; ++i) {
        if (cfg.proc->wpm(base, buf, n)) {
            ++ok;
        }
    }

    return ok;
}

auto run_bench_parallel(bench_config cfg, std::size_t threads) -> bench_result {
    bench_result out{};
    cfg.thread_count = threads;

    for (std::size_t i = 0; i < 16; ++i) {
        cfg.proc->rpm(cfg.target, cfg.read_buf, cfg.bytes);
        cfg.proc->wpm(cfg.target, cfg.write_buf, cfg.bytes);
    }

    std::vector<std::thread> workers;
    workers.reserve(threads);

    auto const freq = qpc_freq();
    auto t0 = qpc_now();

    for (std::size_t t = 0; t < threads; ++t) {
        auto work = cfg;
        work.thread_index = t;
        workers.emplace_back([work]() {
            run_stripe_rpm(work);
        });
    }

    for (auto& w : workers) {
        w.join();
    }

    auto t1 = qpc_now();
    workers.clear();

    for (std::size_t t = 0; t < threads; ++t) {
        auto work = cfg;
        work.thread_index = t;
        workers.emplace_back([work]() {
            run_stripe_wpm(work);
        });
    }

    for (auto& w : workers) {
        w.join();
    }

    auto t2 = qpc_now();

    out.read_ok = cfg.iterations;
    out.write_ok = cfg.iterations;

    auto const read_s = static_cast<double>(t1 - t0) / freq;
    auto const write_s = static_cast<double>(t2 - t1) / freq;
    auto const total = static_cast<double>(cfg.bytes * cfg.iterations);

    if (read_s > 0.0) {
        out.read_mib_s = (total / (1024.0 * 1024.0)) / read_s;
    }
    if (write_s > 0.0) {
        out.write_mib_s = (total / (1024.0 * 1024.0)) / write_s;
    }

    return out;
}

auto run_bench_serial(bench_config cfg) -> bench_result {
    bench_result out{};
    volatile std::uint8_t sink{};

    for (std::size_t i = 0; i < 128; ++i) {
        cfg.proc->rpm_hot(cfg.target, cfg.read_buf, cfg.bytes);
        cfg.proc->wpm_hot(cfg.target, cfg.write_buf, cfg.bytes);
    }

    auto const freq = qpc_freq();
    auto t0 = qpc_now();

    for (std::size_t i = 0; i < cfg.iterations; ++i) {
        cfg.proc->rpm_hot(cfg.target, cfg.read_buf, cfg.bytes);
        ++out.read_ok;
    }
    sink = static_cast<std::uint8_t*>(cfg.read_buf)[0];

    auto t1 = qpc_now();

    for (std::size_t i = 0; i < cfg.iterations; ++i) {
        cfg.proc->wpm_hot(cfg.target, cfg.write_buf, cfg.bytes);
        ++out.write_ok;
    }

    auto t2 = qpc_now();
    (void)sink;

    auto const read_s = static_cast<double>(t1 - t0) / freq;
    auto const write_s = static_cast<double>(t2 - t1) / freq;
    auto const total = static_cast<double>(cfg.bytes * cfg.iterations);

    if (read_s > 0.0 && out.read_ok) {
        out.read_mib_s = (total / (1024.0 * 1024.0)) / read_s;
    }
    if (write_s > 0.0 && out.write_ok) {
        out.write_mib_s = (total / (1024.0 * 1024.0)) / write_s;
    }

    return out;
}

auto verify_rw(process& proc, void* target, void* write_buf, void* read_buf, std::size_t bytes) -> bool {
    auto* sent = static_cast<std::uint8_t*>(write_buf);
    auto* back = static_cast<std::uint8_t*>(read_buf);

    for (std::size_t i = 0; i < bytes; ++i) {
        sent[i] = static_cast<std::uint8_t>(0x11 + (i & 0xFF));
    }

    if (!proc.wpm(target, sent, bytes)) {
        return false;
    }
    if (!proc.rpm(target, back, bytes)) {
        return false;
    }

    return std::memcmp(sent, back, bytes) == 0;
}

auto fill_nt_stream(void* buffer, std::size_t bytes) -> void {
    std::memset(buffer, 0xA5, bytes);
}

}

process::~process() {
    close();
}

auto process::open(std::uint32_t pid) -> bool {
    close();

    client_id_t cid{};
    cid.unique_process = reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(pid));
    cid.unique_thread = nullptr;

    object_attributes_t oa{};
    initialize_object_attributes(&oa);

    HANDLE h{};
    auto const access = PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION;
    auto const status = syscalls::open_process(&h, access, &oa, &cid);
    if (status != STATUS_SUCCESS || !h) {
        return false;
    }

    handle_ = h;
    local_ = pid == GetCurrentProcessId();
    force_nt_ = false;
    streaming_wpm_ = false;
    region_cache_clear(region_);
    return true;
}

auto process::close() -> void {
    if (handle_) {
        CloseHandle(handle_);
        handle_ = nullptr;
    }
    local_ = false;
    force_nt_ = false;
    streaming_wpm_ = false;
    region_cache_clear(region_);
}

auto process::rpm(void* address, void* buffer, std::size_t size) -> bool {
    auto const handle = handle_;
    if (!handle || !address || !buffer || !size) {
        return false;
    }

    if (local_ && !force_nt_) {
        copy_local(buffer, address, size);
        return true;
    }

    return rpm_nt(handle, region_, address, buffer, size);
}

auto process::wpm(void* address, void const* buffer, std::size_t size) -> bool {
    auto const handle = handle_;
    if (!handle || !address || !buffer || !size) {
        return false;
    }

    if (local_ && !force_nt_) {
        copy_local(const_cast<void*>(address), buffer, size);
        return true;
    }

    return wpm_nt(handle, region_, const_cast<void*>(address), buffer, size, streaming_wpm_);
}

auto process::rpm_hot(void* address, void* buffer, std::size_t size) -> bool {
    if (local_ && !force_nt_) {
        copy_local(buffer, address, size);
        return true;
    }
    return rpm(address, buffer, size);
}

auto process::wpm_hot(void* address, void const* buffer, std::size_t size) -> bool {
    if (local_ && !force_nt_) {
        copy_local(const_cast<void*>(address), buffer, size);
        return true;
    }
    return wpm(address, buffer, size);
}

auto main() -> int {
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    log("memory manager\n");

    if (!syscalls::init()) {
        log_err("memory manager: syscall init failed\n");
        return 1;
    }

    process proc{};
    auto const pid = GetCurrentProcessId();
    if (!proc.open(pid)) {
        log_err("memory manager: open process failed (pid %lu)\n", pid);
        return 1;
    }

    auto const large_ok = enable_large_page_privilege();
    log("memory manager: attached pid %lu (%s)\n", pid, proc.is_local() ? "local" : "remote");
    log("memory manager: chunk max %zu MiB align %zu large_pages %s\n",
        k_max_transfer / (1024 * 1024),
        k_buffer_align,
        large_ok ? "on" : "off");

    constexpr std::size_t chunk = 64 * 1024;
    constexpr std::size_t iterations = 4000;
    constexpr std::size_t pool_blocks = 8;
    constexpr std::size_t slab_bytes = chunk * 3 + k_page + k_buffer_align;

    auto* slab_raw = VirtualAlloc(nullptr, slab_bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!slab_raw) {
        log_err("memory manager: slab alloc failed\n");
        return 1;
    }

    auto const slab_base = (reinterpret_cast<std::uintptr_t>(slab_raw) + k_buffer_align - 1) & ~(k_buffer_align - 1);
    auto* target_ptr = reinterpret_cast<void*>(slab_base);
    auto* read_ptr = reinterpret_cast<void*>(slab_base + chunk);
    auto* write_ptr = reinterpret_cast<void*>(slab_base + chunk * 2);

    if (!transfer::init_pool(chunk, pool_blocks, large_ok)) {
        VirtualFree(slab_raw, 0, MEM_RELEASE);
        log_err("memory manager: transfer pool init failed\n");
        return 1;
    }

    auto* read_buf = read_ptr;
    auto* write_buf = write_ptr;
    if (!read_buf || !write_buf) {
        log_err("memory manager: buffer pool acquire failed\n");
        return 1;
    }

    fill_nt_stream(write_buf, chunk);
    transfer::pretouch(read_buf, chunk);
    transfer::pretouch(write_buf, chunk);
    transfer::pretouch(target_ptr, chunk);
    std::memset(target_ptr, 0xCC, chunk);

    if (!verify_rw(proc, target_ptr, write_buf, read_buf, chunk)) {
        log_err("memory manager: rpm/wpm verify failed (memcpy path)\n");
        VirtualFree(slab_raw, 0, MEM_RELEASE);
        return 1;
    }

    proc.set_force_nt(true);
    if (!verify_rw(proc, target_ptr, write_buf, read_buf, chunk)) {
        log_err("memory manager: rpm/wpm verify failed (nt path)\n");
        VirtualFree(slab_raw, 0, MEM_RELEASE);
        return 1;
    }
    proc.set_force_nt(false);

    log("memory manager: rpm/wpm verify ok (memcpy + nt)\n");

    bench_config cfg{};
    cfg.proc = &proc;
    cfg.target = target_ptr;
    cfg.read_buf = read_buf;
    cfg.write_buf = write_buf;
    cfg.bytes = chunk;
    cfg.iterations = iterations;
    cfg.core_base = 0;

    auto const serial = run_bench_serial(cfg);

    proc.set_force_nt(true);
    cfg.read_buf = transfer::acquire_buffer();
    cfg.write_buf = transfer::acquire_buffer();
    fill_nt_stream(cfg.write_buf, chunk);
    transfer::pretouch(cfg.read_buf, chunk);
    transfer::pretouch(cfg.write_buf, chunk);
    auto const serial_nt = run_bench_serial(cfg);

    proc.set_force_nt(false);

    log("memory manager: bench %zu KiB x %zu serial-local\n", chunk / 1024, iterations);
    log("memory manager: rpm %.2f MiB/s (%zu ok)\n", serial.read_mib_s, serial.read_ok);
    log("memory manager: wpm %.2f MiB/s (%zu ok)\n", serial.write_mib_s, serial.write_ok);

    log("memory manager: bench %zu KiB x %zu serial-nt\n", chunk / 1024, iterations);
    log("memory manager: rpm %.2f MiB/s (%zu ok)\n", serial_nt.read_mib_s, serial_nt.read_ok);
    log("memory manager: wpm %.2f MiB/s (%zu ok)\n", serial_nt.write_mib_s, serial_nt.write_ok);

    VirtualFree(slab_raw, 0, MEM_RELEASE);
    return 0;
}

}
