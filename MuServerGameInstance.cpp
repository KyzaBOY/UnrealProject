#include "MuServerGameInstance.h"

#include "Async/Async.h"
UMuServerGameInstance::UMuServerGameInstance()
{
    ServerSocket = nullptr;
    ReceiveThread = nullptr;
    ServerThread = nullptr;

    EnvHandle = SQL_NULL_HENV;
    ConnectionHandle = SQL_NULL_HDBC;
    StatementHandle = SQL_NULL_HSTMT;
}

void UMuServerGameInstance::Init()
{
    Super::Init();
}

void UMuServerGameInstance::Shutdown()
{
    MuSQLDisconnect();
    SQLAsyncDisconnect();
    StopServer();
    Super::Shutdown();
}

// 📌 Inicia o Servidor
bool UMuServerGameInstance::StartServer(int32 Port)
{
    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);

    ServerSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("MuServerSocket"), false);
    if (!ServerSocket)
    {
        UE_LOG(LogTemp, Error, TEXT("Falha ao criar o socket do servidor."));
        return false;
    }

    TSharedPtr<FInternetAddr> ServerAddress = SocketSubsystem->CreateInternetAddr();
    ServerAddress->SetAnyAddress();
    ServerAddress->SetPort(Port);

    if (!ServerSocket->Bind(*ServerAddress))
    {
        UE_LOG(LogTemp, Error, TEXT("Falha ao vincular o servidor à porta %d."), Port);
        return false;
    }

    if (!ServerSocket->Listen(8))  // Permitir até 8 conexões simultâneas
    {
        UE_LOG(LogTemp, Error, TEXT("Falha ao colocar o servidor em modo de escuta."));
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("Servidor iniciado na porta %d!"), Port);

    ReceiveThread = new MuServerReceiveThread(ServerSocket, this);
    ServerThread = FRunnableThread::Create(ReceiveThread, TEXT("MuServerReceiveThread"));

    return true;
}

void UMuServerGameInstance::StopServer()
{
    UE_LOG(LogTemp, Log, TEXT("Finalizando o servidor..."));

    // 🚨 Primeiro, pare a thread de recebimento
    if (ReceiveThread)
    {
        ReceiveThread->Stop();
        ServerThread->WaitForCompletion(); // Aguarde a thread finalizar
        delete ServerThread;
        ServerThread = nullptr;

        delete ReceiveThread;
        ReceiveThread = nullptr;
    }

    // 🚨 Agora feche os sockets APÓS garantir que a thread parou
    if (ServerSocket)
    {
        ServerSocket->Close();
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ServerSocket);
        ServerSocket = nullptr;
    }

    UE_LOG(LogTemp, Log, TEXT("Servidor desligado com sucesso."));
}

void UMuServerGameInstance::SendAsyncPacket(FString SocketID, FString PacketData)
{
    UE_LOG(LogTemp, Log, TEXT("📤 Tentando enviar pacote para SocketID: %s"), *SocketID);

    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, SocketID, PacketData]()
        {
            FSocket* ClientSocket = nullptr;

            // 🔒 Acesso seguro ao mapa de sockets
            ClientSocketsMutex.Lock();
            if (ClientSockets.Contains(SocketID))
            {
                ClientSocket = ClientSockets[SocketID];
            }
            ClientSocketsMutex.Unlock();

            if (!ClientSocket)
            {
                UE_LOG(LogTemp, Error, TEXT("❌ ClientSocket é NULL para SocketID: %s"), *SocketID);
                UE_LOG(LogTemp, Error, TEXT("🔎 Cliente pode ter sido removido ou nunca foi adicionado corretamente!"));
                return;
            }

            if (ClientSocket->GetConnectionState() != ESocketConnectionState::SCS_Connected)
            {
                UE_LOG(LogTemp, Warning, TEXT("⚠️ O Cliente %s não está mais conectado."), *SocketID);
                return;
            }

            // 🔹 Convertendo a string para UTF-8
            FTCHARToUTF8 Converter(*PacketData);
            int32 DataSize = Converter.Length();

            // 🔹 Criando um buffer para envio (tamanho + dados)
            TArray<uint8> Buffer;
            Buffer.SetNum(2 + DataSize);

            // 🔹 Armazena o tamanho do pacote nos primeiros 2 bytes
            uint16 PacketSize = static_cast<uint16>(DataSize);
            FMemory::Memcpy(Buffer.GetData(), &PacketSize, sizeof(uint16));

            // 🔹 Armazena os dados logo em seguida
            FMemory::Memcpy(Buffer.GetData() + 2, Converter.Get(), DataSize);

            // 🔹 Enviar o pacote completo
            int32 BytesSent = 0;
            bool bSuccess = ClientSocket->Send(Buffer.GetData(), Buffer.Num(), BytesSent);

            if (bSuccess)
            {
                UE_LOG(LogTemp, Log, TEXT("✅ Pacote enviado para %s (%d bytes): %s"), *SocketID, BytesSent, *PacketData);
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("❌ Falha ao enviar pacote para %s!"), *SocketID);
            }
        });
}




bool UMuServerGameInstance::MuSQLConnect(const FString& ODBC, const FString& User, const FString& Password)
{
    SQLRETURN RetCode;

    // 🔹 Aloca ambiente ODBC
    RetCode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &EnvHandle);
    if (RetCode != SQL_SUCCESS && RetCode != SQL_SUCCESS_WITH_INFO)
    {
        UE_LOG(LogTemp, Error, TEXT("❌ Falha ao alocar ambiente ODBC."));
        return false;
    }

    // 🔹 Define a versão ODBC
    RetCode = SQLSetEnvAttr(EnvHandle, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
    if (RetCode != SQL_SUCCESS && RetCode != SQL_SUCCESS_WITH_INFO)
    {
        UE_LOG(LogTemp, Error, TEXT("❌ Falha ao definir versão do ODBC."));
        SQLFreeHandle(SQL_HANDLE_ENV, EnvHandle);
        return false;
    }

    // 🔹 Aloca conexão
    RetCode = SQLAllocHandle(SQL_HANDLE_DBC, EnvHandle, &ConnectionHandle);
    if (RetCode != SQL_SUCCESS && RetCode != SQL_SUCCESS_WITH_INFO)
    {
        UE_LOG(LogTemp, Error, TEXT("❌ Falha ao alocar conexão ODBC."));
        SQLFreeHandle(SQL_HANDLE_ENV, EnvHandle);
        return false;
    }

    // 🔹 String de conexão (garanta que os dados estão corretos)
    FString ConnString = TEXT("DRIVER={ODBC Driver 11 for SQL Server};SERVER=(local);DATABASE=VERSUSMU;UID=sa;PWD=wz1wqgwp;");


    // 🔹 Buffer para armazenar a string de conexão
    SQLCHAR ConnStr[1024] = { 0 };
    SQLSMALLINT OutConnStrLen;

    // 🔹 Converte FString para ANSI (evita problemas com encoding)
    FCStringAnsi::Strncpy((char*)ConnStr, TCHAR_TO_UTF8(*ConnString), sizeof(ConnStr));

    // 🔹 Obtém o tamanho correto da string
    SQLSMALLINT ConnStrLen = static_cast<SQLSMALLINT>(strlen((char*)ConnStr));

    // 🔹 Log para verificar a string antes da conexão
    UE_LOG(LogTemp, Log, TEXT("🔎 Tentando conectar com string: %s"), *ConnString);

    // 🔹 Tenta conectar ao banco de dados
    RetCode = SQLDriverConnectA(
        ConnectionHandle,  // Handle de conexão válido
        NULL,              // Handle da janela (NULL para conexões sem UI)
        ConnStr,           // String de conexão convertida
        SQL_NTS,           // Tamanho correto da string
        NULL,              // Buffer de saída (NULL se não precisar)
        0,                 // Tamanho do buffer de saída (0 se NULL)
        &OutConnStrLen,    // Ponteiro para armazenar o tamanho final
        SQL_DRIVER_NOPROMPT // Tipo de conexão (sem UI)
    );

    // 🔹 Verifica sucesso da conexão
    if (RetCode == SQL_SUCCESS || RetCode == SQL_SUCCESS_WITH_INFO)
    {
        UE_LOG(LogTemp, Log, TEXT("✅ Conexão ODBC bem-sucedida!"));
        return true;
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("❌ Falha ao conectar ao banco via ODBC. Código de erro: %d"), RetCode);

        // 🔹 Captura a mensagem de erro detalhada
        SQLCHAR SQLState[6], MessageText[256];
        SQLINTEGER NativeError;
        SQLSMALLINT TextLength;

        if (SQLGetDiagRecA(SQL_HANDLE_DBC, ConnectionHandle, 1, SQLState, &NativeError, MessageText, sizeof(MessageText), &TextLength) == SQL_SUCCESS)
        {
            UE_LOG(LogTemp, Error, TEXT("🔴 ODBC Error: SQLState=%s, Message=%s"), ANSI_TO_TCHAR((char*)SQLState), ANSI_TO_TCHAR((char*)MessageText));
        }

        // 🔹 Libera recursos em caso de falha
        MuSQLDisconnect();
        SQLFreeHandle(SQL_HANDLE_DBC, ConnectionHandle);
        SQLFreeHandle(SQL_HANDLE_ENV, EnvHandle);

        return false;
    }
}




// 📌 Desconectar do banco de dados
void UMuServerGameInstance::MuSQLDisconnect()
{
    if (ConnectionHandle)
    {
        SQLDisconnect(ConnectionHandle);
        SQLFreeHandle(SQL_HANDLE_DBC, ConnectionHandle);
        ConnectionHandle = nullptr;
    }

    if (EnvHandle)
    {
        SQLFreeHandle(SQL_HANDLE_ENV, EnvHandle);
        EnvHandle = nullptr;
    }

    UE_LOG(LogTemp, Log, TEXT("Desconectado do banco de dados com sucesso."));
}

// 📌 Verificar conexão
bool UMuServerGameInstance::MuSQLCheck()
{
    return ConnectionHandle != SQL_NULL_HDBC;
}

// 📌 Executar query síncrona
bool UMuServerGameInstance::MuSQLQuery(const FString& Query)
{
    return ExecuteQuery(Query);
}

// 📌 Fechar query
void UMuServerGameInstance::MuSQLClose()
{
    if (StatementHandle) SQLFreeHandle(SQL_HANDLE_STMT, StatementHandle);
}

int32 UMuServerGameInstance::MuSQLGetNumber(const FString& ColumnName)
{
    int32 Value = 0;
    int32 ColumnIndex = GetColumnIndex(ColumnName);

    if (ColumnIndex == -1)
    {
        UE_LOG(LogTemp, Warning, TEXT("⚠️ Coluna '%s' não encontrada. Retornando 0."), *ColumnName);
        return 0;
    }

    SQLRETURN RetCode = SQLGetData(StatementHandle, ColumnIndex, SQL_C_LONG, &Value, 0, NULL);

    if (RetCode == SQL_SUCCESS || RetCode == SQL_SUCCESS_WITH_INFO)
    {
        return Value;
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("⚠️ Falha ao buscar número da coluna '%s'. Retornando 0."), *ColumnName);
        return 0;
    }
}


float UMuServerGameInstance::MuSQLGetSingle(const FString& ColumnName)
{
    float Value = 0.0f;
    int32 ColumnIndex = GetColumnIndex(ColumnName);

    if (ColumnIndex == -1)
    {
        UE_LOG(LogTemp, Warning, TEXT("⚠️ Coluna '%s' não encontrada. Retornando 0.0."), *ColumnName);
        return 0.0f;
    }

    SQLRETURN RetCode = SQLGetData(StatementHandle, ColumnIndex, SQL_C_FLOAT, &Value, 0, NULL);

    if (RetCode == SQL_SUCCESS || RetCode == SQL_SUCCESS_WITH_INFO)
    {
        return Value;
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("⚠️ Falha ao buscar float da coluna '%s'. Retornando 0.0."), *ColumnName);
        return 0.0f;
    }
}


FString UMuServerGameInstance::MuSQLGetString(const FString& ColumnName)
{
    char Value[256] = { 0 };
    int32 ColumnIndex = GetColumnIndex(ColumnName);

    if (ColumnIndex == -1)
    {
        UE_LOG(LogTemp, Warning, TEXT("⚠️ Coluna '%s' não encontrada. Retornando 'NULL'."), *ColumnName);
        return TEXT("NULL");
    }

    SQLRETURN RetCode = SQLGetData(StatementHandle, ColumnIndex, SQL_C_CHAR, Value, sizeof(Value), NULL);

    if (RetCode == SQL_SUCCESS || RetCode == SQL_SUCCESS_WITH_INFO)
    {
        return FString(Value);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("⚠️ Falha ao buscar string da coluna '%s'. Retornando 'NULL'."), *ColumnName);
        return TEXT("NULL");
    }
}


// 📌 Executar query com parâmetros (interno)
bool UMuServerGameInstance::ExecuteQuery(const FString& Query)
{
    SQLAllocHandle(SQL_HANDLE_STMT, ConnectionHandle, &StatementHandle);
    SQLCHAR* QueryText = (SQLCHAR*)TCHAR_TO_ANSI(*Query);
    return SQLExecDirectA(StatementHandle, QueryText, SQL_NTS) == SQL_SUCCESS;
}

// 📌 Conectar ao banco de dados SQL de forma assíncrona
void UMuServerGameInstance::SQLAsyncConnect(const FString& ODBC, const FString& User, const FString& Password)
{
    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, ODBC, User, Password]()
        {
            bool Success = MuSQLConnect(ODBC, User, Password);
            AsyncTask(ENamedThreads::GameThread, [this, Success]()
                {
                    OnSQLAsyncResult.Broadcast(TEXT("AsyncConnect"), TEXT(""), Success ? 1 : 0);
                });
        });
}

// 📌 Enviar query assíncrona
bool UMuServerGameInstance::SQLAsyncQuery(const FString& Query, const FString& Label, const FString& Param)
{
    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, Query, Label, Param]()
        {
            bool Success = ExecuteQuery(Query);
            int32 Result = Success ? 1 : 0;

            AsyncTask(ENamedThreads::GameThread, [this, Label, Param, Result]()
                {
                    OnSQLAsyncResult.Broadcast(Label, Param, Result);
                });
        });

    return true;
}

// 📌 Verificar conexão assíncrona
bool UMuServerGameInstance::SQLAsyncCheck()
{
    return ConnectionHandle != SQL_NULL_HDBC;
}

// 📌 Desconectar do banco de dados assíncrono
void UMuServerGameInstance::SQLAsyncDisconnect()
{
    MuSQLDisconnect();
}

// 📌 Buscar o próximo conjunto de resultados da consulta
bool UMuServerGameInstance::MuSQLFetch()
{
    if (!StatementHandle)
    {
        UE_LOG(LogTemp, Error, TEXT("❌ SQLFetch falhou: StatementHandle é inválido."));
        return false;
    }

    return SQLFetch(StatementHandle) == SQL_SUCCESS;
}

// 📌 Buscar um resultado específico por índice
int32 UMuServerGameInstance::MuSQLGetResult(int32 ResultIndex)
{
    int32 Value = 0;
    if (!StatementHandle)
    {
        UE_LOG(LogTemp, Error, TEXT("❌ SQLGetResult falhou: StatementHandle é inválido."));
        return 0;
    }

    SQLGetData(StatementHandle, ResultIndex, SQL_C_LONG, &Value, 0, NULL);
    return Value;
}

// 📌 Buscar um resultado assíncrono por índice (somente dentro de OnSQLAsyncResult)
int32 UMuServerGameInstance::SQLAsyncGetResult(int32 ResultIndex)
{
    return MuSQLGetResult(ResultIndex);
}

// 📌 Buscar um número de uma coluna específica no resultado assíncrono
int32 UMuServerGameInstance::SQLAsyncGetNumber(const FString& ColumnName)
{
    return MuSQLGetNumber(ColumnName);
}

// 📌 Buscar um valor float de uma coluna específica no resultado assíncrono
float UMuServerGameInstance::SQLAsyncGetSingle(const FString& ColumnName)
{
    return MuSQLGetSingle(ColumnName);
}

// 📌 Buscar uma string de uma coluna específica no resultado assíncrono
FString UMuServerGameInstance::SQLAsyncGetString(const FString& ColumnName)
{
    return MuSQLGetString(ColumnName);
}

int32 UMuServerGameInstance::GetColumnIndex(const FString& ColumnName)
{
    if (!StatementHandle)
    {
        UE_LOG(LogTemp, Error, TEXT("❌ GetColumnIndex falhou: StatementHandle é inválido."));
        return -1;
    }

    SQLSMALLINT NumColumns = 0;
    SQLNumResultCols(StatementHandle, &NumColumns);

    for (SQLSMALLINT i = 1; i <= NumColumns; i++) // Índice SQL começa em 1
    {
        SQLCHAR ColumnNameBuffer[128];
        SQLSMALLINT ColumnNameLength = 0;

        SQLDescribeColA(StatementHandle, i, ColumnNameBuffer, sizeof(ColumnNameBuffer), &ColumnNameLength, NULL, NULL, NULL, NULL);

        FString ColumnNameSQL = ANSI_TO_TCHAR((char*)ColumnNameBuffer);
        if (ColumnNameSQL.Equals(ColumnName, ESearchCase::IgnoreCase))
        {
            return i; // Retorna o índice correto da coluna
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("⚠️ Coluna '%s' não encontrada no resultado da query."), *ColumnName);
    return -1; // Retorna -1 se não encontrar
}

void UMuServerGameInstance::DisconnectPlayer(FString SocketID)
{
    UE_LOG(LogTemp, Log, TEXT("🔴 Desconectando jogador: %s"), *SocketID);

    ClientSocketsMutex.Lock();

    if (!ClientSockets.Contains(SocketID))
    {
        UE_LOG(LogTemp, Warning, TEXT("⚠️ Tentativa de desconectar um SocketID inexistente: %s"), *SocketID);
        ClientSocketsMutex.Unlock();
        return;
    }

    FSocket* ClientSocket = ClientSockets[SocketID];

    if (ClientSocket)
    {
        ClientSocket->Close(); // Fecha a conexão de forma segura
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ClientSocket);
    }

    ClientSockets.Remove(SocketID);
    ClientSocketsMutex.Unlock();

    // 📌 Emite o evento de desconexão para os Blueprints
    AsyncTask(ENamedThreads::GameThread, [this, SocketID]()
        {
            OnFinishConnection.Broadcast(SocketID, TEXT("Desconhecido"));
        });

    UE_LOG(LogTemp, Log, TEXT("✅ Jogador %s foi desconectado com sucesso!"), *SocketID);
}