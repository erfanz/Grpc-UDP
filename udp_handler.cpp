#include "udp_handler.hpp"
#include <iostream>

#define LOG_TEST(x) gpr_log(GPR_INFO, "%s", #x)
int rcv_buf_size = 1024;
int snd_buf_size = 1024;

int UdpHandler::g_number_of_reads = 0;
int UdpHandler::g_number_of_writes = 0;
int UdpHandler::g_number_of_bytes_read = 0;
int UdpHandler::g_number_of_orphan_calls = 0;
int UdpHandler::g_number_of_starts = 0;
int UdpHandler::g_num_listeners = 1;

int64_t g_fixture_slowdown_factor = 1;
int64_t g_poller_slowdown_factor = 1;

grpc_pollset *UdpHandler::g_pollset;
gpr_mu *UdpHandler::g_mu;

struct test_socket_factory {
	grpc_socket_factory base;
	int number_of_socket_calls;
	int number_of_bind_calls;
};
typedef struct test_socket_factory test_socket_factory;

static int test_socket_factory_socket(grpc_socket_factory *factory, int domain,
		int type, int protocol) {
	test_socket_factory *f = reinterpret_cast<test_socket_factory *>(factory);
	f->number_of_socket_calls++;
	return socket(domain, type, protocol);
}

gpr_timespec grpc_timeout_seconds_to_deadline(int64_t time_s) {
	return gpr_time_add(
			gpr_now(GPR_CLOCK_MONOTONIC),
			gpr_time_from_millis(
					1 * g_fixture_slowdown_factor *
					g_poller_slowdown_factor * static_cast<int64_t>(1e3) * time_s,
					GPR_TIMESPAN));
}

UdpHandler::UdpHandler(grpc_fd *emfd, void *user_data):
				GrpcUdpHandler(emfd, user_data), emfd_(emfd) {
	std::cout << "UDP Handler created" << std::endl;
	g_number_of_starts++;
}


bool UdpHandler::Read() {
	char read_buffer[512];
	ssize_t byte_count;

	gpr_mu_lock(UdpHandler::g_mu);
	byte_count = recv(grpc_fd_wrapped_fd(emfd()), read_buffer, sizeof(read_buffer), 0);
	//std::cout << "receive " << byte_count << " on handler " << this << std::endl;

	g_number_of_reads++;
	g_number_of_bytes_read += static_cast<int>(byte_count);

	processIncomingMsg((void*)read_buffer, byte_count);

	gpr_log(GPR_DEBUG, "receive %zu on handler %p", byte_count, this);
	GPR_ASSERT(GRPC_LOG_IF_ERROR("pollset_kick",
			grpc_pollset_kick(UdpHandler::g_pollset, nullptr)));
	gpr_mu_unlock(UdpHandler::g_mu);
	return false;
}

void UdpHandler::OnCanWrite(void * /*user_data*/,
		grpc_closure * /*notify_on_write_closure*/) {
	gpr_mu_lock(g_mu);
	g_number_of_writes++;

	GPR_ASSERT(GRPC_LOG_IF_ERROR("pollset_kick",
			grpc_pollset_kick(UdpHandler::g_pollset, nullptr)));
	gpr_mu_unlock(g_mu);
}

void UdpHandler::OnFdAboutToOrphan(grpc_closure *orphan_fd_closure, void * /*user_data*/) {
	std::cout << "ORPHAN--- " << std::endl;
	gpr_log(GPR_INFO, "gRPC FD about to be orphaned: %d",
			grpc_fd_wrapped_fd(emfd()));
	grpc_core::ExecCtx::Run(DEBUG_LOCATION, orphan_fd_closure, GRPC_ERROR_NONE);
	g_number_of_orphan_calls++;
}

void UdpHandler::startLoop(volatile bool &udpServerFinished) {
	grpc_core::ExecCtx exec_ctx;
	grpc_millis deadline;

	gpr_mu_lock(g_mu);

	while (!udpServerFinished) {
		deadline = grpc_timespec_to_millis_round_up(grpc_timeout_seconds_to_deadline(10));

		grpc_pollset_worker *worker = nullptr;
		GPR_ASSERT(GRPC_LOG_IF_ERROR(
				"pollset_work", grpc_pollset_work(UdpHandler::g_pollset, &worker, deadline)));

		gpr_mu_unlock(UdpHandler::g_mu);
		grpc_core::ExecCtx::Get()->Flush();
		gpr_mu_lock(UdpHandler::g_mu);

	}
	gpr_mu_unlock(g_mu);
}
