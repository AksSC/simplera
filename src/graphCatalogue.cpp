#include "graphCatalogue.h"

extern Logger logger;

void GraphCatalogue::insertGraph(Graph *graph)
{
    logger.log("GraphCatalogue::insertGraph");
    this->graphs[graph->graphName] = graph;
}

void GraphCatalogue::deleteGraph(string graphName)
{
    logger.log("GraphCatalogue::deleteGraph");
    this->graphs[graphName]->unload();
    delete this->graphs[graphName];
    this->graphs.erase(graphName);
}

Graph *GraphCatalogue::getGraph(string graphName)
{
    logger.log("GraphCatalogue::getGraph");
    if (this->graphs.count(graphName))
        return this->graphs[graphName];
    return nullptr;
}

bool GraphCatalogue::isGraph(string graphName)
{
    logger.log("GraphCatalogue::isGraph");
    return this->graphs.count(graphName) > 0;
}

GraphCatalogue::~GraphCatalogue()
{
    logger.log("GraphCatalogue::~GraphCatalogue");
    for (auto graph : this->graphs)
    {
        graph.second->unload();
        delete graph.second;
    }
}
