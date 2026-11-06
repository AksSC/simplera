# Phase 1 Report — Graph Handling (SimpleRA)

This report describes our Phase 1 implementation for graph support in SimpleRA, focusing on:
- **disk-based storage** (pages/blocks),
- **correct output formats**,
- **memory constraints** (BufferManager with `BLOCK_COUNT = 2`), and
- command implementations with **logic, page design, block accesses, and error handling**.

---

## Page Design and Disk Layout

### Block / Page Model
- The system uses a **page** abstraction stored via `BufferManager`.
- In `src/server.cpp`, the configuration is:
  - `BLOCK_SIZE = 1` (≈ 1000 bytes used in row-capacity calculations)
  - `BLOCK_COUNT = 2` (at most **2 pages** can be resident in memory at a time)
- Every graph is stored as multiple paged “files” inside the buffer layer:
  - `<GraphName>_Nodes` (node pages)
  - `<GraphName>_Edges` (edge pages)
  - `<GraphName>_Degrees` (degree pages)

### Node Pages (`<GraphName>_Nodes`)
- Each row stores:
  - `NodeID, A1, A2, ...` (boolean attributes)
- Pages are written sequentially in **CSV order** during `Graph::blockifyNodes()`.
- We compute:
  - `maxNodesPerBlock = floor((BLOCK_SIZE * 1000) / (sizeof(int) * nodeColumnCount))`
- We also maintain:
  - `nodesPerBlockCount[pageIndex]` for the last partially-filled page.

### Edge Pages (`<GraphName>_Edges`)
- Each row stores:
  - `Src_NodeID, Dest_NodeID, Weight, B1, B2, ...` (boolean attributes)
- Pages are written sequentially in **CSV order** during `Graph::blockifyEdges()`.
- We compute:
  - `maxEdgesPerBlock = floor((BLOCK_SIZE * 1000) / (sizeof(int) * edgeColumnCount))`

#### Undirected Graph Representation
For an undirected graph (`graphType = 'U'`):
- The input contains one row per undirected edge.
- Internally, we **store two rows**:
  1) the original edge `(u, v, ...)`
  2) the reversed edge `(v, u, ...)`
This allows traversal logic (PATH) to treat the edge store uniformly as directed adjacency candidates.

### Degree Pages (`<GraphName>_Degrees`)
- Each row stores:
  - `InternalIndex, OutDegree, InDegree`
- Degree pages support **O(1)** degree lookup (single page read).
- During edge loading, we stream degree “deltas” into temporary bucket files under `../data/temp/`,
  then aggregate them into degree pages. This avoids keeping large per-node degree arrays in memory.

### Justification of Page Design
- **Correctness & format:** Nodes and edges are stored row-wise exactly as in CSV, enabling PRINT/EXPORT to reproduce the correct formatting easily.
- **Memory constraints:** All large data is stored in pages; BufferManager enforces `BLOCK_COUNT = 2`.
- **DEGREE efficiency:** A dedicated degree store provides fast DEGREE queries without scanning edges.
- **Traversal support:** Edges can be scanned via `GraphCursor` with controlled page access. For Phase 1, this is a correct disk-based approach; further optimizations (e.g., adjacency indexing) are natural extensions in later phases.

---

## Command Implementations

## 1) LOAD GRAPH

### Syntax
`LOAD GRAPH <graph_name> <D/U>`

### Logic
- Reads node and edge CSV headers to determine attribute columns.
- Validates:
  - Node attributes are boolean (0/1)
  - Edge weights are non-negative
  - All referenced node IDs exist
  - No duplicate NodeIDs
- Writes:
  - Node pages via `Graph::blockifyNodes()`
  - Edge pages via `Graph::blockifyEdges()`
  - Degree pages via streaming bucket aggregation

### Block Accesses
- Sequential scan of node file and edge file.
- Pages are written sequentially; no random I/O.

### Errors
- If input files are missing or malformed → command fails (semantic/syntax behavior as implemented).

---

## 2) EXPORT GRAPH

### Syntax
`EXPORT GRAPH <graph_name>`

### Logic
- Implemented in `Graph::makePermanent()`.
- Writes two CSVs into `../data/`:
  - `<GraphName>_Nodes_D.csv / <GraphName>_Nodes_U.csv`
  - `<GraphName>_Edges_D.csv / <GraphName>_Edges_U.csv`
- Output headers are taken from stored `nodeAttributes` / `edgeAttributes`, so the exported format matches the input format.

### Undirected Export
- Since undirected edges are stored internally twice (original + reverse),
  export emits only the **original edges** (even indices in the stored edge sequence).

### Block Accesses
- Sequential scan via `GraphCursor` (page-by-page reads).

### Errors
- If graph does not exist → `SEMANTIC ERROR: Graph doesn't exist`

---

## 3) PRINT GRAPH

### Syntax
`PRINT GRAPH <graph_name>`

### Output Format
- Prints metadata **one per line**, followed by a blank line:
  1) number of nodes
  2) number of edges
  3) graph type (`D` or `U`)
- Prints node rows, then a blank line, then edge rows.

### Ordering Requirement
- For loaded graphs: node/edge order follows the input CSV order.
- For PATH-result graphs: node/edge order follows **order of occurrence in the path** (see PATH section).

### Block Accesses
- Sequential scan via `GraphCursor`.

### Errors
- If graph does not exist → `SEMANTIC ERROR: Graph doesn't exist`

---

## 4) DEGREE

### Syntax
`DEGREE <graph_name> <NodeID>`

### Logic
- Uses `Graph::getDegree(nodeId, outDeg, inDeg)`:
  - maps `NodeID → internalIndex`
  - computes which degree page holds the row
  - reads exactly one page using `bufferManager.getGraphPage(...)`
- Output:
  - Directed: `outDeg + inDeg`
  - Undirected: `outDeg` (incident edges)

### Block Accesses
- **O(1)** page read (single degree page).

### Errors
- If node does not exist → `Node does not exist`

---

## 5) PATH

### Syntax
`RES <- PATH <graph_name> <src_NodeID> <dest_NodeID> WHERE <conditions>`

### Output
- Prints:
  - `TRUE <min_weight>` if a valid path exists
  - `FALSE` otherwise

### Condition Support
We support the condition formats from the PDF:
- `A3(N) == 1` / `B2(E) == 0`  (fixed boolean constraints)
- `A3(N)` / `B2(E)`            (uniform constraint: all nodes/edges share same value for that attribute)
- `ANY(N) == 1` / `ANY(E) == 0` (there exists some attribute where all nodes/edges have that value)
- Conditions are combined with `AND`.

### Disk-based Access (Memory Constraint)
PATH does **not** load full node/edge files into memory.
Instead:
- `getNodeAttributes(graph, nodeId)` reads the required node row from `<GraphName>_Nodes` using BufferManager.
- `getNeighbors(graph, nodeId)` scans edge pages via `GraphCursor` and returns outgoing edges from that node.

### Least-weight Path
- Implemented as a Dijkstra-style search (non-negative weights).
- Constraints that depend on the entire path (uniform and ANY) are encoded into the search **state** so that Dijkstra remains correct.

### Result Graph Materialization
If a valid path is found:
- Two files are created in `../data/`:
  - `RES_Nodes_D.csv / RES_Nodes_U.csv`
  - `RES_Edges_D.csv / RES_Edges_U.csv`
- **Ordering (required):**
  - Nodes are written in **order of occurrence in the path** (unique first-seen order).
  - Edges are written in **path order**.
- The resultant graph is then loaded into the graph catalogue as `RES`.

### Errors
- If graph does not exist → `SEMANTIC ERROR: Graph doesn't exist`
- If either endpoint node does not exist → `Node does not exist`

---

## Assumptions

1. All node IDs in a graph are unique and appear exactly once in the node file.
2. Node/edge attributes are integers restricted to {0,1}.
3. Edge weights are non-negative integers (required for Dijkstra-style least-weight path).
4. Graph name tokens contain no spaces (consistent with the command tokenizer).
5. For undirected graphs, the input edge file contains one row per undirected edge; internal duplication is used only for traversal.

---

## Contribution of Each Team Member



- **Aks:** Graph storage/page design (blockify nodes/edges), degree page generation.
- **Agyeya:** PATH parsing and condition handling; disk-based node/edge access helpers.
- **Joshita** EXPORT/PRINT formatting fixes, undirected export correctness, testing and edge cases.

---

## Testing Summary

We tested:
- Loading directed/undirected graphs with boolean attributes
- PRINT/EXPORT output formatting and ordering
- DEGREE correctness and error messages
- PATH correctness on multiple condition combinations:
  - fixed constraints, uniform constraints, ANY constraints, and combinations with AND
- Result graph ordering for PATH (nodes and edges in path order)
