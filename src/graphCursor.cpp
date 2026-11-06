#include "graphCatalogue.h"
#include "graphCursor.h"

extern Logger logger;
extern BufferManager bufferManager;
extern GraphCatalogue graphCatalogue;

GraphCursor::GraphCursor(string graphName, GraphCursorType cursorType, int pageIndex, uint rowCount, uint columnCount, uint maxRowsPerBlock)
{
    logger.log("GraphCursor::GraphCursor");
    this->graphName = graphName;
    this->cursorType = cursorType;
    this->pageIndex = pageIndex;
    this->pagePointer = 0;
    this->columnCount = columnCount;
    this->maxRowsPerBlock = maxRowsPerBlock;
    string pageTableName = graphName + (cursorType == GraphCursorType::NODES ? "_Nodes" : "_Edges");
    this->page = bufferManager.getGraphPage(pageTableName, pageIndex, rowCount, columnCount, maxRowsPerBlock);
}

vector<int> GraphCursor::getNext()
{
    logger.log("GraphCursor::getNext");
    vector<int> result = this->page.getRow(this->pagePointer);
    this->pagePointer++;
    if (result.empty())
    {
        Graph *graph = graphCatalogue.getGraph(this->graphName);
        if (graph != nullptr)
        {
            if (this->cursorType == GraphCursorType::NODES)
                graph->getNextNodePage(this);
            else
                graph->getNextEdgePage(this);
        }
        if (!this->pagePointer)
        {
            result = this->page.getRow(this->pagePointer);
            this->pagePointer++;
        }
    }
    return result;
}

void GraphCursor::nextPage(int pageIndex, uint rowCount)
{
    logger.log("GraphCursor::nextPage");
    string pageTableName = this->graphName + (this->cursorType == GraphCursorType::NODES ? "_Nodes" : "_Edges");
    this->page = bufferManager.getGraphPage(pageTableName, pageIndex, rowCount, this->columnCount, this->maxRowsPerBlock);
    this->pageIndex = pageIndex;
    this->pagePointer = 0;
}
