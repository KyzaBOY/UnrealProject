#include "MuClientReceiveThread.h"
#include "MuClientGameInstance.h"
#include "Async/Async.h"

// Construtor
MuClientReceiveThread::MuClientReceiveThread(FSocket* InSocket, UMuClientGameInstance* InClient)
{
    ClientSocket = InSocket;
    OwnerClient = InClient;
    bRunning = true;
}

// Destrutor
MuClientReceiveThread::~MuClientReceiveThread()
{
    StopThread();
}

// 📌 Método SEGURO para parar a thread sem crash
void MuClientReceiveThread::StopThread()
{
    FScopeLock Lock(&Mutex); // 🔹 Bloqueia acesso simultâneo à thread
    bRunning = false;

    if (ClientSocket)
    {
        UE_LOG(LogTemp, Log, TEXT("🔹 Fechando socket dentro da thread..."));
        ClientSocket->Close();
        ClientSocket = nullptr;
    }
}

uint32 MuClientReceiveThread::Run()
{
    while (bRunning)
    {
        if (!ClientSocket || ClientSocket->GetConnectionState() != ESocketConnectionState::SCS_Connected)
        {
            break;
        }

        uint8 HeaderBuffer[3];
        int32 BytesRead = 0;

        if (!ClientSocket->Recv(HeaderBuffer, 3, BytesRead) || BytesRead != 3)
        {
            continue;
        }

        uint8 PacketSize = HeaderBuffer[0];
        uint8 HeadCode = HeaderBuffer[1];
        uint8 SubCode = HeaderBuffer[2];

        if (PacketSize < 3 || PacketSize > 255)
        {
            continue;
        }

        TArray<uint8> DataBuffer;
        DataBuffer.SetNum(PacketSize - 3 + 1);

        int32 TotalBytesRead = 0;
        while (TotalBytesRead < (PacketSize - 3))
        {
            int32 BytesReadNow = 0;
            if (!ClientSocket->Recv(DataBuffer.GetData() + TotalBytesRead, (PacketSize - 3) - TotalBytesRead, BytesReadNow) || BytesReadNow <= 0)
            {
                break;
            }
            TotalBytesRead += BytesReadNow;
        }

        if (TotalBytesRead != (PacketSize - 3))
        {
            continue;
        }

        DataBuffer[PacketSize - 3] = '\0';
        FString DataString = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(DataBuffer.GetData())));
        DataString.TrimStartAndEndInline();

        if (OwnerClient)
        {
            AsyncTask(ENamedThreads::GameThread, [this, HeadCode, SubCode, DataString]()
                {
                    if (OwnerClient)
                    {
                        OwnerClient->OnPacketReceived.Broadcast(HeadCode, SubCode, DataString);
                    }
                });
        }
    }

    return 0;
}
