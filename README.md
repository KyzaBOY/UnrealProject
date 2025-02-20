# UnrealProject
This project is about CLIENT / SERVER connections and communicate system.

SERVER FUNCTIONS:

Event OnNewConnection(SocketID, IP)
- Called every new player connection.
- SocketID = Player Socket ID.
- IP = Player IP.
  
Event OnFinishConnection(SocketID, IP)
- Called every player disconnection.
- SocketID = Player Socket ID.
- IP = Player IP.

Event OnClientPacketReceived(SocketID, Data)
- Called every player packet received.
- SocketID = Player Socket ID.
- Data = String data, can be received using append to divide using delimiters: (Type: / Data:).

Function StartServer(Port)
- Start server using machine ip
- Port = You can choose wath PORT you want to open the SOCKET SERVER.

Function StopServer()
- Finish server, closing all threads and socket connections.

Function SendAsyncPacket(SocketID, PacketData)
- Send async packet to the choosen SocketID.
- SocketID = Player SocketID.
- PacketData = String data, build them using MakeLiteralString, and divide informations using Append | to divide Type:/Data:


CLIENT FUNCTIONS:

Event OnPacketReceived(Data)
- Called every new packet received from server.
- Data = String data, can be received using append to divide using delimiters: (Type: / Data:).

Function ConnectToServer(IP, Port)
- Connect to a server using an IP/Port if returns TRUE opens a ListenThread.
- IP = Server IP.
- Port = Server Port.

Function DisconnectFromServer()
- Disconnect from current server and close the ListenThread.

Function SendAsyncPacket(Data)
- Send async packet to the Server.
- Data = String data, build them using MakeLiteralString, and divide informations using Append | to divide Type:/Data:



