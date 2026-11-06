#include "global.h"
#include "graphCatalogue.h"
/**
 * @brief
 * SYNTAX: PRINT GRAPH <graph_name>
 */
bool syntacticParsePRINT_GRAPH()
{
    logger.log("syntacticParsePRINT_GRAPH");
    if (tokenizedQuery.size() != 3 || tokenizedQuery[1] != "GRAPH")
    {
        cout << "SYNTAX ERROR" << endl;
        return false;
    }
    parsedQuery.queryType = PRINT_GRAPH;
    parsedQuery.printGraphName = tokenizedQuery[2];
    return true;
}

bool semanticParsePRINT_GRAPH()
{
    logger.log("semanticParsePRINT_GRAPH");
    if (!graphCatalogue.isGraph(parsedQuery.printGraphName))
    {
        cout << "SEMANTIC ERROR: Graph doesn't exist" << endl;
        return false;
    }
    return true;
}

void executePRINT_GRAPH()
{
    logger.log("executePRINT_GRAPH");
    Graph *graph = graphCatalogue.getGraph(parsedQuery.printGraphName);
    graph->print();
    return;
}
