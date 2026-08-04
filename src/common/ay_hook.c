#include "ay_hook.h"
#include "ay_log.h"

#include <string.h>

BOOL ay_patch_jmp(void *target, void *dst) {
    if (!target || !dst) return FALSE;

    BYTE patch[12];
    patch[0] = 0x48; // mov rax, imm64
    patch[1] = 0xB8;
    memcpy(patch + 2, &dst, 8);
    patch[10] = 0xFF; // jmp rax
    patch[11] = 0xE0;

    DWORD old;
    if (!VirtualProtect(target, sizeof(patch), PAGE_EXECUTE_READWRITE, &old)) {
        ay_log("hook: VirtualProtect failed for %p (err %lu)", target, GetLastError());
        return FALSE;
    }
    memcpy(target, patch, sizeof(patch));
    VirtualProtect(target, sizeof(patch), old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, sizeof(patch));
    return TRUE;
}

BOOL ay_hook_export(HMODULE mod, const char *name, void *dst) {
    if (!mod) return FALSE;
    void *target = (void *)GetProcAddress(mod, name);
    if (!target) return FALSE;
    BOOL ok = ay_patch_jmp(target, dst);
    if (ok) ay_log("hook: %s -> ayame", name);
    return ok;
}
