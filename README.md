# SimpleRA

SimpleRA is a C++11, integer-only relational algebra engine extended as a **2026 Data Systems team project**. It runs commands through a terminal interface and stores tables and graphs in pages managed by a buffer pool. The repository includes the original course starter code and our implementations from three project phases.

## What we built

- **Graph storage and queries:** page-backed node, edge, and degree stores; directed and undirected graph loading; degree lookup and path queries.
- **Memory-bounded query processing:** multi-pass external merge sort, Grace hash join with recursive repartitioning, and sort-based `GROUP BY`. `SETBUFFER K` sets the buffer limit from 2 to 10 pages.
- **Concurrency control simulation:** page-level strict two-phase locking with shared/exclusive locks, wait-die deadlock prevention, and abort/restart logging. `TRANSACTION` simulates a schedule; it does **not** change table data.

Implementation details and test cases are in the [graph](docs/Report.md), [query processing](docs/Report2.md), and [concurrency](docs/Report3.md) phase reports.

## Build and run

Requires `g++` with C++11 support and `make`. The commands below were verified on Linux.

```sh
make -C src
cd src
./server
```

Run `./server` **from `src/`**: data paths are relative to that directory. On startup, the program deletes and recreates `data/temp/`, which it uses for scratch pages. Enter `QUIT` to exit.

### Relational example

These commands use the included [`A.csv`](data/A.csv) and [`B.csv`](data/B.csv):

```text
LOAD A
LOAD B
R <- JOIN A, B ON A.a == B.a
PRINT R
SORT B BY a IN DESC
PRINT B
QUIT
```

The join produces four rows for these sample files. `SORT` changes the table loaded in the current session; the source CSV stays unchanged unless explicitly exported.

### Graph example

The small [`DEMO`](data/DEMO_Nodes_D.csv) graph has three nodes and three directed edges:

```text
LOAD GRAPH DEMO D
DEGREE DEMO 1
PRINT GRAPH DEMO
QUIT
```

`DEGREE DEMO 1` prints `2` (the out-degree of node 1).

### Transaction schedule example

In a fresh `./server` session:

```text
LOAD 10
TRANSACTION txn_conflict_waitdie.txt
QUIT
```

The sample schedule is in [`data/txn_conflict_waitdie.txt`](data/txn_conflict_waitdie.txt). The command writes its lock and restart trace to `data/txn_conflict_waitdie output.txt` and overwrites that file if it already exists. The trace shows the older transaction waiting, the younger one aborting, and a restart after the conflict clears.

## Repository map

- [`src/`](src/) — command parsing, executors, buffer manager, table and graph storage.
- [`data/`](data/) — sample tables, transaction schedules, and graph input files.
- [`docs/Report.md`](docs/Report.md) — phase 1 graph storage and queries.
- [`docs/Report2.md`](docs/Report2.md) — phase 2 sort, join, and grouping.
- [`docs/Report3.md`](docs/Report3.md) — phase 3 transaction scheduling.

[`Grammar.md`](Grammar.md) and the [2020 overview slides](docs/Overview.md) came from the course starter. They document the original interface, not every feature in this finished project; use the phase reports and examples above for the extensions.

## Scope

This is an educational database engine, not a SQL server. Its command language is relational-algebra-like, table values are integers, and the transaction component models locking decisions without applying writes to stored pages. No performance claim or production readiness is implied. The original starter's copyright and MIT terms are retained in [`LICENSE`](LICENSE).
