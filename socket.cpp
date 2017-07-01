#include "socket.h"
#include <sys/socket.h>
#include <fcntl.h>
#include <iostream>

using namespace std;


void Socket::setMode(socketMode mode) {
    //assert(state == socketState::open);
    try {
        host->epollChange(this, mode);
    } catch (...) {
        close();
        return;
    }
    this->mode = mode;
}

socketMode Socket::getMode() const {
    return mode;
}

socketState Socket::getState() const {
    return state;
}

unsigned Socket::read(char *buf, unsigned maxSize) {
    assert(state == socketState::open);
    unsigned total = 0;
    long counter;
    while (total < maxSize) {
        if ((counter = recv(fd, buf + total, maxSize - total, 0)) <= 0) {
            if (counter == 0) {
                state = socketState::close;
                break;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            state = socketState::error;
            throw runtime_error("Unable to read from the socket." + string(strerror(errno)));
        }
        total += counter;
    }

    return total;
}

unsigned Socket::write(char *data, unsigned size) {
    assert(state == socketState::open);

    unsigned total = 0;
    long counter;

    while (total < size) {
        if ((counter = send(fd, data + total, size - total, 0)) < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            state = socketState::error;
            throw runtime_error("Unable to write into the socket." + string(strerror(errno)));
        }
        total += counter;
    }

    return total;
}

vector<SocketWrap> Socket::accept(unsigned maxCount = 0) {
    assert(state == socketState::open && mode == socketMode::toListen);

    vector<SocketWrap> accepted;
    int currentFd;

    for (int i = 0; maxCount == 0 || i < maxCount; ++i) {
        if ((currentFd = ::accept(fd, NULL, NULL)) < 0) {
//                if (errno == EWOULDBLOCK || errno == EAGAIN) {
//                    break;
//                }
//                throw runtime_error("Unable to accept connection.");
            break;
        }

        if (fcntl(currentFd, F_SETFL, O_NONBLOCK) < 0) {
            ::close(currentFd);
            continue;
        }
        auto currentSocketPtr = host->addSocket(socketMode::none, socketState::open, currentFd);
        try {
            accepted.push_back(SocketWrap(currentSocketPtr));
        } catch (...) {
            currentSocketPtr.lock()->close();
            break;
        }
    }
    return accepted;

}


Socket::~Socket() {
    try {
        host->epollChange(this, socketMode::none);
    } catch (...) {
        //ignore
    }
    ::close(fd);
}


Socket::Socket(Server *host, socketMode mode, socketState state, int fd) : host(host), mode(mode), state(state),
                                                                           fd(fd) {}

void Socket::close() {
    state = socketState::close;
    host->needToRemove(this);
}


SocketWrap::SocketWrap(std::weak_ptr<Socket> ptr) : sock(ptr) {}

SocketWrap::SocketWrap() {}

SocketWrap::SocketWrap(const SocketWrap &another) : sock(another.sock) {}

void SocketWrap::setMode(socketMode mode) {
    if (!sock.expired()) {
        sock.lock()->setMode(mode);
    }
}

socketMode SocketWrap::getMode() const {
    if (!sock.expired()) {
        return sock.lock()->getMode();
    }

    return socketMode::none;
}

socketState SocketWrap::getState() const {
    if (!sock.expired()) {
        return sock.lock()->getState();
    }
    return socketState::close;
}

unsigned SocketWrap::read(char *buf, unsigned maxSize) {
    if (!sock.expired()) {
        return sock.lock()->read(buf, maxSize);
    }

    return 0;
}

unsigned SocketWrap::write(char *data, unsigned size) {
    if (!sock.expired()) {
        return sock.lock()->write(data, size);
    }

    return 0;
}

std::vector<SocketWrap> SocketWrap::accept(unsigned maxCount) {
    if (!sock.expired()) {
        return sock.lock()->accept(maxCount);
    }

    return std::vector<SocketWrap>();
}

bool SocketWrap::isValid() const {
    return !sock.expired();
}


void SocketWrap::close() {
    if (!sock.expired()) {
        sock.lock()->close();
    }
}

Socket &SocketWrap::toSocket() {
    if (!sock.expired()) {
        return *sock.lock();
    }
    throw std::runtime_error("Invalid socket.");
}