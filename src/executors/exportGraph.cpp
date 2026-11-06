#include "global.h"
#include "graphCatalogue.h"
/**
 * @brief
 * SYNTAX: EXPORT GRAPH <graph_name>
 */
bool syntacticParseEXPORT_GRAPH()
{
    logger.log("syntacticParseEXPORT_GRAPH");
    if (tokenizedQuery.size() != 3 || tokenizedQuery[1] != "GRAPH")
    {
        cout << "SYNTAX ERROR" << endl;
        return false;
    }
    parsedQuery.queryType = EXPORT_GRAPH;
    parsedQuery.exportGraphName = tokenizedQuery[2];
    return true;
}

bool semanticParseEXPORT_GRAPH()
{
    logger.log("semanticParseEXPORT_GRAPH");
    if (!graphCatalogue.isGraph(parsedQuery.exportGraphName))
    {
        cout << "SEMANTIC ERROR: Graph doesn't exist" << endl;
        return false;
    }
    return true;
}

void executeEXPORT_GRAPH()
{
    logger.log("executeEXPORT_GRAPH");
    Graph *graph = graphCatalogue.getGraph(parsedQuery.exportGraphName);
    graph->makePermanent();
    return;
}
