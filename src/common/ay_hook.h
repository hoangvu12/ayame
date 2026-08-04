#pragma once

#include <windows.h>

// Overwrites the first 12 bytes of `target` with `mov rax, dst; jmp rax`.
// Every caller is intercepted regardless of how it resolved the address.
BOOL ay_patch_jmp(void *target, void *dst);

// Convenience: resolves an export and patches it. Returns FALSE when the
// export doesn't exist (older/newer steam_api builds drop flat API names).
BOOL ay_hook_export(HMODULE mod, const char *name, void *dst);
