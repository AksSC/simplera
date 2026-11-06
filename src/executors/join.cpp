#include "global.h"
#include <unordered_map>
#include <algorithm>

using namespace std;

struct JoinPartitionInfo
{
    string name;
    long long rowCount = 0;
    uint blockCount = 0;
    vector<uint> rowsPerBlockCount;
    uint columnCount = 0;
    uint maxRowsPerBlock = 0;
};

namespace
{
    static long long joinTempCounter = 0;
    const int MAX_JOIN_REPARTITION_DEPTH = 8;

    bool parseIntegerStrict(const string &token, int &value)
    {
        try
        {
            size_t used = 0;
            value = stoi(token, &used);
            return used == token.size();
        }
        catch (...)
        {
            return false;
        }
    }

    pair<string, string> parseTableColumn(const string &token)
    {
        size_t dotPos = token.find('.');
        if (dotPos == string::npos || dotPos == 0 || dotPos + 1 >= token.size())
            return make_pair(string(""), string(""));
        if (token.find('.', dotPos + 1) != string::npos)
            return make_pair(string(""), string(""));
        return make_pair(token.substr(0, dotPos), token.substr(dotPos + 1));
    }

    bool parseWhereOperator(const string &token, BinaryOperator &op)
    {
        if (token == "==") op = EQUAL;
        else if (token == "!=") op = NOT_EQUAL;
        else if (token == ">") op = GREATER_THAN;
        else if (token == ">=") op = GEQ;
        else if (token == "<") op = LESS_THAN;
        else if (token == "<=") op = LEQ;
        else return false;
        return true;
    }

    string stripTrailingSemicolon(string token, bool &hadSemicolon)
    {
        hadSemicolon = false;
        if (!token.empty() && token.back() == ';')
        {
            token.pop_back();
            hadSemicolon = true;
        }
        return token;
    }

    bool parseJoinConditionTokens(const vector<string> &onTokens)
    {
        if (onTokens.size() == 3)
        {
            if (onTokens[1] != "==")
                return false;
            pair<string, string> left = parseTableColumn(onTokens[0]);
            pair<string, string> right = parseTableColumn(onTokens[2]);
            if (left.first.empty() || right.first.empty())
                return false;

            parsedQuery.joinConditionType = 0;
            parsedQuery.joinFirstColumnRelationName = left.first;
            parsedQuery.joinFirstColumnName = left.second;
            parsedQuery.joinSecondColumnRelationName = right.first;
            parsedQuery.joinSecondColumnName = right.second;
            return true;
        }

        if (onTokens.size() == 5)
        {
            pair<string, string> left = parseTableColumn(onTokens[0]);
            pair<string, string> right = parseTableColumn(onTokens[2]);
            if (left.first.empty() || right.first.empty() || onTokens[3] != "==")
                return false;

            int targetValue = 0;
            if (!parseIntegerStrict(onTokens[4], targetValue))
                return false;

            if (onTokens[1] == "+")
                parsedQuery.joinConditionType = 1;
            else if (onTokens[1] == "-")
                parsedQuery.joinConditionType = 2;
            else
                return false;

            parsedQuery.joinArithmeticResult = targetValue;
            parsedQuery.joinFirstColumnRelationName = left.first;
            parsedQuery.joinFirstColumnName = left.second;
            parsedQuery.joinSecondColumnRelationName = right.first;
            parsedQuery.joinSecondColumnName = right.second;
            return true;
        }

        return false;
    }

    bool evaluateWhereCondition(int value, BinaryOperator op, int target)
    {
        switch (op)
        {
        case EQUAL: return value == target;
        case NOT_EQUAL: return value != target;
        case GREATER_THAN: return value > target;
        case GEQ: return value >= target;
        case LESS_THAN: return value < target;
        case LEQ: return value <= target;
        default: return true;
        }
    }

    long long logicalJoinKeyForTable1(const vector<int> &row, int col1Idx)
    {
        if (parsedQuery.joinConditionType == 0)
            return row[col1Idx];
        if (parsedQuery.joinConditionType == 1)
            return (long long)parsedQuery.joinArithmeticResult - row[col1Idx];
        return (long long)row[col1Idx] - parsedQuery.joinArithmeticResult;
    }

    long long logicalJoinKeyForTable2(const vector<int> &row, int col2Idx)
    {
        return row[col2Idx];
    }

    bool joinConditionMatches(const vector<int> &row1, const vector<int> &row2, int col1Idx, int col2Idx)
    {
        if (parsedQuery.joinConditionType == 0)
            return row1[col1Idx] == row2[col2Idx];
        if (parsedQuery.joinConditionType == 1)
            return row1[col1Idx] + row2[col2Idx] == parsedQuery.joinArithmeticResult;
        return row1[col1Idx] - row2[col2Idx] == parsedQuery.joinArithmeticResult;
    }

    uint hashPartitionIndex(long long key, uint partitionCount, int seed)
    {
        unsigned long long mixed = (unsigned long long)(key ^ (0x9e3779b97f4a7c15ULL + (unsigned long long)seed * 0x94d049bb133111ebULL));
        mixed ^= (mixed >> 30);
        mixed *= 0xbf58476d1ce4e5b9ULL;
        mixed ^= (mixed >> 27);
        mixed *= 0x94d049bb133111ebULL;
        mixed ^= (mixed >> 31);
        return (uint)(mixed % partitionCount);
    }

    vector<vector<int>> readPartitionPage(const JoinPartitionInfo &part, uint pageIndex)
    {
        vector<vector<int>> rows;
        if (pageIndex >= part.rowsPerBlockCount.size())
            return rows;
        uint rowsInPage = part.rowsPerBlockCount[pageIndex];
        Page page = bufferManager.getGraphPage(part.name, pageIndex, rowsInPage, part.columnCount, part.maxRowsPerBlock);
        rows.reserve(rowsInPage);
        for (uint i = 0; i < rowsInPage; i++)
            rows.push_back(page.getRow(i));
        return rows;
    }

    void flushPartitionBuffer(JoinPartitionInfo &part, vector<vector<int>> &bufferRows)
    {
        if (bufferRows.empty())
            return;
        uint rowsWritten = bufferRows.size();
        bufferManager.writePage(part.name, part.blockCount, bufferRows, rowsWritten);
        part.rowsPerBlockCount.push_back(rowsWritten);
        part.blockCount++;
        part.rowCount += rowsWritten;
        bufferRows.clear();
    }

    vector<JoinPartitionInfo> partitionTable(Table *table, const string &prefix, bool firstTable, int col1Idx, int col2Idx, uint partitionCount, int seed)
    {
        vector<JoinPartitionInfo> partitions(partitionCount);
        vector<vector<vector<int>>> buffers(partitionCount);

        for (uint i = 0; i < partitionCount; i++)
        {
            partitions[i].name = prefix + "_P" + to_string(i);
            partitions[i].columnCount = table->columnCount;
            partitions[i].maxRowsPerBlock = table->maxRowsPerBlock;
            buffers[i].reserve(table->maxRowsPerBlock);
        }

        Cursor cursor = table->getCursor();
        vector<int> row;
        while (!(row = cursor.getNext()).empty())
        {
            long long key = firstTable ? logicalJoinKeyForTable1(row, col1Idx) : logicalJoinKeyForTable2(row, col2Idx);
            uint pid = hashPartitionIndex(key, partitionCount, seed);
            buffers[pid].push_back(row);
            if (buffers[pid].size() == table->maxRowsPerBlock)
                flushPartitionBuffer(partitions[pid], buffers[pid]);
        }

        for (uint i = 0; i < partitionCount; i++)
            flushPartitionBuffer(partitions[i], buffers[i]);
        return partitions;
    }

    vector<JoinPartitionInfo> partitionExistingPartition(const JoinPartitionInfo &source, bool firstTable, int col1Idx, int col2Idx, uint partitionCount, int seed)
    {
        vector<JoinPartitionInfo> partitions(partitionCount);
        vector<vector<vector<int>>> buffers(partitionCount);

        for (uint i = 0; i < partitionCount; i++)
        {
            partitions[i].name = source.name + "_Sub" + to_string(seed) + "_P" + to_string(i);
            partitions[i].columnCount = source.columnCount;
            partitions[i].maxRowsPerBlock = source.maxRowsPerBlock;
            buffers[i].reserve(source.maxRowsPerBlock);
        }

        for (uint pageIndex = 0; pageIndex < source.blockCount; pageIndex++)
        {
            vector<vector<int>> rows = readPartitionPage(source, pageIndex);
            for (const vector<int> &row : rows)
            {
                long long key = firstTable ? logicalJoinKeyForTable1(row, col1Idx) : logicalJoinKeyForTable2(row, col2Idx);
                uint pid = hashPartitionIndex(key, partitionCount, seed);
                buffers[pid].push_back(row);
                if (buffers[pid].size() == source.maxRowsPerBlock)
                    flushPartitionBuffer(partitions[pid], buffers[pid]);
            }
        }

        for (uint i = 0; i < partitionCount; i++)
            flushPartitionBuffer(partitions[i], buffers[i]);
        return partitions;
    }

    void cleanupPartitions(const vector<JoinPartitionInfo> &partitions)
    {
        for (const JoinPartitionInfo &part : partitions)
        {
            for (uint pageIndex = 0; pageIndex < part.blockCount; pageIndex++)
            {
                bufferManager.removeFromPool(part.name, pageIndex);
                bufferManager.deleteFile(part.name, pageIndex);
            }
        }
    }

    struct JoinOutputWriter
    {
        Table *resultTable = nullptr;
        vector<string> resultColumns;
        vector<pair<int, int>> resultColumnSources;
        vector<vector<int>> pageBuffer;
        uint nextPageIndex = 0;

        JoinOutputWriter(const vector<string> &columns, const vector<pair<int, int>> &sources)
        {
            this->resultColumns = columns;
            this->resultColumnSources = sources;
        }

        void flush()
        {
            if (this->resultTable == nullptr || this->pageBuffer.empty())
                return;
            uint rowsWritten = this->pageBuffer.size();
            bufferManager.writePage(this->resultTable->tableName, this->nextPageIndex, this->pageBuffer, rowsWritten);
            this->resultTable->rowsPerBlockCount.push_back(rowsWritten);
            this->resultTable->blockCount++;
            this->nextPageIndex++;
            this->pageBuffer.clear();
        }

        void append(const vector<int> &row)
        {
            if (this->resultTable == nullptr)
            {
                this->resultTable = new Table(parsedQuery.joinResultRelationName, this->resultColumns);
                this->pageBuffer.reserve(this->resultTable->maxRowsPerBlock);
            }
            this->pageBuffer.push_back(row);
            this->resultTable->rowCount++;
            if (this->pageBuffer.size() == this->resultTable->maxRowsPerBlock)
                this->flush();
        }

        Table *finalize()
        {
            this->flush();
            return this->resultTable;
        }
    };

    void buildAndProbePartitions(
        const JoinPartitionInfo &firstPart,
        const JoinPartitionInfo &secondPart,
        int col1Idx,
        int col2Idx,
        bool hasWhere,
        int whereTableNum,
        int whereColIdx,
        JoinOutputWriter &outputWriter)
    {
        bool buildFromFirst = firstPart.blockCount <= secondPart.blockCount;
        const JoinPartitionInfo &buildPart = buildFromFirst ? firstPart : secondPart;
        const JoinPartitionInfo &probePart = buildFromFirst ? secondPart : firstPart;

        unordered_multimap<long long, vector<int>> hashTable;
        for (uint pageIndex = 0; pageIndex < buildPart.blockCount; pageIndex++)
        {
            vector<vector<int>> rows = readPartitionPage(buildPart, pageIndex);
            for (const vector<int> &row : rows)
            {
                long long key = buildFromFirst ? logicalJoinKeyForTable1(row, col1Idx) : logicalJoinKeyForTable2(row, col2Idx);
                hashTable.insert(make_pair(key, row));
            }
        }

        for (uint pageIndex = 0; pageIndex < probePart.blockCount; pageIndex++)
        {
            vector<vector<int>> rows = readPartitionPage(probePart, pageIndex);
            for (const vector<int> &probeRow : rows)
            {
                long long probeKey = buildFromFirst ? logicalJoinKeyForTable2(probeRow, col2Idx) : logicalJoinKeyForTable1(probeRow, col1Idx);
                auto matches = hashTable.equal_range(probeKey);
                for (auto it = matches.first; it != matches.second; ++it)
                {
                    const vector<int> &buildRow = it->second;
                    const vector<int> &row1 = buildFromFirst ? buildRow : probeRow;
                    const vector<int> &row2 = buildFromFirst ? probeRow : buildRow;

                    if (!joinConditionMatches(row1, row2, col1Idx, col2Idx))
                        continue;

                    if (hasWhere)
                    {
                        int whereValue = (whereTableNum == 1) ? row1[whereColIdx] : row2[whereColIdx];
                        if (!evaluateWhereCondition(whereValue, parsedQuery.joinWhereOperator, parsedQuery.joinWhereValue))
                            continue;
                    }

                    vector<int> resultRow;
                    resultRow.reserve(outputWriter.resultColumnSources.size());
                    for (const pair<int, int> &source : outputWriter.resultColumnSources)
                    {
                        if (source.first == 1)
                            resultRow.push_back(row1[source.second]);
                        else
                            resultRow.push_back(row2[source.second]);
                    }
                    outputWriter.append(resultRow);
                }
            }
        }
    }

    void nestedLoopProbePartitions(
        const JoinPartitionInfo &firstPart,
        const JoinPartitionInfo &secondPart,
        int col1Idx,
        int col2Idx,
        bool hasWhere,
        int whereTableNum,
        int whereColIdx,
        JoinOutputWriter &outputWriter)
    {
        for (uint firstPageIndex = 0; firstPageIndex < firstPart.blockCount; firstPageIndex++)
        {
            vector<vector<int>> firstRows = readPartitionPage(firstPart, firstPageIndex);
            for (uint secondPageIndex = 0; secondPageIndex < secondPart.blockCount; secondPageIndex++)
            {
                vector<vector<int>> secondRows = readPartitionPage(secondPart, secondPageIndex);
                for (const vector<int> &row1 : firstRows)
                {
                    for (const vector<int> &row2 : secondRows)
                    {
                        if (!joinConditionMatches(row1, row2, col1Idx, col2Idx))
                            continue;

                        if (hasWhere)
                        {
                            int whereValue = (whereTableNum == 1) ? row1[whereColIdx] : row2[whereColIdx];
                            if (!evaluateWhereCondition(whereValue, parsedQuery.joinWhereOperator, parsedQuery.joinWhereValue))
                                continue;
                        }

                        vector<int> resultRow;
                        resultRow.reserve(outputWriter.resultColumnSources.size());
                        for (const pair<int, int> &source : outputWriter.resultColumnSources)
                        {
                            if (source.first == 1)
                                resultRow.push_back(row1[source.second]);
                            else
                                resultRow.push_back(row2[source.second]);
                        }
                        outputWriter.append(resultRow);
                    }
                }
            }
        }
    }

    void graceHashJoinRecursive(
        const JoinPartitionInfo &firstPart,
        const JoinPartitionInfo &secondPart,
        int col1Idx,
        int col2Idx,
        bool hasWhere,
        int whereTableNum,
        int whereColIdx,
        JoinOutputWriter &outputWriter,
        int depth)
    {
        if (firstPart.rowCount == 0 || secondPart.rowCount == 0)
            return;

        uint memoryBuildPages = max(1U, BLOCK_COUNT > 1 ? BLOCK_COUNT - 1 : 1U);
        if (min(firstPart.blockCount, secondPart.blockCount) <= memoryBuildPages)
        {
            buildAndProbePartitions(firstPart, secondPart, col1Idx, col2Idx, hasWhere, whereTableNum, whereColIdx, outputWriter);
            return;
        }

        if (depth >= MAX_JOIN_REPARTITION_DEPTH)
        {
            nestedLoopProbePartitions(firstPart, secondPart, col1Idx, col2Idx, hasWhere, whereTableNum, whereColIdx, outputWriter);
            return;
        }

        uint partitionCount = max(2U, BLOCK_COUNT > 1 ? BLOCK_COUNT - 1 : 1U);
        vector<JoinPartitionInfo> firstSubs = partitionExistingPartition(firstPart, true, col1Idx, col2Idx, partitionCount, depth + 1);
        vector<JoinPartitionInfo> secondSubs = partitionExistingPartition(secondPart, false, col1Idx, col2Idx, partitionCount, depth + 1);

        for (uint i = 0; i < partitionCount; i++)
            graceHashJoinRecursive(firstSubs[i], secondSubs[i], col1Idx, col2Idx, hasWhere, whereTableNum, whereColIdx, outputWriter, depth + 1);

        cleanupPartitions(firstSubs);
        cleanupPartitions(secondSubs);
    }
}

bool syntacticParseJOIN()
{
    logger.log("syntacticParseJOIN");

    if (tokenizedQuery.size() < 8)
    {
        cout << "SYNTAX ERROR" << endl;
        return false;
    }

    if (tokenizedQuery[1] != "<-" || tokenizedQuery[2] != "JOIN")
    {
        cout << "SYNTAX ERROR" << endl;
        return false;
    }

    parsedQuery.joinResultRelationName = tokenizedQuery[0];
    parsedQuery.joinFirstRelationName = tokenizedQuery[3];
    parsedQuery.joinSecondRelationName = tokenizedQuery[4];

    int onPos = -1;
    int wherePos = -1;
    int projectPos = -1;
    int whereCount = 0;
    int projectCount = 0;

    for (size_t i = 5; i < tokenizedQuery.size(); i++)
    {
        if (tokenizedQuery[i] == "ON" && onPos == -1)
            onPos = (int)i;
        else if (tokenizedQuery[i] == "WHERE")
        {
            wherePos = (int)i;
            whereCount++;
        }
        else if (tokenizedQuery[i] == "PROJECT")
        {
            projectPos = (int)i;
            projectCount++;
        }
    }

    if (onPos == -1 || onPos <= 4 || whereCount > 1 || projectCount > 1 || (wherePos != -1 && projectPos != -1 && wherePos > projectPos))
    {
        cout << "SYNTAX ERROR" << endl;
        return false;
    }

    int onEndPos = (int)tokenizedQuery.size();
    if (wherePos != -1)
        onEndPos = wherePos;
    else if (projectPos != -1)
        onEndPos = projectPos;

    vector<string> onTokens;
    for (int i = onPos + 1; i < onEndPos; i++)
    {
        bool hadSemicolon = false;
        string cleaned = stripTrailingSemicolon(tokenizedQuery[i], hadSemicolon);
        if (hadSemicolon && i != (int)tokenizedQuery.size() - 1)
        {
            cout << "SYNTAX ERROR" << endl;
            return false;
        }
        if (!cleaned.empty())
            onTokens.push_back(cleaned);
    }

    if (!parseJoinConditionTokens(onTokens))
    {
        cout << "SYNTAX ERROR" << endl;
        return false;
    }

    if (wherePos != -1)
    {
        parsedQuery.joinHasWhere = true;
        int whereEnd = (projectPos != -1) ? projectPos : (int)tokenizedQuery.size();
        if (whereEnd - wherePos != 4)
        {
            cout << "SYNTAX ERROR" << endl;
            return false;
        }

        bool hadSemicolon = false;
        parsedQuery.joinWhereColumn = stripTrailingSemicolon(tokenizedQuery[wherePos + 1], hadSemicolon);
        if (hadSemicolon && (wherePos + 1) != (int)tokenizedQuery.size() - 1)
        {
            cout << "SYNTAX ERROR" << endl;
            return false;
        }

        if (!parseWhereOperator(tokenizedQuery[wherePos + 2], parsedQuery.joinWhereOperator))
        {
            cout << "SYNTAX ERROR" << endl;
            return false;
        }

        string valueToken = stripTrailingSemicolon(tokenizedQuery[wherePos + 3], hadSemicolon);
        if (hadSemicolon && (wherePos + 3) != (int)tokenizedQuery.size() - 1)
        {
            cout << "SYNTAX ERROR" << endl;
            return false;
        }
        if (!parseIntegerStrict(valueToken, parsedQuery.joinWhereValue))
        {
            cout << "SYNTAX ERROR" << endl;
            return false;
        }
    }

    if (projectPos != -1)
    {
        parsedQuery.joinHasProject = true;
        for (size_t i = projectPos + 1; i < tokenizedQuery.size(); i++)
        {
            bool hadSemicolon = false;
            string cleaned = stripTrailingSemicolon(tokenizedQuery[i], hadSemicolon);
            if (hadSemicolon && i != tokenizedQuery.size() - 1)
            {
                cout << "SYNTAX ERROR" << endl;
                return false;
            }
            pair<string, string> parsed = parseTableColumn(cleaned);
            if (cleaned.empty() || parsed.first.empty())
            {
                cout << "SYNTAX ERROR" << endl;
                return false;
            }
            parsedQuery.joinProjectColumns.push_back(cleaned);
        }
        if (parsedQuery.joinProjectColumns.empty())
        {
            cout << "SYNTAX ERROR" << endl;
            return false;
        }
    }

    parsedQuery.queryType = JOIN;
    return true;
}

bool semanticParseJOIN()
{
    logger.log("semanticParseJOIN");

    if (tableCatalogue.isTable(parsedQuery.joinResultRelationName))
    {
        cout << "SEMANTIC ERROR: Resultant relation already exists" << endl;
        return false;
    }

    if (!tableCatalogue.isTable(parsedQuery.joinFirstRelationName) ||
        !tableCatalogue.isTable(parsedQuery.joinSecondRelationName))
    {
        cout << "SEMANTIC ERROR: Relation doesn't exist" << endl;
        return false;
    }

    if (parsedQuery.joinFirstRelationName == parsedQuery.joinSecondRelationName)
    {
        cout << "SEMANTIC ERROR" << endl;
        return false;
    }

    if (parsedQuery.joinConditionType != 0 &&
        (parsedQuery.joinArithmeticResult < 0 || parsedQuery.joinArithmeticResult > 10000))
    {
        cout << "SEMANTIC ERROR" << endl;
        return false;
    }

    if (parsedQuery.joinFirstColumnRelationName != parsedQuery.joinFirstRelationName ||
        parsedQuery.joinSecondColumnRelationName != parsedQuery.joinSecondRelationName ||
        !tableCatalogue.isColumnFromTable(parsedQuery.joinFirstColumnName, parsedQuery.joinFirstRelationName) ||
        !tableCatalogue.isColumnFromTable(parsedQuery.joinSecondColumnName, parsedQuery.joinSecondRelationName))
    {
        cout << "SEMANTIC ERROR: Column doesn't exist in relation" << endl;
        return false;
    }

    if (parsedQuery.joinHasWhere)
    {
        pair<string, string> whereParsed = parseTableColumn(parsedQuery.joinWhereColumn);
        if (whereParsed.first.empty() ||
            (whereParsed.first != parsedQuery.joinFirstRelationName && whereParsed.first != parsedQuery.joinSecondRelationName) ||
            !tableCatalogue.isColumnFromTable(whereParsed.second, whereParsed.first))
        {
            cout << "SEMANTIC ERROR: Column doesn't exist in relation" << endl;
            return false;
        }
    }

    if (parsedQuery.joinHasProject)
    {
        for (const string &projectColumn : parsedQuery.joinProjectColumns)
        {
            pair<string, string> parsed = parseTableColumn(projectColumn);
            if (parsed.first.empty() ||
                (parsed.first != parsedQuery.joinFirstRelationName && parsed.first != parsedQuery.joinSecondRelationName) ||
                !tableCatalogue.isColumnFromTable(parsed.second, parsed.first))
            {
                cout << "SEMANTIC ERROR: Column doesn't exist in relation" << endl;
                return false;
            }
        }
    }

    return true;
}

void executeJOIN()
{
    logger.log("executeJOIN");

    Table *table1 = tableCatalogue.getTable(parsedQuery.joinFirstRelationName);
    Table *table2 = tableCatalogue.getTable(parsedQuery.joinSecondRelationName);
    int col1Idx = table1->getColumnIndex(parsedQuery.joinFirstColumnName);
    int col2Idx = table2->getColumnIndex(parsedQuery.joinSecondColumnName);

    vector<string> resultColumns;
    vector<pair<int, int>> resultColumnSources;
    if (parsedQuery.joinHasProject)
    {
        for (const string &projectColumn : parsedQuery.joinProjectColumns)
        {
            pair<string, string> parsed = parseTableColumn(projectColumn);
            if (parsed.first == parsedQuery.joinFirstRelationName)
            {
                resultColumns.push_back(parsed.second);
                resultColumnSources.push_back(make_pair(1, table1->getColumnIndex(parsed.second)));
            }
            else
            {
                resultColumns.push_back(parsed.second);
                resultColumnSources.push_back(make_pair(2, table2->getColumnIndex(parsed.second)));
            }
        }
    }
    else
    {
        for (int i = 0; i < (int)table1->columnCount; i++)
        {
            resultColumns.push_back(table1->columns[i]);
            resultColumnSources.push_back(make_pair(1, i));
        }
        for (int i = 0; i < (int)table2->columnCount; i++)
        {
            resultColumns.push_back(table2->columns[i]);
            resultColumnSources.push_back(make_pair(2, i));
        }
    }

    int whereTableNum = 0;
    int whereColIdx = -1;
    if (parsedQuery.joinHasWhere)
    {
        pair<string, string> whereParsed = parseTableColumn(parsedQuery.joinWhereColumn);
        if (whereParsed.first == parsedQuery.joinFirstRelationName)
        {
            whereTableNum = 1;
            whereColIdx = table1->getColumnIndex(whereParsed.second);
        }
        else
        {
            whereTableNum = 2;
            whereColIdx = table2->getColumnIndex(whereParsed.second);
        }
    }

    uint partitionCount = max(2U, BLOCK_COUNT > 1 ? BLOCK_COUNT - 1 : 1U);
    string prefix = parsedQuery.joinResultRelationName + "_JoinTmp" + to_string(joinTempCounter++);
    vector<JoinPartitionInfo> firstPartitions = partitionTable(table1, prefix + "_R", true, col1Idx, col2Idx, partitionCount, 0);
    vector<JoinPartitionInfo> secondPartitions = partitionTable(table2, prefix + "_S", false, col1Idx, col2Idx, partitionCount, 0);

    JoinOutputWriter outputWriter(resultColumns, resultColumnSources);
    for (uint i = 0; i < partitionCount; i++)
    {
        graceHashJoinRecursive(
            firstPartitions[i],
            secondPartitions[i],
            col1Idx,
            col2Idx,
            parsedQuery.joinHasWhere,
            whereTableNum,
            whereColIdx,
            outputWriter,
            0);
    }

    cleanupPartitions(firstPartitions);
    cleanupPartitions(secondPartitions);

    Table *resultTable = outputWriter.finalize();
    if (resultTable != nullptr)
        tableCatalogue.insertTable(resultTable);
}
