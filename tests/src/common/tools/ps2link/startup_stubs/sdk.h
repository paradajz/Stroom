#pragma once

#define MAXNAMLEN 256

#define DISABLE_PATCHED_FUNCTIONS()
#define DISABLE_EXTRA_TIMERS_FUNCTIONS()
#define PS2_DISABLE_AUTOSTART_PTHREAD()

#define LAUNCHER_MODULES(X) \
    X(poweroff)             \
    X(ps2dev9)              \
    X(netman)               \
    X(smap)                 \
    X(ps2ip_nm)             \
    X(udptty)               \
    X(ioptrap)              \
    X(ps2link)

#define DECLARE_MODULE(name)           \
    extern unsigned char name##_irx[]; \
    extern unsigned int  size_##name##_irx;

LAUNCHER_MODULES(DECLARE_MODULE)

void SifInitRpc(int mode);

/**
 * @brief Substitute SifIopReset in this host fixture.
 */
int SifIopReset(const char* command, int mode);

/**
 * @brief Substitute SifIopSync in this host fixture.
 */
int SifIopSync(void);

/**
 * @brief Substitute sbv_patch_enable_lmb in this host fixture.
 */
int sbv_patch_enable_lmb(void);

/**
 * @brief Substitute sbv_patch_disable_prefix_check in this host fixture.
 */
int sbv_patch_disable_prefix_check(void);

/**
 * @brief Substitute SifExecModuleBuffer in this host fixture.
 */
int SifExecModuleBuffer(void* data, unsigned int size, unsigned int args_size, const char* args, int* result);

/**
 * @brief Substitute init_scr in this host fixture.
 */
void init_scr(void);

/**
 * @brief Substitute scr_printf in this host fixture.
 */
int scr_printf(const char* format, ...);

/**
 * @brief Substitute installExceptionHandlers in this host fixture.
 */
void installExceptionHandlers(void);

/**
 * @brief Substitute initCmdRpc in this host fixture.
 */
int initCmdRpc(void);

/**
 * @brief Substitute GetMemorySize in this host fixture.
 */
int GetMemorySize(void);

/**
 * @brief Substitute ExitDeleteThread in this host fixture.
 */
void ExitDeleteThread(void);
