#include "../ipc/ipc.h"

class Client {
    public:
        Client(IPCClient ipc) : ipc(ipc) {

        }

    private:
        IPCClient ipc;
};
