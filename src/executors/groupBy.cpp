#include "externalSortUtils.h"
#include <cmath>
#include <unordered_map>
#include <set>

using namespace std;

namespace
{
    struct AggregateState
    {
        long long count = 0;
        long long sum = 0;
        long long minValue = 0;
        long long maxValue = 0;
        bool initialized = false;

        void addValue(long long value)
        {
            if (!initialized)
            {
                minValue = value;
                maxValue = value;
                initialized = true;
            }
            else
            {
                if (value < minValue)
                    minValue = value;
                if (value > maxValue)
                    maxValue = value;
            }
            count++;
            sum += value;
        }
    };

    bool parseStrictInt(const string &token, int &value)
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

    bool parseAggregateToken(const string &token, ParsedQuery::AggregateExpression &expr)
    {
        size_t openPos = token.find('(');
        size_t closePos = token.find(')');
        if (openPos == string::npos || closePos == string::npos || closePos != token.size() - 1 || openPos == 0 || closePos <= openPos + 1)
            return false;

        expr.functionName = token.substr(0, openPos);
        expr.columnName = token.substr(openPos + 1, closePos - openPos - 1);
        expr.isCountStar = (expr.functionName == "COUNT" && expr.columnName == "*");

        if (expr.functionName != "MAX" && expr.functionName != "MIN" && expr.functionName != "COUNT" && expr.functionName != "SUM" && expr.functionName != "AVG")
            return false;
        if (expr.columnName.empty())
            return false;
        if (expr.columnName == "*" && !expr.isCountStar)
            return false;
        return true;
    }

    bool parseBinaryOperatorToken(const string &token, BinaryOperator &op)
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

    bool compareLongDouble(long double left, long double right, BinaryOperator op)
    {
        const long double EPS = 1e-12L;
        switch (op)
        {
        case EQUAL: return fabsl(left - right) <= EPS;
        case NOT_EQUAL: return fabsl(left - right) > EPS;
        case GREATER_THAN: return left > right + EPS;
        case GEQ: return left + EPS >= right;
        case LESS_THAN: return left + EPS < right;
        case LEQ: return left <= right + EPS;
        default: return false;
        }
    }

    int resolveAggregateColumnIndex(const ParsedQuery::AggregateExpression &expr, Table *table)
    {
        if (expr.isCountStar)
            return -1;
        return table->getColumnIndex(expr.columnName);
    }

    string getReturnColumnName(const ParsedQuery::AggregateExpression &expr)
    {
        if (expr.isCountStar)
            return "COUNT";
        return expr.functionName + expr.columnName;
    }

    long double getAggregateValueAsLongDouble(
        const ParsedQuery::AggregateExpression &expr,
        const unordered_map<int, AggregateState> &states,
        int columnIndex)
    {
        long double countValue = 0.0L;
        if (columnIndex == -1)
            countValue = states.at(-1).count;
        else
            countValue = states.at(columnIndex).count;

        if (expr.functionName == "COUNT")
            return countValue;
        if (columnIndex == -1)
            return countValue;

        const AggregateState &state = states.at(columnIndex);
        if (expr.functionName == "SUM")
            return (long double)state.sum;
        if (expr.functionName == "MIN")
            return (long double)state.minValue;
        if (expr.functionName == "MAX")
            return (long double)state.maxValue;
        return (long double)state.sum / (long double)state.count;
    }

    int getAggregateValueAsIntForReturn(
        const ParsedQuery::AggregateExpression &expr,
        const unordered_map<int, AggregateState> &states,
        int columnIndex)
    {
        if (expr.functionName == "COUNT")
        {
            if (columnIndex == -1)
                return (int)states.at(-1).count;
            return (int)states.at(columnIndex).count;
        }
        if (expr.functionName == "AVG")
            return (int)ceill(getAggregateValueAsLongDouble(expr, states, columnIndex));

        const AggregateState &state = states.at(columnIndex);
        if (expr.functionName == "SUM")
            return (int)state.sum;
        if (expr.functionName == "MIN")
            return (int)state.minValue;
        return (int)state.maxValue;
    }

    void updateStatesForRow(
        const vector<int> &row,
        const set<int> &trackedColumns,
        unordered_map<int, AggregateState> &states)
    {
        states[-1].count++;
        for (int columnIndex : trackedColumns)
        {
            if (columnIndex == -1)
                continue;
            states[columnIndex].addValue(row[columnIndex]);
        }
    }

    bool evaluateHavingForStates(Table *table, const unordered_map<int, AggregateState> &states)
    {
        int leftColumnIndex = resolveAggregateColumnIndex(parsedQuery.groupByHavingLeft, table);
        long double leftValue = getAggregateValueAsLongDouble(parsedQuery.groupByHavingLeft, states, leftColumnIndex);

        long double rightValue = 0.0L;
        if (parsedQuery.groupByHavingRightIsNumber)
            rightValue = parsedQuery.groupByHavingNumber;
        else
        {
            int rightColumnIndex = resolveAggregateColumnIndex(parsedQuery.groupByHavingRight, table);
            rightValue = getAggregateValueAsLongDouble(parsedQuery.groupByHavingRight, states, rightColumnIndex);
        }
        return compareLongDouble(leftValue, rightValue, parsedQuery.groupByHavingOperator);
    }
}

bool syntacticParseGROUP_BY()
{
    logger.log("syntacticParseGROUP_BY");

    int arrowPos = -1;
    for (size_t i = 0; i < tokenizedQuery.size(); i++)
    {
        if (tokenizedQuery[i] == "<-")
        {
            arrowPos = (int)i;
            break;
        }
    }

    if (arrowPos <= 0 || arrowPos + 7 >= (int)tokenizedQuery.size())
    {
        cout << "SYNTAX ERROR" << endl;
        return false;
    }

    for (int i = 0; i < arrowPos; i++)
        parsedQuery.groupByResultRelationNames.push_back(tokenizedQuery[i]);

    if (tokenizedQuery[arrowPos + 1] != "GROUP" || tokenizedQuery[arrowPos + 2] != "BY")
    {
        cout << "SYNTAX ERROR" << endl;
        return false;
    }

    int fromPos = -1;
    int havingPos = -1;
    int returnPos = -1;
    for (size_t i = arrowPos + 3; i < tokenizedQuery.size(); i++)
    {
        if (tokenizedQuery[i] == "FROM" && fromPos == -1)
            fromPos = (int)i;
        else if (tokenizedQuery[i] == "HAVING" && havingPos == -1)
            havingPos = (int)i;
        else if (tokenizedQuery[i] == "RETURN" && returnPos == -1)
            returnPos = (int)i;
    }

    if (fromPos == -1 || havingPos == -1 || returnPos == -1 || !(arrowPos + 3 < fromPos && fromPos + 1 == havingPos - 1 && havingPos + 3 < returnPos && returnPos + 1 < (int)tokenizedQuery.size()))
    {
        cout << "SYNTAX ERROR" << endl;
        return false;
    }

    for (int i = arrowPos + 3; i < fromPos; i++)
        parsedQuery.groupByAttributes.push_back(tokenizedQuery[i]);
    parsedQuery.groupByRelationName = tokenizedQuery[fromPos + 1];

    if (parsedQuery.groupByAttributes.empty())
    {
        cout << "SYNTAX ERROR" << endl;
        return false;
    }

    if (!parseAggregateToken(tokenizedQuery[havingPos + 1], parsedQuery.groupByHavingLeft) ||
        !parseBinaryOperatorToken(tokenizedQuery[havingPos + 2], parsedQuery.groupByHavingOperator))
    {
        cout << "SYNTAX ERROR" << endl;
        return false;
    }

    string rightToken = tokenizedQuery[havingPos + 3];
    if (!rightToken.empty() && rightToken.back() == ';')
    {
        cout << "SYNTAX ERROR" << endl;
        return false;
    }

    int numberValue = 0;
    if (parseStrictInt(rightToken, numberValue))
    {
        parsedQuery.groupByHavingRightIsNumber = true;
        parsedQuery.groupByHavingNumber = numberValue;
    }
    else if (!parseAggregateToken(rightToken, parsedQuery.groupByHavingRight))
    {
        cout << "SYNTAX ERROR" << endl;
        return false;
    }

    for (size_t i = returnPos + 1; i < tokenizedQuery.size(); i++)
    {
        string token = tokenizedQuery[i];
        bool tokenHadSemicolon = false;
        if (!token.empty() && token.back() == ';')
        {
            token.pop_back();
            tokenHadSemicolon = true;
        }

        ParsedQuery::AggregateExpression expr;
        if (token.empty() || !parseAggregateToken(token, expr))
        {
            cout << "SYNTAX ERROR" << endl;
            return false;
        }
        parsedQuery.groupByReturnAggregates.push_back(expr);

        if (tokenHadSemicolon && i != tokenizedQuery.size() - 1)
        {
            cout << "SYNTAX ERROR" << endl;
            return false;
        }
    }

    if (parsedQuery.groupByResultRelationNames.size() != parsedQuery.groupByAttributes.size() ||
        parsedQuery.groupByAttributes.size() != parsedQuery.groupByReturnAggregates.size())
    {
        cout << "SYNTAX ERROR" << endl;
        return false;
    }

    parsedQuery.queryType = GROUP_BY;
    return true;
}

bool semanticParseGROUP_BY()
{
    logger.log("semanticParseGROUP_BY");

    if (!tableCatalogue.isTable(parsedQuery.groupByRelationName))
    {
        cout << "SEMANTIC ERROR: Relation doesn't exist" << endl;
        return false;
    }

    Table *table = tableCatalogue.getTable(parsedQuery.groupByRelationName);

    set<string> resultNames;
    for (const string &resultName : parsedQuery.groupByResultRelationNames)
    {
        if (tableCatalogue.isTable(resultName) || resultNames.count(resultName))
        {
            cout << "SEMANTIC ERROR" << endl;
            return false;
        }
        resultNames.insert(resultName);
    }

    for (const string &groupAttribute : parsedQuery.groupByAttributes)
    {
        if (!table->isColumn(groupAttribute))
        {
            cout << "SEMANTIC ERROR: Group By column doesn't exist in relation" << endl;
            return false;
        }
    }

    auto validateHavingExpr = [&](const ParsedQuery::AggregateExpression &expr) {
        if (!expr.isCountStar && !table->isColumn(expr.columnName))
        {
            cout << "SEMANTIC ERROR: Having column doesn't exist in relation" << endl;
            return false;
        }
        return true;
    };

    if (!validateHavingExpr(parsedQuery.groupByHavingLeft))
        return false;
    if (!parsedQuery.groupByHavingRightIsNumber && !validateHavingExpr(parsedQuery.groupByHavingRight))
        return false;
    if (parsedQuery.groupByHavingRightIsNumber &&
        (parsedQuery.groupByHavingNumber < 0 || parsedQuery.groupByHavingNumber > 10000))
    {
        cout << "SEMANTIC ERROR" << endl;
        return false;
    }

    for (const ParsedQuery::AggregateExpression &expr : parsedQuery.groupByReturnAggregates)
    {
        if (!expr.isCountStar && !table->isColumn(expr.columnName))
        {
            cout << "SEMANTIC ERROR: Return column doesn't exist in relation" << endl;
            return false;
        }
    }

    return true;
}

void executeGROUP_BY()
{
    logger.log("executeGROUP_BY");

    Table *table = tableCatalogue.getTable(parsedQuery.groupByRelationName);

    for (size_t queryIndex = 0; queryIndex < parsedQuery.groupByAttributes.size(); queryIndex++)
    {
        int groupColumnIndex = table->getColumnIndex(parsedQuery.groupByAttributes[queryIndex]);
        vector<int> sortColumns(1, groupColumnIndex);
        vector<SortingStrategy> sortStrategies(1, ASC);

        TempRun sortedRun = externalSortTableRange(
            table,
            parsedQuery.groupByResultRelationNames[queryIndex] + "_GroupByTmp",
            0,
            table->rowCount,
            sortColumns,
            sortStrategies);

        set<int> trackedColumns;
        trackedColumns.insert(resolveAggregateColumnIndex(parsedQuery.groupByHavingLeft, table));
        if (!parsedQuery.groupByHavingRightIsNumber)
            trackedColumns.insert(resolveAggregateColumnIndex(parsedQuery.groupByHavingRight, table));
        trackedColumns.insert(resolveAggregateColumnIndex(parsedQuery.groupByReturnAggregates[queryIndex], table));

        TempRunReader reader(sortedRun);
        bool hasActiveGroup = false;
        int currentGroupValue = 0;
        unordered_map<int, AggregateState> currentStates;

        string resultRelationName = parsedQuery.groupByResultRelationNames[queryIndex];
        ParsedQuery::AggregateExpression returnExpr = parsedQuery.groupByReturnAggregates[queryIndex];
        string resultColumnName = getReturnColumnName(returnExpr);
        Table *resultTable = nullptr;
        vector<vector<int>> resultPageBuffer;
        uint resultPageIndex = 0;

        auto flushResultPage = [&]() {
            if (resultTable == nullptr || resultPageBuffer.empty())
                return;
            uint rowsWritten = resultPageBuffer.size();
            bufferManager.writePage(resultTable->tableName, resultPageIndex, resultPageBuffer, rowsWritten);
            resultTable->rowsPerBlockCount.push_back(rowsWritten);
            resultTable->blockCount++;
            resultPageIndex++;
            resultPageBuffer.clear();
        };

        auto appendResultRow = [&](int groupValue, int aggregateValue) {
            if (resultTable == nullptr)
            {
                vector<string> columns;
                columns.push_back(parsedQuery.groupByAttributes[queryIndex]);
                columns.push_back(resultColumnName);
                resultTable = new Table(resultRelationName, columns);
                resultPageBuffer.reserve(resultTable->maxRowsPerBlock);
            }
            resultPageBuffer.push_back({groupValue, aggregateValue});
            resultTable->rowCount++;
            if (resultPageBuffer.size() == resultTable->maxRowsPerBlock)
                flushResultPage();
        };

        auto finalizeCurrentGroup = [&]() {
            if (!hasActiveGroup)
                return;
            if (!evaluateHavingForStates(table, currentStates))
                return;
            int returnColumnIndex = resolveAggregateColumnIndex(returnExpr, table);
            int aggregateValue = getAggregateValueAsIntForReturn(returnExpr, currentStates, returnColumnIndex);
            appendResultRow(currentGroupValue, aggregateValue);
        };

        while (reader.hasNext())
        {
            vector<int> row = reader.getNext();
            int groupValue = row[groupColumnIndex];
            if (!hasActiveGroup)
            {
                hasActiveGroup = true;
                currentGroupValue = groupValue;
            }
            else if (groupValue != currentGroupValue)
            {
                finalizeCurrentGroup();
                currentStates.clear();
                currentGroupValue = groupValue;
            }
            updateStatesForRow(row, trackedColumns, currentStates);
        }
        finalizeCurrentGroup();

        flushResultPage();
        cleanupTempRun(sortedRun);

        if (resultTable != nullptr)
            tableCatalogue.insertTable(resultTable);
    }
}
