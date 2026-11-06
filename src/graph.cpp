#include "global.h"
#include "graph.h"
// #include <bits/stdc++.h>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <unordered_map>
#include <functional>
extern Logger logger;
extern BufferManager bufferManager;

Graph::Graph()
{
    logger.log("Graph::Graph");
}

Graph::Graph(string graphName, char graphType)
{
    logger.log("Graph::Graph");
    this->graphName = graphName;
    this->graphType = graphType;
    string suffix = graphType == 'D' ? "_D.csv" : "_U.csv";
    this->nodeSourceFileName = "../data/" + graphName + "_Nodes" + suffix;
    this->edgeSourceFileName = "../data/" + graphName + "_Edges" + suffix;
}

bool Graph::load()
{
    logger.log("Graph::load");
    fstream finNodes(this->nodeSourceFileName, ios::in);
    fstream finEdges(this->edgeSourceFileName, ios::in);
    string line;
    if (getline(finNodes, line))
    {
        finNodes.close();
        if (!this->extractNodeAttributes(line))
        {
            finEdges.close();
            return false;
        }
    }
    else
    {
        finNodes.close();
        finEdges.close();
        return false;
    }

    if (getline(finEdges, line))
    {
        finEdges.close();
        if (!this->extractEdgeAttributes(line))
            return false;
    }
    else
    {
        finEdges.close();
        return false;
    }

    if (!this->blockifyNodes())
        return false;
    if (!this->blockifyEdges())
        return false;
    return true;
}

void Graph::unload()
{
    logger.log("Graph::unload");
    for (uint i = 0; i < this->nodeBlockCount; i++)
        bufferManager.deleteFile(this->graphName + "_Nodes", i);
    for (uint i = 0; i < this->edgeBlockCount; i++)
        bufferManager.deleteFile(this->graphName + "_Edges", i);
    for (uint i = 0; i < this->degreeBlockCount; i++)
        bufferManager.deleteFile(this->graphName + "_Degrees", i);
}

void Graph::print()
{
    logger.log("Graph::print");
    cout << this->nodeCount << endl;
    cout << this->edgeCount << endl;
    cout << this->graphType << endl;
    cout << endl;

    GraphCursor nodeCursor = this->getNodeCursor();
    vector<int> nodeRow = nodeCursor.getNext();
    while (!nodeRow.empty())
    {
        for (size_t i = 0; i < nodeRow.size(); i++)
        {
            if (i != 0)
                cout << ",";
            cout << nodeRow[i];
        }
        cout << endl;
        nodeRow = nodeCursor.getNext();
    }
    cout << endl;

    GraphCursor edgeCursor = this->getEdgeCursor();
    vector<int> edgeRow = edgeCursor.getNext();
    while (!edgeRow.empty())
    {
        for (size_t i = 0; i < edgeRow.size(); i++)
        {
            if (i != 0)
                cout << ",";
            cout << edgeRow[i];
        }
        cout << endl;
        edgeRow = edgeCursor.getNext();
    }
}

void Graph::makePermanent()
{
    logger.log("Graph::makePermanent");
    string suffix = this->graphType == 'D' ? "_D.csv" : "_U.csv";
    string nodeFileName = "../data/" + this->graphName + "_Nodes" + suffix;
    string edgeFileName = "../data/" + this->graphName + "_Edges" + suffix;

    ofstream foutNodes(nodeFileName, ios::trunc);
    for (size_t i = 0; i < this->nodeAttributes.size(); i++)
    {
        if (i != 0)
            foutNodes << ",";
        foutNodes << this->nodeAttributes[i];
    }
    foutNodes << endl;

    GraphCursor nodeCursor = this->getNodeCursor();
    vector<int> nodeRow = nodeCursor.getNext();
    while (!nodeRow.empty())
    {
        for (size_t i = 0; i < nodeRow.size(); i++)
        {
            if (i != 0)
                foutNodes << ",";
            foutNodes << nodeRow[i];
        }
        foutNodes << endl;
        nodeRow = nodeCursor.getNext();
    }
    foutNodes.close();

    ofstream foutEdges(edgeFileName, ios::trunc);
    for (size_t i = 0; i < this->edgeAttributes.size(); i++)
    {
        if (i != 0)
            foutEdges << ",";
        foutEdges << this->edgeAttributes[i];
    }
    foutEdges << endl;

    GraphCursor edgeCursor = this->getEdgeCursor();
    vector<int> edgeRow = edgeCursor.getNext();
    long long int edgeIndex = 0;
    while (!edgeRow.empty())
    {
        // For undirected graphs, only export original edges (every other edge: 0, 2, 4, ...)
        // For directed graphs, export all edges
        bool shouldExport = (this->graphType == 'D') || (edgeIndex % 2 == 0);
        if (shouldExport)
        {
            for (size_t i = 0; i < edgeRow.size(); i++)
            {
                if (i != 0)
                    foutEdges << ",";
                foutEdges << edgeRow[i];
            }
            foutEdges << endl;
        }
        edgeIndex++;
        edgeRow = edgeCursor.getNext();
    }
    foutEdges.close();
}

static inline string trimToken(string token)
{
    token.erase(remove_if(token.begin(), token.end(), [](unsigned char c) {
        return std::isspace(c);
    }), token.end());
    return token;
}

static inline bool parseIntToken(const string &token, int &value)
{
    try
    {
        size_t idx = 0;
        value = stoi(token, &idx);
        return idx == token.size();
    }
    catch (...)
    {
        return false;
    }
}

bool Graph::extractNodeAttributes(string firstLine)
{
    logger.log("Graph::extractNodeAttributes");
    unordered_set<string> attrNames;
    string token;
    stringstream ss(firstLine);
    bool isFirst = true;
    while (getline(ss, token, ','))
    {
        token = trimToken(token);
        if (isFirst)
        {
            if (token != "NodeID")
                return false;
            isFirst = false;
        }
        if (attrNames.count(token))
            return false;
        attrNames.insert(token);
        this->nodeAttributes.emplace_back(token);
    }
    if (this->nodeAttributes.size() < 1)
        return false;
    this->nodeAttributeCount = this->nodeAttributes.size() - 1;
    this->maxNodesPerBlock = (uint)((BLOCK_SIZE * 1000) / (sizeof(int) * this->nodeAttributes.size()));
    return true;
}

bool Graph::extractEdgeAttributes(string firstLine)
{
    logger.log("Graph::extractEdgeAttributes");
    unordered_set<string> attrNames;
    string token;
    stringstream ss(firstLine);
    int colIndex = 0;
    while (getline(ss, token, ','))
    {
        token = trimToken(token);
        if (colIndex == 0 && token != "Src_NodeID")
            return false;
        if (colIndex == 1 && token != "Dest_NodeID")
            return false;
        if (colIndex == 2 && token != "Weight")
            return false;
        if (attrNames.count(token))
            return false;
        attrNames.insert(token);
        this->edgeAttributes.emplace_back(token);
        colIndex++;
    }
    if (this->edgeAttributes.size() < 3)
        return false;
    this->edgeAttributeCount = this->edgeAttributes.size() - 3;
    this->maxEdgesPerBlock = (uint)((BLOCK_SIZE * 1000) / (sizeof(int) * this->edgeAttributes.size()));
    return true;
}

bool Graph::blockifyNodes()
{
    logger.log("Graph::blockifyNodes");
    ifstream fin(this->nodeSourceFileName, ios::in);
    if (!fin.is_open() || this->maxNodesPerBlock == 0)
        return false;
    string line, token;
    vector<int> row(this->nodeAttributes.size(), 0);
    vector<vector<int>> rowsInPage(this->maxNodesPerBlock, row);
    int pageRowCounter = 0;
    int internalIndex = 0;
    getline(fin, line); // skip header
    while (getline(fin, line))
    {
        stringstream ss(line);
        for (int col = 0; col < (int)this->nodeAttributes.size(); col++)
        {
            if (!getline(ss, token, ','))
                return false;
            token = trimToken(token);
            if (!parseIntToken(token, row[col]))
                return false;
            if (col > 0 && row[col] != 0 && row[col] != 1)
                return false;
        }
        while (getline(ss, token, ','))
        {
            token = trimToken(token);
            if (!token.empty())
                return false;
        }
        int nodeId = row[0];
        // Check for duplicate node IDs
        if (this->nodeIdToIndex.count(nodeId))
            return false;
        this->nodeIdToIndex[nodeId] = internalIndex;
        this->nodeIds.push_back(nodeId);
        internalIndex++;
        rowsInPage[pageRowCounter] = row;
        pageRowCounter++;
        this->nodeCount++;
        if (pageRowCounter == (int)this->maxNodesPerBlock)
        {
            bufferManager.writePage(this->graphName + "_Nodes", this->nodeBlockCount, rowsInPage, pageRowCounter);
            this->nodeBlockCount++;
            this->nodesPerBlockCount.emplace_back(pageRowCounter);
            pageRowCounter = 0;
        }
    }
    if (pageRowCounter)
    {
        bufferManager.writePage(this->graphName + "_Nodes", this->nodeBlockCount, rowsInPage, pageRowCounter);
        this->nodeBlockCount++;
        this->nodesPerBlockCount.emplace_back(pageRowCounter);
    }
    if (this->nodeCount == 0)
        return false;
    return true;
}

bool Graph::writeDegreePagesFromBuckets(uint bucketSize, const vector<string> &bucketFiles)
{
    logger.log("Graph::writeDegreePagesFromBuckets");
    if (bucketSize == 0)
        return false;
    uint degreeColumns = 3;
    vector<int> row(degreeColumns, 0);
    vector<vector<int>> rowsInPage(this->maxDegreesPerBlock, row);
    int pageRowCounter = 0;

    long long baseNodeId = 0;
    for (size_t bucketIndex = 0; bucketIndex < bucketFiles.size(); bucketIndex++)
    {
        uint currentBucketSize = (uint)min((long long)bucketSize, this->nodeCount - baseNodeId);
        vector<int> outDegree(currentBucketSize, 0);
        vector<int> inDegree(currentBucketSize, 0);

        ifstream fin(bucketFiles[bucketIndex], ios::in);
        int nodeId = 0, outDelta = 0, inDelta = 0;
        if (fin.is_open())
        {
            while (fin >> nodeId >> outDelta >> inDelta)
            {
                if (nodeId < baseNodeId || nodeId >= baseNodeId + currentBucketSize)
                    return false;
                uint offset = (uint)(nodeId - baseNodeId);
                outDegree[offset] += outDelta;
                inDegree[offset] += inDelta;
            }
            fin.close();
        }

        for (uint offset = 0; offset < currentBucketSize; offset++)
        {
            row[0] = (int)(baseNodeId + offset);
            row[1] = outDegree[offset];
            row[2] = inDegree[offset];
            rowsInPage[pageRowCounter] = row;
            pageRowCounter++;
            if (pageRowCounter == (int)this->maxDegreesPerBlock)
            {
                bufferManager.writePage(this->graphName + "_Degrees", this->degreeBlockCount, rowsInPage, pageRowCounter);
                this->degreeBlockCount++;
                this->degreesPerBlockCount.emplace_back(pageRowCounter);
                pageRowCounter = 0;
            }
        }
        baseNodeId += currentBucketSize;
    }

    if (pageRowCounter)
    {
        bufferManager.writePage(this->graphName + "_Degrees", this->degreeBlockCount, rowsInPage, pageRowCounter);
        this->degreeBlockCount++;
        this->degreesPerBlockCount.emplace_back(pageRowCounter);
    }
    return true;
}

bool Graph::blockifyEdges()
{
    logger.log("Graph::blockifyEdges");
    ifstream fin(this->edgeSourceFileName, ios::in);
    if (!fin.is_open())
        return false;
    string line, token;
    getline(fin, line); // skip header

    vector<int> row(this->edgeAttributes.size(), 0);
    uint degreeColumns = 3;
    this->maxDegreesPerBlock = (uint)((BLOCK_SIZE * 1000) / (sizeof(int) * degreeColumns));
    if (this->maxEdgesPerBlock == 0 || this->maxDegreesPerBlock == 0)
        return false;

    uint bucketSize = this->maxDegreesPerBlock * BLOCK_COUNT;
    if (bucketSize == 0)
        return false;
    uint bucketCount = (uint)((this->nodeCount + bucketSize - 1) / bucketSize);
    vector<string> bucketFiles(bucketCount);
    for (uint i = 0; i < bucketCount; i++)
    {
        bucketFiles[i] = "../data/temp/" + this->graphName + "_DegreeBucket_" + to_string(i);
        ofstream clearFile(bucketFiles[i], ios::trunc);
        clearFile.close();
    }
    auto cleanupBuckets = [&]() {
        for (const auto &fileName : bucketFiles)
            bufferManager.deleteFile(fileName);
    };

    auto appendDegreeDelta = [&](int nodeId, int outDelta, int inDelta) -> bool {
        // Use nodeIdToIndex to get internal index for non-contiguous node IDs
        auto it = this->nodeIdToIndex.find(nodeId);
        if (it == this->nodeIdToIndex.end())
            return false;
        int internalIndex = it->second;
        uint bucketIndex = (uint)(internalIndex / bucketSize);
        ofstream fout(bucketFiles[bucketIndex], ios::app);
        if (!fout.is_open())
            return false;
        fout << internalIndex << " " << outDelta << " " << inDelta << endl;
        fout.close();
        return true;
    };

    vector<vector<int>> rowsInPage(this->maxEdgesPerBlock, row);
    int pageRowCounter = 0;
    bool sawEdge = false;

    auto writeEdgeRow = [&](const vector<int> &edgeRow) {
        rowsInPage[pageRowCounter] = edgeRow;
        pageRowCounter++;
        this->edgeCount++;
        if (pageRowCounter == (int)this->maxEdgesPerBlock)
        {
            bufferManager.writePage(this->graphName + "_Edges", this->edgeBlockCount, rowsInPage, pageRowCounter);
            this->edgeBlockCount++;
            this->edgesPerBlockCount.emplace_back(pageRowCounter);
            pageRowCounter = 0;
        }
    };

    while (getline(fin, line))
    {
        stringstream ss(line);
        for (int col = 0; col < (int)this->edgeAttributes.size(); col++)
        {
            if (!getline(ss, token, ','))
            {
                cleanupBuckets();
                return false;
            }
            token = trimToken(token);
            if (!parseIntToken(token, row[col]))
            {
                cleanupBuckets();
                return false;
            }
            if (col == 2 && row[col] < 0)
            {
                cleanupBuckets();
                return false;
            }
            if (col > 2 && row[col] != 0 && row[col] != 1)
            {
                cleanupBuckets();
                return false;
            }
        }
        while (getline(ss, token, ','))
        {
            token = trimToken(token);
            if (!token.empty())
            {
                cleanupBuckets();
                return false;
            }
        }

        int src = row[0];
        int dest = row[1];
        if (this->nodeIdToIndex.find(src) == this->nodeIdToIndex.end() ||
            this->nodeIdToIndex.find(dest) == this->nodeIdToIndex.end())
        {
            cleanupBuckets();
            return false;
        }

        sawEdge = true;
        if (this->graphType == 'U')
        {
            if (!appendDegreeDelta(src, 1, 1))
            {
                cleanupBuckets();
                return false;
            }
            if (!appendDegreeDelta(dest, 1, 1))
            {
                cleanupBuckets();
                return false;
            }
            writeEdgeRow(row); // Store original edge
            vector<int> rev = row;
            int temp = rev[0];
            rev[0] = rev[1];
            rev[1] = temp;
            writeEdgeRow(rev); // Store reversed edge for internal use
        }
        else
        {
            if (!appendDegreeDelta(src, 1, 0))
            {
                cleanupBuckets();
                return false;
            }
            if (!appendDegreeDelta(dest, 0, 1))
            {
                cleanupBuckets();
                return false;
            }
            writeEdgeRow(row);
        }
    }

    if (!sawEdge)
    {
        cleanupBuckets();
        return false;
    }

    if (pageRowCounter)
    {
        bufferManager.writePage(this->graphName + "_Edges", this->edgeBlockCount, rowsInPage, pageRowCounter);
        this->edgeBlockCount++;
        this->edgesPerBlockCount.emplace_back(pageRowCounter);
    }

    if (!this->writeDegreePagesFromBuckets(bucketSize, bucketFiles))
    {
        cleanupBuckets();
        return false;
    }

    cleanupBuckets();
    return true;
}
GraphCursor Graph::getNodeCursor()
{
    logger.log("Graph::getNodeCursor");
    uint rowCount = this->nodesPerBlockCount[0];
    GraphCursor cursor(this->graphName, GraphCursorType::NODES, 0, rowCount, (uint)this->nodeAttributes.size(), this->maxNodesPerBlock);
    return cursor;
}

GraphCursor Graph::getEdgeCursor()
{
    logger.log("Graph::getEdgeCursor");
    uint rowCount = this->edgesPerBlockCount[0];
    GraphCursor cursor(this->graphName, GraphCursorType::EDGES, 0, rowCount, (uint)this->edgeAttributes.size(), this->maxEdgesPerBlock);
    return cursor;
}

void Graph::getNextNodePage(GraphCursor *cursor)
{
    logger.log("Graph::getNextNodePage");
    if (cursor->pageIndex < (int)this->nodeBlockCount - 1)
        cursor->nextPage(cursor->pageIndex + 1, this->nodesPerBlockCount[cursor->pageIndex + 1]);
}

void Graph::getNextEdgePage(GraphCursor *cursor)
{
    logger.log("Graph::getNextEdgePage");
    if (cursor->pageIndex < (int)this->edgeBlockCount - 1)
        cursor->nextPage(cursor->pageIndex + 1, this->edgesPerBlockCount[cursor->pageIndex + 1]);
}

bool Graph::getDegree(int nodeId, int &outDegree, int &inDegree)
{
    logger.log("Graph::getDegree");
    auto it = this->nodeIdToIndex.find(nodeId);
    if (it == this->nodeIdToIndex.end())
        return false;
    int internalIndex = it->second;
    if (this->degreeBlockCount == 0 || this->maxDegreesPerBlock == 0)
        return false;

    int pageIndex = internalIndex / (int)this->maxDegreesPerBlock;
    int rowIndex = internalIndex % (int)this->maxDegreesPerBlock;
    if (pageIndex < 0 || pageIndex >= (int)this->degreeBlockCount)
        return false;
    uint rowCount = this->degreesPerBlockCount[pageIndex];
    if ((uint)rowIndex >= rowCount)
        return false;

    Page degreePage = bufferManager.getGraphPage(this->graphName + "_Degrees",
        pageIndex, rowCount, 3, this->maxDegreesPerBlock);
    vector<int> row = degreePage.getRow(rowIndex);
    if (row.size() < 3 || row[0] != internalIndex)
        return false;
    outDegree = row[1];
    inDegree = row[2];
    return true;
}
