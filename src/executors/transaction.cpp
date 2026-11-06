#include "global.h"
#include <algorithm>
#include <climits>
#include <deque>
#include <fstream>
#include <set>
#include <sstream>
#include <unordered_map>

namespace
{
enum TransactionOpType
{
    TXN_BEGIN,
    TXN_READ,
    TXN_WRITE,
    TXN_COMMIT
};

enum TxnStatus
{
    TXN_ACTIVE,
    TXN_WAITING,
    TXN_ABORTED,
    TXN_COMMITTED
};

enum PageLockType
{
    LOCK_NONE,
    LOCK_SHARED,
    LOCK_EXCLUSIVE
};

enum LockDecision
{
    LOCK_GRANTED,
    LOCK_WAIT,
    LOCK_ABORT
};

struct TxnOperation
{
    TransactionOpType type = TXN_BEGIN;
    string txnId = "";
    string tableName = "";
    int pageNumber = 0;
    int lineNumber = 0;
};

struct WaitingRequest
{
    string txnId = "";
    PageLockType requestedLockType = LOCK_SHARED;
    TxnOperation operation;
};

struct PageLockState
{
    PageLockType lockType = LOCK_NONE;
    set<string> owners;
    deque<WaitingRequest> waitQueue;
};

struct TransactionState
{
    string txnId = "";
    long long timestamp = 0;
    TxnStatus status = TXN_ACTIVE;
    vector<string> heldPages;
    deque<TxnOperation> deferredOperations;
};

enum TxnLifecycleStatus
{
    TXN_NOT_STARTED,
    TXN_LIFECYCLE_ACTIVE,
    TXN_LIFECYCLE_ENDED
};

static vector<string> tokenizeBySpace(const string &line)
{
    vector<string> tokens;
    string token;
    stringstream ss(line);
    while (ss >> token)
        tokens.emplace_back(token);
    return tokens;
}

static bool parseInteger(const string &token, int &value)
{
    try
    {
        size_t consumedChars = 0;
        int parsedValue = stoi(token, &consumedChars);
        if (consumedChars != token.size())
            return false;
        value = parsedValue;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

static bool parseScheduleLine(const string &line, int lineNumber, TxnOperation &operation)
{
    vector<string> tokens = tokenizeBySpace(line);
    if (tokens.empty())
        return false;

    operation.lineNumber = lineNumber;
    if (tokens[0] == "BEGIN")
    {
        if (tokens.size() != 2)
            return false;
        operation.type = TXN_BEGIN;
        operation.txnId = tokens[1];
        return true;
    }
    if (tokens[0] == "COMMIT")
    {
        if (tokens.size() != 2)
            return false;
        operation.type = TXN_COMMIT;
        operation.txnId = tokens[1];
        return true;
    }
    if (tokens[0] == "READ" || tokens[0] == "WRITE")
    {
        if (tokens.size() != 4)
            return false;
        operation.type = (tokens[0] == "READ") ? TXN_READ : TXN_WRITE;
        operation.txnId = tokens[1];
        operation.tableName = tokens[2];
        if (!parseInteger(tokens[3], operation.pageNumber))
            return false;
        return true;
    }
    return false;
}

static string removeExtension(const string &filename)
{
    size_t dotPos = filename.rfind('.');
    if (dotPos == string::npos)
        return filename;
    return filename.substr(0, dotPos);
}

class TransactionSimulator
{
private:
    vector<TxnOperation> scheduleOperations;
    unordered_map<string, vector<TxnOperation>> restartPlans;
    unordered_map<string, TransactionState> transactions;
    unordered_map<string, PageLockState> pageLocks;
    deque<string> restartQueue;
    set<string> restartQueued;
    vector<string> executionLogs;
    long long logicalTime = 0;

    string makePageKey(const string &tableName, int pageNumber) const
    {
        return tableName + "#" + to_string(pageNumber);
    }

    string displayPageKey(const string &pageKey) const
    {
        size_t sepPos = pageKey.find('#');
        if (sepPos == string::npos)
            return pageKey;
        return pageKey.substr(0, sepPos) + " " + pageKey.substr(sepPos + 1);
    }

    void appendLog(const string &logLine)
    {
        executionLogs.emplace_back(logLine);
    }

    bool holdsPage(const TransactionState &transaction, const string &pageKey) const
    {
        return find(transaction.heldPages.begin(), transaction.heldPages.end(), pageKey) != transaction.heldPages.end();
    }

    void addHeldPage(TransactionState &transaction, const string &pageKey)
    {
        if (!holdsPage(transaction, pageKey))
            transaction.heldPages.emplace_back(pageKey);
    }

    vector<string> conflictingOwners(const string &requester, const set<string> &owners) const
    {
        vector<string> conflicts;
        for (const string &owner : owners)
        {
            if (owner != requester)
                conflicts.emplace_back(owner);
        }
        return conflicts;
    }

    bool canGrantLockNow(const string &txnId, const string &pageKey, PageLockType requestedType) const
    {
        auto lockIt = pageLocks.find(pageKey);
        if (lockIt == pageLocks.end())
            return true;

        const PageLockState &lockState = lockIt->second;
        if (lockState.lockType == LOCK_NONE)
            return true;
        if (lockState.lockType == LOCK_SHARED)
        {
            if (requestedType == LOCK_SHARED)
                return true;
            if (lockState.owners.size() == 1 && lockState.owners.count(txnId))
                return true;
            return false;
        }
        if (lockState.lockType == LOCK_EXCLUSIVE)
        {
            return (lockState.owners.size() == 1 && lockState.owners.count(txnId));
        }
        return false;
    }

    void grantLock(const string &txnId, const string &pageKey, PageLockType requestedType)
    {
        PageLockState &lockState = pageLocks[pageKey];
        if (requestedType == LOCK_SHARED)
        {
            lockState.owners.insert(txnId);
            if (lockState.lockType == LOCK_NONE)
                lockState.lockType = LOCK_SHARED;
        }
        else
        {
            lockState.owners.clear();
            lockState.owners.insert(txnId);
            lockState.lockType = LOCK_EXCLUSIVE;
        }
        addHeldPage(transactions[txnId], pageKey);
    }

    void enqueueRestart(const string &txnId)
    {
        if (restartQueued.count(txnId))
            return;
        restartQueue.push_back(txnId);
        restartQueued.insert(txnId);
    }

    string dequeueNextRestart()
    {
        if (restartQueue.empty())
            return "";

        auto bestIt = restartQueue.begin();
        for (auto it = restartQueue.begin(); it != restartQueue.end(); ++it)
        {
            const string &candidateTxn = *it;
            const string &bestTxn = *bestIt;

            long long candidateTs = transactions.count(candidateTxn) ? transactions[candidateTxn].timestamp : LLONG_MAX;
            long long bestTs = transactions.count(bestTxn) ? transactions[bestTxn].timestamp : LLONG_MAX;

            if (candidateTs < bestTs || (candidateTs == bestTs && candidateTxn < bestTxn))
                bestIt = it;
        }

        string chosenTxn = *bestIt;
        restartQueue.erase(bestIt);
        restartQueued.erase(chosenTxn);
        return chosenTxn;
    }

    void executeDeferredOperations(const string &txnId)
    {
        if (!transactions.count(txnId))
            return;
        TransactionState &transaction = transactions[txnId];
        while (transaction.status == TXN_ACTIVE && !transaction.deferredOperations.empty())
        {
            TxnOperation operation = transaction.deferredOperations.front();
            transaction.deferredOperations.pop_front();
            processOperation(operation, true);
        }
    }

    void releaseAllLocks(const string &txnId)
    {
        if (!transactions.count(txnId))
            return;
        TransactionState &transaction = transactions[txnId];
        vector<string> pagesToRelease = transaction.heldPages;
        transaction.heldPages.clear();

        for (const string &pageKey : pagesToRelease)
        {
            PageLockState &lockState = pageLocks[pageKey];
            lockState.owners.erase(txnId);
            if (lockState.owners.empty())
                lockState.lockType = LOCK_NONE;
            appendLog("unlock(" + displayPageKey(pageKey) + ") by " + txnId);
        }

        tryWakeWaitingTransactions();
    }

    void abortTransaction(const string &txnId)
    {
        if (!transactions.count(txnId))
            return;
        TransactionState &transaction = transactions[txnId];
        if (transaction.status == TXN_ABORTED || transaction.status == TXN_COMMITTED)
            return;

        appendLog("ABORT " + txnId);
        appendLog("ROLLBACK " + txnId);
        transaction.status = TXN_ABORTED;
        transaction.deferredOperations.clear();
        releaseAllLocks(txnId);
        enqueueRestart(txnId);
    }

    LockDecision requestLock(const TxnOperation &operation, PageLockType requestedType, bool fromWakeQueue)
    {
        TransactionState &transaction = transactions[operation.txnId];
        string pageKey = makePageKey(operation.tableName, operation.pageNumber);
        string lockName = (requestedType == LOCK_SHARED) ? "shared" : "exclusive";

        if (!fromWakeQueue)
            appendLog(operation.txnId + " requests " + lockName + " lock(" + operation.tableName + " " + to_string(operation.pageNumber) + ")");

        if (canGrantLockNow(operation.txnId, pageKey, requestedType))
        {
            grantLock(operation.txnId, pageKey, requestedType);
            if (!fromWakeQueue)
                appendLog("Lock granted");
            return LOCK_GRANTED;
        }

        PageLockState &lockState = pageLocks[pageKey];
        vector<string> conflicts = conflictingOwners(operation.txnId, lockState.owners);
        bool isOlderThanAllConflicts = true;
        for (const string &owner : conflicts)
        {
            if (transaction.timestamp > transactions[owner].timestamp)
            {
                isOlderThanAllConflicts = false;
                break;
            }
        }

        if (isOlderThanAllConflicts)
        {
            if (!fromWakeQueue)
            {
                transaction.status = TXN_WAITING;
                WaitingRequest waitingRequest;
                waitingRequest.txnId = operation.txnId;
                waitingRequest.requestedLockType = requestedType;
                waitingRequest.operation = operation;
                lockState.waitQueue.push_back(waitingRequest);
                appendLog(operation.txnId + " waits (older transaction allowed to wait)");
            }
            return LOCK_WAIT;
        }

        appendLog(operation.txnId + " dies (younger transaction cannot wait for older)");
        abortTransaction(operation.txnId);
        return LOCK_ABORT;
    }

    void tryWakeWaitingTransactions()
    {
        bool progress = true;
        while (progress)
        {
            progress = false;
            vector<string> pageKeys;
            pageKeys.reserve(pageLocks.size());
            for (const auto &entry : pageLocks)
                pageKeys.emplace_back(entry.first);

            for (const string &pageKey : pageKeys)
            {
                while (true)
                {
                    auto lockIt = pageLocks.find(pageKey);
                    if (lockIt == pageLocks.end() || lockIt->second.waitQueue.empty())
                        break;

                    WaitingRequest waitingRequest = lockIt->second.waitQueue.front();
                    if (!transactions.count(waitingRequest.txnId) ||
                        transactions[waitingRequest.txnId].status != TXN_WAITING)
                    {
                        lockIt->second.waitQueue.pop_front();
                        progress = true;
                        continue;
                    }

                    if (!canGrantLockNow(waitingRequest.txnId, pageKey, waitingRequest.requestedLockType))
                        break;

                    lockIt->second.waitQueue.pop_front();
                    grantLock(waitingRequest.txnId, pageKey, waitingRequest.requestedLockType);
                    transactions[waitingRequest.txnId].status = TXN_ACTIVE;
                    appendLog(waitingRequest.txnId + " " +
                              ((waitingRequest.requestedLockType == LOCK_SHARED) ? "shared" : "exclusive") +
                              " lock(" + displayPageKey(pageKey) + ") granted");
                    processReadOrWrite(waitingRequest.operation, true);
                    executeDeferredOperations(waitingRequest.txnId);
                    progress = true;
                }
            }
        }
    }

    void processReadOrWrite(const TxnOperation &operation, bool fromWakeQueue)
    {
        if (!transactions.count(operation.txnId))
            return;

        TransactionState &transaction = transactions[operation.txnId];
        if (transaction.status == TXN_ABORTED || transaction.status == TXN_COMMITTED)
            return;

        PageLockType lockType = (operation.type == TXN_READ) ? LOCK_SHARED : LOCK_EXCLUSIVE;
        LockDecision decision = requestLock(operation, lockType, fromWakeQueue);
        if (decision != LOCK_GRANTED)
            return;

        // Phase-3 simulation still routes page access via existing cursor/buffer flow.
        Cursor cursor(operation.tableName, operation.pageNumber);
        (void)cursor.getNext();

        if (operation.type == TXN_READ)
            appendLog("READ " + operation.txnId + " " + operation.tableName + " " + to_string(operation.pageNumber));
        else
            appendLog("WRITE " + operation.txnId + " " + operation.tableName + " " + to_string(operation.pageNumber));
    }

    void processCommit(const TxnOperation &operation)
    {
        if (!transactions.count(operation.txnId))
            return;
        TransactionState &transaction = transactions[operation.txnId];
        if (transaction.status != TXN_ACTIVE)
            return;

        appendLog("COMMIT " + operation.txnId);
        transaction.status = TXN_COMMITTED;
        releaseAllLocks(operation.txnId);
    }

    void processBegin(const TxnOperation &operation, bool isRestart)
    {
        TransactionState transaction;
        transaction.txnId = operation.txnId;
        transaction.timestamp = logicalTime;
        transaction.status = TXN_ACTIVE;
        transactions[operation.txnId] = transaction;

        if (isRestart)
            appendLog("Restart " + operation.txnId + " [TS=" + to_string(transaction.timestamp) + "]");
        else
            appendLog("BEGIN " + operation.txnId + " [TS=" + to_string(transaction.timestamp) + "]");
    }

    void processOperation(const TxnOperation &operation, bool isDeferredOperation)
    {
        if (operation.type == TXN_BEGIN)
        {
            processBegin(operation, false);
            return;
        }

        if (!transactions.count(operation.txnId))
            return;

        TransactionState &transaction = transactions[operation.txnId];
        if (transaction.status == TXN_WAITING)
        {
            if (!isDeferredOperation)
                transaction.deferredOperations.push_back(operation);
            return;
        }

        if (transaction.status == TXN_ABORTED || transaction.status == TXN_COMMITTED)
            return;

        if (operation.type == TXN_READ || operation.type == TXN_WRITE)
            processReadOrWrite(operation, false);
        else if (operation.type == TXN_COMMIT)
            processCommit(operation);
    }

public:
    bool loadSchedule(const string &inputFilePath)
    {
        ifstream fin(inputFilePath);
        if (!fin.is_open())
            return false;

        string line;
        int lineNumber = 0;
        while (getline(fin, line))
        {
            lineNumber++;
            if (line.empty())
                continue;
            TxnOperation operation;
            if (!parseScheduleLine(line, lineNumber, operation))
                return false;

            scheduleOperations.emplace_back(operation);
            if (operation.type != TXN_BEGIN)
                restartPlans[operation.txnId].emplace_back(operation);
        }
        fin.close();
        return true;
    }

    void run()
    {
        for (const TxnOperation &operation : scheduleOperations)
        {
            logicalTime++;
            processOperation(operation, false);
        }

        while (!restartQueue.empty())
        {
            string txnId = dequeueNextRestart();
            if (txnId.empty())
                break;

            TxnOperation restartBegin;
            restartBegin.type = TXN_BEGIN;
            restartBegin.txnId = txnId;
            logicalTime++;
            processBegin(restartBegin, true);

            for (const TxnOperation &plannedOperation : restartPlans[txnId])
            {
                if (!transactions.count(txnId) || transactions[txnId].status != TXN_ACTIVE)
                    break;
                logicalTime++;
                processOperation(plannedOperation, false);
            }
        }
    }

    const vector<string> &logs() const
    {
        return executionLogs;
    }
};
} // namespace

/**
 * @brief
 * SYNTAX: TRANSACTION input_file
 */
bool syntacticParseTRANSACTION()
{
    logger.log("syntacticParseTRANSACTION");
    if (tokenizedQuery.size() != 2)
    {
        cout << "SYNTAX ERROR" << endl;
        return false;
    }

    parsedQuery.queryType = TRANSACTION;
    parsedQuery.transactionInputFileName = tokenizedQuery[1];
    parsedQuery.transactionOutputFileName = removeExtension(tokenizedQuery[1]) + " output.txt";
    return true;
}

bool semanticParseTRANSACTION()
{
    logger.log("semanticParseTRANSACTION");

    string scheduleFilePath = "../data/" + parsedQuery.transactionInputFileName;
    ifstream fin(scheduleFilePath);
    if (!fin.is_open())
    {
        cout << "SEMANTIC ERROR: Data file doesn't exist" << endl;
        return false;
    }

    unordered_map<string, TxnLifecycleStatus> transactionLifecycle;
    string line;
    int lineNumber = 0;
    while (getline(fin, line))
    {
        lineNumber++;
        if (line.empty())
            continue;

        TxnOperation operation;
        if (!parseScheduleLine(line, lineNumber, operation))
        {
            cout << "SEMANTIC ERROR: Invalid transaction schedule" << endl;
            fin.close();
            return false;
        }

        if (operation.type == TXN_BEGIN)
        {
            if (transactionLifecycle.count(operation.txnId) &&
                transactionLifecycle[operation.txnId] != TXN_NOT_STARTED)
            {
                cout << "SEMANTIC ERROR: Invalid transaction schedule" << endl;
                fin.close();
                return false;
            }
            transactionLifecycle[operation.txnId] = TXN_LIFECYCLE_ACTIVE;
            continue;
        }

        if (!transactionLifecycle.count(operation.txnId) ||
            transactionLifecycle[operation.txnId] != TXN_LIFECYCLE_ACTIVE)
        {
            cout << "SEMANTIC ERROR: Invalid transaction schedule" << endl;
            fin.close();
            return false;
        }

        if (operation.type == TXN_READ || operation.type == TXN_WRITE)
        {
            if (!tableCatalogue.isTable(operation.tableName))
            {
                cout << "SEMANTIC ERROR: Relation doesn't exist" << endl;
                fin.close();
                return false;
            }

            Table *table = tableCatalogue.getTable(operation.tableName);
            if (operation.pageNumber < 0 || operation.pageNumber >= table->blockCount)
            {
                cout << "SEMANTIC ERROR: Page doesn't exist" << endl;
                fin.close();
                return false;
            }
        }

        if (operation.type == TXN_COMMIT)
            transactionLifecycle[operation.txnId] = TXN_LIFECYCLE_ENDED;
    }
    fin.close();
    return true;
}

void executeTRANSACTION()
{
    logger.log("executeTRANSACTION");

    string scheduleFilePath = "../data/" + parsedQuery.transactionInputFileName;
    string outputFilePath = "../data/" + parsedQuery.transactionOutputFileName;

    TransactionSimulator simulator;
    if (!simulator.loadSchedule(scheduleFilePath))
    {
        cout << "SEMANTIC ERROR: Invalid transaction schedule" << endl;
        return;
    }

    simulator.run();

    ofstream fout(outputFilePath, ios::out);
    const vector<string> &logLines = simulator.logs();
    for (size_t i = 0; i < logLines.size(); i++)
    {
        fout << logLines[i];
        if (i + 1 < logLines.size())
            fout << "\n";
    }
    fout.close();

    cout << "Transaction schedule executed. Output file: " << parsedQuery.transactionOutputFileName << endl;
}
