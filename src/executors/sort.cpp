#include "externalSortUtils.h"
#include <algorithm>

using namespace std;

namespace
{
    bool parsePositiveInteger(const string &token, int &value)
    {
        try
        {
            size_t used = 0;
            value = stoi(token, &used);
            return used == token.size() && value > 0;
        }
        catch (...)
        {
            return false;
        }
    }

    bool isSortDirection(const string &token)
    {
        return token == "ASC" || token == "DESC";
    }
}

bool syntacticParseSORT()
{
    logger.log("syntacticParseSORT");

    if (tokenizedQuery.size() < 6)
    {
        cout << "SYNTAX ERROR" << endl;
        return false;
    }

    if (tokenizedQuery[0] != "SORT" || tokenizedQuery[2] != "BY")
    {
        cout << "SYNTAX ERROR" << endl;
        return false;
    }

    parsedQuery.sortRelationName = tokenizedQuery[1];

    int inPos = -1;
    for (size_t i = 3; i < tokenizedQuery.size(); i++)
    {
        if (tokenizedQuery[i] == "IN")
        {
            inPos = (int)i;
            break;
        }
    }

    if (inPos == -1 || inPos == 3)
    {
        cout << "SYNTAX ERROR" << endl;
        return false;
    }

    for (int i = 3; i < inPos; i++)
        parsedQuery.sortColumnNames.push_back(tokenizedQuery[i]);

    int topPos = -1;
    int bottomPos = -1;
    for (size_t i = inPos + 1; i < tokenizedQuery.size(); i++)
    {
        if (tokenizedQuery[i] == "TOP" && topPos == -1)
            topPos = (int)i;
        else if (tokenizedQuery[i] == "BOTTOM" && bottomPos == -1)
            bottomPos = (int)i;
    }

    int directionEnd = (int)tokenizedQuery.size();
    if (topPos != -1)
        directionEnd = topPos;
    else if (bottomPos != -1)
        directionEnd = bottomPos;

    if (directionEnd <= inPos + 1)
    {
        cout << "SYNTAX ERROR" << endl;
        return false;
    }

    for (int i = inPos + 1; i < directionEnd; i++)
    {
        if (tokenizedQuery[i] == "ASC")
            parsedQuery.sortingStrategies.push_back(ASC);
        else if (tokenizedQuery[i] == "DESC")
            parsedQuery.sortingStrategies.push_back(DESC);
        else
        {
            cout << "SYNTAX ERROR" << endl;
            return false;
        }
    }

    if (parsedQuery.sortColumnNames.size() != parsedQuery.sortingStrategies.size())
    {
        cout << "SYNTAX ERROR" << endl;
        return false;
    }

    if (topPos != -1)
    {
        if (topPos + 1 >= (int)tokenizedQuery.size() ||
            !parsePositiveInteger(tokenizedQuery[topPos + 1], parsedQuery.sortTopX))
        {
            cout << "SYNTAX ERROR" << endl;
            return false;
        }
        if (topPos + 2 < (int)tokenizedQuery.size() && bottomPos == -1)
        {
            cout << "SYNTAX ERROR" << endl;
            return false;
        }
    }

    if (bottomPos != -1)
    {
        if (bottomPos + 1 >= (int)tokenizedQuery.size() ||
            !parsePositiveInteger(tokenizedQuery[bottomPos + 1], parsedQuery.sortBottomY))
        {
            cout << "SYNTAX ERROR" << endl;
            return false;
        }
        if (bottomPos + 2 < (int)tokenizedQuery.size())
        {
            cout << "SYNTAX ERROR" << endl;
            return false;
        }
    }

    parsedQuery.queryType = SORT;
    return true;
}

bool semanticParseSORT()
{
    logger.log("semanticParseSORT");

    if (!tableCatalogue.isTable(parsedQuery.sortRelationName))
    {
        cout << "SEMANTIC ERROR: Relation doesn't exist" << endl;
        return false;
    }

    Table *table = tableCatalogue.getTable(parsedQuery.sortRelationName);
    for (const string &columnName : parsedQuery.sortColumnNames)
    {
        if (!tableCatalogue.isColumnFromTable(columnName, parsedQuery.sortRelationName))
        {
            cout << "SEMANTIC ERROR: Column doesn't exist in relation" << endl;
            return false;
        }
    }

    if (parsedQuery.sortTopX > (int)table->rowCount)
        parsedQuery.sortTopX = (int)table->rowCount;
    if (parsedQuery.sortBottomY > (int)table->rowCount)
        parsedQuery.sortBottomY = (int)table->rowCount;

    if (parsedQuery.sortTopX != -1 && parsedQuery.sortBottomY != -1 &&
        (long long)parsedQuery.sortTopX + (long long)parsedQuery.sortBottomY > table->rowCount)
    {
        cout << "SEMANTIC ERROR" << endl;
        return false;
    }

    return true;
}

void executeSORT()
{
    logger.log("executeSORT");

    Table *table = tableCatalogue.getTable(parsedQuery.sortRelationName);
    vector<int> sortColumnIndices;
    for (const string &columnName : parsedQuery.sortColumnNames)
        sortColumnIndices.push_back(table->getColumnIndex(columnName));

    auto sortAndRewriteRange = [&](long long startRow, long long endRow, const string &tag) {
        if (startRow >= endRow)
            return;

        TempRun sortedRun = externalSortTableRange(
            table,
            parsedQuery.sortRelationName + "_SortTmp_" + tag,
            startRow,
            endRow,
            sortColumnIndices,
            parsedQuery.sortingStrategies);

        rewriteTableRangeFromRun(table, sortedRun, startRow, endRow);
        cleanupTempRun(sortedRun);
    };

    if (parsedQuery.sortTopX == -1 && parsedQuery.sortBottomY == -1)
    {
        sortAndRewriteRange(0, table->rowCount, "FULL");
        return;
    }

    if (parsedQuery.sortTopX != -1)
        sortAndRewriteRange(0, parsedQuery.sortTopX, "TOP");

    if (parsedQuery.sortBottomY != -1)
        sortAndRewriteRange(table->rowCount - parsedQuery.sortBottomY, table->rowCount, "BOTTOM");
}
