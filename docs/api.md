# API and threading

The supported application API is namespace `decramp` in `DecrampEQ.hpp`.
`analog_eq_2sos` is a compatibility namespace for the earlier two-section
prototype designer. Its `detail` namespace and `decramp::detail` are internal and
may change independently of the public API.

When a Windows application includes `Windows.h`, define `NOMINMAX` to avoid its
global `min`/`max` macros conflicting with C++ standard-library algorithms.

## Objects

| Object | Purpose and ownership |
| --- | --- |
| `Band` | Plain input parameters; construct or copy on the control thread. |
| `Design` | A trivially copyable coefficient snapshot with up to 16 sections. |
| `design(rate, band)` | Bounded numerical fitting; call on a worker/control thread. |
| `isValid(design)` | Validate counts, finite values, and stable poles; no fit-tolerance policy. |
| `responseDb(design, rate, hz)` | Magnitude helper for immutable snapshots, usually for UI/control code. |
| `Processor<Channels, Sample>` | Apply a coefficient cascade with independent channel states. |
| `SmoothProcessor<Channels, Sample>` | Apply the newest target using bounded-history crossfades. |
| `DesignQueue<Capacity>` | Fixed-capacity SPSC handoff; exactly one producer and one consumer. |

Use a separate processor per EQ band and run them in series. Processors support
`float` and `double` samples, with `double` internal states. A valid zero-section
`Design` represents unity. Invalid designs are rejected without replacing active
coefficients. Validate a custom design before plotting or applying it; assigning
`valid=true` alone does not validate its contents.

## Shape conventions

- **Bell and shelves:** Cookbook analog Q, with ±24 dB gain. Shelf Q is not the
  Cookbook's shelf parameter S. A Q of 1/sqrt(2) corresponds to S=1. Zero gain
  represents unity.
- **Cuts:** `frequencyHz` is the joint normalized −3.0103 dB cutoff. Integer
  orders at default Q follow Butterworth placement. Other Q values adjust
  resonance. Internal prototype frequencies may fall below 20 Hz even though
  requested frequencies must be at least 20 Hz. A required prototype at or above
  Nyquist is unavailable. `gainDb` is unused.
- **Notch:** `frequencyHz` places the mathematical numerator zero. Q controls
  its analog width. Floating-point coefficients and sample arithmetic limit the
  measured null depth.
- **Tilt:** `frequencyHz` is the 0 dB pivot. `gainDb` is the gain difference
  between 20 Hz and `fitTopHz`, positive toward higher frequencies. Q is unused.
- **Band-pass:** The target is a cascade of analog second-order HP and LP with
  Q=1/sqrt(2), centered geometrically and normalized to 0 dB at the center. Band Q
  determines the two analog corner frequencies; it is not a resonant biquad Q.

All numeric input fields must be finite and satisfy the README's ranges, even
when a selected shape does not use one of them. `partialHighPass` is reserved for
the internal fractional-stage implementation and is rejected by the public
`design(rate, Band)` function.

## Realtime contract

1. Design coefficients on a worker/control thread. This has fixed iteration
   bounds and fixed-size storage, but can take several milliseconds.
2. Publish completed designs through one producer per `DesignQueue`. If `push()`
   returns false, retain the newest update and retry from the producer. Do not
   overwrite queue storage or let another producer write to it.
3. At an audio-block boundary, drain the queue and apply its newest result.
4. Call each processor's `set()`, `reset()`, and processing methods from that
   same audio thread, or while the host has stopped it. These objects do not
   synchronize concurrent method calls.

`process()` modifies `samples` entries in each supplied planar channel buffer.
The non-null buffers must be writable, large enough, and non-overlapping.
A null pointer skips that channel and retains its history. An out-of-range
`Processor::processSample()` channel is passed through. Inputs must be finite;
the library does not sanitize NaN/Inf audio or limit excessive custom gain.

`SmoothProcessor` reconstructs incoming states from at most 2,048 recent input
samples per channel, then crossfades. Its `set()` call can therefore have more
work than a normal block. This finite history is not a guarantee of a perfectly
matched state for every low-frequency/high-Q setting. At most one newest update
waits during a fade. Identical active coefficients do not start another fade.
Changing only accuracy metadata or inactive slots does not affect audio.

`reset()` clears audio history. For `SmoothProcessor`, it also completes any
transition at the newest accepted target. For `Processor`, it keeps the current
coefficients. Recompute coefficients after a sample-rate change, reset histories,
and choose the fade length in samples for the new rate. Do not reconstruct the
queue or processor every audio block.

## Responses and failure policy

`responseDb()` returns NaN for an invalid design or out-of-domain frequency/rate,
and negative infinity for a represented zero. It sums scaled section dB values,
so extreme finite response values do not depend on multiplying the entire
cascade's magnitude first.

`maxErrorDb` is measured at validation samples. For notch centers the common
analog/digital zero is canceled analytically and its limiting error is checked.
It is still not a mathematical uniform bound. A host should select its own
tolerance and explicitly handle a rejected/unavailable design. The realtime
example retains the last accepted target and reports rejection to its caller.
