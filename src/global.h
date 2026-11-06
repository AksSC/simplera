#pragma once
#include"executor.h"

class GraphCatalogue;

extern float BLOCK_SIZE;
extern uint BLOCK_COUNT;
extern uint PRINT_COUNT;
extern vector<string> tokenizedQuery;
extern ParsedQuery parsedQuery;
extern TableCatalogue tableCatalogue;
extern GraphCatalogue graphCatalogue;
extern BufferManager bufferManager;