#pragma once

#include <boost/signals2.hpp>
#include "socket.h"
#include <memory>
#include <vector>
#include <map>
#include <string>
#include <list>

class Socket;
class SocketWrap;
enum class socketMode;
enum class socketState;

/* Required slots
 * necessary: toRead, toWrite, toListen, and error (in setErrorSlot)
 * optional: none
 * do NOT use: toReadAndWrite */

class Server {

    friend class Socket;
    friend class SocketWrap;

    typedef boost::signals2::signal<void(Socket& socket)> signalType;

    std::list<std::shared_ptr<Socket>> servedSockets;
    std::vector<Socket*> toRemoveList;
    int epollFd;
    std::map<socketMode,signalType> signalsHolder;
    signalType errorSignalHolder;

    std::weak_ptr<Socket> addSocket(socketMode mode, socketState state, int fd);
    void epollChange(Socket* socket, socketMode mode);
    void needToRemove(Socket* socket);
public:
    typedef signalType::slot_type slotType;
    static const int listenBacklog = 10;
    
    Server();
    Server(const Server&) = delete;
    ~Server();

    SocketWrap connect(const std::string& address, const std::string&  port, socketMode mode, void* dataPtr);
    SocketWrap listen(const std::string& port, void* dataPtr);

    //do not pass toReadAndWrite via mode
    void setSlot(const slotType&  slot, socketMode mode);
    void removeSlot(const slotType& slot, socketMode mode);

    void setErrorSlot(const slotType& slot);
    void removeErrorSlot(const slotType& slot);
    
    void run(int timeOut = -1);

};
