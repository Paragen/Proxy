#include <signal.h>
#include "proxy.h"

   Proxy proxy;

void my_handler(int sig,siginfo_t *siginfo,void *context) {
    exit(0);
}


int main(int argc, char** argv) {
    if (argc != 3) {
        cout << "Usage: [HTTP port] [HTTPS port]\n";
        return 0;
    }

    struct sigaction sa;
    sa.sa_sigaction = &my_handler;
    sigset_t ss;
    sigemptyset(&ss);
    sigaddset(&ss,SIGINT);
    sa.sa_mask = ss;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGINT,&sa,NULL);

 
    proxy.run(string(argv[1]),string(argv[2]));
}
