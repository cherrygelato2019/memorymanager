#include "syscalls.hxx"

#include <Windows.h>

#include <cstring>

extern "C" {
std::uint32_t g_ssn_nt_open_process{};
std::uint32_t g_ssn_nt_read_virtual_memory{};
std::uint32_t g_ssn_nt_write_virtual_memory{};
void* g_gadget{};
}

namespace memorymanager::syscalls {

namespace {

auto read_ssn(std::uint8_t const* stub) -> std::uint32_t {
    if (stub[0] == 0x4C && stub[1] == 0x8B && stub[2] == 0xD1 && stub[3] == 0xB8) {
        return *reinterpret_cast<std::uint32_t const*>(stub + 4);
    }
    return 0;
}

auto resolve_export_ssn(char const* name) -> std::uint32_t {
    auto const ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        return 0;
    }

    auto const dos = reinterpret_cast<IMAGE_DOS_HEADER*>(ntdll);
    auto const nt = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<std::uint8_t*>(ntdll) + dos->e_lfanew);
    auto const exports = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(
        reinterpret_cast<std::uint8_t*>(ntdll) + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);

    auto const names = reinterpret_cast<DWORD*>(reinterpret_cast<std::uint8_t*>(ntdll) + exports->AddressOfNames);
    auto const ordinals = reinterpret_cast<WORD*>(reinterpret_cast<std::uint8_t*>(ntdll) + exports->AddressOfNameOrdinals);
    auto const functions = reinterpret_cast<DWORD*>(reinterpret_cast<std::uint8_t*>(ntdll) + exports->AddressOfFunctions);

    for (DWORD i = 0; i < exports->NumberOfNames; ++i) {
        auto const export_name = reinterpret_cast<char const*>(reinterpret_cast<std::uint8_t*>(ntdll) + names[i]);
        if (std::strcmp(export_name, name) != 0) {
            continue;
        }

        auto const fn = reinterpret_cast<std::uint8_t*>(ntdll) + functions[ordinals[i]];
        auto ssn = read_ssn(fn);
        if (ssn != 0) {
            return ssn;
        }

        for (int delta = 1; delta < 512; ++delta) {
            ssn = read_ssn(fn + delta);
            if (ssn != 0) {
                return ssn - static_cast<std::uint32_t>(delta);
            }
            if (delta <= static_cast<int>(fn - reinterpret_cast<std::uint8_t*>(ntdll))) {
                ssn = read_ssn(fn - delta);
                if (ssn != 0) {
                    return ssn + static_cast<std::uint32_t>(delta);
                }
            }
        }
        return 0;
    }

    return 0;
}

auto find_syscall_gadget() -> void* {
    auto const ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        return nullptr;
    }

    auto const dos = reinterpret_cast<IMAGE_DOS_HEADER*>(ntdll);
    auto const nt = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<std::uint8_t*>(ntdll) + dos->e_lfanew);
    auto const size = nt->OptionalHeader.SizeOfImage;
    auto const base = reinterpret_cast<std::uint8_t*>(ntdll);

    for (std::size_t i = 0; i + 2 < size; ++i) {
        if (base[i] == 0x0F && base[i + 1] == 0x05 && base[i + 2] == 0xC3) {
            return base + i;
        }
    }

    return nullptr;
}

}

auto init() -> bool {
    g_gadget = find_syscall_gadget();
    g_ssn_nt_open_process = resolve_export_ssn("NtOpenProcess");
    g_ssn_nt_read_virtual_memory = resolve_export_ssn("NtReadVirtualMemory");
    g_ssn_nt_write_virtual_memory = resolve_export_ssn("NtWriteVirtualMemory");

    return g_gadget
        && g_ssn_nt_open_process
        && g_ssn_nt_read_virtual_memory
        && g_ssn_nt_write_virtual_memory;
}

auto open_process(
    HANDLE* process_handle,
    ACCESS_MASK desired_access,
    object_attributes_t* object_attributes,
    client_id_t* client_id) -> NTSTATUS {
    return nt_open_process_indirect(process_handle, desired_access, object_attributes, client_id);
}

auto read_virtual_memory(
    HANDLE process_handle,
    PVOID base_address,
    PVOID buffer,
    SIZE_T size,
    SIZE_T* bytes_read) -> NTSTATUS {
    return nt_read_virtual_memory_indirect(process_handle, base_address, buffer, size, bytes_read);
}

auto write_virtual_memory(
    HANDLE process_handle,
    PVOID base_address,
    PVOID buffer,
    SIZE_T size,
    SIZE_T* bytes_written) -> NTSTATUS {
    return nt_write_virtual_memory_indirect(process_handle, base_address, buffer, size, bytes_written);
}

}
