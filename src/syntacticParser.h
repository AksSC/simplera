#pragma once
#include "tableCatalogue.h"

using namespace std;

enum QueryType
{
    CLEAR,
    CROSS,
    DISTINCT,
    EXPORT,
    INDEX,
    JOIN,
    LIST,
    LOAD,
    PRINT,
    PROJECTION,
    RENAME,
    SELECTION,
    SETBUFFER,
    SORT,
    SOURCE,
    LOAD_GRAPH,
    EXPORT_GRAPH,
    PATH,
    DEGREE,
    PRINT_GRAPH,
    GROUP_BY,
    TRANSACTION,
    UNDETERMINED
};

enum BinaryOperator
{
    LESS_THAN,
    GREATER_THAN,
    LEQ,
    GEQ,
    EQUAL,
    NOT_EQUAL,
    NO_BINOP_CLAUSE
};

enum SortingStrategy
{
    ASC,
    DESC,
    NO_SORT_CLAUSE
};

enum SelectType
{
    COLUMN,
    INT_LITERAL,
    NO_SELECT_CLAUSE
};

class ParsedQuery
{

public:
    QueryType queryType = UNDETERMINED;

    string clearRelationName = "";

    string crossResultRelationName = "";
    string crossFirstRelationName = "";
    string crossSecondRelationName = "";

    string distinctResultRelationName = "";
    string distinctRelationName = "";

    string exportRelationName = "";

    IndexingStrategy indexingStrategy = NOTHING;
    string indexColumnName = "";
    string indexRelationName = "";

    BinaryOperator joinBinaryOperator = NO_BINOP_CLAUSE;
    string joinResultRelationName = "";
    string joinFirstRelationName = "";
    string joinSecondRelationName = "";
    string joinFirstColumnRelationName = "";
    string joinFirstColumnName = "";
    string joinSecondColumnRelationName = "";
    string joinSecondColumnName = "";
    
    // Extended JOIN fields for Phase 2 HASH JOIN
    // Join condition type: 0 = A.a == B.b, 1 = A.a + B.b == num, 2 = A.a - B.b == num
    int joinConditionType = 0;
    int joinArithmeticResult = 0;  // The number in A.a +/- B.b == number
    
    // WHERE clause
    bool joinHasWhere = false;
    string joinWhereColumn = "";       // Format: TableName.ColumnName
    BinaryOperator joinWhereOperator = NO_BINOP_CLAUSE;
    int joinWhereValue = 0;
    
    // PROJECT clause
    bool joinHasProject = false;
    vector<string> joinProjectColumns;  // Format: TableName.ColumnName

    string loadRelationName = "";

    string printRelationName = "";

    string projectionResultRelationName = "";
    vector<string> projectionColumnList;
    string projectionRelationName = "";

    string renameFromColumnName = "";
    string renameToColumnName = "";
    string renameRelationName = "";

    SelectType selectType = NO_SELECT_CLAUSE;
    BinaryOperator selectionBinaryOperator = NO_BINOP_CLAUSE;
    string selectionResultRelationName = "";
    string selectionRelationName = "";
    string selectionFirstColumnName = "";
    string selectionSecondColumnName = "";
    int selectionIntLiteral = 0;

    SortingStrategy sortingStrategy = NO_SORT_CLAUSE;
    string sortResultRelationName = "";
    string sortColumnName = "";
    string sortRelationName = "";
    
    // Extended SORT fields for Phase 2
    vector<string> sortColumnNames;
    vector<SortingStrategy> sortingStrategies;
    int sortTopX = -1;      // -1 means not specified
    int sortBottomY = -1;   // -1 means not specified

    string sourceFileName = "";
    string transactionInputFileName = "";
    string transactionOutputFileName = "";

    int setBufferSize = 0;

    // Graph-related fields
    string loadGraphName = "";
    char loadGraphType = 'D';

    string exportGraphName = "";

    string pathResultGraphName = "";
    string pathGraphName = "";
    int pathSrcNodeId = 0;
    int pathDestNodeId = 0;

    struct PathCondition
    {
        string attribute = "";
        char target = 'N';
        int requiredValue = -1;
        bool isUniformCheck = false;
        bool isAnyCheck = false;
    };
    vector<PathCondition> pathConditions;

    string degreeGraphName = "";
    int degreeNodeId = 0;

    string printGraphName = "";


    struct AggregateExpression
    {
        string functionName = "";
        string columnName = "";
        bool isCountStar = false;
    };

    vector<string> groupByResultRelationNames;
    vector<string> groupByAttributes;
    string groupByRelationName = "";
    AggregateExpression groupByHavingLeft;
    AggregateExpression groupByHavingRight;
    bool groupByHavingRightIsNumber = false;
    int groupByHavingNumber = 0;
    BinaryOperator groupByHavingOperator = NO_BINOP_CLAUSE;
    vector<AggregateExpression> groupByReturnAggregates;

    ParsedQuery();
    void clear();
};

bool syntacticParse();
bool syntacticParseCLEAR();
bool syntacticParseCROSS();
bool syntacticParseDISTINCT();
bool syntacticParseEXPORT();
bool syntacticParseINDEX();
bool syntacticParseJOIN();
bool syntacticParseLIST();
bool syntacticParseLOAD();
bool syntacticParsePRINT();
bool syntacticParsePROJECTION();
bool syntacticParseRENAME();
bool syntacticParseSELECTION();
bool syntacticParseSETBUFFER();
bool syntacticParseSORT();
bool syntacticParseSOURCE();
bool syntacticParseLOAD_GRAPH();
bool syntacticParseEXPORT_GRAPH();
bool syntacticParsePATH();
bool syntacticParseDEGREE();
bool syntacticParsePRINT_GRAPH();
bool syntacticParseGROUP_BY();
bool syntacticParseTRANSACTION();

bool isFileExists(string tableName);
bool isQueryFile(string fileName);
