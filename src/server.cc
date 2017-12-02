#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <iostream>
#include <pthread.h>
#include <unistd.h>
#include <limits.h>
#include <thread>
#include <chrono>
#include <string>
#include <sstream>
#include <sys/stat.h>
#include <sys/un.h>
#include <vector>
#include <pthread.h>
#include <thread>
#include <chrono>

#include "libmigrate.h"

typedef struct ConnectionStruct {
  int sock;
  MigrationClientStructure *migration_client;
  int client_id;
  int current_value;
} ConnectionStruct;

void * HandleConnection(void *c) {
  ConnectionStruct *conn = (ConnectionStruct *) c;
  struct sockaddr_in remote_addr;
  socklen_t remote_addrlen = sizeof(remote_addr);
  char remote_ip[INET_ADDRSTRLEN];
  getpeername(conn->sock, (struct sockaddr *) &remote_addr, &remote_addrlen);
  inet_ntop(AF_INET, &(remote_addr.sin_addr), remote_ip, INET_ADDRSTRLEN);
  std::string remote_ip_str = std::string(remote_ip);
  ClientData *client = GetIpClient(conn->migration_client, 8000, remote_ip_str);
  int i = 0;
  std::this_thread::sleep_for(std::chrono::seconds(5));
  if (client != NULL) {
    std::cout << "Migrated client" << std::endl;
    std::string data = GetAppData(std::string(client->GetState(), client->GetStateSize()));
    i = std::stoi(data);
  } else {
    std::cout << "New client" << std::endl;
  }
  while (1) {
    std::string msg = std::to_string(i++);
    std::cout << "Sending " << msg << std::endl;
    int send_res = send(conn->sock, msg.c_str(), msg.length(), 0);
    if (send_res < 0) {
      perror("HandleConnection() send");
    }
    std::stringstream state_data_ss;
    state_data_ss << msg << std::endl;
    std::string state_data = state_data_ss.str();
    SendApplicationStateWithTcp(conn->migration_client, 8000, conn->client_id, conn->sock, state_data.c_str(), state_data.length());
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
}

void ProcessMigratedConnection(MigrationClientStructure *migration_client, ClientData *data) {
  int sock = data->GetDescriptor();

  ConnectionStruct *conn = new ConnectionStruct;

  conn->sock = sock;
  conn->client_id = data->GetClientIdentifier();
  conn->migration_client = migration_client;

  std::cout << std::string(data->GetState(), data->GetStateSize()) << std::endl;

  std::istringstream is_state(std::string(data->GetState(), data->GetStateSize()));
  std::string ip_str;
  std::string port_str;
  std::string tcp_send_seq_str;
  std::string tcp_recv_seq_str;
  std::string mss_clamp_str;
  std::string snd_wscale_str;
  std::string rcv_wscale_str;
  std::string timestamp_str;
  std::string app_info_length_str;
  std::string app_data;
  if (std::getline(is_state, ip_str, ' ') && std::getline(is_state, port_str, ' ') && std::getline(is_state, tcp_send_seq_str, ' ') && std::getline(is_state, tcp_recv_seq_str, ' ') && std::getline(is_state, mss_clamp_str, ' ') && std::getline(is_state, snd_wscale_str, ' ') && std::getline(is_state, rcv_wscale_str, ' ') && std::getline(is_state, timestamp_str, ' ') && std::getline(is_state, app_info_length_str, ' ') && std::getline(is_state, app_data)) {
      int current_value = std::stoi(app_data);
      conn->current_value = current_value;

      pthread_t client_pthread;

      std::cout << "Creating thread for migrated connection " << conn->sock << std::endl;

      // START OF DEBUG CODE
      sockaddr_in peer_addr;
      socklen_t peer_addr_len = sizeof(peer_addr);

      getpeername(conn->sock, (sockaddr *) &peer_addr, &peer_addr_len);

      char new_ip_str[INET_ADDRSTRLEN];

      inet_ntop(AF_INET, &(peer_addr.sin_addr), new_ip_str, INET_ADDRSTRLEN);

      std::cout << "New peer address: " << new_ip_str << std::endl;
      // END OF DEBUG CODE

      std::this_thread::sleep_for(std::chrono::seconds(10));
      pthread_create(&client_pthread, NULL, HandleConnection, (void *) conn);
  }
}

int main() {
  int sock;
  struct sockaddr_in addr;

  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("sock");
    exit(1);
  }

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(8000);

  if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
    perror("bind");
    exit(1);
  }

  listen(sock, 500);

  MigrationClientStructure *migration_client = RegisterAndInitMigrationService(sock, 8000);
  migration_client->context->migration_handlers[8000] = &ProcessMigratedConnection;

  int client_sock;
  struct sockaddr_in client_addr;
  socklen_t client_addrlen = sizeof(client_addr);

  int client_id = 0;

  while (1) {
    client_sock = accept(sock, (struct sockaddr *) &client_addr, &client_addrlen);
    ConnectionStruct *conn = new ConnectionStruct;
    conn->sock = client_sock;
    conn->migration_client = migration_client;
    conn->client_id = client_id++;

    pthread_t client_pthread;

    pthread_create(&client_pthread, NULL, HandleConnection, (void *) conn);
  }

  char ch;
  std::cin >> ch;
}
