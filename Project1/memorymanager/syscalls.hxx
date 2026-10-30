#pragma once

#include <cstddef>
#include <cstdint>

using NTSTATUS = long;
using HANDLE = void*;
using PVOID = void*;
using ULONG = unsigned long;
using ULONG_PTR = std::uintptr_t;
using SIZE_T = std::size_t;
using ACCESS_MASK = ULONG;

inline constexpr NTSTATUS STATUS_SUCCESS = 0;
inline constexpr NTSTATUS STATUS_PARTIAL_COPY = static_cast<NTSTATUS>(0x8000000DL);

inline constexpr ACCESS_MASK PROCESS_VM_READ = 0x0010;
inline constexpr ACCESS_MASK PROCESS_VM_WRITE = 0x0020;
inline constexpr ACCESS_MASK PROCESS_VM_OPERATION = 0x0008;
inline constexpr ACCESS_MASK PROCESS_QUERY_INFORMATION = 0x0400;

struct object_attributes_t {
    ULONG length;
    HANDLE root_directory;
    void* object_name;
    ULONG attributes;
    PVOID security_descriptor;
    PVOID security_quality_of_service;
};

struct client_id_t {
    HANDLE unique_process;
    HANDLE unique_thread;
};

inline auto initialize_object_attributes(object_attributes_t* attrs) -> void {
    attrs->length = sizeof(object_attributes_t);
    attrs->root_directory = nullptr;
    attrs->object_name = nullptr;
    attrs->attributes = 0;
    attrs->security_descriptor = nullptr;
    attrs->security_quality_of_service = nullptr;
}

extern "C" std::uint32_t g_ssn_nt_open_process;
extern "C" std::uint32_t g_ssn_nt_read_virtual_memory;
extern "C" std::uint32_t g_ssn_nt_write_virtual_memory;
extern "C" void* g_gadget;
extern "C" NTSTATUS nt_open_process_indirect(
    HANDLE* process_handle,
    ACCESS_MASK desired_access,
    object_attributes_t* object_attributes,
    client_id_t* client_id);

extern "C" NTSTATUS nt_read_virtual_memory_indirect(
    HANDLE process_handle,
    PVOID base_address,
    PVOID buffer,
    SIZE_T number_of_bytes_to_read,
    SIZE_T* number_of_bytes_read);

extern "C" NTSTATUS nt_write_virtual_memory_indirect(
    HANDLE process_handle,
    PVOID base_address,
    PVOID buffer,
    SIZE_T number_of_bytes_to_write,
    SIZE_T* number_of_bytes_written);

namespace memorymanager::syscalls {

auto init() -> bool;

auto open_process(
    HANDLE* process_handle,
    ACCESS_MASK desired_access,
    object_attributes_t* object_attributes,
    client_id_t* client_id) -> NTSTATUS;

auto read_virtual_memory(
    HANDLE process_handle,
    PVOID base_address,
    PVOID buffer,
    SIZE_T size,
    SIZE_T* bytes_read) -> NTSTATUS;

auto write_virtual_memory(
    HANDLE process_handle,
    PVOID base_address,
    PVOID buffer,
    SIZE_T size,
    SIZE_T* bytes_written) -> NTSTATUS;

}
