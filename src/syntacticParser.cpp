#include "global.h"

bool syntacticParse()
{
    logger.log("syntacticParse");
    string possibleQueryType = tokenizedQuery[0];

    if (tokenizedQuery.size() < 2)
    {
        cout << "SYNTAX ERROR" << endl;
        return false;
    }

    if (possibleQueryType == "CLEAR")
        return syntacticParseCLEAR();
    else if (possibleQueryType == "INDEX")
        return syntacticParseINDEX();
    else if (possibleQueryType == "LIST")
        return syntacticParseLIST();
    else if (possibleQueryType == "LOAD")
    {
        if (tokenizedQuery.size() >= 3 && tokenizedQuery[1] == "GRAPH")
            return syntacticParseLOAD_GRAPH();
        return syntacticParseLOAD();
    }
    else if (possibleQueryType == "PRINT")
    {
        if (tokenizedQuery.size() >= 3 && tokenizedQuery[1] == "GRAPH")
            return syntacticParsePRINT_GRAPH();
        return syntacticParsePRINT();
    }
    else if (possibleQueryType == "RENAME")
        return syntacticParseRENAME();
    else if (possibleQueryType == "EXPORT")
    {
        if (tokenizedQuery.size() >= 3 && tokenizedQuery[1] == "GRAPH")
            return syntacticParseEXPORT_GRAPH();
        return syntacticParseEXPORT();
    }
    else if (possibleQueryType == "DEGREE")
        return syntacticParseDEGREE();
    else if (possibleQueryType == "SETBUFFER")
        return syntacticParseSETBUFFER();
    else if (possibleQueryType == "SORT")
        return syntacticParseSORT();
    else if (possibleQueryType == "SOURCE")
        return syntacticParseSOURCE();
    else if (possibleQueryType == "TRANSACTION")
        return syntacticParseTRANSACTION();
    else
    {
        int arrowPos = -1;
        for (size_t i = 0; i < tokenizedQuery.size(); i++)
        {
            if (tokenizedQuery[i] == "<-")
            {
                arrowPos = (int)i;
                break;
            }
        }

        if (arrowPos <= 0 || arrowPos + 1 >= (int)tokenizedQuery.size())
        {
            cout << "SYNTAX ERROR" << endl;
            return false;
        }

        possibleQueryType = tokenizedQuery[arrowPos + 1];
        if (possibleQueryType == "PATH")
            return syntacticParsePATH();
        else if (possibleQueryType == "PROJECT")
            return syntacticParsePROJECTION();
        else if (possibleQueryType == "SELECT")
            return syntacticParseSELECTION();
        else if (possibleQueryType == "JOIN")
            return syntacticParseJOIN();
        else if (possibleQueryType == "CROSS")
            return syntacticParseCROSS();
        else if (possibleQueryType == "DISTINCT")
            return syntacticParseDISTINCT();
        else if (possibleQueryType == "SORT")
            return syntacticParseSORT();
        else if (possibleQueryType == "GROUP")
            return syntacticParseGROUP_BY();
        else
        {
            cout << "SYNTAX ERROR" << endl;
            return false;
        }
    }
}

ParsedQuery::ParsedQuery()
{
}

void ParsedQuery::clear()
{
    logger.log("ParseQuery::clear");
    this->queryType = UNDETERMINED;

    this->clearRelationName = "";

    this->crossResultRelationName = "";
    this->crossFirstRelationName = "";
    this->crossSecondRelationName = "";

    this->distinctResultRelationName = "";
    this->distinctRelationName = "";

    this->exportRelationName = "";

    this->indexingStrategy = NOTHING;
    this->indexColumnName = "";
    this->indexRelationName = "";

    this->joinBinaryOperator = NO_BINOP_CLAUSE;
    this->joinResultRelationName = "";
    this->joinFirstRelationName = "";
    this->joinSecondRelationName = "";
    this->joinFirstColumnRelationName = "";
    this->joinFirstColumnName = "";
    this->joinSecondColumnRelationName = "";
    this->joinSecondColumnName = "";
    
    // Extended JOIN fields for Phase 2
    this->joinConditionType = 0;
    this->joinArithmeticResult = 0;
    this->joinHasWhere = false;
    this->joinWhereColumn = "";
    this->joinWhereOperator = NO_BINOP_CLAUSE;
    this->joinWhereValue = 0;
    this->joinHasProject = false;
    this->joinProjectColumns.clear();

    this->loadRelationName = "";

    this->printRelationName = "";

    this->projectionResultRelationName = "";
    this->projectionColumnList.clear();
    this->projectionRelationName = "";

    this->renameFromColumnName = "";
    this->renameToColumnName = "";
    this->renameRelationName = "";

    this->selectType = NO_SELECT_CLAUSE;
    this->selectionBinaryOperator = NO_BINOP_CLAUSE;
    this->selectionResultRelationName = "";
    this->selectionRelationName = "";
    this->selectionFirstColumnName = "";
    this->selectionSecondColumnName = "";
    this->selectionIntLiteral = 0;

    this->sortingStrategy = NO_SORT_CLAUSE;
    this->sortResultRelationName = "";
    this->sortColumnName = "";
    this->sortRelationName = "";
    this->sortColumnNames.clear();
    this->sortingStrategies.clear();
    this->sortTopX = -1;
    this->sortBottomY = -1;

    this->sourceFileName = "";
    this->transactionInputFileName = "";
    this->transactionOutputFileName = "";

    this->setBufferSize = 0;

    this->loadGraphName = "";
    this->loadGraphType = 'D';
    this->exportGraphName = "";
    this->pathResultGraphName = "";
    this->pathGraphName = "";
    this->pathSrcNodeId = 0;
    this->pathDestNodeId = 0;
    this->pathConditions.clear();
    this->degreeGraphName = "";
    this->degreeNodeId = 0;
    this->printGraphName = "";

    this->groupByResultRelationNames.clear();
    this->groupByAttributes.clear();
    this->groupByRelationName = "";
    this->groupByHavingLeft = AggregateExpression();
    this->groupByHavingRight = AggregateExpression();
    this->groupByHavingRightIsNumber = false;
    this->groupByHavingNumber = 0;
    this->groupByHavingOperator = NO_BINOP_CLAUSE;
    this->groupByReturnAggregates.clear();
}

/**
 * @brief Checks to see if source file exists. Called when LOAD command is
 * invoked.
 *
 * @param tableName 
 * @return true 
 * @return false 
 */
bool isFileExists(string tableName)
{
    string fileName = "../data/" + tableName + ".csv";
    struct stat buffer;
    return (stat(fileName.c_str(), &buffer) == 0);
}

/**
 * @brief Checks to see if source file exists. Called when SOURCE command is
 * invoked.
 *
 * @param tableName 
 * @return true 
 * @return false 
 */
bool isQueryFile(string fileName){
    fileName = "../data/" + fileName + ".ra";
    struct stat buffer;
    return (stat(fileName.c_str(), &buffer) == 0);
}
