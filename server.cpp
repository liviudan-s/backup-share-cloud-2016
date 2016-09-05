#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <dirent.h>
#include <string>
#include <map>
#include <unordered_map>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>

#define MAX_CLIENTS	100
#define BUFLEN 4096

using namespace std;

void error(string msg)
{
    perror(msg.c_str());
    exit(1);
}

void copy_to_buffer(char buffer[], int number, int &buffer_size)
{
    memcpy(buffer + buffer_size,&number,sizeof(number));
    buffer_size += sizeof(number);
}

void copy_to_buffer(char buffer[], long number, int &buffer_size)
{
    memcpy(buffer + buffer_size,&number,sizeof(number));
    buffer_size += sizeof(number);
}

void copy_to_buffer(char buffer[], string param, int &buffer_size)
{
    int temp_size;
    temp_size = param.length() + 1;
    copy_to_buffer(buffer, temp_size, buffer_size);
    memcpy(buffer + buffer_size, param.c_str(), temp_size);
    buffer_size += temp_size;
}

void copy_from_buffer(char buffer[], int &number, int &buffer_size)
{
    memcpy(&number, buffer + buffer_size, sizeof(number));
    buffer_size += sizeof(number);
}

void copy_from_buffer(char buffer[], long &number, int &buffer_size)
{
    memcpy(&number, buffer + buffer_size, sizeof(number));
    buffer_size += sizeof(number);
}

void copy_from_buffer(char buffer[], string& param, int &buffer_size)
{
    int temp_size;
    char param_c[255];
    copy_from_buffer(buffer, temp_size, buffer_size);
    memcpy(param_c, buffer + buffer_size, temp_size);
    buffer_size += temp_size;
    string param_temp(param_c);
    param = param_temp;
}

struct client_info
{
    client_info() : user(), login_attempts(0) {}
    client_info(string new_user, int new_login_attempts)
        : user(new_user), login_attempts(new_login_attempts) {}

    string user;
    int login_attempts;
    ifstream* in_file = nullptr;
    ofstream* out_file = nullptr;
    long blocks_left_in = 0;
    long blocks_left_out = 0;
    int last_block_size = 0;
    char buffer[BUFLEN + BUFLEN];
    int buffer_end_pos = 0;
    int current_buffer_place = 0;
    string in_file_name;
    string out_file_name;
    string user_downloading_from;
};



struct file_info
{
    file_info() : file_size(0), is_shared(false), in_use(0) {}
    file_info(long new_file_size, bool new_is_shared)
        : file_size(new_file_size), is_shared(new_is_shared), in_use(0) {}

    long file_size;
    bool is_shared;
    int in_use;
};

struct user_info
{
    user_info() : password(), file_infos() {}
    user_info(string new_password)
        : password(new_password), file_infos() {}

    string password;
    map<string, file_info> file_infos;
};

int is_frame_complete(char buffer[], int buffer_end_pos, int blocks_left_out, int last_block_size, int& end_pos)
{
    if(buffer_end_pos < 4)
    {
        return 0;
    }
    buffer_end_pos -= 4;
    end_pos += 4;

    int command_number,current_place_in_buffer = 0;
    copy_from_buffer(buffer, command_number, current_place_in_buffer);

    if(command_number == 1 || command_number == 4 || command_number == 7 || command_number == 8
        || command_number == 9 || command_number == 6)
    {
        if(buffer_end_pos < 4)
            return 0;
        else
        {
          int frame_size;
          copy_from_buffer(buffer, frame_size, current_place_in_buffer);
          buffer_end_pos -= 4;
          end_pos += 4;
          if(buffer_end_pos >= frame_size)
          {
            end_pos += frame_size;
            return 1;
          }
          else
            return 0;
        }
    }
    else
    {
        if(command_number == 2 || command_number == 3)
        {
            return 1;
        }
        else
        {
            switch(command_number)
            {
                case 5:
                    if(buffer_end_pos < 4)
                        return 0;
                    else
                    {
                      int frame_size;
                      copy_from_buffer(buffer, frame_size, current_place_in_buffer);
                      buffer_end_pos -= 4;
                      end_pos += 4;
                      int sizeof_long = sizeof(long);
                      if(buffer_end_pos >= (frame_size + sizeof_long))
                      {
                        end_pos += frame_size + sizeof_long;
                        return 1;
                      }
                      else
                        return 0;
                    }
                    break;
                case 0:
                    if(blocks_left_out == 2)
                    {
                        int sizeof_int = sizeof(int);
                        if(buffer_end_pos >= sizeof_int)
                        {
                            return 1;
                        }
                        else
                        {
                            return 0;
                        }
                    }
                    else
                    {
                        if(blocks_left_out == 1)
                        {
                            if(buffer_end_pos >= last_block_size)
                            {
                                return 1;
                            }
                            else
                            {
                                return 0;
                            }
                        }
                        else
                        {
                            int sizeof_int = sizeof(int);
                            if(buffer_end_pos >= BUFLEN - sizeof_int)
                            {
                                return 1;
                            }
                            else
                            {
                                return 0;
                            }
                        }
                    }
                    break;
                default:
                    return 0;
                    break;
            }
        }

    }
}

int main(int argc, char *argv[])
{
    int sockfd, newsockfd, portno;
    socklen_t clilen;
    char buffer[BUFLEN];
    struct sockaddr_in serv_addr, cli_addr;
    int n, i;
    map<string, user_info> user_infos;
    map<int, client_info> client_infos;
    ifstream users_config_file;
    ifstream shares_config_file;
    vector<int> sending_to_clients;
    vector<int> receiving_from_clients;

    if (argc < 4)
    {
        fprintf(stderr,"Usage : %s <port_server> <users_config_file> <static_shares_config_file>\n", argv[0]);
        exit(1);
    }

    users_config_file.open(argv[2]); // <users_config_file>;
    if(!users_config_file.good())
    {
        error("Error on opening users config file");
    }

    string number;
    getline(users_config_file, number); //get the number of users
    int user_count = stoi(number);
    for(int i = 0; i < user_count; i++)
  	{
        string id, password;
        getline(users_config_file, id,' ');
        getline(users_config_file, password);
        user_infos[id] = user_info(password);
        mkdir(id.c_str(), 0777);
        DIR *dir;
        struct dirent *ent;
        if ((dir = opendir (id.c_str())) != NULL)
        {
          while ((ent = readdir (dir)) != NULL)
          {
            string filename(ent->d_name);
            if(filename != "." && filename != "..")
            {
                struct stat file_buffer;
                string filepath = id + "/" + filename;
                if(stat(filepath.c_str(), &file_buffer) == 0)
                {
                    user_infos[id].file_infos[filename] = file_info(file_buffer.st_size,false);
                }
            }
          }
          closedir (dir);
        }

  	}

  	shares_config_file.open(argv[3]); // <static_shares_config_file>
  	if(!shares_config_file.good())
    {
        error("Error on opening shares config file");
    }

    getline(shares_config_file, number);
    int file_count = stoi(number);
    for(int i = 0; i < file_count; i++)
  	{
        string user, filename;
        getline(shares_config_file, user,':');
        getline(shares_config_file, filename);
        if(user_infos.find(user) != user_infos.end())
        {
            struct stat file_buffer;
            string filepath = user + "/" + filename;
            if(stat(filepath.c_str(), &file_buffer) != 0)
            {
                cout << "Error: File " + filepath + " not found or is not accesible. Line \"" + user + ":" + filename + "\" ignored. (in file " << argv[3] << ")" << '\n';
            }
            else
            {
                user_infos[user].file_infos[filename].is_shared = true;
            }
        }
        else
        {
            cout << "Error: User " + user + " not found. Line \"" + user + ":" + filename + "\" ignored. (in file " << argv[3] << ")" << '\n';
        }

  	}


    fd_set read_fds;	//multimea de citire folosita in select()
    fd_set tmp_fds;	   //multime folosita temporar
    int fdmax;		  //valoare maxima file descriptor din multimea read_fds

     //golim multimea de descriptori de citire (read_fds) si multimea tmp_fds
    FD_ZERO(&read_fds);
    FD_ZERO(&tmp_fds);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");

    portno = atoi(argv[1]);

    memset((char *) &serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;	// foloseste adresa IP a masinii
    serv_addr.sin_port = htons(portno);

    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(struct sockaddr)) < 0)
     	error("ERROR on binding");

    FD_SET(0, &read_fds);

    listen(sockfd, MAX_CLIENTS);

     //adaugam noul file descriptor (socketul pe care se asculta conexiuni) in multimea read_fds
    FD_SET(sockfd, &read_fds);
    fdmax = sockfd;

    string unlogged_user = "$";
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    int wants_to_quit = 0;
     // main loop
    while (1)
    {
		tmp_fds = read_fds;
		if (select(fdmax + 1, &tmp_fds, NULL, NULL, &tv) == -1)
			error("ERROR in select");

		if (FD_ISSET(0, &tmp_fds))
        {   //citesc de la tastatura
	    	memset(buffer, 0 , BUFLEN);
            string command;
            getline(cin, command);
            if(command == "quit")
            {
                wants_to_quit = 1;
                for(i = 1; i <= fdmax; i++)
                {
                    if (FD_ISSET(i, &read_fds) && i != sockfd)
                    {
                        int quit_command_number = 10, current_buffer_place = 0;
                        memset(buffer, 0, BUFLEN);
                        copy_to_buffer(buffer, quit_command_number, current_buffer_place);
                        send(i, buffer, current_buffer_place, 0);
                    }
                }
            }
        }

		for(i = 1; i <= fdmax; i++)
		{
			if (FD_ISSET(i, &tmp_fds))
			{
				if (i == sockfd && wants_to_quit != 1)
				{
					// a venit ceva pe socketul inactiv(cel cu listen) = o noua conexiune
					// actiunea serverului: accept()
					clilen = sizeof(cli_addr);
					if ((newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen)) == -1)
					{
						error("ERROR in accept");
					}
					else
					{
						//adaug noul socket intors de accept() la multimea descriptorilor de citire
						FD_SET(newsockfd, &read_fds);
						if (newsockfd > fdmax)
						{
							fdmax = newsockfd;
						}
						client_infos[newsockfd] = client_info(unlogged_user, 0);

					}
				}
				else
				{
					// am primit date pe unul din socketii cu care vorbesc cu clientii
					//actiunea serverului: recv()
					memset(buffer, 0, BUFLEN);
					if ((n = recv(i, buffer, sizeof(buffer), 0)) <= 0)
					{
						if (n == 0)
						{
							//conexiunea s-a inchis
							//printf("server: socket %d hung up\n", i);
						}
						else //recv intoarce < 0
						{
							error("ERROR in recv");
						}
						close(i);
						FD_CLR(i, &read_fds); // scoatem din multimea de citire socketul pe care
					}
					else
					{ //recv intoarce >0
                        int are_commands_left_in_buffer = 1;
                        while(n != 0 || are_commands_left_in_buffer)
                        {
                            int command_number = 0, frame_size = 0, current_buffer_place = 0, parse_command = 0;
                            int size_left_in_buffer = BUFLEN + BUFLEN - client_infos[i].buffer_end_pos;
                            int temp_size = n >= size_left_in_buffer ? size_left_in_buffer : n;
                            memcpy(client_infos[i].buffer + client_infos[i].buffer_end_pos, buffer, temp_size);
                            n -= temp_size;
                            client_infos[i].buffer_end_pos += temp_size;
                            int end_pos = 0;
                            if(is_frame_complete(client_infos[i].buffer, client_infos[i].buffer_end_pos, client_infos[i].blocks_left_out, client_infos[i].last_block_size, end_pos) == 1)
                            {
                                parse_command = 1;
                            }
                            else
                            {
                                break;
                            }

                            if(wants_to_quit == 1)
                            {
                                int temp_place = 0;
                                int command_number;
                                copy_from_buffer(client_infos[i].buffer, command_number, temp_place);
                                if(command_number != 0)
                                {
                                    parse_command = 0;
                                    client_infos[i].current_buffer_place = end_pos;
                                }
                            }

                            if(parse_command == 1)
                            {
                                copy_from_buffer(client_infos[i].buffer, command_number, client_infos[i].current_buffer_place);
                                switch(command_number)
                                {
                                    case 1: //login
                                    {
                                        string user, password;
                                        copy_from_buffer(client_infos[i].buffer, frame_size, client_infos[i].current_buffer_place);
                                        copy_from_buffer(client_infos[i].buffer, user, client_infos[i].current_buffer_place);
                                        copy_from_buffer(client_infos[i].buffer, password, client_infos[i].current_buffer_place);

                                        int correct = 0;
                                        if(user_infos.find(user) != user_infos.end())
                                        {
                                            if(user_infos[user].password == password)
                                            {
                                                correct = 1;
                                            }
                                        }

                                        memset(buffer, 0, BUFLEN);
                                        if(client_infos[i].user != unlogged_user)
                                        {
                                            int error_number = -2; //client already auth
                                            current_buffer_place = 0;
                                            copy_to_buffer(buffer, error_number, current_buffer_place);
                                            send(i, buffer, current_buffer_place, 0);
                                        }
                                        else
                                        {
                                            if(correct == 1)
                                            {
                                                current_buffer_place = 0;
                                                copy_to_buffer(buffer, command_number, current_buffer_place);
                                                send(i, buffer, current_buffer_place, 0);
                                                client_infos[i].user = user;
                                            }
                                            else
                                            {
                                                client_infos[i].login_attempts++;
                                                int error_number, bruteforce_close = 0;
                                                if(client_infos[i].login_attempts >= 3)
                                                {
                                                    error_number = -8; //brute-force error
                                                    bruteforce_close = 1;
                                                }
                                                else
                                                {
                                                    error_number = -3; //wrong login
                                                }
                                                current_buffer_place = 0;
                                                copy_to_buffer(buffer, error_number, current_buffer_place);
                                                send(i, buffer, current_buffer_place, 0);
                                                if(bruteforce_close == 1)
                                                {
                                                    close(i);
                                                    FD_CLR(i, &read_fds);
                                                }
                                            }
                                        }
                                        break;
                                    }
                                    case 2: //logout
                                    {
                                        int command_number_or_error;
                                        if(client_infos[i].user == unlogged_user)
                                        {
                                            command_number_or_error = -1; //error, unauthenticated client
                                        }
                                        else
                                        {
                                            command_number_or_error = 2; //command number
                                            client_infos[i].user = unlogged_user;
                                        }
                                        memset(buffer, 0, BUFLEN);
                                        current_buffer_place = 0;
                                        copy_to_buffer(buffer, command_number_or_error, current_buffer_place);
                                        send(i, buffer, current_buffer_place, 0);
                                        break;
                                    }
                                    case 3: //getuserlist
                                    {
                                        int command_number_or_error;
                                        memset(buffer, 0, BUFLEN);
                                        current_buffer_place = 0;
                                        if(client_infos[i].user == unlogged_user)
                                        {
                                            command_number_or_error = -1; //error, unauthenticated client
                                            copy_to_buffer(buffer, command_number_or_error, current_buffer_place);
                                        }
                                        else
                                        {
                                            command_number_or_error = 3; //command number
                                            string user_list;
                                            copy_to_buffer(buffer, command_number_or_error, current_buffer_place);
                                            for(const auto &kv : user_infos)
                                            {
                                                user_list += kv.first + "\n";
                                            }
                                            copy_to_buffer(buffer, user_list, current_buffer_place);
                                        }
                                        send(i, buffer, current_buffer_place, 0);
                                        break;
                                    }
                                    case 4: //getfilelist
                                    {
                                        string user;
                                        copy_from_buffer(client_infos[i].buffer, user, client_infos[i].current_buffer_place);

                                        int command_number_or_error;
                                        memset(buffer, 0, BUFLEN);
                                        current_buffer_place = 0;
                                        if(client_infos[i].user == unlogged_user)
                                        {
                                            command_number_or_error = -1; //error, unauthenticated client
                                            copy_to_buffer(buffer, command_number_or_error, current_buffer_place);
                                        }
                                        else
                                        {
                                            if(user_infos.find(user) != user_infos.end())
                                            {
                                                command_number_or_error = 4; //command number
                                                string file_list;
                                                copy_to_buffer(buffer, command_number_or_error, current_buffer_place);
                                                for(const auto &kv : user_infos[user].file_infos)
                                                {
                                                    string numWithDots = to_string(kv.second.file_size);
                                                    int insertPosition = numWithDots.length() - 3;
                                                    while (insertPosition > 0)
                                                    {
                                                        numWithDots.insert(insertPosition, ".");
                                                        insertPosition-=3;
                                                    }
                                                    file_list += kv.first + "  " + numWithDots + " bytes  " + (kv.second.is_shared? "SHARED":"PRIVATE") + '\n';
                                                }
                                                copy_to_buffer(buffer, file_list, current_buffer_place);
                                            }
                                            else
                                            {
                                                command_number_or_error = -11; //error, user not found
                                                copy_to_buffer(buffer, command_number_or_error, current_buffer_place);
                                            }
                                        }
                                        send(i, buffer, current_buffer_place, 0);
                                        break;
                                    }
                                    case 7: //share
                                    {
                                        string filename;
                                        copy_from_buffer(client_infos[i].buffer, filename, client_infos[i].current_buffer_place);
                                        string user(client_infos[i].user);

                                        int command_number_or_error;
                                        memset(buffer, 0, BUFLEN);
                                        current_buffer_place = 0;
                                        if(client_infos[i].user == unlogged_user)
                                        {
                                            command_number_or_error = -1; //error, unauthenticated client
                                            copy_to_buffer(buffer, command_number_or_error, current_buffer_place);
                                        }
                                        else
                                        {
                                            if(user_infos[user].file_infos.find(filename) != user_infos[user].file_infos.end())
                                            {
                                                if(user_infos[user].file_infos[filename].is_shared)
                                                {
                                                    command_number_or_error = -6; //error, file already shared
                                                    copy_to_buffer(buffer, command_number_or_error, current_buffer_place);
                                                }
                                                else
                                                {
                                                    command_number_or_error = 7; //command number
                                                    user_infos[user].file_infos[filename].is_shared = true;
                                                    copy_to_buffer(buffer, command_number_or_error, current_buffer_place);
                                                }
                                            }
                                            else
                                            {
                                                command_number_or_error = -4; //error, file not found
                                                copy_to_buffer(buffer, command_number_or_error, current_buffer_place);
                                            }

                                        }
                                        send(i, buffer, current_buffer_place, 0);
                                        break;
                                    }
                                    case 8: //unshare
                                    {
                                        string filename;
                                        copy_from_buffer(client_infos[i].buffer, filename, client_infos[i].current_buffer_place);
                                        string user(client_infos[i].user);

                                        int command_number_or_error;
                                        memset(buffer, 0, BUFLEN);
                                        current_buffer_place = 0;
                                        if(client_infos[i].user == unlogged_user)
                                        {
                                            command_number_or_error = -1; //error, unauthenticated client
                                            copy_to_buffer(buffer, command_number_or_error, current_buffer_place);
                                        }
                                        else
                                        {
                                            if(user_infos[user].file_infos.find(filename) != user_infos[user].file_infos.end())
                                            {
                                                if(!user_infos[user].file_infos[filename].is_shared)
                                                {
                                                    command_number_or_error = -7; //error, file already private
                                                    copy_to_buffer(buffer, command_number_or_error, current_buffer_place);
                                                }
                                                else
                                                {
                                                    command_number_or_error = 8; //command number
                                                    user_infos[user].file_infos[filename].is_shared = false;
                                                    copy_to_buffer(buffer, command_number_or_error, current_buffer_place);
                                                }
                                            }
                                            else
                                            {
                                                command_number_or_error = -4; //error, file not found
                                                copy_to_buffer(buffer, command_number_or_error, current_buffer_place);
                                            }

                                        }
                                        send(i, buffer, current_buffer_place, 0);
                                        break;
                                    }
                                    case 9: //delete
                                    {
                                        string filename;
                                        copy_from_buffer(client_infos[i].buffer, filename, client_infos[i].current_buffer_place);
                                        string user(client_infos[i].user);

                                        int command_number_or_error;
                                        memset(buffer, 0, BUFLEN);
                                        current_buffer_place = 0;
                                        if(client_infos[i].user == unlogged_user)
                                        {
                                            command_number_or_error = -1; //error, unauthenticated client
                                            copy_to_buffer(buffer, command_number_or_error, current_buffer_place);
                                        }
                                        else
                                        {
                                            if(user_infos[user].file_infos.find(filename) != user_infos[user].file_infos.end())
                                            {
                                                if(user_infos[user].file_infos[filename].in_use != 0)
                                                {
                                                    command_number_or_error = -10; //error, file in use
                                                    copy_to_buffer(buffer, command_number_or_error, current_buffer_place);
                                                }
                                                else
                                                {
                                                    command_number_or_error = 9; //command number
                                                    string filepath(user + "/" + filename);
                                                    unlink(filepath.c_str());
                                                    user_infos[user].file_infos.erase(filename);
                                                    copy_to_buffer(buffer, command_number_or_error, current_buffer_place);
                                                }
                                            }
                                            else
                                            {
                                                command_number_or_error = -4; //error, file not found
                                                copy_to_buffer(buffer, command_number_or_error, current_buffer_place);
                                            }

                                        }
                                        send(i, buffer, current_buffer_place, 0);
                                        break;
                                    }
                                    case 5: //upload
                                    {
                                        string filename;
                                        copy_from_buffer(client_infos[i].buffer, filename, client_infos[i].current_buffer_place);
                                        string user(client_infos[i].user);
                                        long nr_of_blocks;
                                        copy_from_buffer(client_infos[i].buffer, nr_of_blocks, client_infos[i].current_buffer_place);

                                        int command_number_or_error;
                                        memset(buffer, 0, BUFLEN);
                                        current_buffer_place = 0;
                                        if(client_infos[i].user == unlogged_user)
                                        {
                                            command_number_or_error = -1; //error, unauthenticated client
                                            copy_to_buffer(buffer, command_number_or_error, current_buffer_place);
                                        }
                                        else
                                        {
                                            struct stat file_buffer;
                                            string filepath(user + "/" + filename);
                                            if(user_infos[user].file_infos.find(filename) == user_infos[user].file_infos.end() && stat(filepath.c_str(), &file_buffer) != 0)
                                            {

                                                command_number_or_error = 5; //command number
                                                client_infos[i].out_file = new ofstream(user + "/" + filename);
                                                client_infos[i].out_file_name = filename;
                                                client_infos[i].blocks_left_out = nr_of_blocks + 1;
                                                long temp = -1;
                                                copy_to_buffer(buffer, command_number_or_error, current_buffer_place);
                                                copy_to_buffer(buffer, temp, current_buffer_place);
                                                receiving_from_clients.push_back(i);
                                            }
                                            else
                                            {
                                                command_number_or_error = -9; //error, file already on server
                                                copy_to_buffer(buffer, command_number_or_error, current_buffer_place);
                                            }

                                        }
                                        send(i, buffer, current_buffer_place, 0);
                                        break;
                                    }
                                    case 0: // file block received
                                    {
                                        if(client_infos[i].blocks_left_out != 0)
                                        {
                                            client_infos[i].blocks_left_out--;
                                            int buffer_size;
                                            if(client_infos[i].blocks_left_out == 1)
                                            {
                                                int temp;
                                                copy_from_buffer(client_infos[i].buffer, temp, client_infos[i].current_buffer_place);
                                                client_infos[i].last_block_size = temp;
                                            }
                                            else
                                            {
                                                if(client_infos[i].blocks_left_out == 0)
                                                {
                                                    buffer_size = client_infos[i].last_block_size;
                                                }
                                                else
                                                {
                                                    buffer_size = BUFLEN - 4;
                                                }
                                                (*client_infos[i].out_file).write(client_infos[i].buffer + 4,buffer_size);
                                                client_infos[i].current_buffer_place += buffer_size;

                                                if(client_infos[i].blocks_left_out == 0)
                                                {
                                                    long pos = (*client_infos[i].out_file).tellp();
                                                    (*client_infos[i].out_file).close();

                                                    memset(buffer, 0, BUFLEN);
                                                    current_buffer_place = 0;
                                                    int command_number = 5;
                                                    copy_to_buffer(buffer, command_number, current_buffer_place);
                                                    copy_to_buffer(buffer, pos, current_buffer_place);
                                                    send(i, buffer, current_buffer_place, 0);
                                                    user_infos[client_infos[i].user].file_infos[client_infos[i].out_file_name] = file_info(pos, false);
                                                    vector<int>::iterator position = find(receiving_from_clients.begin(), receiving_from_clients.end(), i);
                                                    if (position != receiving_from_clients.end())
                                                        receiving_from_clients.erase(position);
                                                }
                                            }
                                        }
                                        break;
                                    }
                                    case 6:
                                    {
                                        string user, filename;
                                        copy_from_buffer(client_infos[i].buffer, frame_size, client_infos[i].current_buffer_place);
                                        copy_from_buffer(client_infos[i].buffer, user, client_infos[i].current_buffer_place);
                                        copy_from_buffer(client_infos[i].buffer, filename, client_infos[i].current_buffer_place);
                                        if(user == "@")
                                        {
                                            user = client_infos[i].user;
                                        }

                                        int command_number_or_error;
                                        memset(buffer, 0, BUFLEN);
                                        current_buffer_place = 0;
                                        if(client_infos[i].user == unlogged_user)
                                        {
                                            command_number_or_error = -1; //error, unauthenticated client
                                            copy_to_buffer(buffer, command_number_or_error, current_buffer_place);
                                        }
                                        else
                                        {
                                            if(user_infos[user].file_infos.find(filename) != user_infos[user].file_infos.end())
                                            {
                                                if(user != client_infos[i].user && !user_infos[user].file_infos[filename].is_shared)
                                                {
                                                    command_number_or_error = -5; //error, permission denied
                                                    copy_to_buffer(buffer, command_number_or_error, current_buffer_place);
                                                }
                                                else
                                                {
                                                    command_number_or_error = 6; //command number
                                                    struct stat file_buffer;
                                                    string filepath(user + "/" + filename);
                                                    int n = stat(filepath.c_str(), &file_buffer);
                                                    client_infos[i].in_file = new ifstream(filepath);
                                                    client_infos[i].in_file_name = filename;
                                                    long file_size = file_buffer.st_size;
                                                    int sizeof_block = BUFLEN - sizeof(int);
                                                    long nr_of_blocks = file_size % sizeof_block == 0 ? file_size / sizeof_block : file_size / sizeof_block + 1;
                                                    client_infos[i].blocks_left_in = nr_of_blocks;
                                                    copy_to_buffer(buffer, command_number_or_error, current_buffer_place);
                                                    copy_to_buffer(buffer, nr_of_blocks, current_buffer_place);
                                                    sending_to_clients.push_back(i);
                                                    user_infos[user].file_infos[filename].in_use++;
                                                    client_infos[i].user_downloading_from = user;
                                                }
                                            }
                                            else
                                            {
                                                command_number_or_error = -4; //error, file not on server
                                                copy_to_buffer(buffer, command_number_or_error, current_buffer_place);
                                            }
                                        }
                                        send(i, buffer, current_buffer_place, 0);
                                        break;
                                    }
                                    default:
                                        break;
                                }
                            }

                            char temp_buffer[BUFLEN + BUFLEN];
                            int size_to_copy = client_infos[i].buffer_end_pos - client_infos[i].current_buffer_place;
                            if(size_to_copy == 0)
                            {
                                memset(client_infos[i].buffer, 0, BUFLEN);
                                are_commands_left_in_buffer = 0;
                                client_infos[i].buffer_end_pos = 0;
                                client_infos[i].current_buffer_place = 0;
                            }
                            else
                            {
                                memcpy(temp_buffer, client_infos[i].buffer + client_infos[i].current_buffer_place, size_to_copy);
                                memset(client_infos[i].buffer, 0, BUFLEN + BUFLEN);
                                memcpy(client_infos[i].buffer, temp_buffer, size_to_copy);
                                client_infos[i].buffer_end_pos = size_to_copy;
                                client_infos[i].current_buffer_place = 0;
                                int end_pos = 0;
                                if(is_frame_complete(client_infos[i].buffer, client_infos[i].buffer_end_pos, client_infos[i].blocks_left_out, client_infos[i].last_block_size, end_pos) == 1)
                                {
                                    are_commands_left_in_buffer = 1;
                                }
                                else
                                {
                                    are_commands_left_in_buffer = 0;
                                }
                            }
                        }
					}
				}
			}
		}

        int sending_to_clients_size = sending_to_clients.size();
        for (int j = 0; j < sending_to_clients_size; j++)
        {
            int x = sending_to_clients.at(j);
            client_infos[x].blocks_left_in--;
            memset(buffer, 0 , BUFLEN);
            int buffer_size = 0;
            int command_number = 0;
            copy_to_buffer(buffer, command_number, buffer_size);
            int sizeof_int = sizeof(int);
            (*client_infos[x].in_file).read(buffer + 4,BUFLEN - sizeof_int);
            if(client_infos[x].blocks_left_in == 0)
            {
                int lastblock_size = (*client_infos[x].in_file).gcount(), temp_buffer_place = 0;
                (*client_infos[x].in_file).close();
                user_infos[client_infos[x].user_downloading_from].file_infos[client_infos[x].in_file_name].in_use--;
                sending_to_clients.erase(sending_to_clients.begin() + j);
                j--;
                sending_to_clients_size = sending_to_clients.size();
                char temp_buffer[8];
                copy_to_buffer(temp_buffer, command_number, temp_buffer_place);
                copy_to_buffer(temp_buffer, lastblock_size, temp_buffer_place);
                n = send(x,temp_buffer,temp_buffer_place, 0);
                if (n < 0)
                {
                    error("ERROR writing to socket");
                }
                buffer_size += lastblock_size;
            }
            else
            {
                buffer_size = BUFLEN;
            }
            n = send(x,buffer,buffer_size, 0);
            if(client_infos[x].blocks_left_in == 0)
            {
                char temp_buffer[12];
                long temp = -1;
                int command_number = 6, temp_buffer_place = 0;
                copy_to_buffer(temp_buffer, command_number, temp_buffer_place);
                copy_to_buffer(temp_buffer, temp, temp_buffer_place);
                n = send(x,temp_buffer,temp_buffer_place, 0);
            }

            if (n < 0)
            {
                error("ERROR writing to socket");
            }
        }

        if(wants_to_quit == 1 && sending_to_clients.size() == 0 && receiving_from_clients.size() == 0)
        {
            cout << "Quitting.\n";
            fflush(stdout);
            for(i = 1; i <= fdmax; i++)
            {
                if (FD_ISSET(i, &read_fds))
                {
                    close(i);
                }
            }
            return 0;
        }

    }


    close(sockfd);

    return 0;
}
