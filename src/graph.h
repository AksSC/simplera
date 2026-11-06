#pragma once
#include "graphCursor.h"
#include <unordered_map>

class Graph
{
public:
    string graphName = "";
    char graphType = 'D';

    // Source file names
    string nodeSourceFileName = "";
    string edgeSourceFileName = "";

    // Node metadata
    vector<string> nodeAttributes;
    uint nodeAttributeCount = 0;
    long long int nodeCount = 0;
    uint nodeBlockCount = 0;
    uint maxNodesPerBlock = 0;
    vector<uint> nodesPerBlockCount;
    unordered_map<int, int> nodeIdToIndex; // Maps original nodeId -> internal index
    vector<int> nodeIds; // List of original node IDs in order
    bool directed = true;

    // Edge metadata
    vector<string> edgeAttributes;
    uint edgeAttributeCount = 0;
    long long int edgeCount = 0;
    uint edgeBlockCount = 0;
    uint maxEdgesPerBlock = 0;
    vector<uint> edgesPerBlockCount;

    // Degree metadata (stored in file blocks)
    uint degreeBlockCount = 0;
    uint maxDegreesPerBlock = 0;
    vector<uint> degreesPerBlockCount;

    Graph();
    Graph(string graphName, char graphType);

    // Core operations (to be implemented in later phases)
    bool load();
    void unload();
    void print();
    void makePermanent();
    bool extractNodeAttributes(string firstLine);
    bool extractEdgeAttributes(string firstLine);
    bool blockifyNodes();
    bool blockifyEdges();
    bool writeDegreePagesFromBuckets(uint bucketSize, const vector<string> &bucketFiles);

    GraphCursor getNodeCursor();
    GraphCursor getEdgeCursor();
    void getNextNodePage(GraphCursor *cursor);
    void getNextEdgePage(GraphCursor *cursor);
    bool getDegree(int nodeId, int &outDegree, int &inDegree);
};
