# Change the GRPC location according to your system 
GRPC_DIR=/Users/erfanz/workspace/grpc




PROJECT_ROOT = $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

OBJ = main.o udp_handler.o

CXX = g++
CPPFLAGS += `pkg-config --cflags protobuf grpc`
CXXFLAGS += -std=c++17 \
			-g \
			-I$(GRPC_DIR) \
			-I$(GRPC_DIR)/third_party/abseil-cpp


LDFLAGS += -L/usr/local/lib `pkg-config --libs protobuf grpc++`\
			-pthread\
			-lgrpc++_reflection\
			-ldl


all:	run

run:	$(OBJ)
	$(CXX) -o $@ $^ $(LDFLAGS)

#%.o:	$(PROJECT_ROOT)%.cpp
#	$(CXX) -c $(CFLAGS) $(CXXFLAGS) $(CPPFLAGS) -o $@ $<
#
#%.o:	$(PROJECT_ROOT)%.c
#	$(CC) -c $(CFLAGS) $(CPPFLAGS) -o $@ $<

clean:
	rm -fr run $(OBJ) $(SVR_OBJ)
