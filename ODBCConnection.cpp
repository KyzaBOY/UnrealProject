#include "ODBCConnection.h"
/*
FODBCConnection::FODBCConnection() : EnvHandle(SQL_NULL_HENV), ConnectionHandle(SQL_NULL_HDBC), StatementHandle(SQL_NULL_HSTMT) {}

FODBCConnection::~FODBCConnection()
{
    Disconnect();
}

bool FODBCConnection::Connect(const FString& ODBC, const FString& Login, const FString& Pass, const FString& Database)
{
    SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &EnvHandle);
    SQLSetEnvAttr(EnvHandle, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
    SQLAllocHandle(SQL_HANDLE_DBC, EnvHandle, &ConnectionHandle);

    SQLCHAR ConnStr[1024];
    SQLCHAR OutStr[1024];
    SQLSMALLINT OutStrLen;

    FString ConnString = FString::Printf(TEXT("DRIVER={SQL Server};SERVER=%s;DATABASE=%s;UID=%s;PWD=%s;"), *ODBC, *Database, *Login, *Pass);
    FCStringAnsi::Strncpy((char*)ConnStr, TCHAR_TO_UTF8(*ConnString), ConnString.Len());

    if (SQLDriverConnectA(ConnectionHandle, NULL, ConnStr, SQL_NTS, OutStr, 1024, &OutStrLen, SQL_DRIVER_NOPROMPT) == SQL_SUCCESS)
    {
        return true;
    }

    return false;
}

void FODBCConnection::Disconnect()
{
    if (StatementHandle) SQLFreeHandle(SQL_HANDLE_STMT, StatementHandle);
    if (ConnectionHandle) SQLDisconnect(ConnectionHandle);
    if (ConnectionHandle) SQLFreeHandle(SQL_HANDLE_DBC, ConnectionHandle);
    if (EnvHandle) SQLFreeHandle(SQL_HANDLE_ENV, EnvHandle);
}

bool FODBCConnection::ExecuteQuery(const FString& Query, const TArray<FString>& Parameters)
{
    SQLAllocHandle(SQL_HANDLE_STMT, ConnectionHandle, &StatementHandle);

    FString FinalQuery = Query;
    for (const FString& Param : Parameters)
    {
        FinalQuery = FinalQuery.Replace(TEXT("?"), *Param, ESearchCase::IgnoreCase);
    }

    SQLCHAR* QueryText = (SQLCHAR*)TCHAR_TO_ANSI(*FinalQuery);
    return SQLExecDirectA(StatementHandle, QueryText, SQL_NTS) == SQL_SUCCESS;
}
*/