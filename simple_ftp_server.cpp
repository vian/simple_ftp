/*
 *  simple_ftp_server.cpp
 *  simple_ftp
 *
 *  Created by Viatcheslav Gachkaylo on 10/11/10.
 *  Copyright 2010 Get2freegames. All rights reserved.
 *
 */

#include "simple_ftp_server.h"
#include "simple_ftp_common.h"

#include <ctime>
#include <iostream>
#include <string>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/algorithm/string.hpp>

using boost::asio::ip::tcp;
namespace fs = boost::filesystem;

//template <class T> class FtpConnection
//	: public boost::enable_shared_from_this<T>
//{
//public:
//	typedef boost::shared_ptr<T> pointer;
//	static pointer create(boost::asio::io_service &io_service) 
//	{
//		return pointer(new FtpControlConnection(io_service));
//	}
//};

class FtpQuery {
public:
	FtpQuery(const std::string &type)
	: m_type(type)
	{
	}
	
	std::string type() const { return m_type; }
	
private:
	std::string m_type;
};

class FtpServerState {
public:
	static FtpServerState* initInstance(unsigned short port, unsigned short dataPort, const std::string &dataDirectory)
	{
		instance = new FtpServerState(port, dataPort, dataDirectory);
		return instance;
	}
	
	static FtpServerState* Instance()
	{
		return instance;
	}
	
	unsigned short port() const { return m_port; }
	unsigned short dataPort() const { return m_dataPort; }
	const char * dataDirectory() const { return m_dataDirectory.c_str(); }
	const char * workingDirectory() const { return m_workingDirectory.c_str(); }
	void setWorkingDirectory(const char *wdfr) 
	{ 
		m_workingDirectory = m_dataDirectory + wdfr;
		chdir(m_workingDirectory.c_str());
	}
	
private:
	FtpServerState(unsigned short port, unsigned short dataPort, const std::string &dataDirectory)
	:m_port(port), m_dataDirectory(dataDirectory), m_workingDirectory(dataDirectory), m_dataPort(dataPort)
	{
		chdir(m_workingDirectory.c_str());
	}
	
	static FtpServerState *	instance;
	unsigned short		m_port;
	unsigned short		m_dataPort;
	std::string			m_dataDirectory;
	std::string			m_workingDirectory;
};

FtpServerState * FtpServerState::instance = 0;

class FtpDataConnection 
: public boost::enable_shared_from_this<FtpDataConnection>
{
public:
	typedef boost::shared_ptr<FtpDataConnection> pointer;
	static pointer create(boost::asio::io_service &io_service, boost::shared_ptr<FtpQuery> query)
	{
		return pointer(new FtpDataConnection(io_service, query));
	}
	
	tcp::socket & socket() 
	{
		return m_socket;
	}
	
	void start(const boost::system::error_code& error)
	{
		if (!error) {
			if (m_query->type().compare("LIST") == 0) {
				std::string listData = getListData();
				boost::asio::async_write(m_socket, 
										 boost::asio::buffer(listData), 
										 boost::bind(&FtpDataConnection::handle_write, shared_from_this(), boost::asio::placeholders::error));
			}
			else if (m_query->type().compare("PUT") == 0) {
				boost::asio::async_read(m_socket,
										boost::asio::buffer(buf, MAX_BUF_SIZE),
										boost::asio::transfer_at_least(1),
										boost::bind(&FtpDataConnection::handle_file_read, shared_from_this(), 
													true, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
			}
		} else {
			throw boost::system::system_error(error);
		}
	}
private:
	FtpDataConnection(boost::asio::io_service& io_service, boost::shared_ptr<FtpQuery> query)
	: m_socket(io_service), m_query(query)
	{
	}
	
	void handle_file_read(bool firstTime, const boost::system::error_code &error, size_t bytes_transferred)
	{
		if (!error) {
			char *b = buf;
			if (firstTime) {
				std::stringstream ss(std::string(buf, bytes_transferred));
				std::string filename, empty;
				
				ss >> filename >> m_fileSize >> empty;
				b += ss.tellg();
				bytes_transferred -= ss.tellg();

				fs::path myPath(filename.c_str());

				m_file = fopen(myPath.filename().c_str(), "wb");
			}
			
			fwrite(b, bytes_transferred, 1, m_file);
			
			boost::asio::async_read(m_socket,
									boost::asio::buffer(buf, MAX_BUF_SIZE),
									boost::asio::transfer_at_least(1),
									boost::bind(&FtpDataConnection::handle_file_read, shared_from_this(), 
												false, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
		} else if (error == boost::asio::error::eof) {
			fclose(m_file);
			m_file = 0;
		}
	}
	
	void handle_write(const boost::system::error_code& error)
	{
		if (error) {
			std::cerr << "Error sending data: " << error << std::endl;
		}
	}
	
	std::string getListData()
	{
		std::stringstream out;
		
		fs::path full_path = fs::current_path();
		
		unsigned long file_count = 0;
		unsigned long dir_count = 0;
		unsigned long other_count = 0;
		unsigned long err_count = 0;
		
		out << "\nIn directory: "
		<< full_path.directory_string() << "\n\n";
		fs::directory_iterator end_iter;
		for ( fs::directory_iterator dir_itr( full_path );
			 dir_itr != end_iter;
			 ++dir_itr )
		{
			try
			{
				if ( fs::is_directory( dir_itr->status() ) )
				{
					++dir_count;
					out << dir_itr->path().filename() << " [directory]\n";
				}
				else if ( fs::is_regular_file( dir_itr->status() ) )
				{
					++file_count;
					out << dir_itr->path().filename() << "\n";
				}
				else
				{
					++other_count;
					out << dir_itr->path().filename() << " [other]\n";
				}
				
			}
			catch ( const std::exception & ex )
			{
				++err_count;
				std::cerr << dir_itr->path().filename() << " " << ex.what() << std::endl;
			}
		}
		out << "\n" << file_count << " files\n"
		<< dir_count << " directories\n"
		<< other_count << " others\n"
		<< err_count << " errors\n";
		
		return out.str();
	}
	
	tcp::socket m_socket;
	
	static const unsigned long MAX_BUF_SIZE = 1024;
	char buf[MAX_BUF_SIZE];
	
	FILE *m_file;
	size_t m_fileSize;
	
	boost::shared_ptr<FtpQuery> m_query;
};

class FtpControlConnection 
	: public boost::enable_shared_from_this<FtpControlConnection> 
{
public:
	typedef boost::shared_ptr<FtpControlConnection> pointer;
	static pointer create(boost::asio::io_service &io_service, boost::shared_ptr<tcp::acceptor> acceptor) 
	{
		return pointer(new FtpControlConnection(io_service, acceptor));
	}

	tcp::socket & socket() 
	{
	   return m_socket;
	}
	
	void start()
	{
		boost::asio::async_read(m_socket, 
								boost::asio::buffer(m_command, MAX_COMMAND_SIZE-1),
								boost::asio::transfer_at_least(1),
								boost::bind(&FtpControlConnection::handle_read, 
											shared_from_this(),
											boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
	}

private:
	FtpControlConnection(boost::asio::io_service& io_service, boost::shared_ptr<tcp::acceptor> acceptor)
	: m_socket(io_service), m_acceptor(acceptor)
	{
	}

	void processCommand(const std::string & command_line)
	{
		std::string cmd, arg;
		getCmdAndArg(command_line, cmd, arg);
		
		std::string response;
		std::cout << cmd << std::endl;
		if (cmd.compare("LIST") == 0 || cmd.compare("PUT") == 0) {
			boost::shared_ptr<FtpQuery> query(new FtpQuery(cmd));
			FtpDataConnection::pointer listConnection = FtpDataConnection::create(m_socket.get_io_service(), query);
			m_acceptor->async_accept(listConnection->socket(), boost::bind(&FtpDataConnection::start, listConnection, boost::asio::placeholders::error));
			response = "OK 40002";
		} else if (!cmd.empty()) {
			response = "Error: unknown command";
		}
		
		if (!cmd.empty()) {
			boost::asio::async_write(m_socket,
									 boost::asio::buffer(response),
									 boost::bind(&FtpControlConnection::handle_write, shared_from_this(),
												 boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
		}
	}
	
	void getCmdAndArg(const std::string & command_line, std::string &op, std::string &arg)
	{
		std::string t = command_line;
		boost::algorithm::trim(t);
		
		std::vector<std::string> splitVec;
		boost::algorithm::split(splitVec, t, boost::is_any_of(" "));
		op = "";
		arg = "";
		
		if (splitVec.size()) {
			op = splitVec[0];
		}
		
		if (splitVec.size() > 1) {
			arg = splitVec[1];
		}
		
		return;		
	}
	
	void handle_read(const boost::system::error_code &error, size_t bytes_transferred)
	{
		if (!error) {
			m_command[bytes_transferred] = '\0';
			char *p = m_command;
			while (*p) {
				while (*p && *p != '\n' && *p != '\r') {
					m_commandLine += *p;
					p++;
				}
				if (*p == '\n' || *p == '\r') {
					processCommand(m_commandLine);
					m_commandLine = "";
				}
				while (*p == '\n' || *p == '\r') p++;
			}								
			
			boost::asio::async_read(m_socket, 
									boost::asio::buffer(m_command, MAX_COMMAND_SIZE-1),
									boost::asio::transfer_at_least(1),
									boost::bind(&FtpControlConnection::handle_read, 
												shared_from_this(),
												boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
		}
	}
	
	void handle_write(const boost::system::error_code& error,
					  size_t bytes_transferred)
	{
	}
	
	tcp::socket m_socket;
	static const unsigned long MAX_COMMAND_SIZE = 4*1024;
	char m_command[MAX_COMMAND_SIZE];
	std::string m_commandLine;
	boost::shared_ptr<tcp::acceptor> m_acceptor;
};

class FtpServer
{
public:
	FtpServer(boost::asio::io_service &io_service)
	: m_acceptor(io_service, tcp::endpoint(tcp::v4(), FtpServerState::Instance()->port())),
	m_dataAcceptor(new tcp::acceptor(io_service, tcp::endpoint(tcp::v4(), FtpServerState::Instance()->dataPort())))
	{
		m_acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
		m_dataAcceptor->set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
		start_accept();
	}
	
private:
	void start_accept()
	{
		FtpControlConnection::pointer new_connection =
			FtpControlConnection::create(m_acceptor.io_service(), m_dataAcceptor);
		 
		m_acceptor.async_accept(new_connection->socket(),
							   boost::bind(&FtpServer::handle_accept, this, new_connection,
										   boost::asio::placeholders::error));
	}
	
	void handle_accept(FtpControlConnection::pointer new_connection,
					   const boost::system::error_code& error)
	{
		if (!error)
		{
			new_connection->start();
			start_accept();
		}
	}
	
	tcp::acceptor m_acceptor;
	boost::shared_ptr<tcp::acceptor> m_dataAcceptor;
};

int main()
{
	try
	{
		boost::asio::io_service io_service;
		FtpServerState::initInstance(40000, 40002, "/Users/vian/dev");
		FtpServer server(io_service);
		io_service.run();
	}
	catch (std::exception& e)
	{
		std::cerr << e.what() << std::endl;
	}
	
	return 0;
}

