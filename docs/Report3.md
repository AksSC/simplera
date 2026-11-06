# Report3

## Concurrency Control Technique Chosen

This phase implements **Strict Two-Phase Locking (S2PL)** with the **wait-die** deadlock prevention protocol at **page granularity**, as required by the specification.

### Why this technique was chosen

- It is one of the two explicitly allowed options in Phase 3.
- It integrates cleanly with the current SimpleRA command pipeline (`syntacticParse -> semanticParse -> executeCommand`) without changing existing command behavior.
- The simulation-only requirement is naturally supported by lock-state transitions and logging, without page data modification.

## Implementation Details

### 1) Command integration

Added a new command:

- `TRANSACTION <input_file>`

Integration points:

- Added `TRANSACTION` in `QueryType` and parsed fields in `src/syntacticParser.h`.
- Added dispatch in:
  - `src/syntacticParser.cpp`
  - `src/semanticParser.cpp`
  - `src/executor.cpp`
- Added declarations in:
  - `src/semanticParser.h`
  - `src/executor.h`
- Added implementation file:
  - `src/executors/transaction.cpp`

### 2) Parsing and semantic validation

Schedule format supported inside input file (stored in `/data`):

- `BEGIN T1`
- `READ T1 table_name page_number`
- `WRITE T1 table_name page_number`
- `COMMIT T1`

Semantic validation checks:

- Input file existence in `/data`.
- Each line conforms to one of the supported formats.
- `READ/WRITE` only for transactions that have already begun.
- Referenced table exists in loaded catalogue:
  - error: `SEMANTIC ERROR: Relation doesn't exist`
- Referenced page exists in table block range:
  - error: `SEMANTIC ERROR: Page doesn't exist`

### 3) Lock manager data structures (in-memory)

For each page:

- current lock type (`none/shared/exclusive`)
- lock owners (transaction IDs)
- wait queue of pending lock requests

For each transaction:

- transaction timestamp (used for wait-die ordering)
- status (`active/waiting/aborted/committed`)
- set of held pages
- deferred operations encountered while the transaction is waiting
- timestamp assignment follows logical step-time (first `BEGIN` at `t=1`, and each
  `BEGIN/READ/WRITE/COMMIT` step advances time by one unit)

### 4) Strict 2PL enforcement and wait-die

Before each page access:

- `READ` requests shared lock.
- `WRITE` requests exclusive lock (supports lock upgrade from shared to exclusive when safe).

Conflict handling:

- If request can be granted immediately, lock is granted and operation executes.
- Else apply wait-die:
  - requester older than conflicting holders -> requester waits
  - requester younger than any conflicting older holder -> requester aborts

Strictness:

- Locks are released **only** on `COMMIT` or `ABORT`.

### 5) Abort, rollback, unlock and restart

On abort:

- log `ABORT Tx`
- log `ROLLBACK Tx`
- release all held locks (`unlock(...) by Tx`)
- queue transaction for restart

Restart policy:

- Restarted transaction gets a fresh timestamp.
- Restart is logged as `Restart Tx [TS=...]`.
- Original transaction operations (excluding `BEGIN`) are replayed.
- Restart and replay also advance logical step-time per operation.

### 6) Output log generation

For input file `<name>.txt`, output is written as:

- `/data/<name> output.txt`

The log includes lock requests, grant/wait/die decisions, reads/writes, commits, aborts, rollbacks, unlocks, and restarts.

## Block Access and Memory Notes

- Concurrency metadata is maintained in memory per page (as required).
- Existing page access architecture (`cursor` and `bufferManager`) remains unchanged.
- This phase simulates decisions and does not modify page contents.
- Wake-up processing iterates over a stable page-key snapshot to avoid unsafe
  hash-table iterator invalidation during lock-map growth.

## Assumptions

- Schedule file contains one operation per line.
- Transaction IDs are treated as string identifiers.
- A restarted transaction reuses the same ID in log output with a fresh timestamp.
- Empty lines in schedule files are ignored.
- Since output format is manually evaluated, a deterministic, consistent internal log format is used.

## Testing Summary

The following schedules were executed after loading required tables:

- `txn_basic.txt`: single transaction read/write/commit.
- `txn_conflict_waitdie.txt`: shared->exclusive conflict, wait-die abort, restart.
- `txn_sample1.txt`: conflict pattern matching Phase 3 sample style.
- `txn_edgecases.txt`: multiple younger aborts, restart chain, begin-commit with no IO.
- `txn_multi_page.txt`: interleaved locks across two tables/pages.
- `txn_invalid_table.txt`: relation semantic error.
- `txn_invalid_page.txt`: page semantic error.
- `txn_invalid_format.txt`: malformed schedule semantic error.
- missing file case: schedule file semantic error.

Regression smoke checks also ran for existing features:

- table commands (`LOAD`, `PRINT`, `SORT`)
- phase 2 join command (`JOIN`)
- graph commands (`LOAD GRAPH`, `PRINT GRAPH`)

## Contribution of Team Members

- Agyeya- Testing and Report
- Aks- Section 1 implementation 
- Joshita - Section 2 implementation 
