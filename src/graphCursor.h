#pragma once
#include "bufferManager.h"

enum class GraphCursorType
{
    NODES,
    EDGES
};

class GraphCursor
{
public:
    Page page;
    int pageIndex;
    string graphName;
    GraphCursorType cursorType;
    int pagePointer;
    uint columnCount;
    uint maxRowsPerBlock;

public:
    GraphCursor(string graphName, GraphCursorType cursorType, int pageIndex, uint rowCount, uint columnCount, uint maxRowsPerBlock);
    vector<int> getNext();
    void nextPage(int pageIndex, uint rowCount);
};
