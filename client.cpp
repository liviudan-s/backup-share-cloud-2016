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

#define BUFLEN 4096

using namespace std;

enum commands
{
    login,
    logout,
    getuserlist,
    getfilelist,
    upload,
    download,
    share,
    unshare_file,
    delete_file,
    quit,
    undefined_command
};

commands get_command(string command)
{
    if(command == "login") return login;
    if(command == "logout") return logout;
    if(command == "getuserlist") return getuserlist;
    if(command == "getfilelist") return getfilelist;
    if(command == "upload") return upload;
    if(command == "download") return download;
    if(command == "share") return share;
    if(command == "unshare") return unshare_file;
    if(command == "delete") return delete_file;
    if(command == "quit") return quit;
    return undefined_command;
}

void output_with_logging(string output, string& command_log)
{
    cout << output;
    fflush(stdout);
    command_log += output;
}

void error(int error_number, string& command_log)
{
    switch(error_number)
    {
        case -1:
            output_with_logging("-1 : Clientul nu e autentificat\n", command_log);
            break;
        case -2:
            output_with_logging("-2 : Sesiune deja deschisa\n", command_log);
            break;
        case -3:
            output_with_logging("-3 : User/parola gresita\n", command_log);
            break;
        case -4:
            output_with_logging("-4 : Fisier inexistent\n", command_log);
            break;
        case -5:
            output_with_logging("-5 : Descarcare interzisa\n", command_log);
            break;
        case -6:
            output_with_logging("-6 : Fisier deja partajat\n", command_log);
            break;
        case -7:
            output_with_logging("-7 : Fisier deja privat\n", command_log);
            break;
        case -8:
            output_with_logging("-8 : Brute-force detectat\n", command_log);
            break;
        case -9:
            output_with_logging("-9 : Fiser existent pe server\n", command_log);
            break;
        case -10:
            output_with_logging("-10 : Fisier in transfer\n", command_log);
            break;
        case -11:
            output_with_logging("-11 : Utilizator inexistent\n", command_log);
            break;
        case -99:
            output_with_logging("Unknown command\n", command_log);
            break;
        default:
            output_with_logging(to_string(error_number) + " : Unknow error number\n", command_log);
            break;
    }
}

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

int is_frame_complete(char buffer[], int buffer_end_pos, int blocks_left_out, int last_block_size)
{
    if(buffer_end_pos < 4)
    {
        return 0;
    }
    buffer_end_pos -= 4;

    int command_number,current_place_in_buffer = 0;
    copy_from_buffer(buffer, command_number, current_place_in_buffer);

    if(command_number == 3 || command_number == 4)
    {
        if(buffer_end_pos < 4)
            return 0;
        else
        {
          int frame_size;
          copy_from_buffer(buffer, frame_size, current_place_in_buffer);
          buffer_end_pos -= 4;
          if(buffer_end_pos >= frame_size)
          {
            return 1;
          }
          else
            return 0;
        }
    }
    else
    {
        if(command_number == 5 || command_number == 6)
        {
            int sizeof_long = sizeof(long);
            if(buffer_end_pos >= sizeof_long)
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
            switch(command_number)
            {
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
                    return 1;
                    break;
            }
        }
    }
}

int main(int argc, char *argv[])
{
    int sockfd, n, sizeof_block = BUFLEN - sizeof(int);;
    struct sockaddr_in serv_addr;
    fd_set read_fds;	//multimea de citire folosita in select()
    fd_set tmp_fds;		//multime folosita temporar

    char buffer[BUFLEN];
    if (argc < 3) {
       fprintf(stderr,"Usage %s server_address server_port\n", argv[0]);
       exit(0);
    }

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(argv[2]));
    inet_aton(argv[1], &serv_addr.sin_addr);


    if (connect(sockfd,(struct sockaddr*) &serv_addr,sizeof(serv_addr)) < 0)
        error("ERROR connecting");
    //golim multimea de descriptori de citire (read_fds) si multimea tmp_fds
    FD_ZERO(&read_fds);
    FD_ZERO(&tmp_fds);

    FD_SET(0, &read_fds);
    FD_SET(sockfd, &read_fds);


    int id = getpid();
    string logger_filename = "client-" + to_string(id) + ".log";
    ofstream logger(logger_filename);

    string unlogged_user = "$", current_user = unlogged_user, user_attempt = "", last_file_queried = "", current_command_log = "", file_to_upload, file_to_download;
    char persistent_buffer[BUFLEN + BUFLEN];
    int buffer_end_pos = 0;
    int current_buffer_place = 0;
    int last_block_size;
    long nr_of_blocks_last_upload;
    int print_prompt;
    output_with_logging(current_user + "> ", current_command_log);
    print_prompt = 0;
    ifstream in_file;
    ofstream out_file;
    long blocks_left_in = 0;
    long blocks_left_out = 0;
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    int wants_to_quit = 0;
    while(1){
    	tmp_fds = read_fds;
  		if (select(sockfd + 1, &tmp_fds, NULL, NULL, &tv) == -1)
			error("ERROR in select");

		if(FD_ISSET(sockfd, &tmp_fds))
		{
			memset(buffer, 0, BUFLEN);
			int n = recv(sockfd, buffer, sizeof(buffer), 0);
			if(n == 0)
			{
                exit(0);
			}

            int are_commands_left_in_buffer = 1;
            while(n != 0 || are_commands_left_in_buffer)
            {
                int parse_command = 0;
                int size_left_in_buffer = BUFLEN + BUFLEN - buffer_end_pos;
                int temp_size = n >= size_left_in_buffer ? size_left_in_buffer : n;
                memcpy(persistent_buffer + buffer_end_pos, buffer, temp_size);
                n -= temp_size;
                buffer_end_pos += temp_size;
                if(is_frame_complete(persistent_buffer, buffer_end_pos, blocks_left_out, last_block_size) == 1)
                {
                    parse_command = 1;
                }
                else
                {
                    break;
                }

                if(parse_command == 1)
                {
                    int command_number, is_filechunk = 0;
                    copy_from_buffer(persistent_buffer, command_number, current_buffer_place);
                    switch(command_number)
                    {
                        case 1:
                            current_user = user_attempt;
                            break;
                        case 2:
                            current_user = unlogged_user;
                            break;
                        case 3:
                        {
                            string user_list;
                            copy_from_buffer(persistent_buffer, user_list, current_buffer_place);
                            output_with_logging(user_list, current_command_log);
                            break;
                        }
                        case 4:
                        {
                            string file_list;
                            copy_from_buffer(persistent_buffer, file_list, current_buffer_place);
                            output_with_logging(file_list, current_command_log);
                            break;
                        }
                        case 7:
                            output_with_logging("200 Fisierul " + last_file_queried + " a fost partajat.\n", current_command_log);
                            break;
                        case 8:
                            output_with_logging("200 Fisierul a fost setat ca PRIVATE\n", current_command_log);
                            break;
                        case 9:
                            output_with_logging("200 Fisier sters\n", current_command_log);
                            break;
                        case 5:
                        {
                            long file_size;
                            copy_from_buffer(persistent_buffer, file_size, current_buffer_place);
                            if(file_size == -1)
                            {
                                in_file.open(file_to_upload, ios::in | ios::binary);
                                blocks_left_in = nr_of_blocks_last_upload;
                            }
                            else
                            {
                                string numWithDots = to_string(file_size);
                                int insertPosition = numWithDots.length() - 3;
                                while (insertPosition > 0)
                                {
                                    numWithDots.insert(insertPosition, ".");
                                    insertPosition-=3;
                                }
                                output_with_logging("Upload finished: " + file_to_upload + " - " + numWithDots + " bytes\n", current_command_log);
                            }
                            break;
                        }
                        case 6:
                        {
                            long number_of_blocks;
                            copy_from_buffer(persistent_buffer, number_of_blocks, current_buffer_place);
                            if(number_of_blocks != -1)
                            {
                                out_file.open(to_string(id) + "_" + file_to_download, ios::out | ios::binary);
                                blocks_left_out = number_of_blocks + 1;
                            }
                            else
                            {
                                struct stat file_buffer;
                                string filename(to_string(id) + "_" + file_to_download);
                                int n = stat(filename.c_str(), &file_buffer);
                                long file_size = file_buffer.st_size;
                                string numWithDots = to_string(file_size);
                                int insertPosition = numWithDots.length() - 3;
                                while (insertPosition > 0)
                                {
                                    numWithDots.insert(insertPosition, ".");
                                    insertPosition-=3;
                                }
                                output_with_logging("Download finished: " + file_to_download + " - " + numWithDots + " bytes\n", current_command_log);
                            }
                            break;
                        }
                        case 10: //quit received from server
                            output_with_logging("Server is quitting. Waiting for transfers to finish. Further commands will be ignored.\n", current_command_log);
                            wants_to_quit = 1;
                            break;
                        case 0:
                        {
                            is_filechunk = 1;
                            if(blocks_left_out != 0)
                            {
                                blocks_left_out--;
                                int buffer_size;
                                if(blocks_left_out == 1)
                                {
                                    int temp;
                                    copy_from_buffer(persistent_buffer, temp, current_buffer_place);
                                    last_block_size = temp;
                                }
                                else
                                {
                                    if(blocks_left_out == 0)
                                    {
                                        buffer_size = last_block_size;
                                    }
                                    else
                                    {
                                        buffer_size = BUFLEN - 4;
                                    }
                                    out_file.write(persistent_buffer + 4,buffer_size);
                                    current_buffer_place += buffer_size;

                                    if(blocks_left_out == 0)
                                    {
                                        out_file.close();
                                    }
                                }
                            }
                            break;
                        }
                        default:
                            error(command_number, current_command_log);
                            break;
                    }

                    if(is_filechunk == 0)
                    {
                        logger << current_command_log + "\n";
                        logger.flush();
                        current_command_log = "";
                        print_prompt = 1;
                    }
                }

                char temp_buffer[BUFLEN + BUFLEN];
                int size_to_copy = buffer_end_pos - current_buffer_place;
                if(size_to_copy == 0)
                {
                    memset(persistent_buffer, 0, BUFLEN);
                    are_commands_left_in_buffer = 0;
                    buffer_end_pos = 0;
                    current_buffer_place = 0;
                }
                else
                {
                    memcpy(temp_buffer, persistent_buffer + current_buffer_place, size_to_copy);
                    memset(persistent_buffer, 0, BUFLEN + BUFLEN);
                    memcpy(persistent_buffer, temp_buffer, size_to_copy);
                    buffer_end_pos = size_to_copy;
                    current_buffer_place = 0;
                    if(is_frame_complete(persistent_buffer, buffer_end_pos, blocks_left_out, last_block_size) == 1)
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

		if(FD_ISSET(0, &tmp_fds) && wants_to_quit != 1)
		{ 	//citesc de la tastatura
	    	memset(buffer, 0 , BUFLEN);
            string input, command, param1, param2;
            getline(cin, input);
            current_command_log += input + "\n";
            istringstream iss(input);
            getline(iss, command, ' ');

            int command_number, buffer_size = 0, continue_with_rest = 1, temp_size, temp_size2, frame_size;
			switch(get_command(command))
			{
                case login:
                    if(current_user != unlogged_user)
                    {
                        error(-2, current_command_log);
                        continue_with_rest = 0;
                        print_prompt = 1;
                        logger << current_command_log + "\n";
                        logger.flush();
                        current_command_log = "";
                    }
                    else
                    {
                        command_number = 1;
                        copy_to_buffer(buffer, command_number, buffer_size);
                        getline(iss, param1, ' ');
                        getline(iss, param2);
                        temp_size = param1.length() + 1;
                        temp_size2 = param2.length() + 1;
                        frame_size = temp_size + temp_size2 + sizeof(int) * 2;
                        copy_to_buffer(buffer, frame_size, buffer_size);
                        copy_to_buffer(buffer, param1, buffer_size);
                        copy_to_buffer(buffer, param2, buffer_size);
                        user_attempt = param1;
                    }
                    break;
                case logout:
                    if(current_user == unlogged_user)
                    {
                        error(-1, current_command_log);
                        continue_with_rest = 0;
                        print_prompt = 1;
                        logger << current_command_log + "\n";
                        logger.flush();
                        current_command_log = "";
                    }
                    else
                    {
                        command_number = 2;
                        copy_to_buffer(buffer, command_number, buffer_size);
                    }
                    break;
                case getuserlist:
                    if(current_user == unlogged_user)
                    {
                        error(-1, current_command_log);
                        continue_with_rest = 0;
                        print_prompt = 1;
                        logger << current_command_log + "\n";
                        logger.flush();
                        current_command_log = "";
                    }
                    else
                    {
                        command_number = 3;
                        copy_to_buffer(buffer, command_number, buffer_size);
                    }
                    break;
                case getfilelist:
                    if(current_user == unlogged_user)
                    {
                        error(-1, current_command_log);
                        continue_with_rest = 0;
                        print_prompt = 1;
                        logger << current_command_log + "\n";
                        logger.flush();
                        current_command_log = "";
                    }
                    else
                    {
                        command_number = 4;
                        copy_to_buffer(buffer, command_number, buffer_size);
                        getline(iss, param1);
                        copy_to_buffer(buffer, param1, buffer_size);
                    }
                    break;
                case share:
                    if(current_user == unlogged_user)
                    {
                        error(-1, current_command_log);
                        continue_with_rest = 0;
                        print_prompt = 1;
                        logger << current_command_log + "\n";
                        logger.flush();
                        current_command_log = "";
                    }
                    else
                    {
                        command_number = 7;
                        copy_to_buffer(buffer, command_number, buffer_size);
                        getline(iss, param1);
                        copy_to_buffer(buffer, param1, buffer_size);
                        last_file_queried = param1;
                    }
                    break;
                case unshare_file:
                    if(current_user == unlogged_user)
                    {
                        error(-1, current_command_log);
                        continue_with_rest = 0;
                        print_prompt = 1;
                        logger << current_command_log + "\n";
                        logger.flush();
                        current_command_log = "";
                    }
                    else
                    {
                        command_number = 8;
                        copy_to_buffer(buffer, command_number, buffer_size);
                        getline(iss, param1);
                        copy_to_buffer(buffer, param1, buffer_size);
                        last_file_queried = param1;
                    }
                    break;
                case delete_file:
                    if(current_user == unlogged_user)
                    {
                        error(-1, current_command_log);
                        continue_with_rest = 0;
                        print_prompt = 1;
                        logger << current_command_log + "\n";
                        logger.flush();
                        current_command_log = "";
                    }
                    else
                    {
                        command_number = 9;
                        copy_to_buffer(buffer, command_number, buffer_size);
                        getline(iss, param1);
                        copy_to_buffer(buffer, param1, buffer_size);
                        last_file_queried = param1;
                    }
                    break;
                case upload:
                {
                    if(current_user == unlogged_user)
                    {
                        error(-1, current_command_log);
                        continue_with_rest = 0;
                        print_prompt = 1;
                        logger << current_command_log + "\n";
                        logger.flush();
                        current_command_log = "";
                    }
                    else
                    {
                        getline(iss, param1);
                        struct stat file_buffer;
                        int n = stat(param1.c_str(), &file_buffer);
                        if(n != 0)
                        {
                            error(-4, current_command_log);
                            continue_with_rest = 0;
                            print_prompt = 1;
                            logger << current_command_log + "\n";
                            logger.flush();
                            current_command_log = "";
                        }
                        else
                        {
                            if(blocks_left_in != 0)
                            {
                                error(-10, current_command_log); // a file is already uploading
                                continue_with_rest = 0;
                                print_prompt = 1;
                                logger << current_command_log + "\n";
                                logger.flush();
                                current_command_log = "";
                            }
                            else
                            {
                                command_number = 5;
                                copy_to_buffer(buffer, command_number, buffer_size);
                                copy_to_buffer(buffer, param1, buffer_size);
                                long file_size = file_buffer.st_size;
                                nr_of_blocks_last_upload = file_size % sizeof_block == 0 ? file_size / sizeof_block : file_size / sizeof_block + 1;
                                copy_to_buffer(buffer, nr_of_blocks_last_upload, buffer_size);
                                file_to_upload = param1;
                            }
                        }
                    }
                    break;
                }
                case download:
                    if(current_user == unlogged_user)
                    {
                        error(-1, current_command_log);
                        continue_with_rest = 0;
                        print_prompt = 1;
                        logger << current_command_log + "\n";
                        logger.flush();
                        current_command_log = "";
                    }
                    else
                    {
                        if(blocks_left_out != 0)
                        {
                            error(-10, current_command_log); // a file is already downloading
                            continue_with_rest = 0;
                            print_prompt = 1;
                            logger << current_command_log + "\n";
                            logger.flush();
                            current_command_log = "";
                        }
                        else
                        {
                            command_number = 6;
                            copy_to_buffer(buffer, command_number, buffer_size);
                            getline(iss, param1, ' ');
                            getline(iss, param2);
                            temp_size = param1.length() + 1;
                            temp_size2 = param2.length() + 1;
                            frame_size = temp_size + temp_size2 + sizeof(int) * 2;
                            copy_to_buffer(buffer, frame_size, buffer_size);
                            copy_to_buffer(buffer, param1, buffer_size);
                            copy_to_buffer(buffer, param2, buffer_size);
                            file_to_download = param2;
                        }
                    }
                    break;
                case quit:
                    wants_to_quit = 1;
                    output_with_logging("Waiting for transfers to finish. Further commands will be ignored.\n", current_command_log);
                    continue_with_rest = 0;
                    print_prompt = 1;
                    logger << current_command_log + "\n";
                    logger.flush();
                    current_command_log = "";
                    break;
                default:
                    error(-99, current_command_log);
                    continue_with_rest = 0;
                    print_prompt = 1;
                    logger << current_command_log + "\n";
                    logger.flush();
                    current_command_log = "";
                    break;
			}

            if(continue_with_rest > 0)
            {
                //trimit mesaj la server
                n = send(sockfd,buffer,buffer_size, 0);
                if (n < 0)
                {
                    error("ERROR writing to socket");
                }
            }
		}

        if(wants_to_quit == 1 && blocks_left_in == 0 && blocks_left_out == 0)
        {
            output_with_logging("Quitting.\n", current_command_log);
            logger << current_command_log + "\n";
            logger.flush();
            close(sockfd);
            return 0;
        }

        if(print_prompt)
        {

            output_with_logging(current_user + "> ", current_command_log);
            print_prompt = 0;
        }

        if(blocks_left_in != 0)
        {
            blocks_left_in--;
            memset(buffer, 0 , BUFLEN);
            int buffer_size = 0;
            int command_number = 0;
            copy_to_buffer(buffer, command_number, buffer_size);
            in_file.read(buffer + 4,sizeof_block);
            if(blocks_left_in == 0)
            {
                int lastblock_size = in_file.gcount(), temp_buffer_size = 0;
                in_file.close();
                char temp_buffer[8];
                copy_to_buffer(temp_buffer, command_number, temp_buffer_size);
                copy_to_buffer(temp_buffer, lastblock_size, temp_buffer_size);
                n = send(sockfd,temp_buffer,temp_buffer_size, 0);
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
            n = send(sockfd,buffer,buffer_size, 0);
            if (n < 0)
            {
                error("ERROR writing to socket");
            }
        }

    }
    return 0;
}


