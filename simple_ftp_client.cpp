/*
 *  simple_ftp_client.cpp
 *  simple_ftp
 *
 *  Created by Viatcheslav Gachkaylo on 10/12/10.
 *  Copyright 2010 Get2freegames. All rights reserved.
 *
 */

#include "simple_ftp_client.h"
#include <iostream>
#include <fstream>
#include <boost/array.hpp>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>

using boost::asio::ip::tcp;
void sendFile(tcp::socket &socket, const std::string &fname);

int main(int argc, char* argv[])
{
	try
	{
		if (argc != 2)
		{
			std::cerr << "Usage: client <host>" << std::endl;
			return 1;
		}
		
		boost::asio::io_service io_service;
		
		tcp::resolver resolver(io_service);
		tcp::resolver::query query(argv[1], "40000");
		tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
		tcp::resolver::iterator end;
		
		tcp::socket socket(io_service);
		boost::system::error_code error = boost::asio::error::host_not_found;
		while (error && endpoint_iterator != end)
		{
			socket.close();
			socket.connect(*endpoint_iterator++, error);
		}
		if (error)
			throw boost::system::system_error(error);
		
		for (;;)
		{
			boost::array<char, 128> buf;
			boost::system::error_code error;
			
			std::string cmd;
			std::getline(std::cin, cmd);
			cmd += '\n';
			
			size_t len1 = socket.write_some(boost::asio::buffer(cmd), error);
			if (error)
				throw boost::system::system_error(error);
			
			size_t len = socket.read_some(boost::asio::buffer(buf), error);
			
			if (error == boost::asio::error::eof)
				break; // Connection closed cleanly by peer.
			else if (error)
				throw boost::system::system_error(error); // Some other error.
			
			std::cout.write(buf.data(), len);
			std::cout << std::endl;
			
			std::string res;
			for (int i = 0; i < len; ++i) {
				res += buf[i];
			}
			
			typedef std::vector<std::string> split_vector_type;
			
			split_vector_type splitVec;
			boost::algorithm::split(splitVec, res, boost::is_any_of(" "));
			
			if (splitVec.size() && splitVec[0].compare("OK") == 0) {
				if (splitVec.size() == 2) {
					tcp::socket dataSock(io_service);
					
					tcp::socket::endpoint_type ep = socket.remote_endpoint();
					std::cout << splitVec[1] << std::endl;
					ep.port(atoi(splitVec[1].c_str()));
					
					dataSock.connect(ep, error);
					if (error)
						throw boost::system::system_error(error);
					
					boost::algorithm::trim_if(cmd, boost::is_any_of("\n "));
					boost::algorithm::split(splitVec, cmd, boost::is_any_of(" "));
					if (splitVec.size()) {
						std::string op = splitVec[0];
						
						if (op.compare("LIST") == 0) {
							while (1) {
								len = dataSock.read_some(boost::asio::buffer(buf), error);
								if (error == boost::asio::error::eof)
									break; // Connection closed cleanly by peer.
								else if (error)
									throw boost::system::system_error(error); // Some other error.
								
								std::cout.write(buf.data(), len);
							}
							std::cout << std::endl;
						} else if (op.compare("PUT") == 0 && splitVec.size() > 1) {
							std::string fname = splitVec[1];
							sendFile(dataSock, fname);
						}
					}
				}
			}
		}
	}
	catch (std::exception& e)
	{
		std::cerr << e.what() << std::endl;
	}
	
	return 0;
}

void sendFile(tcp::socket &socket, const std::string &fname) 
{
	boost::array<char, 1024> buf;
	boost::system::error_code error;
	std::ifstream source_file(fname.c_str(), std::ios_base::binary | std::ios_base::ate);
	if (!source_file)
	{
		throw std::runtime_error("Failed to open required file");
	}
	size_t file_size = source_file.tellg();
	source_file.seekg(0);
	// first send file name and file size to server
	boost::asio::streambuf request;
	std::ostream request_stream(&request);
	request_stream << fname << "\n"
	<< file_size << "\n\n";
	boost::asio::write(socket, request);
	std::cout << "start sending file content.\n";
	for (;;)
	{
		
		if (source_file.eof()==false)
		{
			source_file.read(buf.c_array(), (std::streamsize)buf.size());
			if (source_file.gcount()<=0)
			{
				throw std::runtime_error("read file error");
			}
			boost::asio::write(socket, boost::asio::buffer(buf.c_array(), 
														   source_file.gcount()),
							   boost::asio::transfer_all(), error);
			if (error)
			{
				throw std::runtime_error("can not send");
			}
		}
		else
			break;
	}
	std::cout << "send file " << fname << " completed successfully.\n";
}
