/*
 * client.cpp
 *
 *  Created on: Sep 21, 2020
 *      Author: ezamanian
 */


#include "udp_handler.hpp"

#include <netdb.h>
#include <string>
#include <thread>         // this_thread::sleep_for
#include <grpcpp/grpcpp.h>
#include <vector>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using namespace std;


int client_port = 6666;
int server_port = 5555;
int num_of_msgs = 1000;


int listening_port;
int remote_port;

int fd;

int received_msgs_cnt = 0;
vector<bool> is_received(num_of_msgs, false);


enum Role {
	CLIENT,
	SERVER
};

struct Request {
	int id;
	char content[10];
};

struct Response {
	int id;
	char content[15];
};

Role role;

bool udpServerFinished = false;


void sendUdp(const char *hostname, int port, const char* payload, size_t size) {
	auto transferred = write(fd, (void*)payload, size);
	assert(size == transferred);
}


class DbUdpHandler final: public UdpHandler {
public:
	DbUdpHandler(grpc_fd *emfd, void *user_data):
		UdpHandler(emfd, user_data) {
	}
	void processIncomingMsg(void* msg, ssize_t size) override {
		received_msgs_cnt++;
		(void)size;
		int id;
		if (role == Role::CLIENT) {
			Response res;
			assert(size == sizeof(Response));
			memcpy((void*)&res, (void*)msg, size);
			id = res.id;
			cout << "Msg: response for request " << res.id
					<< ": content: " << res.content << endl;
		}
		else {
			Request req;
			assert(size == sizeof(Request));
			memcpy((void*)&req, (void*)msg, size);
			id = req.id;
			cout << "Msg: request " << req.id
					<< ": content: " << req.content << endl;

			// send response
			Response res;
			res.id = req.id;
			string con = "Res::" + string(req.content);
			strcpy((char*)res.content, con.c_str());
			sendUdp("127.0.0.1", remote_port, (const char*)&res, sizeof(Response));
		}

		// check for termination condition (both for client and server)
		if (received_msgs_cnt == num_of_msgs) {
			cout << "This is the last msg" << endl;
			udpServerFinished = true;
		}

		// mark the id of the current message
		is_received[id] = true;

		// if this was the last message, print the missing msg ids
		if (id == num_of_msgs - 1) {
			cout << "missing ids: ";
			for (int i = 0; i < num_of_msgs; i++) {
				if (is_received[i] == false)
					cout << i << ", ";
			}
			cout << endl;
			cout << "% of missing messages: "
					<< 1.0 - ((double)received_msgs_cnt / num_of_msgs) << endl;
		}
	}
};


class DbUdpHandlerFactory : public GrpcUdpHandlerFactory {
public:
	GrpcUdpHandler *CreateUdpHandler(grpc_fd *emfd, void *user_data) override {
		gpr_log(GPR_INFO, "create udp handler for fd %d", grpc_fd_wrapped_fd(emfd));
		DbUdpHandler *dbUdpHandler = new DbUdpHandler(emfd, user_data);
		return dbUdpHandler;
	}

	void DestroyUdpHandler(GrpcUdpHandler *handler) override {
		gpr_log(GPR_INFO, "Destroy handler");
		delete reinterpret_cast<DbUdpHandler *>(handler);
	}
};





int main(int argc, char *argv[]) {
	if (argc != 2) {
		cerr << "Usage: './run client'  or  './run server' " << endl;
		return 1;
	}

	string r(argv[1]);
	if (r == "client") {
		cout << "Client is initializing to send requests!" << endl;
		role = Role::CLIENT;
		listening_port = client_port;
		remote_port = server_port;
	}
	else if (r == "server") {
		cout << "Server is initializing to accept requests!" << endl;
		role = Role::SERVER;
		listening_port = server_port;
		remote_port = client_port;
	}
	else {
		cerr << "Usage: './run client'  or  './run server' " << endl;
		return 1;
	}

	/********************************************************
	 * Initialize UDP Listener
	 ********************************************************/
	/* Initialize the grpc library. After it's called,
	 * a matching invocation to grpc_shutdown() is expected. */
	grpc_init();

	grpc_core::ExecCtx exec_ctx;
	DbUdpHandler::g_pollset = static_cast<grpc_pollset *>(
			gpr_zalloc(grpc_pollset_size()));
	grpc_pollset_init(DbUdpHandler::g_pollset, &DbUdpHandler::g_mu);

	grpc_resolved_address resolved_addr;
	struct sockaddr_storage *addr =
			reinterpret_cast<struct sockaddr_storage *>(resolved_addr.addr);
	int svrfd;

	grpc_udp_server *s = grpc_udp_server_create(nullptr);
	grpc_pollset *pollsets[1];

	memset(&resolved_addr, 0, sizeof(resolved_addr));
	resolved_addr.len = static_cast<socklen_t>(sizeof(struct sockaddr_storage));
	addr->ss_family = AF_INET;

	grpc_sockaddr_set_port(&resolved_addr, listening_port);

	/* setup UDP server */
	DbUdpHandlerFactory handlerFactory;

	int rcv_buf_size = 1024;
	int snd_buf_size = 1024;

	GPR_ASSERT(grpc_udp_server_add_port(s, &resolved_addr, rcv_buf_size,
			snd_buf_size, &handlerFactory,
			DbUdpHandler::g_num_listeners) > 0);

	svrfd = grpc_udp_server_get_fd(s, 0);
	GPR_ASSERT(svrfd >= 0);
	GPR_ASSERT(getsockname(svrfd, (struct sockaddr *) addr,
			(socklen_t *) &resolved_addr.len) == 0);
	GPR_ASSERT(resolved_addr.len <= sizeof(struct sockaddr_storage));

	pollsets[0] = DbUdpHandler::g_pollset;
	grpc_udp_server_start(s, pollsets, 1, nullptr);

	string addr_str = grpc_sockaddr_to_string(&resolved_addr, 1);
	cout << "UDP Server listening on: " << addr_str << endl;
	thread udpPollerThread(
			DbUdpHandler::startLoop, ref(udpServerFinished));



	/********************************************************
	 * Establish connection to the other side
	 ********************************************************/
//	grpc_core::ExecCtx exec_ctx;
	struct sockaddr_in serv_addr;
	struct hostent *server = gethostbyname("127.0.0.1");

	bzero((char *) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	bcopy((char *) server->h_addr,
			(char *) &serv_addr.sin_addr.s_addr,
			server->h_length);
	serv_addr.sin_port = htons(remote_port);

	fd = socket(serv_addr.sin_family, SOCK_DGRAM, 0);
	GPR_ASSERT(fd >= 0);
	GPR_ASSERT(connect(fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) == 0);


	/********************************************************
	 * Send requests
	 ********************************************************/
	if (role == Role::CLIENT) {
		static int counter = 0;

		for (int i = 0; i < num_of_msgs; i++) {
			Request req;
			req.id = counter++;
			strcpy((char*)req.content, to_string(req.id).c_str());
			cout << "Sending request " << req.id << endl;
			sendUdp("127.0.0.1", remote_port, (char*)&req, sizeof(Request));
		}
	}

	/********************************************************
	 * wait for client to finish
	 ********************************************************/
	udpPollerThread.join();


	/********************************************************
	 * cleanup
	 ********************************************************/
	close(fd);
	gpr_free(DbUdpHandler::g_pollset);
	grpc_shutdown();


	cout << "finished successfully!" << endl;

	return 0;
}
