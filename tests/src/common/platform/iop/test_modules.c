#include "platform/iop/modules.h"
#include "sdk.h"
#include "unity.h"
#include <stdint.h>
#include <string.h>

#define TEST_RPC_ID 0x1234

static int                 resident;
static int                 load_id;
static int                 start_result;
static int                 loads;
static int                 patches;
static int                 rpc_result;
static int                 rpc_available;
static const Ps2IopModule* expected;

/**
 * @brief Simulate resident-module lookup.
 * @param name Expected module name.
 * @param info SDK result storage.
 * @return Configured resident ID or zero.
 */
int smod_get_mod_by_name(const char* name, smod_mod_info_t* info)
{
    TEST_ASSERT_EQUAL_STRING(expected->name, name);
    TEST_ASSERT_NOT_NULL(info);

    return resident;
}

/**
 * @brief Check ROM module arguments and simulate loading.
 * @param path Expected ROM path.
 * @param length Argument byte count.
 * @param args Argument buffer.
 * @return Configured module ID or error.
 */
int SifLoadModule(const char* path, int length, const char* args)
{
    TEST_ASSERT_EQUAL_STRING(expected->path, path);
    TEST_ASSERT_EQUAL_UINT(expected->args_size, length);
    TEST_ASSERT_EQUAL_PTR(expected->args, args);
    ++loads;

    return load_id;
}

/**
 * @brief Count patch attempts.
 * @return Already-patched failure code.
 */
int sbv_patch_enable_lmb(void)
{
    ++patches;

    return -1; /* An already-patched loader need not return success. */
}

/**
 * @brief Check embedded image arguments and simulate loading.
 * @param data Image bytes.
 * @param size Image byte count.
 * @param length Argument byte count.
 * @param args Argument buffer.
 * @param result Receives the configured startup result.
 * @return Configured module ID or error.
 */
int SifExecModuleBuffer(void* data, unsigned size, unsigned length, const char* args, int* result)
{
    TEST_ASSERT_EQUAL_INT(loads + 1, patches);
    TEST_ASSERT_EQUAL_PTR(expected->data, data);
    TEST_ASSERT_EQUAL_UINT(expected->size, size);
    TEST_ASSERT_EQUAL_UINT(expected->args_size, length);
    TEST_ASSERT_EQUAL_PTR(expected->args, args);
    ++loads;

    *result = start_result;

    return load_id;
}

/**
 * @brief Check probe storage and simulate RPC binding.
 * @param client Zeroed, aligned client storage.
 * @param id Expected service ID.
 * @param mode Expected blocking mode.
 * @return Configured binding result.
 */
int sceSifBindRpc(SifRpcClientData_t* client, unsigned id, int mode)
{
    TEST_ASSERT_EQUAL_HEX32(TEST_RPC_ID, id);
    TEST_ASSERT_EQUAL_INT(0, mode);
    TEST_ASSERT_EQUAL_UINT(0, (uintptr_t)client % 64);
    TEST_ASSERT_NULL(client->server);

    client->server = rpc_available ? client : NULL;

    return rpc_result;
}

/**
 * @brief Reset module and RPC fixtures.
 */
void setUp(void)
{
    resident = start_result = loads = patches = rpc_result = rpc_available = 0;
    load_id                                                                = 7;
    expected                                                               = NULL;
}

/**
 * @brief No resources survive a test.
 */
void tearDown(void)
{}

/**
 * @brief Resident modules bypass loading, arguments, and patching.
 */
static void resident_module(void)
{
    const Ps2IopModule module = { .name = "resident" };

    expected = &module;
    resident = 1;

    char error[48] = "old error";

    TEST_ASSERT_TRUE(platform_iop_module(&module, error, sizeof(error)) == 0);
    TEST_ASSERT_EQUAL_STRING("", error);
    TEST_ASSERT_EQUAL_INT(0, loads);
    TEST_ASSERT_EQUAL_INT(0, patches);
}

/**
 * @brief ROM loads preserve arguments and report loader failures without patching.
 */
static void rom_module(void)
{
    const char         args[] = "one\0two";
    const Ps2IopModule module = { .name = "rom", .path = "rom0:MODULE", .args = args, .args_size = sizeof(args) };

    expected = &module;

    char error[48];

    TEST_ASSERT_TRUE(platform_iop_module(&module, error, sizeof(error)) == 0);

    load_id = -12;

    TEST_ASSERT_TRUE(!(platform_iop_module(&module, error, sizeof(error)) == 0));
    TEST_ASSERT_EQUAL_STRING("rom LOAD -12", error);
    TEST_ASSERT_EQUAL_INT(2, loads);
    TEST_ASSERT_EQUAL_INT(0, patches);
}

/**
 * @brief Embedded loads patch independently and distinguish load and startup failures.
 */
static void embedded_module(void)
{
    unsigned char      image[16] = { 0 };
    const char         args[]    = "one\0two";
    const Ps2IopModule module    = { .name = "embedded", .data = image, .size = sizeof(image), .args = args, .args_size = sizeof(args) };

    expected = &module;

    char error[48];

    TEST_ASSERT_TRUE(platform_iop_module(&module, error, sizeof(error)) == 0);

    start_result = 1;

    TEST_ASSERT_TRUE(!(platform_iop_module(&module, error, sizeof(error)) == 0));
    TEST_ASSERT_EQUAL_STRING("embedded START 1", error);

    load_id = -12;

    TEST_ASSERT_TRUE(!(platform_iop_module(&module, error, sizeof(error)) == 0));
    TEST_ASSERT_EQUAL_STRING("embedded LOAD -12", error);
    TEST_ASSERT_TRUE(!(platform_iop_module(&module, NULL, 0) == 0));

    load_id      = 7;
    start_result = 0;

    TEST_ASSERT_TRUE(platform_iop_module(&module, error, sizeof(error)) == 0);
    TEST_ASSERT_EQUAL_STRING("", error);
    TEST_ASSERT_EQUAL_INT(5, loads);
    TEST_ASSERT_EQUAL_INT(loads, patches);
}

/**
 * @brief RPC probes distinguish absence, presence, and failure without stale server state.
 */
static void rpc_probe(void)
{
    TEST_ASSERT_EQUAL_INT(0, platform_iop_probe(TEST_RPC_ID));

    rpc_available = 1;

    TEST_ASSERT_EQUAL_INT(1, platform_iop_probe(TEST_RPC_ID));

    rpc_result = -1;

    TEST_ASSERT_EQUAL_INT(PS2_IOP_ERROR_RPC_BIND, platform_iop_probe(TEST_RPC_ID));

    rpc_result = rpc_available = 0;

    TEST_ASSERT_EQUAL_INT(0, platform_iop_probe(TEST_RPC_ID));
}

/**
 * @brief Run IOP startup regressions.
 * @return Failed case count.
 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(resident_module);
    RUN_TEST(rom_module);
    RUN_TEST(embedded_module);
    RUN_TEST(rpc_probe);

    return UNITY_END();
}
