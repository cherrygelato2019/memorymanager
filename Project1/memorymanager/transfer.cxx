#include "io.hxx"

namespace memorymanager::transfer {

namespace {

buffer_pool g_pool{};

}

auto init_pool(std::size_t block_bytes, std::size_t blocks, bool large_pages) -> bool {
    return g_pool.init(block_bytes, blocks, large_pages);
}

auto acquire_buffer() -> void* {
    return g_pool.acquire();
}

auto pool_block_size() -> std::size_t {
    return g_pool.block_size();
}

auto pretouch(void* buffer, std::size_t bytes) -> void {
    if (!buffer || !bytes) {
        return;
    }
    auto* p = static_cast<volatile std::uint8_t*>(buffer);
    for (std::size_t i = 0; i < bytes; i += k_page) {
        p[i] = p[i];
    }
}

}
