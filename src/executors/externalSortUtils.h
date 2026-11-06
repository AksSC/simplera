#pragma once
#include "global.h"
#include <queue>

struct TempRun
{
    string name;
    uint columnCount = 0;
    uint maxRowsPerBlock = 0;
    long long rowCount = 0;
    vector<uint> rowsPerBlockCount;
};

class TempRunReader
{
    TempRun run;
    uint pageIndex = 0;
    uint rowIndexInPage = 0;
    vector<vector<int>> currentPageRows;
    bool pageLoaded = false;

    bool loadPage();

public:
    TempRunReader();
    TempRunReader(const TempRun &run);
    bool hasNext();
    vector<int> getNext();
};

class TempRunWriter
{
    TempRun run;
    vector<vector<int>> bufferRows;
    uint nextPageIndex = 0;

    void flush();

public:
    TempRunWriter();
    TempRunWriter(const string &runName, uint columnCount, uint maxRowsPerBlock);
    void appendRow(const vector<int> &row);
    TempRun finalize();
};

class ExternalSortComparator
{
    vector<int> columnIndices;
    vector<SortingStrategy> strategies;

public:
    ExternalSortComparator();
    ExternalSortComparator(const vector<int> &columnIndices, const vector<SortingStrategy> &strategies);
    bool operator()(const vector<int> &a, const vector<int> &b) const;
};

TempRun externalSortTableRange(
    Table *table,
    const string &runPrefix,
    long long startRow,
    long long endRow,
    const vector<int> &sortColumnIndices,
    const vector<SortingStrategy> &sortingStrategies);

void rewriteTableRangeFromRun(Table *table, const TempRun &sortedRun, long long startRow, long long endRow);
void cleanupTempRun(const TempRun &run);
void cleanupTempRuns(const vector<TempRun> &runs);
