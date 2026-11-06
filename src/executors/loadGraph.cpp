#include "graphCatalogue.h"
#include "global.h"

/**
 * @brief
 * SYNTAX: LOAD GRAPH <graph_name> U/D
 */
bool syntacticParseLOAD_GRAPH()
{
    logger.log("syntacticParseLOAD_GRAPH");
    if (tokenizedQuery.size() != 4 || tokenizedQuery[1] != "GRAPH")
    {
        cout << "SYNTAX ERROR" << endl;
        return false;
    }
    parsedQuery.queryType = LOAD_GRAPH;
    parsedQuery.loadGraphName = tokenizedQuery[2];
    string typeToken = tokenizedQuery[3];
    if (typeToken != "U" && typeToken != "D")
    {
        cout << "SYNTAX ERROR" << endl;
        return false;
    }
    parsedQuery.loadGraphType = typeToken[0];
    return true;
}

static bool isGraphFileExists(string graphName, char graphType)
{
    string suffix = graphType == 'D' ? "_D.csv" : "_U.csv";
    string nodeFile = "../data/" + graphName + "_Nodes" + suffix;
    string edgeFile = "../data/" + graphName + "_Edges" + suffix;
    ifstream finNode(nodeFile, ios::in);
    ifstream finEdge(edgeFile, ios::in);
    bool exists = finNode.good() && finEdge.good();
    finNode.close();
    finEdge.close();
    return exists;
}

bool semanticParseLOAD_GRAPH()
{
    logger.log("semanticParseLOAD_GRAPH");
    if (graphCatalogue.isGraph(parsedQuery.loadGraphName))
    {
        cout << "SEMANTIC ERROR: Graph already exists" << endl;
        return false;
    }
    if (!isGraphFileExists(parsedQuery.loadGraphName, parsedQuery.loadGraphType))
    {
        cout << "SEMANTIC ERROR: Data file doesn't exist" << endl;
        return false;
    }
    return true;
}

void executeLOAD_GRAPH()
{
    logger.log("executeLOAD_GRAPH");
    Graph *graph = new Graph(parsedQuery.loadGraphName, parsedQuery.loadGraphType);
    if (graph->load())
    {
        graphCatalogue.insertGraph(graph);
        cout << "Loaded Graph.Node Count:" << graph->nodeCount << ",Edge Count:" << graph->edgeCount << endl;
        return;
    }
    cout << "LOAD GRAPH ERROR" << endl;
    graph->unload();
    delete graph;
}
