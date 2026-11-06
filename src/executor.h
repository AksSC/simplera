#pragma once
#include"semanticParser.h"

void executeCommand();

void executeCLEAR();
void executeCROSS();
void executeDISTINCT();
void executeEXPORT();
void executeEXPORT_GRAPH();
void executeINDEX();
void executeJOIN();
void executeLIST();
void executeLOAD();
void executeLOAD_GRAPH();
void executeDEGREE();
void executePATH();
void executePRINT();
void executePRINT_GRAPH();
void executeGROUP_BY();
void executePROJECTION();
void executeRENAME();
void executeSELECTION();
void executeSETBUFFER();
void executeSORT();
void executeSOURCE();
void executeTRANSACTION();

bool evaluateBinOp(int value1, int value2, BinaryOperator binaryOperator);
void printRowCount(int rowCount);