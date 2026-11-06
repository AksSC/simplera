#include "externalSortUtils.h"
#include <algorithm>

using namespace std;

namespace
{
    struct RowLocator
    {
        uint pageIndex = 0;
        long long globalRowBeforePage = 0;
    };

    RowLocator locateRow(Table *table, long long rowIndex)
    {
        RowLocator locator;
        if (table->blockCount == 0)
            return locator;

        if (rowIndex <= 0)
            return locator;

        if (rowIndex >= table->rowCount)
        {
            locator.pageIndex = table->blockCount - 1;
            long long rowsBefore = table->rowCount - table->rowsPerBlockCount[locator.pageIndex];
            locator.globalRowBeforePage = max(0LL, rowsBefore);
            return locator;
        }

        long long rowsBefore = 0;
        for (uint pageIndex = 0; pageIndex < table->blockCount; pageIndex++)
        {
            long long pageRows = table->rowsPerBlockCount[pageIndex];
            if (rowIndex < rowsBefore + pageRows)
            {
                locator.pageIndex = pageIndex;
                locator.globalRowBeforePage = rowsBefore;
                return locator;
            }
            rowsBefore += pageRows;
        }

        locator.pageIndex = table->blockCount - 1;
        locator.globalRowBeforePage = max(0LL, table->rowCount - table->rowsPerBlockCount[locator.pageIndex]);
        return locator;
    }

    struct PQNode
    {
        vector<int> row;
        int runIndex = 0;
    };

    class PQComparator
    {
        ExternalSortComparator rowComparator;

    public:
        PQComparator() {}
        PQComparator(const ExternalSortComparator &rowComparator) : rowComparator(rowComparator) {}

        bool operator()(const PQNode &a, const PQNode &b) const
        {
            if (rowComparator(a.row, b.row))
                return false;
            if (rowComparator(b.row, a.row))
                return true;
            return a.runIndex > b.runIndex;
        }
    };

    static long long sortRunCounter = 0;

    void stableMergeSortRecursive(vector<vector<int>> &rows, int left, int right, const ExternalSortComparator &comparator)
    {
        if (left >= right)
            return;
        int mid = left + (right - left) / 2;
        stableMergeSortRecursive(rows, left, mid, comparator);
        stableMergeSortRecursive(rows, mid + 1, right, comparator);

        vector<vector<int>> merged;
        merged.reserve(right - left + 1);
        int i = left;
        int j = mid + 1;
        while (i <= mid && j <= right)
        {
            if (comparator(rows[i], rows[j]))
                merged.push_back(rows[i++]);
            else if (comparator(rows[j], rows[i]))
                merged.push_back(rows[j++]);
            else
                merged.push_back(rows[i++]);
        }
        while (i <= mid)
            merged.push_back(rows[i++]);
        while (j <= right)
            merged.push_back(rows[j++]);
        for (int k = 0; k < (int)merged.size(); k++)
            rows[left + k] = merged[k];
    }

    void stableMergeSortRows(vector<vector<int>> &rows, const ExternalSortComparator &comparator)
    {
        if (!rows.empty())
            stableMergeSortRecursive(rows, 0, (int)rows.size() - 1, comparator);
    }

    TempRun createSortedRunFromRows(
        const vector<vector<int>> &rows,
        const string &runName,
        uint columnCount,
        uint maxRowsPerBlock,
        const ExternalSortComparator &comparator)
    {
        vector<vector<int>> sortedRows = rows;
        stableMergeSortRows(sortedRows, comparator);

        TempRunWriter writer(runName, columnCount, maxRowsPerBlock);
        for (const vector<int> &row : sortedRows)
            writer.appendRow(row);
        return writer.finalize();
    }

    vector<TempRun> createInitialSortedRuns(
        Table *table,
        const string &runPrefix,
        long long startRow,
        long long endRow,
        const ExternalSortComparator &comparator)
    {
        vector<TempRun> runs;
        if (startRow >= endRow)
            return runs;

        long long rowsPerRun = (long long)max(1U, BLOCK_COUNT) * (long long)table->maxRowsPerBlock;
        vector<vector<int>> chunkRows;
        chunkRows.reserve((size_t)min(rowsPerRun, endRow - startRow));

        RowLocator startLocator = locateRow(table, startRow);
        Cursor cursor(table->tableName, startLocator.pageIndex);
        vector<int> row;
        long long currentRowIndex = startLocator.globalRowBeforePage;

        while (currentRowIndex < startRow && !(row = cursor.getNext()).empty())
            currentRowIndex++;

        while (!(row = cursor.getNext()).empty())
        {
            if (currentRowIndex >= endRow)
                break;

            if (currentRowIndex >= startRow)
            {
                chunkRows.push_back(row);
                if ((long long)chunkRows.size() == rowsPerRun)
                {
                    string runName = runPrefix + "_Run" + to_string(sortRunCounter++);
                    runs.push_back(createSortedRunFromRows(
                        chunkRows,
                        runName,
                        table->columnCount,
                        table->maxRowsPerBlock,
                        comparator));
                    chunkRows.clear();
                }
            }
            currentRowIndex++;
        }

        if (!chunkRows.empty())
        {
            string runName = runPrefix + "_Run" + to_string(sortRunCounter++);
            runs.push_back(createSortedRunFromRows(
                chunkRows,
                runName,
                table->columnCount,
                table->maxRowsPerBlock,
                comparator));
        }

        return runs;
    }

    TempRun mergeRunBatch(const vector<TempRun> &runsToMerge, const string &runName, const ExternalSortComparator &comparator)
    {
        TempRunWriter writer(runName, runsToMerge[0].columnCount, runsToMerge[0].maxRowsPerBlock);
        priority_queue<PQNode, vector<PQNode>, PQComparator> pq{PQComparator(comparator)};
        vector<TempRunReader> readers;
        readers.reserve(runsToMerge.size());

        for (size_t i = 0; i < runsToMerge.size(); i++)
        {
            readers.push_back(TempRunReader(runsToMerge[i]));
            if (readers.back().hasNext())
            {
                PQNode node;
                node.row = readers.back().getNext();
                node.runIndex = (int)i;
                pq.push(node);
            }
        }

        while (!pq.empty())
        {
            PQNode topNode = pq.top();
            pq.pop();
            writer.appendRow(topNode.row);

            if (readers[topNode.runIndex].hasNext())
            {
                PQNode nextNode;
                nextNode.row = readers[topNode.runIndex].getNext();
                nextNode.runIndex = topNode.runIndex;
                pq.push(nextNode);
            }
        }

        return writer.finalize();
    }
}

TempRunReader::TempRunReader() {}

TempRunReader::TempRunReader(const TempRun &run)
{
    this->run = run;
}

bool TempRunReader::loadPage()
{
    if (this->pageIndex >= this->run.rowsPerBlockCount.size())
        return false;

    uint rowsInPage = this->run.rowsPerBlockCount[this->pageIndex];
    Page page = bufferManager.getGraphPage(
        this->run.name,
        this->pageIndex,
        rowsInPage,
        this->run.columnCount,
        this->run.maxRowsPerBlock);

    this->currentPageRows.clear();
    this->currentPageRows.reserve(rowsInPage);
    for (uint i = 0; i < rowsInPage; i++)
        this->currentPageRows.push_back(page.getRow(i));

    this->rowIndexInPage = 0;
    this->pageLoaded = true;
    return true;
}

bool TempRunReader::hasNext()
{
    if (!this->pageLoaded)
        return this->loadPage();

    if (this->rowIndexInPage < this->currentPageRows.size())
        return true;

    this->pageIndex++;
    this->pageLoaded = false;
    return this->loadPage();
}

vector<int> TempRunReader::getNext()
{
    if (!this->hasNext())
        return {};
    return this->currentPageRows[this->rowIndexInPage++];
}

TempRunWriter::TempRunWriter() {}

TempRunWriter::TempRunWriter(const string &runName, uint columnCount, uint maxRowsPerBlock)
{
    this->run.name = runName;
    this->run.columnCount = columnCount;
    this->run.maxRowsPerBlock = maxRowsPerBlock;
    this->bufferRows.reserve(maxRowsPerBlock);
}

void TempRunWriter::flush()
{
    if (this->bufferRows.empty())
        return;

    uint rowsWritten = this->bufferRows.size();
    bufferManager.writePage(this->run.name, this->nextPageIndex, this->bufferRows, rowsWritten);
    this->run.rowsPerBlockCount.push_back(rowsWritten);
    this->run.rowCount += rowsWritten;
    this->nextPageIndex++;
    this->bufferRows.clear();
}

void TempRunWriter::appendRow(const vector<int> &row)
{
    this->bufferRows.push_back(row);
    if (this->bufferRows.size() == this->run.maxRowsPerBlock)
        this->flush();
}

TempRun TempRunWriter::finalize()
{
    this->flush();
    return this->run;
}

ExternalSortComparator::ExternalSortComparator() {}

ExternalSortComparator::ExternalSortComparator(
    const vector<int> &columnIndices,
    const vector<SortingStrategy> &strategies)
{
    this->columnIndices = columnIndices;
    this->strategies = strategies;
}

bool ExternalSortComparator::operator()(const vector<int> &a, const vector<int> &b) const
{
    for (size_t i = 0; i < this->columnIndices.size(); i++)
    {
        int colIndex = this->columnIndices[i];
        if (a[colIndex] == b[colIndex])
            continue;
        if (this->strategies[i] == ASC)
            return a[colIndex] < b[colIndex];
        return a[colIndex] > b[colIndex];
    }
    return false;
}

TempRun externalSortTableRange(
    Table *table,
    const string &runPrefix,
    long long startRow,
    long long endRow,
    const vector<int> &sortColumnIndices,
    const vector<SortingStrategy> &sortingStrategies)
{
    ExternalSortComparator comparator(sortColumnIndices, sortingStrategies);
    vector<TempRun> runs = createInitialSortedRuns(table, runPrefix, startRow, endRow, comparator);

    if (runs.empty())
        return TempRun();

    uint mergeFanIn = max(2U, BLOCK_COUNT > 1 ? BLOCK_COUNT - 1 : 1U);

    while (runs.size() > 1)
    {
        vector<TempRun> mergedRuns;
        for (size_t i = 0; i < runs.size(); i += mergeFanIn)
        {
            size_t batchEnd = min(i + mergeFanIn, runs.size());
            vector<TempRun> batch(runs.begin() + i, runs.begin() + batchEnd);
            string mergedRunName = runPrefix + "_Merge" + to_string(sortRunCounter++);
            mergedRuns.push_back(mergeRunBatch(batch, mergedRunName, comparator));
            cleanupTempRuns(batch);
        }
        runs.swap(mergedRuns);
    }

    return runs.front();
}

void rewriteTableRangeFromRun(Table *table, const TempRun &sortedRun, long long startRow, long long endRow)
{
    if (sortedRun.name.empty() || startRow >= endRow)
        return;

    TempRunReader reader(sortedRun);
    RowLocator startLocator = locateRow(table, startRow);
    RowLocator endLocator = locateRow(table, endRow - 1);
    uint firstPage = startLocator.pageIndex;
    uint lastPage = endLocator.pageIndex;

    long long pageGlobalStart = startLocator.globalRowBeforePage;
    for (uint pageIndex = firstPage; pageIndex <= lastPage; pageIndex++)
    {
        Page page = bufferManager.getPage(table->tableName, pageIndex);
        uint rowsInPage = table->rowsPerBlockCount[pageIndex];
        vector<vector<int>> rewrittenRows;
        rewrittenRows.reserve(rowsInPage);
        long long pageGlobalEnd = pageGlobalStart + rowsInPage;
        uint replaceBegin = (uint)max(0LL, startRow - pageGlobalStart);
        uint replaceEnd = (uint)min((long long)rowsInPage, endRow - pageGlobalStart);

        for (uint rowIndex = 0; rowIndex < rowsInPage; rowIndex++)
        {
            if (rowIndex >= replaceBegin && rowIndex < replaceEnd)
            {
                rewrittenRows.push_back(reader.getNext());
            }
            else
            {
                rewrittenRows.push_back(page.getRow(rowIndex));
            }
        }
        bufferManager.writePage(table->tableName, pageIndex, rewrittenRows, rowsInPage);
        bufferManager.removeFromPool(table->tableName, pageIndex);
        pageGlobalStart = pageGlobalEnd;
    }
}

void cleanupTempRun(const TempRun &run)
{
    if (run.name.empty())
        return;
    for (uint pageIndex = 0; pageIndex < run.rowsPerBlockCount.size(); pageIndex++)
    {
        bufferManager.removeFromPool(run.name, pageIndex);
        bufferManager.deleteFile(run.name, pageIndex);
    }
}

void cleanupTempRuns(const vector<TempRun> &runs)
{
    for (const TempRun &run : runs)
        cleanupTempRun(run);
}
