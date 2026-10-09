# Stroom CD recognition HTTP API

This specification describes how an application uses a hosted Stroom service to
identify an audio CD and obtain its album labels, all track labels and cover art.
Clients can be a PS2 player, desktop applications or command-line tools.
They do not need to implement a recognition service, contact metadata
providers themselves, or know Stroom's internal report format.
An application needs a complete audio program TOC from a drive or disc-image
source to start recognition. Artist/title search and recognition from an audio sample are outside
this API. Knowing only a disc ID permits reading an existing result, not creating
a fresh recognition job without its TOC.

**MUST** and **MUST NOT** are protocol requirements, **SHOULD** is a recommendation,
and **MAY** is optional. Defaults below describe this repository's implementation.
The deployment targets PS2 clients connecting directly over plain HTTP to a
numeric public IPv4 address. Other HTTP clients can use the same API.

## Recognition flow

- Check the actual recognition service over HTTP after networking has a usable
  address. A host responding to ICMP ping is insufficient.
- Submit the inserted CD's table of contents (TOC) after a successful check.
- One successful recognition submission supplies the entire disc's track list.
  Track changes use the client's local copy and do not resend the TOC.
- If a metadata attempt fails through missing responses or a transport error, the PS2
  waits until the next track change before attempting recognition again.
- Artwork status and image-download retries never resubmit the TOC.
- Recognition is optional background work. Playback continues when unavailable.

The wire protocol uses short HTTP requests: submission returns either
the result or an identifier for an accepted lookup. Clients fetch an accepted
lookup's status without resubmitting its TOC. This is one recognition submission,
not a promise that recognition and artwork finish in a single HTTP exchange.

## Contents

- [Addressing and HTTP conventions](#1-addressing-and-http-conventions)
- [Endpoint overview](#2-endpoint-overview)
- [Service availability check](#3-service-availability-check)
- [Submit a disc](#4-submit-a-disc)
- [Fetch an accepted lookup](#5-fetch-an-accepted-lookup)
- [Completed metadata](#6-completed-metadata)
- [Artwork](#7-artwork)
- [Errors and retry delays](#8-errors-and-retry-delays)
- [Server admission and resource limits](#9-server-admission-and-resource-limits)
- [PS2 client profile](#10-ps2-client-profile)
- [Example client for any platform](#11-example-client-for-any-platform)
- [Direct public hosting](#12-direct-public-hosting)
- [Disc-ID derivation (normative appendix)](#appendix-a-disc-id-derivation)
- [CD-Extra normalization (normative appendix)](#appendix-b-disc-types-and-cd-extra-normalization)

Server memory, storage and worker details are documented separately in the
[implementation notes](implementation.md).

## 1. Addressing and HTTP conventions

The operator supplies a **base URL**. For a direct PS2-compatible deployment:

```text
http://203.0.113.10:12890
```

This is a documentation address, not a live service. The operator supplies the
real public IPv4 address. The PS2's `lookup_host` setting contains only that
numeric address, without a URL scheme or port. Both the PS2 client and this
service use fixed TCP port **12890**; there is no configurable port setting.
Other HTTP clients may use a hostname resolving to that address. Such a hostname
MUST resolve to IPv4 only; do not publish IPv6 (AAAA) records for it.

All API paths start with `/cd/v2`. Image paths remain under `/covers/`. There is
no UDP transport in this protocol and no API authentication or client account.
The service listens on IPv4, and clients connect directly to its HTTP port.

All per-caller limits use the actual TCP peer IPv4 address, including connection,
HTTP, cover and cache-aware recognition admission. Here and below, "per IP" and
"admission key" mean that address. Devices behind the same public NAT address
share these allowances. `Forwarded`, `X-Forwarded-For` and `X-Stroom-Client-IP`
headers are ignored and MUST NOT change caller identity. There is no trusted-proxy
mode or separate connection pool.

HTTP rules:

- The direct PS2 endpoint uses HTTP/1.1 and closes the connection after each
  response. Clients MUST NOT require keep-alive or send pipelined requests.
- JSON requests use `Content-Type: application/json` and a known `Content-Length`.
  Only the TOC submission accepts a request body. Request compression and chunked
  request bodies are not supported in this initial protocol.
  An optional `charset=utf-8` media-type parameter is accepted. Other charsets
  receive `415`. Unsupported/malformed HTTP framing may close the connection
  without a JSON envelope.
- Responses use a known `Content-Length`, no content compression, and
  `Cache-Control: no-store` for API JSON. JSON is UTF-8, with content type
  `application/json; charset=utf-8`. Servers emit compact JSON with raw UTF-8,
  escaping only quotes, backslashes and ASCII control characters.
- Maximum JSON request body: **1,024 bytes**. Maximum JSON response body:
  **64 KiB (65,536 bytes)**, including JSON escaping. Clients enforce bounds
  before allocating or parsing a complete body and reject truncated responses.
  The request bound includes whitespace; use compact JSON for large TOCs.
  Duplicate keys anywhere in the request body receive `400 invalid_request`,
  including names equivalent after JSON escape decoding (`first` and
  `f\u0069rst`). Distinct objects may use the same key independently.
  Direct HTTP/1.1 response status/headers, including terminating CRLFs, MUST fit
  **2,048 bytes** so the PS2 can use a bounded header parser.
- Clients send `Accept: application/json` for API calls and may request
  `Accept-Encoding: identity`. A descriptive `User-Agent` is recommended when the
  client platform permits it.
- Version 2 API JSON responses always contain `"protocol": 2`. Clients reject unsupported
  versions and ignore unknown response fields. JSON property ordering carries
  no meaning. Missing required fields and invalid field types are protocol errors.
- `Retry-After`, when supplied, is a positive integer number of **seconds**. The
  initial protocol does not require clients to parse HTTP dates. JSON retry
  fields also use seconds, not milliseconds. JSON clients use the body delay;
  a missing or different header does not invalidate a valid body. When no usable
  JSON response exists, a valid header supplies a fallback cooldown.
- Clients treat the configured origin as authoritative. Resolve returned relative
  paths against that origin and reject paths that resolve to another origin or
  outside the expected API/image route. Redirects are not part of the initial
  client contract.

### Required fields and value rules

All API JSON bodies below are objects; images are excluded.
**Every field listed in the following table is required**, including fields
nested inside `error` and `artwork`.
Unknown fields are ignored; optional fields are omitted rather than set to `null`.
Strings, status values and field names are case-sensitive. Integers must be
finite whole numbers, not booleans or quoted decimal strings. Servers MUST NOT
emit duplicate object keys. Text MUST be valid Unicode without U+0000. Display
labels MUST contain no ASCII control characters (U+0000 through U+001F); servers
replace those characters with spaces before enforcing UTF-8 byte bounds.

| Response                | Required fields                                                                                                    |
| ----------------------- | ------------------------------------------------------------------------------------------------------------------ |
| Ready service (`200`)   | `protocol: 2`, `service: "stroom-cd-recognition"`, `ready: true`                                                   |
| Pending lookup (`202`)  | `protocol: 2`, `disc_id`, `status: "pending"`, `result_path`, `retry_after_seconds`                                |
| Matched lookup (`200`)  | `protocol: 2`, `disc_id`, `status: "matched"`, `track_count`, `album`, `artist`, `tracks`, `artwork`               |
| Each track object       | `number`, `title`, `artist`                                                                                        |
| No-match lookup (`200`) | `protocol: 2`, `disc_id`, `status: "no_match"`, `expires_in_seconds`                                               |
| Available `artwork`     | `status: "available"`, `path`                                                                                      |
| Pending `artwork`       | `status: "pending"`, `retry_after_seconds`                                                                         |
| Unavailable `artwork`   | `status: "unavailable"`                                                                                            |
| API error               | `protocol: 2`, `status: "error"`, `error.code`, `error.message`, `error.retryable`; see section 8 for retry fields |

`disc_id` is a 28-character ASCII string matching `[A-Za-z0-9._]{27}-`.
`result_path` MUST equal `/cd/v2/lookups/` followed by that identifier. A malformed identifier
receives `400 invalid_request`; a well-formed identifier without an available
result receives `404 result_unavailable`.

Retry delays, in both HTTP headers and JSON, are integers from **1 through
604,800 seconds (7 days)**. Seven days is 604,800,000 milliseconds, so a single
delay fits a signed 32-bit millisecond timer; clients need no staged timers.
Use monotonic elapsed-time comparisons that handle the platform clock wrapping.
Malformed required JSON fields fail the current attempt. A valid body delay is
authoritative; clients do not compare it with the header. Preserve the applicable
cooldown before allowing a later attempt.
If an upstream delay is longer, the service advertises at most seven days while
continuing to enforce the full saved provider deadline. A request after that
advertised interval may receive another bounded delay without provider work.

No-match `expires_in_seconds` uses the same positive integer range but describes
the remaining cache lifetime. It is informational, not a request retry delay.
Clients must not interpret it as an instruction to poll or resubmit the TOC.

## 2. Endpoint overview

| Method | Path                       | Purpose                                          | Can start recognition work?     |
| ------ | -------------------------- | ------------------------------------------------ | ------------------------------- |
| `GET`  | `/cd/v2/service`           | Verify the service and protocol are available    | No                              |
| `POST` | `/cd/v2/lookups`           | Submit a TOC or obtain an existing result for it | Yes, if admitted and not cached |
| `GET`  | `/cd/v2/lookups/{disc_id}` | Fetch an accepted lookup's state/result          | No                              |
| `GET`  | `/covers/{filename}.jpg`   | Download a prepared cover                        | No                              |

GET requests never need the TOC or a current track number. A track change is a
local presentation operation once the disc metadata has been obtained.

## 3. Service availability check

After the local network has a usable address, send:

```http
GET /cd/v2/service HTTP/1.1
Host: 203.0.113.10:12890
Accept: application/json
Accept-Encoding: identity
Connection: close

```

Successful response: HTTP `200` with:

```json
{
  "protocol": 2,
  "service": "stroom-cd-recognition",
  "ready": true
}
```

Validate the status, protocol, service identifier and readiness flag. An unrelated
HTML page or a successful TCP connection alone does not enable recognition.
The check must not query MusicBrainz, GnuDB, CTDB or Discogs. It confirms that the
application can accept this protocol, not that every provider is healthy.

Default client timeout: **3 seconds total**, including connecting, writing and
reading. On failure, do not submit the TOC. Never wait indefinitely during startup.
The PS2 recovery policy is described in section 10.

If the application cannot accept this protocol, it returns `503 service_unavailable`
with the error envelope, rather than `200` with `ready: false`. HTTP `429` is also
possible when the caller's request allowance is exhausted. A successful check is
local client state; it creates no server session or permission token. The server
does not keep a per-client list of who has probed.

The probe endpoint is an application handshake. This endpoint does not introduce
an external monitoring system or require providers to participate in health checks.

## 4. Submit a disc

### Request

Send `POST /cd/v2/lookups` with this JSON body:

```json
{
  "type": "stroom-cd-toc",
  "first": 1,
  "offsets": [150, 15363],
  "leadout": 32314
}
```

| Field     | Type          | Meaning                                           |
| --------- | ------------- | ------------------------------------------------- |
| `type`    | string        | Exactly `stroom-cd-toc`                           |
| `first`   | integer       | Exactly `1`; submitted audio tracks start at 1    |
| `offsets` | integer array | Start of each audio track, in absolute CD sectors |
| `leadout` | integer       | Audio-session lead-out, in the same sector units  |

The URL path selects version 2; the request body has no version field.

A CD sector is 1/75 second. Values include the 150-sector lead-in. If the device
API reports LBA offsets starting at zero, add **150 to every track start and the
lead-out** before submission. Do not submit milliseconds, sample positions or
sector differences. The lead-out is the first sector after the audio program
(an exclusive end). If a source instead reports the last included audio sector,
add one before applying any LBA-to-absolute conversion.

Validation rules:

- Between **1 and 99** offsets, with one offset per track in playback order.
- All positions are integers. The first offset is at least 150.
- Offsets strictly increase; lead-out is greater than the last offset.
- Lead-out is at most **450,000 sectors**, matching the current 100-minute bound.
- Unknown request fields MUST be ignored after the body-size check. They must not
  be interpreted as network destinations, filenames or provider URLs.

There is no per-track request field, client IP field, callback address or
`service` address field. Local disc-generation counters remain client state and
are not part of this request.

The submitted TOC describes only the audio program. Clients inspect track types
and sessions before constructing it; this API has no track-type fields and cannot
detect a data track from its numeric offset alone.
Clients supporting CD-Extra MUST apply [appendix B](#appendix-b-disc-types-and-cd-extra-normalization)
before submission. Data-first and other unsupported mixed layouts MUST NOT be
submitted. The PS2 currently rejects entries marked as data and does not perform
CD-Extra conversion.

### Immediate result or accepted work

The server validates the complete bounded body, derives the disc identity, then
checks existing work and saved outcomes before admitting new provider work.

- **HTTP 200:** a completed `matched` or `no_match` result is available.
- **HTTP 202:** the lookup is accepted and still queued or running.
- **HTTP 400/413/415:** the request is invalid, oversized or has an unsupported
  media type. Correct the client; do not automatically repeat it unchanged.
- **HTTP 429/503:** request or work capacity is temporarily unavailable. Honor
  the JSON retry delay; this POST has not been acknowledged as accepted. The presence
  of a `disc_id` in an error body does not change that.
- **HTTP 502:** recognition encountered a temporary provider failure. This can
  also be a saved, unexpired failure outcome.

A POST `202` response includes `Retry-After: 5`. Clients follow the JSON
`result_path`. Example body:

```json
{
  "protocol": 2,
  "disc_id": "R5ZoxkYIj.9IlgyFd.SaQ9dcXnM-",
  "status": "pending",
  "result_path": "/cd/v2/lookups/R5ZoxkYIj.9IlgyFd.SaQ9dcXnM-",
  "retry_after_seconds": 5
}
```

This identifier corresponds to the exact TOC in the example. It is a disc key,
not a secret, an authentication credential or a unique client session.

`202` is the only acknowledgement that an asynchronous submission was accepted.
`200` is an already completed result. `502` is a failed outcome, even when it
contains a disc identifier. Clients MUST NOT infer acceptance from an identifier,
retry delay or successful transport alone. Clients use the body’s `result_path`
to fetch an accepted lookup.

### Identity and duplicate submissions

Clients can treat `disc_id` as opaque and follow `result_path`; computing it is
not necessary. Compatible servers MUST derive it as specified in
[appendix A](#appendix-a-disc-id-derivation).

Concurrent requests for the same TOC share one recognition job. If a POST reaches
the server but its response is lost, a later retry may send that TOC again; the
server must reuse the job or cached outcome. Once-per-CD is a client policy, not
an exactly-once network-delivery guarantee.

## 5. Fetch an accepted lookup

After receiving `202`, wait at least the body’s `retry_after_seconds`, then GET
`result_path`.
Never resend the TOC merely because a lookup is still pending.

The result endpoint returns:

| HTTP status    | JSON `status` / error            | Client action                                                |
| -------------- | -------------------------------- | ------------------------------------------------------------ |
| `202`          | `pending`                        | Wait, then GET the result again                              |
| `200`          | `matched`                        | Store the complete disc result locally                       |
| `200`          | `no_match`                       | Stop recognition for this inserted CD                        |
| `502`          | `error`, `provider_failure`      | Keep playback working; apply failure/retry policy            |
| `404`          | `error`, `result_unavailable`    | Result was lost, evicted or is unknown; apply failure policy |
| `429` or `503` | `error`, admission/capacity code | Retain the accepted disc ID and wait before another GET      |

A GET can load a saved outcome but must not create a new recognition job. After
a restart, persisted results can still be returned. An unfinished job that did
not produce a saved result may return `404`.

Completed matches have no protocol TTL; they remain readable while retained in
RAM or on disk. No-match and failed outcomes are readable only until their saved
expiry, after which a GET returns `404 result_unavailable`. A GET never renews
that expiry. A later POST can admit fresh work after expiry, while a POST before
expiry returns the saved outcome with its remaining cache lifetime or failure
retry delay, as appropriate. Eviction or restart
can also make a result unavailable before a client retrieves it.

Pending responses use the same envelope as the submission response. The default
normal polling interval is **5 seconds**, with nonnegative client jitter of up
to one extra second. Never shorten the server's requested delay.

Default client bounds: **5 seconds per metadata HTTP exchange** and **5 minutes
for one accepted lookup's overall waiting period**, including queue time. On
transport failure or overall expiry, the PS2 suspends this attempt until the next
track change; it does not enter a timed TOC-resubmission loop. Other applications
can offer an explicit retry action after the advertised delay.

**Recognition admits at most three waiting jobs plus one running job.** A new
uncached submission receives `503 busy` with a 15-second retry delay when the
waiting queue is full. Cache hits and requests joining accepted work still
succeed without using another queue slot or recognition token.

Each admission key may hold **one waiting recognition job**, plus a running job
if it owns the worker. Accepted waiting jobs are served round-robin by key, so
refilling a running caller cannot skip another caller’s admitted turn.

This protects queue space from a small number of callers; it is not guaranteed
admission under sustained overload. Three or more keys can fill the waiting queue
and compete for newly free slots, so other uncached submissions may continue to
receive `503 busy`. This is a known capacity limit of an unauthenticated service
with one recognition worker. Round-robin ordering protects already admitted work;
it does not reserve future admission for callers whose requests were rejected.

With the default service budgets, the last admitted lookup can require about
four minutes of active processing including its own recognition. This leaves room in
the default five-minute client window under ordinary storage conditions. A
`202` acknowledges admission, not a completion SLA: final persistence,
cancellation cleanup and network delays can still exceed that window. See the
[worker budgets](implementation.md#worker-budgets) for implementation details.

The client ending its five-minute wait does not cancel the server job. On the
next eligible retry event, GET the known result path first; it may now return
`matched`, `no_match` or still `pending`. Do not submit another TOC solely
because the earlier waiting window expired.

Start the overall waiting clock when the first `202` is received. Later `202`,
`429` and `503` replies do not restart it. Each exchange's deadline is the smaller
of five seconds and the overall time remaining; do not start a request after the
overall deadline. A server delay extending beyond that deadline suspends this
attempt while preserving the longer cooldown for future retry events.

## 6. Completed metadata

### Recognized disc

HTTP `200` example:

```json
{
  "protocol": 2,
  "disc_id": "R5ZoxkYIj.9IlgyFd.SaQ9dcXnM-",
  "status": "matched",
  "track_count": 2,
  "album": "Example album",
  "artist": "Example artist",
  "tracks": [
    { "number": 1, "title": "First track", "artist": "" },
    { "number": 2, "title": "Second track", "artist": "Guest artist" }
  ],
  "artwork": {
    "status": "pending",
    "retry_after_seconds": 5
  }
}
```

These are display labels, not a guarantee of a particular pressing or edition.
The response contains only the selected disc/medium's tracks. Provider raw data,
rejected candidates, detailed comparison scores and server report paths are not
part of the client API.

Validation and display rules:

- `track_count` equals the submitted offset count, and `tracks` has that length.
- Track `number` values are contiguous integers from 1 through `track_count`.
- `album`, root `artist`, track `title` and track `artist` are strings. Each is
  at most **127 UTF-8 bytes after JSON decoding**, excluding a C terminator.
  Servers truncate only at complete Unicode character boundaries.
- An empty track artist means use the root album artist. Empty display labels
  are permitted; the application can keep its usual fallback label.
- Validate the entire result before replacing the existing local disc table.
- Only publish a result if it still belongs to the locally active disc attempt.
  Ejecting/replacing the disc cancels or invalidates outstanding responses. A
  track change on the same disc must not invalidate a disc-wide result.
- On every track change, read the corresponding entry from this local table.
  No recognition request, provider request or TOC resend is needed.

Store only the currently inserted disc if memory is constrained. A compact table
of 99 titles and per-track artists needs at most 25,344 bytes for two 128-byte
string buffers per track, plus album information, artwork state and bookkeeping.
This is not a bound on temporary JSON parsing memory. Keep receive buffers and
parser allocations bounded; do not place a maximum-size response on the PS2's
16 KiB recognition-worker stack.

### No acceptable match

HTTP `200` example:

```json
{
  "protocol": 2,
  "disc_id": "R5ZoxkYIj.9IlgyFd.SaQ9dcXnM-",
  "status": "no_match",
  "expires_in_seconds": 86400
}
```

This is an explicit completed result, not a missing response. Keep the normal
fallback labels and stop recognition for this insertion, including on track
changes. `expires_in_seconds` is the **remaining** server no-match-cache lifetime;
86,400 seconds is the full 24-hour example. It is not a polling interval or an
instruction to retry recognition. A different application may explicitly refresh
after expiry. The PS2 validates the field, then ignores its value for scheduling:
this insertion is complete, with no automatic daily or track-change retry.

## 7. Artwork

The metadata response's `artwork` object has one of these shapes:

```json
{
  "status": "available",
  "path": "/covers/R5ZoxkYIj.9IlgyFd.SaQ9dcXnM--release.jpg"
}
```

```json
{
  "status": "pending",
  "retry_after_seconds": 5
}
```

```json
{ "status": "unavailable" }
```

An available path is a same-origin relative path under `/covers/`, never an
upstream provider URL or local filesystem path. For the PS2, the resolved image
URL must fit **319 bytes**, excluding its terminating NUL.

For pending artwork, wait at least its `retry_after_seconds`, then GET
`/cd/v2/lookups/{disc_id}` again. HTTP `200` returns the complete matched result
from section 6, including the current `artwork` object. Its state can remain
`pending`, become `available`, or become `unavailable`. Keep the locally stored
labels while polling; artwork failures never restart recognition.

The default delay is 5 seconds while work is active. Provider cooldowns can
request a longer delay. A lookup GET for a retained match may schedule eligible
artwork-only recovery using saved metadata. Repeated requests share the artwork
job and cannot bypass its saved retry deadline. The GET never starts recognition.

Saved artwork cooldowns survive RAM eviction and restart while the saved result
is retained; unsaved state has no such guarantee. See the
[persistence details](implementation.md#artwork-retry-persistence).

If a lookup GET returns `404 result_unavailable`, stop artwork recovery and retain labels;
do not POST the TOC just to recover artwork. Other unexpected lookup states after
a match fail the artwork request without replacing the stored labels.

Temporary artwork provider/conversion failures remain `200 matched` with
`artwork.status: "pending"` and a delay. Confirmed absence uses `unavailable`.
HTTP admission errors can still return `429`/`503`.

For an available cover, GET its `path` with `Accept-Encoding: identity`:

- Success is HTTP `200`, `Content-Type: image/jpeg`, with a known content length.
- Prepared covers are baseline JPEG, at most **512 KiB (524,288 bytes)**, with
  dimensions no larger than **600 × 600**. Preserve aspect ratio and do not
  enlarge smaller originals. The server lowers JPEG quality or dimensions when
  necessary to meet the byte limit. Clients still validate image data.
- Client and server use a **30-second total download deadline**; the server
  deadline includes file access and sending.
- Track changes retain the cover and do not cancel an unchanged disc/path
  download. Disc replacement cancels it.

### Artwork failure recovery

Keep **one request in flight**. Apply these rules to artwork lookup polls and
image downloads:

- On timeout, disconnect, invalid response or another failed request, keep labels
  and wait at least **5 seconds**, or the server’s longer applicable delay.
- Count **three failed artwork requests without obtaining a usable image**, then
  suspend until the next track change or explicit retry action after cooldown.
  Successful pending/available status responses and changed image paths do not
  reset the counter. A permitted retry event resets it. Downloading a usable
  image completes the work.
- A valid `429`/`503` pauses requests for the advertised origin delay; it does
  not count as a failed artwork request.
- An image `404` counts as one failure. After the delay, refresh the lookup’s
  artwork state before another image download. It uses the same failure counter.
- A malformed, oversized or unsupported image is rejected for this insertion.
  Never download that path again, including after track changes. Suspend artwork
  until a later retry event can discover another path. The PS2 remembers up to
  eight rejected paths; after eight it stops artwork work for that insertion.
- A lookup `404`, `unavailable`, eject or cancellation stops artwork work.
  Completed metadata remains available locally.

Pending artwork polling follows server delays and does not use the five-minute
metadata waiting deadline. There is no further TOC submission for artwork.

## 8. Errors and retry delays

API error responses use this envelope; connection-level rejection can close the
socket before an HTTP response exists:

```json
{
  "protocol": 2,
  "status": "error",
  "error": {
    "code": "rate_limited",
    "message": "Request allowance exceeded",
    "retryable": true
  },
  "retry_after_seconds": 6
}
```

An error associated with a known lookup also includes `disc_id`. Clients branch
on status and `error.code`, not human-readable `message` text.
An error on POST never acknowledges submission acceptance, even with `disc_id`.
Valid HTTP error responses on cover routes use the same JSON envelope. An image
client reads JSON errors and their body delay; it must not pass the error body
to an image decoder. Unknown future error codes use the
HTTP status and `error.retryable` policy; unknown success-state values are
protocol errors.

| HTTP status | `error.code`             | Meaning                                                          |
| ----------- | ------------------------ | ---------------------------------------------------------------- |
| `400`       | `invalid_request`        | Invalid JSON, TOC, identifier or field type                      |
| `404`       | `result_unavailable`     | Lookup result is not retained                                    |
| `404`       | `not_found`              | Unknown endpoint or image path                                   |
| `405`       | `method_not_allowed`     | Wrong method; include `Allow`                                    |
| `413`       | `request_too_large`      | Body exceeds the request bound                                   |
| `415`       | `unsupported_media_type` | Unsupported body type/encoding                                   |
| `429`       | `rate_limited`           | Per-IP HTTP allowance exhausted                                  |
| `503`       | `busy`                   | Global HTTP, lookup admission, job or cover capacity unavailable |
| `503`       | `service_unavailable`    | Application temporarily unable to accept this protocol           |
| `502`       | `provider_failure`       | Recognition failed temporarily, including a job deadline         |

`429`, `503` and `502` errors set `error.retryable: true` and include both
`Retry-After` and top-level `retry_after_seconds`. Servers normally emit the same
positive integer in both; JSON clients use the body without checking equality.
Other listed API errors set `error.retryable: false` and omit both retry fields.
Here `retryable` describes repeating the **failed request**, not every possible
future action. In particular, `404 result_unavailable` terminates the current GET
attempt, but a later client retry event may submit a TOC again. An image `404`
uses the separate artwork recovery rule above. A wrong-route error never starts
a TOC submission.

Retry field placement is fixed:

| Response                                      | Delay fields                                                                                       |
| --------------------------------------------- | -------------------------------------------------------------------------------------------------- |
| Pending lookup (`202`)                        | Top-level `retry_after_seconds` is authoritative; header is also supplied                          |
| Matched metadata with pending artwork (`200`) | Only nested `artwork.retry_after_seconds`; no retry header for completed metadata                  |
| No-match (`200`)                              | Only top-level `expires_in_seconds`, describing remaining cache lifetime; no retry field or header |
| Retryable API error                           | Top-level `retry_after_seconds` is authoritative; header is also supplied                          |

Remaining cache lifetimes are rounded **up** to whole seconds, with a minimum of
one while unexpired. Do not return an expired negative outcome with a zero lifetime.

Wait at least the advertised interval, measured using a monotonic local clock.
Use the JSON delay when the response is valid; otherwise retain a valid header
delay as a fallback. No header/body comparison is required. Positive jitter is
permitted. Track changes must not bypass a server-imposed
cooldown. Transport failures have no reliable server retry instruction; apply a
bounded client policy rather than continuously reconnecting.

A provider failure is cached for at least **60 seconds**, or until a longer
provider cooldown expires. It must never be represented as `no_match`. Returning
a cached failure must report its remaining delay up to the seven-day wire cap,
without resetting the saved expiry.

Delay scope is also explicit:

- `429` or `503`: conservatively pause this client's further requests to the same
  service origin until the delay expires, including probes and artwork calls.
- `502 provider_failure`: delay a new recognition attempt for that disc; this
  does not invalidate completed metadata or prevent a different disc's lookup.
- Pending lookup (`202`): delay the next GET for that accepted lookup.
- Pending artwork: delay the next lookup GET used to refresh artwork for that disc.
- No-match `expires_in_seconds` is cache information, not a request cooldown.
  The PS2 stops recognition for that insertion; other clients may explicitly
  refresh after expiry. It imposes no pause on unrelated requests.

## 9. Server admission and resource limits

The service uses these fixed admission limits. Operators configure disk-cache
capacity and cover upload speed in [config.json](config.json); clients must honor
busy responses and retry delays.

| Resource                  | Limit                                                                            |
| ------------------------- | -------------------------------------------------------------------------------- |
| TCP connections           | 64 globally; 8 per public IPv4 address, including silent connections             |
| Complete incoming headers | Within 5 seconds of TCP acceptance                                               |
| Accepted POST body        | Complete bounded body within 5 seconds of TCP acceptance                         |
| Global HTTP requests      | 20/second; burst of 40                                                           |
| Active cover transfers    | 8 globally; 1 per IP (fixed), counted through response/file cleanup              |
| Cover GET allowance       | 10/minute per IP; burst of 5                                                     |
| New uncached recognition  | At most 1 waiting per key, plus its running job; burst of 2, refill 1/10 seconds |
| Queued recognition jobs   | 3 waiting globally; additional uncached submissions get 503                      |

Memory and cache capacity can also cause `503 busy` or result eviction. Their
defaults and accounting are documented in the [implementation notes](implementation.md#memory-and-disk-cache).

The general per-IP HTTP allowance is **60 requests/minute, burst of 10**, with
an additional, separate cover-GET allowance of **10/minute, burst of 5**.
All HTTP requests consume the general allowance, including probes, status reads
and invalid paths. Per-IP rejections do not consume the shared global allowance.

Checking an existing job, reading a cached outcome, probing availability or
reading artwork through the lookup GET does not spend a new-recognition token.
POST admission must occur only after the full TOC is validated and cache/shared-job checks have
completed. Body limits/timeouts still apply before job admission. GET requests
never bypass recognition admission by creating fresh recognition work.

Limits apply to public IPs, so applications behind one router share allowances.
Per-IP tracking remains bounded; active work and spent allowances must not be
forgotten merely to admit a new caller. Server caching is an optimization, not a
promise that every result/path exists permanently. Cover bodies share a default
10 MiB/s upload limit with a 1 MiB burst; pacing retains each active slot and its
transfer deadline. Hosting resource limits and monthly traffic budgets remain
deployment work. KiB/MiB denote binary
units. Hosted operators publish any changed deployment limits with the base URL.

## 10. PS2 client profile

This section supplies playback-specific policy. Other applications use the same
wire API but can expose explicit retry/cancel controls instead of track events.

### Startup

1. Read `lookup_host`, the configured service IPv4 address. An empty value disables
   recognition. Connect on fixed TCP port 12890, as specified in section 1.
2. Initialize local networking during the existing initializing phase. A successful
   network-module startup does not mean asynchronous DHCP has already finished.
3. Once an IPv4 address and an active Ethernet link are available, perform the
   bounded `/cd/v2/service` check. An assigned address alone is insufficient.
4. A valid successful response enables a TOC submission when an audio CD is ready.
5. A failed check does not send the TOC or block playback indefinitely.

**Default recovery policy:** after a failed startup check, retry
the check once on the next track change (and on insertion of another CD), rather
than disabling recognition for the entire session. Only after a successful check
can that event cause a TOC submission. Do not run an automatic timed probe loop.
If DHCP completes after the initializing screen ends, the check must still be
possible in background work; it must not wait forever behind the startup screen.
The first transition to a usable local IP with an active link triggers the initial probe even if the
screen has already changed and no track change occurs. Until that transition,
there has been no failed service attempt to retry. Once a probe succeeds, reuse
that verification across tracks/discs in the same app session and service origin.
A transport/protocol failure or `503 service_unavailable` invalidates it; a valid provider
failure or rate-limit response does not. Probe again before any later TOC POST
when verification has been invalidated.

The PS2 enables nonblocking socket mode before connecting and polls without
blocking network waits. It enforces exchange deadlines between SDK socket calls and checks elapsed
time again before accepting a reply. A single synchronous SDK RPC cannot be
interrupted by this client. Recognition runs on its background worker; playback
and the initializing screen do not wait for a recognition response.

### Cooldowns

For the PS2’s one active disc, keep two cooldown timestamps:

- `origin-blocked-until`: applies to every request, including probes; preserve it
  across disc changes.
- `disc-next-request-not-before`: applies to requests for this disc, covering
  recognition, lookup polling and artwork. Extend it when any disc delay arrives.

A request waits until both deadlines permit it. These cooldowns are separate from
the active request timeout and the overall metadata waiting deadline. Cooldown
expiry permits a request; it does not create a new PS2 retry event.

The PS2 accumulates unsigned differences between successive 32-bit millisecond
ticks into a 64-bit elapsed clock, then stores both cooldowns in that clock.
Expired deadlines cannot become future deadlines after a raw tick wrap. Clients
using wrapping 32-bit deadlines instead MUST clear a cooldown once it is observed
expired; retaining it indefinitely can make it appear active again after wrap.

### Per-disc lifecycle

| State                               | Trigger / next action                                                                            |
| ----------------------------------- | ------------------------------------------------------------------------------------------------ |
| Service not verified                | Probe first; no TOC submission                                                                   |
| Disc ready, no attempt              | Submit its TOC once                                                                              |
| Accepted and pending                | Fetch the saved result path after its delay; track changes do not resubmit                       |
| Metadata matched                    | Retain all tracks; track changes only update displayed labels locally                            |
| No match                            | Retain that outcome for this insertion; no track-change lookup                                   |
| Metadata transport/provider failure | Suspend the attempt; retry on a later track change, after any cooldown                           |
| Artwork pending/downloading         | Continue only artwork status/download work; keep metadata                                        |
| Artwork suspended                   | A later track change starts an artwork-only retry; previously rejected image paths stay rejected |
| Disc removed/replaced               | Cancel old requests, invalidate old replies, release the old disc table/cover                    |

The retry opportunity is an actual change of track identity, not every frame,
seek within a track or repeated pause/resume. If a new submission receives `202`,
it may simply rejoin an earlier job whose response was lost. If the result path
is still known, try that result first rather than resending the TOC.

For suspended metadata, on a retry event after both cooldowns, re-probe if
unverified. Once verified, make one recovery GET if a result path is known, or
one POST of the TOC if it is not. A recovery GET returning `404` permits
one POST during that same event. There is at most one recovery GET and one POST
per event; accepted work resumes normal polling. A recovery GET returning
`429`/`503` keeps the result handle and waits for the next eligible retry event
after its cooldown. A `502`, or a `404` during
normal polling, discards the result handle and ends the attempt. Any next POST
waits for a later eligible retry event, preserving the server delay.
Completed matches and `no_match` never restart recognition.

A track change before a cooldown expires does not schedule a delayed TOC POST;
another track change or explicit user action after expiry is needed. Once a
recovery GET/POST returns `202`, it starts a new bounded waiting period. Normal
polling, including `429`/`503`, never restarts the existing five-minute clock.

Failed artwork never makes a completed metadata lookup eligible for another TOC
submission. Track changes do not clear server busy/provider delays. Local disc
identity should survive temporary UI/source-status changes when the same inserted
disc is still valid; stale asynchronous responses must never update a replacement
disc. Eject/reinsert starts a new local insertion lifecycle, even for the same TOC.

## 11. Example client for any platform

The [standalone Node.js client](examples/recognize.mjs) demonstrates the
HTTP v2 handshake, single submission and result-following flow. It requires
Node.js 24 LTS and uses built-in APIs with no PS2 code. Its header explains
how to run it. Supply an operator-provided base URL and replace its sample TOC
with your disc's complete TOC.

For a production client, apply all schema/text bounds above and persist retry
state in the application's state machine. The example deliberately stops on
failed submissions and transport/provider errors, leaving the later retry event
to its caller. It does not implement artwork transfer; section 7 specifies that
independent flow. Other applications use the same requests with their platform's
HTTP library.

## 12. Direct public hosting

Run the service on a host reachable at a public IPv4 address, and allow inbound
TCP 12890. Configure the PS2's `lookup_host` with that numeric address only;
the port is fixed at 12890. Recognition and cover downloads use this same HTTP
endpoint. See section 9 for connection and request limits.

Plain HTTP does not encrypt traffic or authenticate the server. The service
check establishes readiness and protocol version. Outbound provider requests
continue to use their provider URLs independently of this client connection.

Publish the real base URL and protocol version before distributing clients.
Configure process supervision and read the service's monitoring through
the local journal.
On Linux, [the background-service installer](README.md#install-as-a-background-service-linux)
runs under the operator's existing account and configures systemd restart and
resource limits. It works for local and public service installations.

## Appendix A. Disc-ID derivation

This appendix is normative for compatible service implementations. Clients may
treat `disc_id` as opaque. Servers derive it from the validated, normalized audio
TOC using the MusicBrainz disc ID convention:

1. Concatenate uppercase zero-padded hexadecimal fields: first track (2 digits),
   last track (2 digits, equal to the offset count), lead-out (8 digits), then
   exactly 99 offsets (8 digits each, padding absent offsets with zeros).
2. SHA-1 hash the ASCII bytes of this string.
3. Base64 encode the digest; replace `+` with `.`, `/` with `_`, and `=` with `-`.

## Appendix B. Disc types and CD-Extra normalization

This appendix is normative for clients supporting CD-Extra. The submitted TOC
describes only the audio program. Inspect track types and sessions before
constructing it; numeric offsets alone cannot identify a data track.

For a confirmed two-session **CD-Extra / Enhanced CD** with audio tracks starting
at track 1 and a trailing data session:

1. Include only the audio-track starts in `offsets`; omit every data track.
2. If the source supplies an unadjusted start for the first data track, set
   `leadout = data_track_start_absolute - 11400`.
   For an LBA source, this is `data_track_start_lba + 150 - 11400`.
   For example, data starting at LBA 50,000 gives an absolute audio lead-out of
   38,750 sectors. Keep audio-track starts in the same absolute sector units.
3. If the source already supplies the corrected audio-session TOC, such as a
   libdiscid result, use it directly. **Do not subtract 11,400 a second time.**
4. Validate the normalized TOC against the ordering/count/lead-out limits in
   [section 4](#4-submit-a-disc). If track types or the required session boundary
   are unknown, fail locally; do not guess or submit the physical data-session
   lead-out as the audio end.

This adjustment follows [MusicBrainz's CD-Extra calculation](https://musicbrainz.org/doc/Disc_ID_Calculation).
It is specific to that session layout. Discs with data first, interleaved data
and audio, or other unsupported session layouts are out of scope; clients MUST
NOT submit them by renumbering tracks or applying this subtraction blindly.

**PS2 limitation:** its current TOC parser rejects entries marked as data and
has no CD-Extra session conversion. This protocol rule enables other clients to
submit a normalized audio TOC; it does not add Enhanced-CD playback to Stroom.
