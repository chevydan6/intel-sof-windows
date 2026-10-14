# PUJJOTEEN guarded amplifier control

`sofamp` binds to `ACPI\MX98360A` on the reviewed PUJJOTEEN board. It validates
exactly one translated GPIO connection and the captured 42-byte `_CRS`:
exclusive output-only pin 75 on `\_SB.PCI0.GPIO`. No guessed resource IDs or raw
pad-register writes are used. Its default is EN low on D0 entry and exit.

The original shutdown-only driver is recorded in
[the thermal investigation](../../docs/n100-amplifier-thermal-review.md). The
current source adds a diagnostic control interface and production-tested state
machine. It is **not a demonstrated repair of the speakers or a temperature
monitor**. No ordinary speaker playback is enabled by installing this package;
the parent still holds SSP1 speaker playback. Headphones use their separate path.

## Guarded operations

- QUERY and STOP are administrator diagnostics. QUERY reports state and latched
  faults; STOP requests EN low and releases stream ownership.
- START and RENEW reject user-mode requests, including administrator callers.
  Only a kernel client may request them. START waits the board's 5 ms delay;
  the caller must establish valid, quiet clocks/data first.
- START takes ownership of the caller's file and grants a 100 ms software
  lease. Renewal can extend that lease, but never past 250 ms from START in
  this diagnostic revision. Repeated START cannot extend an active lease.
- A separate kernel-only START_LISTEN command permits a fixed 2200 ms maximum
  for a two-second parent-controlled listening test. The 100 ms heartbeat,
  ownership and latched-fault rules still apply; ordinary START stays 250 ms.
  Status version 2 reports the supported listening maximum in MaximumMs.
- START_PREVIEW_30 and QUERY_PREVIEW_30 add a distinct 30200 ms cap for
  a thirty-second application test, retaining the 100 ms lease. Earlier commands
  keep their existing limits. This mode does not permit continuous playback.
- A separate kernel-only START_PREVIEW command supports an 8200 ms absolute
  ceiling for an eight-second application test. QUERY_PREVIEW advertises this
  capability while off; older drivers reject it before the parent can arm.
  The 100 ms heartbeat and all ownership/fault rules remain unchanged. Only
  these two new commands report MaximumMs=8200; legacy command replies and
  the two-second listening mode remain compatible.
- Expiration, owner-file cleanup, explicit STOP and D0 exit request shutdown.
  Closing a query handle does not interrupt another owner's lease.
- Transfer failures or expired leases latch a fault and block further enables
  until a device restart. A failed enable still attempts shutdown. A failed
  shutdown reports output state unknown, not off. Successful later STOP does
  not clear the fault.

An explicit passive wait lock serializes GPIO, request, file and timer state.
The passive one-shot timer is rearmed on renewal; an already queued early
callback rechecks the current deadline. Timer draining occurs outside the lock.
GPIO requests have a 100 ms timeout. These are **software deadlines**, not
independent electrical cutoff: scheduler delays, GPIO failure, a hung kernel,
and physical faults can defeat timely shutdown. The 250 ms budget is not a
certified maximum powered time or safe energy limit.

`OutputRequest` values are 0=unknown, 1=accepted low request, 2=accepted high
request. The GPIO reply echoes the requested bit; none of these values measures
pin voltage, amplifier temperature, speaker current or coil resistance.
Diagnostics expose accepted on/off counts, watchdog trips and separate fault and
cleanup statuses. Surprise removal closes resources without assuming GPIO is
still accessible or that the output retains its previous level.

## Validation and activation

`python3 tools/test-amp-lifecycle.py` compiles the actual state machine and
request/timer/cleanup callbacks with ASan/UBSan. It checks user-mode enable
rejection, ownership, late/slow/failed GPIO operations, expiration, renewal
limits, stale timer callbacks, and shutdown failure reporting.
`python3 tools/test-amp-hold.py` checks exact resource rejection and the parent's
held D0 callback. CI builds with the WDK and independently verifies the INF.
These tests do not measure the physical GPIO or prove a thermal repair.

A supervised powered speaker diagnostic needs a separately reviewed kernel
caller, fixed low-level PCM, valid SSP1 configuration, and independent user
observation for heat. Do not reinstall an old automatic-playback parent or use
an unbounded GPIO toggle to bypass this process.

References: [Linux MAX98357A/MAX98360A lifecycle](https://github.com/torvalds/linux/blob/v6.8/sound/soc/codecs/max98357a.c),
[GPIO write contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/gpio/ni-gpio-ioctl_gpio_write_pins),
[requestor-mode check](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfrequest/nf-wdfrequest-wdfrequestgetrequestormode),
[WDF timers](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/using-timers).
