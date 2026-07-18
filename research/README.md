# research/ — Rivian status project evidence base

Primary research gathered 2026-07-17 (Claude Code deep-research + a focused protocol dig).
This is the **raw evidence** behind the design decisions in `plans/01-rivian-status-plan.md`.
When something in the plan needs justification — or when the unofficial API breaks and you
need to re-derive it — start here.

> **Everything about Rivian's API is unofficial and reverse-engineered.** Rivian publishes no
> public developer API. Treat every endpoint/header/field below as "true as of 2026-07-17,
> may change without notice." The maintained community client (`bretterer/rivian-python-client`)
> is the closest thing to a spec — diff against its upstream if things stop working.

## Contents

- **`rivian-cloud-api-protocol.md`** — the concrete protocol: endpoints, the 4-step auth
  handshake (CSRF → login → OTP → re-CSRF), exact GraphQL mutations/queries, the header matrix,
  telemetry fields, TLS/cert facts, and ESP32 feasibility notes. *This is the file you code
  against.* Sourced from `bretterer/rivian-python-client` `main` + RivDocs.
- **`deep-research-report.md`** — the broader survey: verified findings (with confidence +
  vote tallies), refuted claims (things that sound true but failed verification), open
  questions, and the full source list with quality ratings. Answers "what are ALL the options
  and which did we rule out and why."

## The decision, in one paragraph

Use Rivian's **cloud GraphQL App API** directly from the ESP32-S3 (read-only telemetry).
It's **ruled IN**: the endpoint is behind AWS CloudFront (no bot/JS challenge), auth is plain
JSON POSTs, and with no display the ESP32 has ample heap for TLS. **Local/BLE is ruled OUT**
(needs cloud-bootstrapped ECDH phone-key enrollment + HMAC signing, no turnkey ESP32 project,
and probably no live charge telemetry anyway). Read-only means **no crypto at all** — just
login + `getVehicleState`. See the plan for the full build.

## Key facts future sessions keep needing

- **Endpoint:** `POST https://rivian.com/api/gql/gateway/graphql`
- **Auth:** CreateCSRFToken → Login → (LoginWithOTP if MFA) → `userSessionToken`. **No refresh
  mutation** — on expiry, re-run CreateCSRFToken reusing `u-sess`; full re-login only as last resort.
- **Headers:** base (`User-Agent: RivianApp/707...`, `Apollographql-Client-Name:
  com.rivian.ios.consumer-apollo-ios`, self-gen `dc-cid`) + `Csrf-Token`/`A-Sess`/`U-Sess`
  per the matrix. **No `dc-sess` header exists** (older-API conflation).
- **Telemetry fields:** `batteryLevel`, `chargerState`, `chargePortState`, `distanceToEmpty`
  — each returns `{timeStamp, value}`. Request only the subset you need.
- **TLS:** Amazon Root CA 1, **pin the root not the leaf** (leaf rotates ~200 days).
- **Cadence:** poll 30 s, exponential backoff capped at 900 s. API rate-limits (`RATE_LIMIT` code).
- **UNCONFIRMED:** `distanceToEmpty` units (km vs miles) and exact token lifetimes. Verify units
  live before trusting the low-range threshold.
