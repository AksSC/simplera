# Phase 2 Report — External Sort, Hash Join, Group By (SimpleRA)

This report describes our Phase 2 implementation for SimpleRA, focusing on:
- **SETBUFFER K** — dynamic buffer count configuration,
- **SORT** — K-way multi-phase external merge sort with TOP/BOTTOM support,
- **JOIN** — Grace Hash Join (partition-hash join) with WHERE/PROJECT clauses,
- **GROUP BY** — sort-based grouping with HAVING and aggregate functions.

---

## Page Design and Memory Model

### Buffer Configuration
- `BLOCK_COUNT` is used as the main memory buffer limit (default = 10).
- `SETBUFFER K` modifies `BLOCK_COUNT` to K (2 ≤ K ≤ 10), treated as an environment variable.
- All subsequent operations (SORT, JOIN, GROUP BY) respect the current `BLOCK_COUNT` for both the algorithm's fan-in/partition count and the buffer pool eviction.

### Temporary Pages
- Sort runs, join partitions, and intermediate results are stored as pages in `../data/temp/`.
- All temp pages are read/written through `BufferManager` using the `Cursor` interface.
- Temporary pages are cleaned up after each operation completes.

---

## Command Implementations

## 1) SETBUFFER

### Syntax
`SETBUFFER K`

### Logic
- Parses integer K from the command using strict parsing (`SETBUFFER 3.5` is rejected as syntax error).
- Validates 2 ≤ K ≤ 10 in the semantic parser.
- Sets `BLOCK_COUNT = K` globally.

### Block Accesses
- None (no disk I/O).

### Errors
- Non-integer or missing argument → `SYNTAX ERROR`
- K < 2 or K > 10 → `SEMANTIC ERROR: Buffer size must be between 2 and 10`

---

## 2) SORT

### Syntax
`SORT <table_name> BY <col1>, <col2>, ... IN <ASC|DESC>, <ASC|DESC>, ... [TOP X] [BOTTOM Y]`

### Algorithm — K-Way Multi-Phase External Merge Sort
Following Elmasri & Navathe Section 18.2:

1. **Initial Run Creation:**
   - Read up to `BLOCK_COUNT` pages worth of rows into memory for each run.
   - Sort each chunk in-memory using a stable merge sort.
   - Write each sorted chunk as a temporary run to disk.
   - For partial sorts (`TOP`/`BOTTOM`), run creation starts from the page containing the start row of the target range instead of scanning from row 0.
   
2. **Multi-Phase Merge:**
   - Fan-in = `BLOCK_COUNT - 1` (one buffer reserved for output).
   - In each merge pass, merge up to `fan-in` runs using a priority queue.
   - Repeat until a single sorted run remains.

3. **In-Place Rewrite:**
   - Read the final sorted run and overwrite the original table pages.
   - For partial sorts, only pages intersecting the target row-range are rewritten.
   - Rewritten pages are removed from buffer pool immediately to avoid stale reads.

### TOP X / BOTTOM Y Support
- `TOP X`: Sort only the first X rows of the table. Remaining rows stay in original order.
- `BOTTOM Y`: Sort only the last Y rows. Remaining rows stay in original order.
- When both specified: TOP X and BOTTOM Y are sorted independently. The middle rows remain untouched.
- If neither specified: Full table sort.

### Sorting Behavior
- Supports multi-column sort with per-column ASC/DESC direction.
- Stable sort: rows with equal sort keys retain their original relative order.
- **In-place sorting**: modifies the temp pages of the table. The original CSV in `/data` is unaffected (updated only via EXPORT).
- When sorting the same table multiple times, subsequent sorts operate on the current temp pages (the previously sorted state).

### Block Accesses
- Initial run creation: O(N) reads where N = number of pages in the sort range (plus at most one partial lead-page read for range alignment).
- Each merge phase: O(N) reads + O(N) writes for all runs.
- Number of merge phases: O(log_{K-1}(N/K)) passes.
- Final rewrite: O(N) reads + O(N) writes for affected range pages only.

### Key Implementation Files
- `src/executors/sort.cpp` — parsing and execution entry point
- `src/executors/externalSortUtils.cpp` — sort infrastructure (runs, merge, rewrite)
- `src/executors/externalSortUtils.h` — TempRun, TempRunReader, TempRunWriter, ExternalSortComparator

### Errors
- Table doesn't exist → `SEMANTIC ERROR: Relation doesn't exist`
- Column doesn't exist → `SEMANTIC ERROR: Column doesn't exist in relation`
- Mismatch between column count and direction count → `SYNTAX ERROR`
- TOP + BOTTOM exceeds rowCount → `SEMANTIC ERROR`

---

## 3) JOIN (Grace Hash Join)

### Syntax
`Result <- JOIN <T1>, <T2> ON <join_condition> [WHERE <sel_condition>] [PROJECT <attr_list>]`

### Join Conditions Supported
1. `T1.colA == T2.colB` — Standard equi-join
2. `T1.colA + T2.colB == value` — Arithmetic equi-join (addition)
3. `T1.colA - T2.colB == value` — Arithmetic equi-join (subtraction)

### Algorithm — Partition-Hash Join (Grace Hash Join)
Following Elmasri & Navathe Section 18.4.4:

1. **Partitioning Phase:**
   - Hash both tables into `BLOCK_COUNT - 1` partitions using a hash function on the join key.
   - For arithmetic conditions (`A.a + B.b == val`), the hash key is transformed:
     - Table 1: `hash(val - A.a)` for addition, `hash(A.a - val)` for subtraction
     - Table 2: `hash(B.b)`
   - This ensures matching rows land in the same partition.
   - Partitions are written to temporary pages.

2. **Probing Phase (per matching partition pair):**
   - If the smaller partition fits in memory (`blockCount ≤ BLOCK_COUNT - 1`), build an in-memory hash table (`std::unordered_multimap`) from it.
   - Scan the larger partition page-by-page, probing the hash table for matches.
   - Apply WHERE filter during probing.
   - Emit matching rows (with PROJECT if specified).

3. **Recursive Partitioning:**
   - If neither partition fits in memory, recursively repartition with a different hash seed.
   - Different seeds at each depth prevent hash collisions from propagating.
   - If repartition depth cap is reached and partitions still do not fit build-memory, fallback uses page-wise nested probing for that partition pair (memory-safe fallback).

### WHERE Clause
- Supports `TableName.column op value` where `op` ∈ {==, !=, >, >=, <, <=}.
- Evaluated during the probe phase for efficiency.

### PROJECT Clause
- If specified, only the listed columns appear in the result.
- If omitted, all columns from both tables are included.
- Result column names use the base column name (without table prefix).

### Block Accesses
- Partitioning: O(|T1| + |T2|) reads + writes.
- Probing: O(|T1| + |T2|) reads (one pass each).
- Total: ~3 * (|T1| + |T2|) block accesses for single-level partitioning.

### Key Implementation File
- `src/executors/join.cpp`

### Errors
- Result table already exists → `SEMANTIC ERROR: Resultant relation already exists`
- Source table doesn't exist → `SEMANTIC ERROR: Relation doesn't exist`
- Column doesn't exist → `SEMANTIC ERROR: Column doesn't exist in relation`

---

## 4) GROUP BY

### Syntax
`Result1 Result2 ... <- GROUP BY attr1 attr2 ... FROM <table> HAVING <agg_condition> RETURN <agg1> <agg2> ...`

### Algorithm — Sort-Based Grouping
1. **For each grouping attribute independently:**
   - Sort the table by the grouping attribute using external merge sort.
   - Stream through the sorted run, accumulating aggregate state per group.
   - On group boundary change, evaluate the HAVING condition.
   - If HAVING passes, emit a row to the result table.

### Aggregate Functions
- `MAX(col)`, `MIN(col)`, `SUM(col)`, `COUNT(col)`, `COUNT(*)`, `AVG(col)`
- `AVG` is rounded **up** (ceiling) to integer, per spec.

### HAVING Clause
- Format: `aggregate_expr op value_or_aggregate_expr`
- Left side: an aggregate expression (e.g., `COUNT(*)`, `AVG(Salary)`)
- Right side: integer literal or another aggregate expression
- Operators: ==, !=, >, >=, <, <=
- If right side is a constant, semantic bound enforced: `0 <= number <= 10000`.
- Uses floating-point comparison with epsilon tolerance for AVG comparisons.

### Multi-Attribute Support
- Each grouping attribute produces a **separate** result table.
- 1:1:1 correspondence: `N result names : N attributes : N return aggregates`.
- Result table has 2 columns: `[grouping_attribute_name, aggregate_result_name]`
- Aggregate result column named as `FUNCTIONcolumn` (e.g., `MAXSalary`, `COUNT`).

### Block Accesses
- External sort: O(N * log(N/K)) per grouping attribute.
- Streaming aggregation: O(N) reads through the sorted run.
- Result writing: O(result_size) writes.

### Key Implementation File
- `src/executors/groupBy.cpp`

### Errors
- Table doesn't exist → `SEMANTIC ERROR: Relation doesn't exist`
- Result table already exists → `SEMANTIC ERROR`
- Column doesn't exist → `SEMANTIC ERROR` (for group columns, having columns, return columns)

---

## Assumptions

1. Values of `TOP X` and `BOTTOM Y` are always positive integers.
2. `TOP X + BOTTOM Y` will never exceed the total row count.
3. ASC/DESC direction is provided for every sort column.
4. For arithmetic join conditions, the target value satisfies 0 ≤ value ≤ 10000.
5. All table data consists of integers (following the existing LOAD implementation).
6. Sorting is performed on the current temp pages (not re-reading the original CSV), and this is noted as an accepted approach per the Q&A clarifications.
7. Empty JOIN results produce no table in the catalogue.
8. GROUP BY result order is not required to follow any specific ordering.
9. JOIN result row order is not assumed for correctness (as clarified in Q&A).
10. Trailing semicolon in JOIN/GROUP BY is treated as optional and accepted by parser.

---

## Contribution of Each Team Member

<!-- Fill in your actual contributions -->
- **Aks: HASH JOIN function** 
- **Agyeya: GROUP BY function and Report** 
- **Joshita: SET BUFFER and SORT function** 

---

## Testing Summary

We tested:
- **SETBUFFER**: Verified K=2,5,10 and boundary cases (K=1 rejected, K=11 rejected), and strict parsing (`SETBUFFER 3.5` rejected).
- **SORT**:
  - Single-column ASC/DESC sort on small and large tables.
  - Multi-column sort with mixed ASC/DESC directions.
  - TOP X, BOTTOM Y, and combined TOP X BOTTOM Y.
  - Stability: rows with equal keys retain original relative order.
  - In-place correctness: PRINT after SORT shows sorted data; original CSV unchanged.
  - Partial range performance optimization path verified (range-local run creation and rewrite).
- **JOIN**:
  - Standard equi-join on matching and non-matching columns.
  - Arithmetic join conditions (+ and -).
  - WHERE clause filtering.
  - PROJECT clause column selection.
  - Empty result (no matching rows).
  - Behavior verified under multiple buffer sizes (`K=2,3,10`).
- **GROUP BY**:
  - All five aggregate functions (MAX, MIN, COUNT, SUM, AVG).
  - COUNT(*) support.
  - HAVING with integer literal and with aggregate-vs-aggregate comparison.
  - HAVING numeric bound enforcement (`0 <= number <= 10000`).
  - Multi-attribute grouping producing separate result tables.
  - AVG with ceiling rounding.
