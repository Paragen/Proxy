#pragma once

#include "server.h"

class Server;

enum class socketMode {
    toRead, toWrite, toReadAndWrite, toListen, none
};

enum class socketState {
    error, open, close, connecting
};

class SocketWrap;

class Socket {

public :
    friend class Server;

    socketMode mode;
    socketState state;
    int fd;
    Server* host;
    void* dataPtr;


    Socket(Server* host, socketMode mode, socketState state, int fd);
    Socket(const Socket&) = delete;
    Socket(Socket&&) = delete;


    void setMode(socketMode mode);
    socketMode getMode() const;
    
    socketState getState() const;

    unsigned read(char* buf, unsigned maxSize);
    unsigned write(char* data, unsigned size);
    std::vector<SocketWrap> accept(unsigned maxCount);

    template<class T>
    T* getData() const;
    template<class T>
    void setData(T*);


    void close();

    ~Socket();
};


class SocketWrap {

    std::weak_ptr<Socket> sock;

    SocketWrap(std::weak_ptr<Socket>);

public:

    friend class Socket;
    friend class Server;

    SocketWrap();
    SocketWrap(const SocketWrap& another);

    void setMode(socketMode mode);
    socketMode getMode() const;

    socketState getState() const;

    unsigned read(char* buf, unsigned maxSize);
    unsigned write(char* data, unsigned size);
    std::vector<SocketWrap> accept(unsigned maxCount);

    bool isValid() const;
    Socket& toSocket();

    template<class T>
    void setData(T* ptr);
    template<class T>
    T* getData() const;

    void close();

};


template<class T>
void Socket::setData(T* ptr) {
    dataPtr = (void*)ptr;
}

template<class T>
T* Socket::getData() const {
    return (T*)dataPtr;
}


template<class T>
void SocketWrap::setData(T* ptr) {
    if (!sock.expired()) {
        sock.lock()->setData(ptr);
    }
}

template<class T>
T* SocketWrap::getData() const {
    if (!sock.expired()) {
        return sock.lock()->getData<T>();
    }

    return nullptr;
}



