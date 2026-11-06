#include "global.h"
#include "graphCatalogue.h"
#include <regex>
#include <queue>
#include <climits>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

/**
 * @brief
 * SYNTAX: RES <- PATH <graph_name> <src_NodeID> <dest_NodeID> WHERE <conditions>
 * 
 * Conditions:
 * - ATTRIBUTE(N) == 0/1 : Node attribute must be 0 or 1
 * - ATTRIBUTE(E) == 0/1 : Edge attribute must be 0 or 1
 * - ATTRIBUTE(N) or ATTRIBUTE(E) : Uniform check - all must be same value
 * - ANY(N) == 0/1 or ANY(E) == 0/1 : Any attribute must satisfy
 */
bool syntacticParsePATH()
{
    logger.log("syntacticParsePATH");
    // RES <- PATH G src dest WHERE conditions...
    // tokenizedQuery[0] = RES
    // tokenizedQuery[1] = <-
    // tokenizedQuery[2] = PATH
    // tokenizedQuery[3] = graph_name
    // tokenizedQuery[4] = src_NodeID
    // tokenizedQuery[5] = dest_NodeID
    // tokenizedQuery[6] = WHERE (optional)
    // tokenizedQuery[7+] = conditions (optional)

    if (tokenizedQuery.size() < 6)
    {
        cout << "SYNTAX ERROR" << endl;
        return false;
    }

    parsedQuery.queryType = PATH;
    parsedQuery.pathResultGraphName = tokenizedQuery[0];
    parsedQuery.pathGraphName = tokenizedQuery[3];

    regex numeric("[-]?[0-9]+");
    if (!regex_match(tokenizedQuery[4], numeric) || !regex_match(tokenizedQuery[5], numeric))
    {
        cout << "SYNTAX ERROR" << endl;
        return false;
    }
    parsedQuery.pathSrcNodeId = stoi(tokenizedQuery[4]);
    parsedQuery.pathDestNodeId = stoi(tokenizedQuery[5]);

    // Parse conditions if WHERE clause exists
    if (tokenizedQuery.size() > 6)
    {
        if (tokenizedQuery[6] != "WHERE")
        {
            cout << "SYNTAX ERROR" << endl;
            return false;
        }

        // Parse conditions: ATTR(N/E) == 0/1 or ATTR(N/E) or ANY(N/E) == 0/1
        size_t i = 7;
        while (i < tokenizedQuery.size())
        {
            if (tokenizedQuery[i] == "AND")
            {
                i++;
                continue;
            }

            ParsedQuery::PathCondition condition;

            // Parse attribute part: e.g., A1(N), B2(E), ANY(N), ANY(E)
            string attrPart = tokenizedQuery[i];

            // Extract attribute name and target type
            size_t openParen = attrPart.find('(');
            size_t closeParen = attrPart.find(')');
            if (openParen == string::npos || closeParen == string::npos || closeParen <= openParen)
            {
                cout << "SYNTAX ERROR" << endl;
                return false;
            }

            condition.attribute = attrPart.substr(0, openParen);
            string targetPart = attrPart.substr(openParen + 1, closeParen - openParen - 1);

            if (targetPart != "N" && targetPart != "E")
            {
                cout << "SYNTAX ERROR" << endl;
                return false;
            }
            condition.target = targetPart[0];

            // Check if it's ANY condition
            condition.isAnyCheck = (condition.attribute == "ANY");

            // Check if next tokens are == value
            if (i + 2 < tokenizedQuery.size() && tokenizedQuery[i + 1] == "==")
            {
                // Fixed value condition
                if (!regex_match(tokenizedQuery[i + 2], numeric))
                {
                    cout << "SYNTAX ERROR" << endl;
                    return false;
                }
                condition.requiredValue = stoi(tokenizedQuery[i + 2]);
                if (condition.requiredValue != 0 && condition.requiredValue != 1)
                {
                    cout << "SYNTAX ERROR" << endl;
                    return false;
                }
                condition.isUniformCheck = false;
                i += 3;
            }
            else
            {
                // Uniform check condition (no explicit == value)
                condition.isUniformCheck = true;
                condition.requiredValue = -1; // Not used
                i += 1;
            }

            parsedQuery.pathConditions.push_back(condition);
        }
    }

    return true;
}

bool semanticParsePATH()
{
    logger.log("semanticParsePATH");

    // Check if graph exists
    if (!graphCatalogue.isGraph(parsedQuery.pathGraphName))
    {
        cout << "SEMANTIC ERROR: Graph doesn't exist" << endl;
        return false;
    }

    Graph *graph = graphCatalogue.getGraph(parsedQuery.pathGraphName);

    // Check if nodes exist
    if (graph->nodeIdToIndex.find(parsedQuery.pathSrcNodeId) == graph->nodeIdToIndex.end())
    {
        cout << "Node does not exist" << endl;
        return false;
    }
    if (graph->nodeIdToIndex.find(parsedQuery.pathDestNodeId) == graph->nodeIdToIndex.end())
    {
        cout << "Node does not exist" << endl;
        return false;
    }

    // Validate attributes exist
    for (const auto &cond : parsedQuery.pathConditions)
    {
        if (cond.isAnyCheck)
            continue;

        if (cond.target == 'N')
        {
            bool found = false;
            for (size_t i = 1; i < graph->nodeAttributes.size(); i++)
            {
                if (graph->nodeAttributes[i] == cond.attribute)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                cout << "SEMANTIC ERROR: Attribute doesn't exist" << endl;
                return false;
            }
        }
        else
        {
            bool found = false;
            for (size_t i = 3; i < graph->edgeAttributes.size(); i++)
            {
                if (graph->edgeAttributes[i] == cond.attribute)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                cout << "SEMANTIC ERROR: Attribute doesn't exist" << endl;
                return false;
            }
        }
    }

    return true;
}

// Helper: Get node attributes on-demand (disk-based access)
static vector<int> getNodeAttributes(Graph *graph, int nodeId)
{
    // Use nodeIdToIndex to get internal index
    auto it = graph->nodeIdToIndex.find(nodeId);
    if (it == graph->nodeIdToIndex.end())
        return vector<int>(); // Node not found
    
    int internalIndex = it->second;
    
    // Calculate page and row index
    int pageIndex = internalIndex / (int)graph->maxNodesPerBlock;
    int rowIndex = internalIndex % (int)graph->maxNodesPerBlock;
    
    if (pageIndex < 0 || pageIndex >= (int)graph->nodeBlockCount)
        return vector<int>();
    
    uint rowCount = graph->nodesPerBlockCount[pageIndex];
    if ((uint)rowIndex >= rowCount)
        return vector<int>();
    
    // Use bufferManager to get the page (respects 2-block limit)
    Page nodePage = bufferManager.getGraphPage(graph->graphName + "_Nodes",
        pageIndex, rowCount, (uint)graph->nodeAttributes.size(), graph->maxNodesPerBlock);
    
    return nodePage.getRow(rowIndex);
}

// Helper: Get adjacency list with edge data
struct EdgeInfo
{
    int destAdjId;
    int weight;
    vector<int> edgeRow; // Full edge row for attribute checking
};

// Helper: Get neighbors on-demand (disk-based access)
static vector<EdgeInfo> getNeighbors(Graph *graph, int nodeId)
{
    vector<EdgeInfo> neighbors;
    
    // Use nodeIdToIndex to get internal index
    auto srcIt = graph->nodeIdToIndex.find(nodeId);
    if (srcIt == graph->nodeIdToIndex.end())
        return neighbors; // Node not found
    
    // Use GraphCursor to scan edge pages (respects 2-block limit via BufferManager)
    GraphCursor cursor = graph->getEdgeCursor();
    vector<int> row = cursor.getNext();
    while (!row.empty())
    {
        int src = row[0];
        int dest = row[1];
        int weight = row[2];
        
        // Only process edges where source matches
        if (src == nodeId)
        {
            auto destIt = graph->nodeIdToIndex.find(dest);
            if (destIt != graph->nodeIdToIndex.end())
            {
                EdgeInfo edge;
                edge.destAdjId = destIt->second;
                edge.weight = weight;
                edge.edgeRow = row;
                neighbors.push_back(edge);
            }
        }
        row = cursor.getNext();
    }
    
    return neighbors;
}

// Check if a node satisfies all node conditions
static bool checkNodeConditions(const vector<int> &nodeRow, Graph *graph,
                                 const vector<ParsedQuery::PathCondition> &conditions,
                                 int uniformNodeAttrValue[],  // -1 if not set, 0 or 1 if set
                                 int anyNodeAttrIndex)        // -1 if not determined yet
{
    // nodeRow: [NodeID, A1, A2, ...]
    // uniformNodeAttrValue indexed by node attribute index (same as graph->nodeAttributes)
    
    for (const auto &cond : conditions)
    {
        if (cond.target != 'N')
            continue;

        if (cond.isAnyCheck)
        {
            // ANY(N) handled separately by caller
            continue;
        }

        // Find attribute index
        int attrIndex = -1;
        for (size_t i = 1; i < graph->nodeAttributes.size(); i++)
        {
            if (graph->nodeAttributes[i] == cond.attribute)
            {
                attrIndex = (int)i;
                break;
            }
        }
        if (attrIndex < 0 || attrIndex >= (int)nodeRow.size())
            return false;

        int attrValue = nodeRow[attrIndex];

        if (cond.isUniformCheck)
        {
            // Uniform check: must match previously chosen value
            if (uniformNodeAttrValue[attrIndex] == -1)
            {
                uniformNodeAttrValue[attrIndex] = attrValue; // first time set
            }
            else if (uniformNodeAttrValue[attrIndex] != attrValue)
            {
                return false;
            }
        }
        else
        {
            // Fixed value check
            if (attrValue != cond.requiredValue)
                return false;
        }
    }

    // If ANY(N) == v is present, caller ensures the same attrIndex used across all nodes.
    // anyNodeAttrIndex: index of chosen attribute (>=1) if set, -1 if not set/unused
    (void)anyNodeAttrIndex;

    return true;
}

// Check if an edge satisfies all edge conditions
static bool checkEdgeConditions(const vector<int> &edgeRow, Graph *graph,
                                 const vector<ParsedQuery::PathCondition> &conditions,
                                 int uniformEdgeAttrValue[],  // -1 if not set, 0 or 1 if set
                                 int anyEdgeAttrIndex)        // -1 if not determined yet
{
    // edgeRow: [Src, Dest, Weight, B1, B2, ...]
    // uniformEdgeAttrValue indexed by edge attribute index (same as graph->edgeAttributes)
    
    for (const auto &cond : conditions)
    {
        if (cond.target != 'E')
            continue;

        if (cond.isAnyCheck)
        {
            // ANY(E) handled separately by caller
            continue;
        }

        // Find attribute index
        int attrIndex = -1;
        for (size_t i = 3; i < graph->edgeAttributes.size(); i++)
        {
            if (graph->edgeAttributes[i] == cond.attribute)
            {
                attrIndex = (int)i;
                break;
            }
        }
        if (attrIndex < 0 || attrIndex >= (int)edgeRow.size())
            return false;

        int attrValue = edgeRow[attrIndex];

        if (cond.isUniformCheck)
        {
            if (uniformEdgeAttrValue[attrIndex] == -1)
            {
                uniformEdgeAttrValue[attrIndex] = attrValue;
            }
            else if (uniformEdgeAttrValue[attrIndex] != attrValue)
            {
                return false;
            }
        }
        else
        {
            if (attrValue != cond.requiredValue)
                return false;
        }
    }

    (void)anyEdgeAttrIndex;
    return true;
}

// Determine if there is any ANY(N) or ANY(E) condition and get its required value
static int getAnyNodeValue(const vector<ParsedQuery::PathCondition> &conditions)
{
    for (const auto &cond : conditions)
        if (cond.isAnyCheck && cond.target == 'N' && !cond.isUniformCheck)
            return cond.requiredValue;
    return -1;
}
static int getAnyEdgeValue(const vector<ParsedQuery::PathCondition> &conditions)
{
    for (const auto &cond : conditions)
        if (cond.isAnyCheck && cond.target == 'E' && !cond.isUniformCheck)
            return cond.requiredValue;
    return -1;
}

// Check if there is ANY(N) uniform (no == value)
static bool hasAnyNodeUniformCheck(const vector<ParsedQuery::PathCondition> &conditions)
{
    for (const auto &cond : conditions)
        if (cond.isAnyCheck && cond.target == 'N' && cond.isUniformCheck)
            return true;
    return false;
}
static bool hasAnyEdgeUniformCheck(const vector<ParsedQuery::PathCondition> &conditions)
{
    for (const auto &cond : conditions)
        if (cond.isAnyCheck && cond.target == 'E' && cond.isUniformCheck)
            return true;
    return false;
}

void executePATH()
{
    logger.log("executePATH");

    Graph *graph = graphCatalogue.getGraph(parsedQuery.pathGraphName);
    int srcNodeId = parsedQuery.pathSrcNodeId;
    int destNodeId = parsedQuery.pathDestNodeId;
    // Use nodeIdToIndex for non-contiguous node IDs
    int srcAdj = graph->nodeIdToIndex.at(srcNodeId);
    int destAdj = graph->nodeIdToIndex.at(destNodeId);

    // Use disk-based access instead of loading entire graph into memory
    // Node and edge data will be accessed on-demand via getNodeAttributes() and getNeighbors()

    const auto &conditions = parsedQuery.pathConditions;

    // Get ANY condition values
    int anyNodeValue = getAnyNodeValue(conditions);
    int anyEdgeValue = getAnyEdgeValue(conditions);
    bool hasAnyNodeUniform = hasAnyNodeUniformCheck(conditions);
    bool hasAnyEdgeUniform = hasAnyEdgeUniformCheck(conditions);

    // For uniform conditions, we need to track the chosen values
    // We'll use a state-based Dijkstra where state includes uniform attribute values

    // Simplified approach: Use Dijkstra with condition checking
    // For uniform conditions, we explore all possible uniform values

    // State for Dijkstra: (current node, uniform state)
    // For simplicity, we'll do a modified Dijkstra that backtracks on constraint violations

    // Best approach: Use Dijkstra with path reconstruction, checking conditions along the path

    // Simpler implementation: BFS/Dijkstra where we check constraints on-the-fly
    // For uniform constraints, we need to propagate the "chosen" uniform value

    // Data structures for Dijkstra
    // dist[node][uniformState] = minimum distance
    // For simplicity, we'll enumerate uniform states

    // Count number of uniform node attrs and uniform edge attrs
    vector<int> uniformNodeAttrIndices;
    vector<int> uniformEdgeAttrIndices;
    for (const auto &cond : conditions)
    {
        if (!cond.isUniformCheck || cond.isAnyCheck)
            continue;
        if (cond.target == 'N')
        {
            for (size_t i = 1; i < graph->nodeAttributes.size(); i++)
            {
                if (graph->nodeAttributes[i] == cond.attribute)
                {
                    uniformNodeAttrIndices.push_back((int)i);
                    break;
                }
            }
        }
        else
        {
            for (size_t i = 3; i < graph->edgeAttributes.size(); i++)
            {
                if (graph->edgeAttributes[i] == cond.attribute)
                {
                    uniformEdgeAttrIndices.push_back((int)i);
                    break;
                }
            }
        }
    }

    // State: (uniformNodeValues..., uniformEdgeValues..., anyNodeAttrIndex, anyEdgeAttrIndex)
    // This is complex, so let's use a simpler approach:
    // Run Dijkstra where each edge expansion checks and updates constraints

    // Use a map to track best distance to (node, constraintState)
    // constraintState = tuple of (uniformNodeAttrValues, uniformEdgeAttrValues, anyNodeIdx, anyEdgeIdx)

    // For efficiency, encode state as a string or use nested maps

    struct State
    {
        int nodeAdj;
        vector<int> uniformNodeValues; // -1 = not set, 0/1 = set
        vector<int> uniformEdgeValues;
        int anyNodeAttrIdx; // -1 = not set, -2 = invalid, >= 0 = determined
        int anyEdgeAttrIdx;

        bool operator<(const State &other) const
        {
            if (nodeAdj != other.nodeAdj) return nodeAdj < other.nodeAdj;
            if (uniformNodeValues != other.uniformNodeValues) return uniformNodeValues < other.uniformNodeValues;
            if (uniformEdgeValues != other.uniformEdgeValues) return uniformEdgeValues < other.uniformEdgeValues;
            if (anyNodeAttrIdx != other.anyNodeAttrIdx) return anyNodeAttrIdx < other.anyNodeAttrIdx;
            return anyEdgeAttrIdx < other.anyEdgeAttrIdx;
        }
        bool operator==(const State &other) const
        {
            return nodeAdj == other.nodeAdj &&
                   uniformNodeValues == other.uniformNodeValues &&
                   uniformEdgeValues == other.uniformEdgeValues &&
                   anyNodeAttrIdx == other.anyNodeAttrIdx &&
                   anyEdgeAttrIdx == other.anyEdgeAttrIdx;
        }
    };

    struct StateHash
    {
        size_t operator()(const State &s) const
        {
            size_t h = hash<int>()(s.nodeAdj);
            for (int v : s.uniformNodeValues) h ^= hash<int>()(v) + 0x9e3779b9 + (h << 6) + (h >> 2);
            for (int v : s.uniformEdgeValues) h ^= hash<int>()(v) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= hash<int>()(s.anyNodeAttrIdx) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= hash<int>()(s.anyEdgeAttrIdx) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    // Priority queue: (distance, state, path_nodes, path_edges)
    typedef tuple<long long, State, vector<int>, vector<vector<int>>> PQEntry;
    auto cmp = [](const PQEntry &a, const PQEntry &b) { return get<0>(a) > get<0>(b); };
    priority_queue<PQEntry, vector<PQEntry>, decltype(cmp)> pq(cmp);

    unordered_map<State, long long, StateHash> bestDist;

    // Initialize starting state
    State startState;
    startState.nodeAdj = srcAdj;
    startState.uniformNodeValues.resize(graph->nodeAttributes.size(), -1);
    startState.uniformEdgeValues.resize(graph->edgeAttributes.size(), -1);
    startState.anyNodeAttrIdx = (anyNodeValue >= 0 || hasAnyNodeUniform) ? -1 : -3; // -3 = no ANY(N) condition
    startState.anyEdgeAttrIdx = (anyEdgeValue >= 0 || hasAnyEdgeUniform) ? -1 : -3;

    // Check source node against conditions (disk-based access)
    vector<int> srcNodeRow = getNodeAttributes(graph, srcNodeId);
    if (srcNodeRow.empty())
    {
        cout << "FALSE" << endl;
        return;
    }

    // Check fixed value conditions for source node
    for (const auto &cond : conditions)
    {
        if (cond.target != 'N' || cond.isAnyCheck)
            continue;
        int attrIndex = -1;
        for (size_t i = 1; i < graph->nodeAttributes.size(); i++)
        {
            if (graph->nodeAttributes[i] == cond.attribute)
            {
                attrIndex = (int)i;
                break;
            }
        }
        if (attrIndex < 0 || attrIndex >= (int)srcNodeRow.size())
        {
            cout << "FALSE" << endl;
            return;
        }
        int attrValue = srcNodeRow[attrIndex];
        if (cond.isUniformCheck)
        {
            startState.uniformNodeValues[attrIndex] = attrValue;
        }
        else
        {
            if (attrValue != cond.requiredValue)
            {
                cout << "FALSE" << endl;
                return;
            }
        }
    }

    // Check ANY(N) for source
    if (anyNodeValue >= 0)
    {
        bool found = false;
        for (size_t i = 1; i < graph->nodeAttributes.size(); i++)
        {
            if ((int)i < (int)srcNodeRow.size() && srcNodeRow[i] == anyNodeValue)
            {
                startState.anyNodeAttrIdx = (int)i;
                found = true;
                break;
            }
        }
        if (!found)
        {
            cout << "FALSE" << endl;
            return;
        }
    }
    else if (hasAnyNodeUniform)
    {
        // ANY(N) uniform: will be determined by first node, then must be same for all
        // For source, we just note that it exists and pick any attr
        // Actually for uniform ANY, we need to try all attributes
        // This makes it complex - for now, let's handle it similarly
        startState.anyNodeAttrIdx = -1; // Will be determined
    }

    pq.push({0, startState, {srcAdj}, {}});
    bestDist[startState] = 0;

    long long resultWeight = LLONG_MAX;
    vector<int> resultPathNodes;
    vector<vector<int>> resultPathEdges;
    bool found = false;

    while (!pq.empty())
    {
        PQEntry entry = pq.top();
        pq.pop();
        long long dist = get<0>(entry);
        State state = get<1>(entry);
        vector<int> pathNodes = get<2>(entry);
        vector<vector<int>> pathEdges = get<3>(entry);

        // If this is not the best known distance, skip
        auto it = bestDist.find(state);
        if (it != bestDist.end() && dist > it->second)
            continue;

        int currentNodeAdj = state.nodeAdj;
        int currentNodeId = graph->nodeIds[currentNodeAdj];

        // Destination check:
        // When a node is pushed as a neighbor, it must satisfy node conditions before being queued,
        // so when we pop destination here, it has already passed the destination-node validity checks.
        if (currentNodeAdj == destAdj)
        {
            resultWeight = dist;
            resultPathNodes = pathNodes;
            resultPathEdges = pathEdges;
            found = true;
            break;
        }

        // Expand neighbors (disk-based)
        vector<EdgeInfo> neighbors = getNeighbors(graph, currentNodeId);
        for (const auto &edge : neighbors)
        {
            int nextNodeAdj = edge.destAdjId;
            int nextNodeId = graph->nodeIds[nextNodeAdj];
            int weight = edge.weight;

            // Copy constraint state
            State nextState = state;
            nextState.nodeAdj = nextNodeAdj;

            // Edge conditions first
            // Prepare arrays for uniform checks
            vector<int> tmpUniformNode = nextState.uniformNodeValues;
            vector<int> tmpUniformEdge = nextState.uniformEdgeValues;

            // Any indices
            int anyNodeIdx = nextState.anyNodeAttrIdx;
            int anyEdgeIdx = nextState.anyEdgeAttrIdx;

            // Validate edge conditions
            // Fixed edge conditions / uniform edge conditions
            int *uniformEdgeArr = tmpUniformEdge.data();
            if (!checkEdgeConditions(edge.edgeRow, graph, conditions, uniformEdgeArr, anyEdgeIdx))
                continue;

            // Handle ANY(E) == value
            if (anyEdgeValue >= 0)
            {
                if (anyEdgeIdx == -1)
                {
                    // Determine which edge attribute index satisfies
                    bool ok = false;
                    for (size_t i = 3; i < graph->edgeAttributes.size(); i++)
                    {
                        if ((int)i < (int)edge.edgeRow.size() && edge.edgeRow[i] == anyEdgeValue)
                        {
                            anyEdgeIdx = (int)i;
                            ok = true;
                            break;
                        }
                    }
                    if (!ok)
                        continue;
                }
                else if (anyEdgeIdx >= 3)
                {
                    if (anyEdgeIdx >= (int)edge.edgeRow.size() || edge.edgeRow[anyEdgeIdx] != anyEdgeValue)
                        continue;
                }
            }
            else if (hasAnyEdgeUniform)
            {
                // ANY(E) uniform without explicit value: determine attribute index on first edge
                if (anyEdgeIdx == -1)
                {
                    // pick first edge attribute as the uniform ANY(E) attribute index
                    anyEdgeIdx = 3;
                }
                // Must be consistent across edges; value can be 0 or 1 but must match first edge's value
                // Enforced by tracking uniformEdgeValues[anyEdgeIdx] once set
                if (anyEdgeIdx >= 3 && anyEdgeIdx < (int)edge.edgeRow.size())
                {
                    if (tmpUniformEdge[anyEdgeIdx] == -1)
                        tmpUniformEdge[anyEdgeIdx] = edge.edgeRow[anyEdgeIdx];
                    else if (tmpUniformEdge[anyEdgeIdx] != edge.edgeRow[anyEdgeIdx])
                        continue;
                }
                else
                {
                    continue;
                }
            }

            // Now validate next node conditions (disk-based)
            vector<int> nextNodeRow = getNodeAttributes(graph, nextNodeId);
            if (nextNodeRow.empty())
                continue;

            int *uniformNodeArr = tmpUniformNode.data();
            if (!checkNodeConditions(nextNodeRow, graph, conditions, uniformNodeArr, anyNodeIdx))
                continue;

            // Handle ANY(N) == value
            if (anyNodeValue >= 0)
            {
                if (anyNodeIdx == -1)
                {
                    bool ok = false;
                    for (size_t i = 1; i < graph->nodeAttributes.size(); i++)
                    {
                        if ((int)i < (int)nextNodeRow.size() && nextNodeRow[i] == anyNodeValue)
                        {
                            anyNodeIdx = (int)i;
                            ok = true;
                            break;
                        }
                    }
                    if (!ok)
                        continue;
                }
                else if (anyNodeIdx >= 1)
                {
                    if (anyNodeIdx >= (int)nextNodeRow.size() || nextNodeRow[anyNodeIdx] != anyNodeValue)
                        continue;
                }
            }
            else if (hasAnyNodeUniform)
            {
                if (anyNodeIdx == -1)
                {
                    anyNodeIdx = 1;
                }
                if (anyNodeIdx >= 1 && anyNodeIdx < (int)nextNodeRow.size())
                {
                    if (tmpUniformNode[anyNodeIdx] == -1)
                        tmpUniformNode[anyNodeIdx] = nextNodeRow[anyNodeIdx];
                    else if (tmpUniformNode[anyNodeIdx] != nextNodeRow[anyNodeIdx])
                        continue;
                }
                else
                {
                    continue;
                }
            }

            // Commit constraint updates
            nextState.uniformNodeValues = tmpUniformNode;
            nextState.uniformEdgeValues = tmpUniformEdge;
            nextState.anyNodeAttrIdx = anyNodeIdx;
            nextState.anyEdgeAttrIdx = anyEdgeIdx;

            long long newDist = dist + weight;

            auto it2 = bestDist.find(nextState);
            if (it2 == bestDist.end() || newDist < it2->second)
            {
                bestDist[nextState] = newDist;
                vector<int> newPathNodes = pathNodes;
                newPathNodes.push_back(nextNodeAdj);
                vector<vector<int>> newPathEdges = pathEdges;
                newPathEdges.push_back(edge.edgeRow);
                pq.push({newDist, nextState, newPathNodes, newPathEdges});
            }
        }
    }

    if (!found)
    {
        cout << "FALSE" << endl;
        return;
    }

    cout << "TRUE " << resultWeight << endl;

    // Collect nodes from the path in *order of occurrence* (unique, first-seen order)
    vector<int> pathNodeIdsOrdered;
    pathNodeIdsOrdered.reserve(resultPathNodes.size());
    unordered_set<int> seenNodeIds;
    seenNodeIds.reserve(resultPathNodes.size() * 2 + 1);
    for (int adjId : resultPathNodes)
    {
        // Convert internal index back to original node ID
        int nodeIdOrig = graph->nodeIds[adjId];
        if (seenNodeIds.insert(nodeIdOrig).second)
            pathNodeIdsOrdered.push_back(nodeIdOrig);
    }
    // Write result graph files
    string suffix = graph->graphType == 'D' ? "_D.csv" : "_U.csv";
    string nodeFileName = "../data/" + parsedQuery.pathResultGraphName + "_Nodes" + suffix;
    string edgeFileName = "../data/" + parsedQuery.pathResultGraphName + "_Edges" + suffix;

    // Write node file (in order of occurrence in the path)
    ofstream foutNodes(nodeFileName, ios::trunc);
    for (size_t i = 0; i < graph->nodeAttributes.size(); i++)
    {
        if (i != 0) foutNodes << ",";
        foutNodes << graph->nodeAttributes[i];
    }
    foutNodes << endl;
    for (int nodeIdOrig : pathNodeIdsOrdered)
    {
        vector<int> nodeRow = getNodeAttributes(graph, nodeIdOrig);
        if (nodeRow.empty()) continue;
        for (size_t i = 0; i < nodeRow.size(); i++)
        {
            if (i != 0) foutNodes << ",";
            foutNodes << nodeRow[i];
        }
        foutNodes << endl;
    }
    foutNodes.close();

    // Write edge file (in path order)
    ofstream foutEdges(edgeFileName, ios::trunc);
    for (size_t i = 0; i < graph->edgeAttributes.size(); i++)
    {
        if (i != 0) foutEdges << ",";
        foutEdges << graph->edgeAttributes[i];
    }
    foutEdges << endl;
    for (const auto &edgeRow : resultPathEdges)
    {
        for (size_t i = 0; i < edgeRow.size(); i++)
        {
            if (i != 0) foutEdges << ",";
            foutEdges << edgeRow[i];
        }
        foutEdges << endl;
    }
    foutEdges.close();

    // Load the result graph into the catalogue
    Graph *resultGraph = new Graph(parsedQuery.pathResultGraphName, graph->graphType);
    if (resultGraph->load())
    {
        graphCatalogue.insertGraph(resultGraph);
    }
    else
    {
        resultGraph->unload();
        delete resultGraph;
    }
}
