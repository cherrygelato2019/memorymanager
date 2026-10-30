.CODE

EXTERN g_gadget:QWORD
EXTERN g_ssn_nt_open_process:DWORD
EXTERN g_ssn_nt_read_virtual_memory:DWORD
EXTERN g_ssn_nt_write_virtual_memory:DWORD

PUBLIC nt_open_process_indirect
PUBLIC nt_read_virtual_memory_indirect
PUBLIC nt_write_virtual_memory_indirect

nt_open_process_indirect PROC
    mov r10, rcx
    mov eax, g_ssn_nt_open_process
    jmp qword ptr [g_gadget]
nt_open_process_indirect ENDP

nt_read_virtual_memory_indirect PROC
    mov r10, rcx
    mov eax, g_ssn_nt_read_virtual_memory
    syscall
    ret
nt_read_virtual_memory_indirect ENDP

nt_write_virtual_memory_indirect PROC
    mov r10, rcx
    mov eax, g_ssn_nt_write_virtual_memory
    syscall
    ret
nt_write_virtual_memory_indirect ENDP

END
