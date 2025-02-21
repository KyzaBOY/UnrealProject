#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Networking.h"
#include "MuServerReceiveThread.h"
// 🔹 Prevenindo conflitos entre Windows e ODBC
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX  // Previne conflito com min/max da UE5
#endif

#include <Windows.h>  // Deve vir antes de SQL Headers
#include <sql.h>
#include <sqlext.h>

#include "MuServerGameInstance.generated.h"

// 🔹 Eventos para Blueprints
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnNewConnection, FString, SocketID, FString, IP);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnFinishConnection, FString, SocketID, FString, IP);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnClientPacketReceived, FString, SocketID, FString, Data);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnSQLAsyncResult, FString, Label, FString, Param, int32, Result);


UCLASS()
class MUSERVER_API UMuServerGameInstance : public UGameInstance
{
    GENERATED_BODY()

public:
    UMuServerGameInstance();

protected:
    virtual void Init() override;
    virtual void Shutdown() override;

public:
    UPROPERTY(BlueprintAssignable, Category = "Network")
    FOnNewConnection OnNewConnection;

    UPROPERTY(BlueprintAssignable, Category = "Network")
    FOnFinishConnection OnFinishConnection;

    UPROPERTY(BlueprintAssignable, Category = "Network")
    FOnClientPacketReceived OnClientPacketReceived;

    UPROPERTY(BlueprintAssignable, Category = "SQL")
    FOnSQLAsyncResult OnSQLAsyncResult;

    UFUNCTION(BlueprintCallable, Category = "Network")
    bool StartServer(int32 Port);

    UFUNCTION(BlueprintCallable, Category = "Network")
    void StopServer();

    UFUNCTION(BlueprintCallable, Category = "Network")
    void SendAsyncPacket(FString SocketID, FString PacketData);

    // 🔹 Métodos SQL síncronos
    UFUNCTION(BlueprintCallable, Category = "SQL")
    bool MuSQLConnect(const FString& ODBC, const FString& User, const FString& Password);

    UFUNCTION(BlueprintCallable, Category = "SQL")
    void MuSQLDisconnect();

    UFUNCTION(BlueprintCallable, Category = "SQL")
    bool MuSQLCheck();

    UFUNCTION(BlueprintCallable, Category = "SQL")
    bool MuSQLQuery(const FString& Query);

    UFUNCTION(BlueprintCallable, Category = "SQL")
    void MuSQLClose();

    UFUNCTION(BlueprintCallable, Category = "SQL")
    bool MuSQLFetch();

    UFUNCTION(BlueprintCallable, Category = "SQL")
    int32 MuSQLGetResult(int32 ResultIndex);

    UFUNCTION(BlueprintCallable, Category = "SQL")
    int32 MuSQLGetNumber(const FString& ColumnName);

    UFUNCTION(BlueprintCallable, Category = "SQL")
    float MuSQLGetSingle(const FString& ColumnName);

    UFUNCTION(BlueprintCallable, Category = "SQL")
    FString MuSQLGetString(const FString& ColumnName);

    // 🔹 Métodos SQL assíncronos
    UFUNCTION(BlueprintCallable, Category = "SQL")
    void SQLAsyncConnect(const FString& ODBC, const FString& User, const FString& Password);

    UFUNCTION(BlueprintCallable, Category = "SQL")
    void SQLAsyncDisconnect();

    UFUNCTION(BlueprintCallable, Category = "SQL")
    bool SQLAsyncCheck();

    UFUNCTION(BlueprintCallable, Category = "SQL")
    bool SQLAsyncQuery(const FString& Query, const FString& Label = TEXT(""), const FString& Param = TEXT(""));

    UFUNCTION(BlueprintCallable, Category = "SQL")
    int32 SQLAsyncGetResult(int32 ResultIndex);

    UFUNCTION(BlueprintCallable, Category = "SQL")
    int32 SQLAsyncGetNumber(const FString& ColumnName);

    UFUNCTION(BlueprintCallable, Category = "SQL")
    float SQLAsyncGetSingle(const FString& ColumnName);

    UFUNCTION(BlueprintCallable, Category = "SQL")
    FString SQLAsyncGetString(const FString& ColumnName);

    FCriticalSection ClientSocketsMutex;
    TMap<FString, FSocket*> ClientSockets;

private:
    FSocket* ServerSocket;

    FRunnableThread* ServerThread;
    MuServerReceiveThread* ReceiveThread;
    // 🔒 Mutex para sincronizar acessos ao mapa

    // 🔒 Conexão com ODBC
    SQLHENV EnvHandle;
    SQLHDBC ConnectionHandle;
    SQLHSTMT StatementHandle;

    bool ExecuteQuery(const FString& Query);

};
