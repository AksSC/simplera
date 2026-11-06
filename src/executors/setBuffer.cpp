#include "global.h"
/**
 * @brief 
 * SYNTAX: SETBUFFER K
 * Sets the number of buffers (blocks that can be stored in main memory).
 * 2 <= K <= 10
 */

bool syntacticParseSETBUFFER()
{
    logger.log("syntacticParseSETBUFFER");
    if (tokenizedQuery.size() != 2)
    {
        cout << "SYNTAX ERROR" << endl;
        return false;
    }
    
    int bufferSize;
    try
    {
        size_t used = 0;
        bufferSize = stoi(tokenizedQuery[1], &used);
        if (used != tokenizedQuery[1].size())
        {
            cout << "SYNTAX ERROR" << endl;
            return false;
        }
    }
    catch (...)
    {
        cout << "SYNTAX ERROR" << endl;
        return false;
    }
    
    parsedQuery.setBufferSize = bufferSize;
    parsedQuery.queryType = SETBUFFER;
    return true;
}

bool semanticParseSETBUFFER()
{
    logger.log("semanticParseSETBUFFER");
    int k = parsedQuery.setBufferSize;
    if (k < 2 || k > 10)
    {
        cout << "SEMANTIC ERROR: Buffer size must be between 2 and 10" << endl;
        return false;
    }
    return true;
}

void executeSETBUFFER()
{
    logger.log("executeSETBUFFER");
    BLOCK_COUNT = parsedQuery.setBufferSize;
    cout << "Buffer size set to " << BLOCK_COUNT << endl;
}
