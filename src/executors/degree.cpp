#include "global.h"
#include "graphCatalogue.h"
#include <regex>
/**
 * @brief
 * SYNTAX: DEGREE <graph_name> <node_id>
 */
bool syntacticParseDEGREE()
{
    logger.log("syntacticParseDEGREE");
    if (tokenizedQuery.size() != 3)
    {
        cout << "SYNTAX ERROR" << endl;
        return false;
    }
    parsedQuery.queryType = DEGREE;
    parsedQuery.degreeGraphName = tokenizedQuery[1];

    regex numeric("[-]?[0-9]+");
    if (!regex_match(tokenizedQuery[2], numeric))
    {
        cout << "SYNTAX ERROR" << endl;
        return false;
    }
    parsedQuery.degreeNodeId = stoi(tokenizedQuery[2]);
    return true;
}

bool semanticParseDEGREE()
{
    logger.log("semanticParseDEGREE");
    if (!graphCatalogue.isGraph(parsedQuery.degreeGraphName))
    {
        cout << "SEMANTIC ERROR: Graph doesn't exist" << endl;
        return false;
    }
    Graph *graph = graphCatalogue.getGraph(parsedQuery.degreeGraphName);
    if (graph->nodeIdToIndex.find(parsedQuery.degreeNodeId) == graph->nodeIdToIndex.end())
    {
        cout << "Node does not exist" << endl;
        return false;
    }
    return true;
}

void executeDEGREE()
{
    logger.log("executeDEGREE");
    Graph *graph = graphCatalogue.getGraph(parsedQuery.degreeGraphName);
    int outDegree = 0;
    int inDegree = 0;
    if (!graph->getDegree(parsedQuery.degreeNodeId, outDegree, inDegree))
    {
        cout << "Node does not exist" << endl;
        return;
    }
    if (graph->graphType == 'U')
        cout << outDegree << endl;
    else
        cout << (outDegree + inDegree) << endl;
    return;
}
