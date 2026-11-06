#include"global.h"

bool semanticParse(){
    logger.log("semanticParse");
    switch(parsedQuery.queryType){
        case CLEAR: return semanticParseCLEAR();
        case CROSS: return semanticParseCROSS();
        case DISTINCT: return semanticParseDISTINCT();
        case EXPORT: return semanticParseEXPORT();
        case EXPORT_GRAPH: return semanticParseEXPORT_GRAPH();
        case INDEX: return semanticParseINDEX();
        case JOIN: return semanticParseJOIN();
        case LIST: return semanticParseLIST();
        case LOAD: return semanticParseLOAD();
        case LOAD_GRAPH: return semanticParseLOAD_GRAPH();
        case DEGREE: return semanticParseDEGREE();
        case PATH: return semanticParsePATH();
        case PRINT: return semanticParsePRINT();
        case PRINT_GRAPH: return semanticParsePRINT_GRAPH();
        case GROUP_BY: return semanticParseGROUP_BY();
        case PROJECTION: return semanticParsePROJECTION();
        case RENAME: return semanticParseRENAME();
        case SELECTION: return semanticParseSELECTION();
        case SETBUFFER: return semanticParseSETBUFFER();
        case SORT: return semanticParseSORT();
        case SOURCE: return semanticParseSOURCE();
        case TRANSACTION: return semanticParseTRANSACTION();
        default: cout<<"SEMANTIC ERROR"<<endl;
    }

    return false;
}