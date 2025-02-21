#pragma once
/*
#include "CoreMinimal.h"
#include <sql.h>
#include <sqlext.h>

class YOURGAME_API FODBCConnection
{
public:
    FODBCConnection();
    ~FODBCConnection();

    bool Connect(const FString& ODBC, const FString& Login, const FString& Pass, const FString& Database);
    void Disconnect();

    bool ExecuteQuery(const FString& Query, const TArray<FString>& Parameters);
    int32 GetNumber(const FString& ColumnName);
    float GetFloat(const FString& ColumnName);
    FString GetString(const FString& ColumnName);

private:
    SQLHENV EnvHandle;
    SQLHDBC ConnectionHandle;
    SQLHSTMT StatementHandle;
};
*/