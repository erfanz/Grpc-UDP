#ifndef SRC_COMMON_UDP_HANDLER_HPP_
#define SRC_COMMON_UDP_HANDLER_HPP_

#include "src/core/lib/iomgr/udp_server.h"
#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"

using namespace std;

class UdpHandler : public GrpcUdpHandler {

public:
    UdpHandler(grpc_fd *emfd, void *user_data);
    virtual ~UdpHandler() {}
    static void startLoop(volatile bool &udpServerFinished);
    virtual void processIncomingMsg(void* msg, ssize_t size) = 0;
    static grpc_pollset *g_pollset;
    static gpr_mu *g_mu;

public:
    static int g_number_of_reads;
    static int g_number_of_writes;
    static int g_number_of_bytes_read;
    static int g_number_of_orphan_calls;
    static int g_number_of_starts;
    static int g_num_listeners;

protected:
    bool Read() override;
    void OnCanWrite(void* /*user_data*/, grpc_closure* /*notify_on_write_closure*/) override;
    void OnFdAboutToOrphan(grpc_closure *orphan_fd_closure, void* /*user_data*/) override;

    grpc_fd *emfd() { return emfd_; }

private:
    grpc_fd *emfd_;
};

#endif /* SRC_COMMON_UDP_HANDLER_HPP_ */
