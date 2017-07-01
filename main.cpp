#include "proxy.h"

int main(int argc, char** argv) {
    if (argc != 3) {
        cout << "Usage: [HTTP port] [HTTPS port]\n";
        return 0;
    }
    Proxy proxy;
    proxy.run(string(argv[1]),string(argv[2]));
}
