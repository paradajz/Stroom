import { spawnSync } from "node:child_process";
import { mkdtemp, mkdir, copyFile, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";

// Invoked by the SDK builder while the exact pinned lwIP source is available.
// No network access or additional dependency download is needed for these tests.
const [sdk] = process.argv.slice(2);
if (!sdk) throw Error("Usage: node test_recovery.mjs PS2SDK_SOURCE_DIR");
const here = fileURLToPath(new URL(".", import.meta.url));
const directory = await mkdtemp(join(tmpdir(), "stroom-tx-recovery-"));
function run(command, args) {
  const result = spawnSync(command, args, { encoding: "utf8" });
  if (result.error || result.status !== 0)
    throw Error(
      `${command} failed: ${result.error ?? result.stdout + result.stderr}`,
    );
  if (result.stdout) process.stdout.write(result.stdout);
}
try {
  const lwip = join(sdk, "common/external_deps/lwip/src");
  const object = join(directory, "api_msg.o");
  run("cc", [
    "-std=c11",
    "-ffunction-sections",
    "-fdata-sections",
    "-I",
    join(here, "stubs"),
    "-I",
    join(lwip, "include"),
    "-idirafter",
    join(sdk, "iop/tcpip/tcpip-base/include"),
    "-c",
    join(lwip, "api/api_msg.c"),
    "-o",
    object,
  ]);
  const executable = join(directory, "test-write");
  run("cc", [
    "-std=c11",
    "-I",
    join(here, "stubs"),
    "-I",
    join(lwip, "include"),
    join(here, "test_write.c"),
    object,
    "-Wl,--gc-sections",
    "-o",
    executable,
  ]);
  run(executable, []);

  // Unlike the hardware substitutes, these options must match the built SDK:
  // otherwise the host's SO_REUSEADDR support hides console restart failures.
  const options = join(directory, "options");
  await mkdir(options);
  await copyFile(
    join(sdk, "iop/tcpip/tcpip-base/include/lwipopts.h"),
    join(options, "lwipopts.h"),
  );
  const bindHeaders = [
    "-I",
    options,
    "-I",
    join(here, "stubs"),
    "-I",
    join(lwip, "include"),
  ];
  const tcp = join(directory, "tcp.o");
  run("cc", [
    "-std=c11",
    "-ffunction-sections",
    "-fdata-sections",
    ...bindHeaders,
    "-c",
    join(lwip, "core/tcp.c"),
    "-o",
    tcp,
  ]);
  run("cc", [
    "-std=c11",
    ...bindHeaders,
    join(here, "test_bind.c"),
    tcp,
    "-Wl,--gc-sections",
    "-o",
    executable,
  ]);
  run(executable, []);

  // Initialize the complete IOP stack with hardware substitutes, then exercise
  // its registered linkoutput and NETMAN queue callbacks through public APIs.
  const driver = join(directory, "ps2ip.o");
  const headers = [
    "-I",
    join(here, "stubs"),
    "-I",
    join(lwip, "include"),
    "-I",
    join(here, "../rpc/sdk_stubs"),
    "-idirafter",
    join(sdk, "iop/tcpip/tcpip-base/include"),
    "-I",
    join(sdk, "common/include"),
  ];
  run("cc", [
    "-std=c11",
    "-ffunction-sections",
    "-fdata-sections",
    "-D_start=network_module_start",
    "-D_exit=network_module_exit",
    ...headers,
    "-c",
    join(sdk, "iop/tcpip/tcpip-netman/src/ps2ip.c"),
    "-o",
    driver,
  ]);
  run("cc", [
    "-std=c11",
    ...headers,
    join(here, "test_driver.c"),
    driver,
    "-Wl,--gc-sections",
    "-o",
    executable,
  ]);
  run(executable, []);
  run("cc", [
    "-std=c11",
    "-DLWIP_SOCKET=1",
    "-I",
    join(here, "stubs"),
    "-I",
    join(lwip, "include"),
    "-I",
    fileURLToPath(new URL("../../../../../../src/common", import.meta.url)),
    join(here, "test_tcp_state.c"),
    "-o",
    executable,
  ]);
  run(executable, []);
} finally {
  await rm(directory, { recursive: true, force: true });
}
