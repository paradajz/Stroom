import assert from "node:assert/strict";
import net from "node:net";
import dgram from "node:dgram";
import { once } from "node:events";
import { Service, toc, delay } from "./http_service_fixture.mjs";
const service = await Service.create();
try {
  await service.start();
  for (const [raw, headers, status] of [
    ["{broken", {}, 400],
    [
      '{"type":"stroom-cd-toc","first":0,"first":1,"offsets":[150],"leadout":300}',
      {},
      400,
    ],
    [
      '{"type":"stroom-cd-toc","first":0,"f\\u0069rst":1,"offsets":[150],"leadout":300}',
      {},
      400,
    ],
    [
      '{"type":"stroom-cd-toc","first":1,"offsets":[150],"leadout":300,"extra":{"a":1,"a":2}}',
      {},
      400,
    ],
    [" ".repeat(1025), {}, 413],
    [JSON.stringify({ ...toc, type: "other" }), {}, 400],
    [
      JSON.stringify({
        ...toc,
        type: "stroom-cd-toc",
        leadout: 450001,
      }),
      {},
      400,
    ],
    [Buffer.from([255]), {}, 400],
    ["{}", { "Content-Type": "text/plain" }, 415],
    ["{}", { "Content-Encoding": "gzip" }, 415],
  ]) {
    const result = await service.request("/cd/v2/lookups", { raw, headers });
    assert.equal(result.status, status);
    assert.equal(result.body.protocol, 2);
    assert.equal(result.body.error.retryable, false);
  }
  assert.ok(!service.output.includes("FIXTURE_REQUEST"));
  assert.ok(!service.output.includes("{broken"));
  // Claimed addresses cannot bypass the actual peer's HTTP allowance.
  for (let i = 0; i < 11; i++) {
    const result = await service.request("/cd/v2/service", {
      ip: "127.0.0.250",
      headers: {
        "X-Stroom-Client-IP": "198.51.100." + (i + 1),
        "X-Forwarded-For": "203.0.113." + (i + 1),
        Forwarded: "for=192.0.2." + (i + 1),
      },
    });
    assert.equal(result.status, i < 10 ? 200 : 429);
    if (i === 10) assert.equal(result.body.error.code, "rate_limited");
  }
  assert.equal(
    (await service.request("/cd/v2/service", { ip: "127.0.0.251" })).status,
    200,
  );
  assert.equal((await service.request("/cd/v2/lookups/bad-id")).status, 400);
  assert.equal((await service.result()).status, 404);
  assert.equal((await service.art()).status, 404);
  const wrong = await service.request("/cd/v2/service", { method: "POST" });
  assert.equal(wrong.status, 405);
  assert.equal(wrong.headers.allow, "GET");
  const options = await service.request("/cd/v2/lookups", {
    method: "OPTIONS",
    headers: {
      Origin: "https://client.test",
      "Access-Control-Request-Method": "POST",
      "Access-Control-Request-Headers": "content-type",
    },
  });
  assert.equal(options.status, 405);
  assert.equal(options.headers.allow, "POST");
  assert.equal(options.body.error.code, "method_not_allowed");
  assert.equal(
    (await service.request("/missing")).body.error.code,
    "not_found",
  );
  assert.equal(
    (
      await service.request("/cd/v2/lookups", {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          "Transfer-Encoding": "chunked",
        },
      })
    ).status,
    400,
  );
  // HTTP recognition leaves UDP 12890 available.
  const udp = dgram.createSocket("udp4");
  udp.bind(12890, "127.0.0.1");
  await once(udp, "listening");
  udp.close();
  // Slow partial bodies have a total deadline, not a sliding inactivity timeout.
  const socket = net.connect({ host: "127.0.0.1", port: 12890 });
  socket.on("error", () => {});
  await once(socket, "connect");
  socket.write(
    "POST /cd/v2/lookups HTTP/1.1\r\nHost: test\r\nContent-Type: application/json\r\nContent-Length: 100\r\n\r\n{",
  );
  socket.resume();
  const end = once(socket, "close");
  await end;
  assert.ok(!service.output.includes("FIXTURE_REQUEST"));
  await service.stop();
} finally {
  await service.close();
}
console.log(
  "PASS: byte limits before JSON parsing, fatal UTF-8, invalid TOCs, peer-based admission despite forwarding headers, error envelopes, unsupported methods, fixed framing, total body deadline and no UDP listener.",
);
