# IPC Milestone 2A Plan

## Proposed File Structure
- `main/kernel/core/ipc/ipc_core.c` + `.h`: shared core abstractions (handle table, waiter lists, error codes).
- `main/kernel/core/ipc/ipc_scheduler_bridge.c` + `.h`: interfaces between IPC wait/wake logic and Magnolia scheduler/timer.
- `main/kernel/core/ipc/ipc_signal.c` + `.h`: signal primitive implementation and waitset hooks.
- `main/kernel/core/ipc/ipc_diag.c` + `.h`: diagnostics helpers for IPC objects (starts with signals).
- `main/kernel/core/ipc/tests/`: unit tests covering core functionality and signal behavior.

## Shared Helpers and IPC Object Abstraction
- `IpcObject` base struct with header (handle, type enum, generation counter, destroyed flag).
- Handle table managing registration, generation verification, lookups via `ipc_validate_handle` and `ipc_get_object` helpers.
- Uniform waiter list supporting enqueue/popping for waiters and destruction handling.
- Shared error codes (INVALID_HANDLE, DESTROYED, TIMEOUT, NOT_READY, SUCCESS).

## API Surface
### Core IPC Object Management
- `ipc_register_object`, `ipc_unregister_object`, `ipc_mark_destroyed`.
- Validation helpers returning typed pointers and generation checks.
- Common waiter list operations for signal/event primitives.

### Signals
- `ipc_signal_create`, `ipc_signal_destroy`, `ipc_signal_set`, `ipc_signal_clear`, `ipc_signal_wait`, `ipc_signal_try_wait`, `ipc_signal_timed_wait`.
- Modes: one-shot vs counting, controlling pending/counter state.
- Hook for waitset readiness and subscribe/unsubscribe callbacks.

### Scheduler/Wake Integration
- `ipc_wait_block` and `ipc_wait_timeout` bridging Magnolia scheduler.
- `ipc_wake_one`, `ipc_wake_all` walking waiter list, honoring FIFO among same priority.
- Waiter context struct linking scheduler wait context with IPC waiter entries.

### Minimal Diagnostics
- Queries for each `IpcObject` (destroyed flag, waiter count).
- Signal-specific diagnostics: pending flag, counter value.
- Counters (optional) for creations, destructions, waits/timeouts.

### Unified Wake Path Vision
- All IPC primitives will use the same wait/wake helpers (`ipc_wait_block`, `ipc_wait_timeout`, `ipc_wake_*`).
- Signals and later event flags/waitsets will report readiness via waitset hooks, allowing the scheduler bridge to wake tasks deterministically.
- Destroy path wakes all waiters with DESTROYED result.

## Next Steps After Plan
1. Implement core/bridge files.
2. Add signal primitive atop the core abstractions.
3. Provide diagnostics + tests validating new behavior.
