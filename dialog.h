//
// Created by denys on 22/01/2021.
//

#ifndef SAFE_SERVER_DIALOG_H
#define SAFE_SERVER_DIALOG_H

#include <boost/noncopyable.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <memory>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <mutex>
#include <iostream>
#include <pqxx/pqxx>

#define DEBUG

class client : boost::noncopyable
{
public:
    client(boost::asio::io_context& context, const std::string& login, const std::string& password) :
        sock(new boost::asio::ip::tcp::socket(context)), buf(new char[1024])
    {
        this->pass = password;
        this->login = login;
        already_logged = false;
#ifdef DEBUG
        std::cout<<"client created: "<<login<<std::endl;
#endif
    }
    void connect(const std::string& ip, unsigned short port)
    {
#ifdef DEBUG
        std::cout<<"client start connect: "<<this->login<<std::endl;
#endif
        //Подключение std::shared_ptr<socket> в конечной точке
        this->sock->async_connect(boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address(ip), port),
                                  [this](const boost::system::error_code& error)
        {
            if(!error)
            {
#ifdef DEBUG
                std::cout<<"client connected without error: "<<this->login<<std::endl;
#endif
                std::string msg = this->login + " "+this->pass;
                std::copy(msg.begin(), msg.end(), this->buf);
                //Ввод логина и пароля по подключенному сокету
                this->sock->async_write_some(boost::asio::buffer(this->buf, msg.size()),
                                             [this](const boost::system::error_code& error, size_t bytes)
                {
                   if(!error)
                   {
#ifdef DEBUG
                       std::cout<<"client write after connect without error: "<<login<<std::endl;
#endif
                       //Чтение, выбрасывающее ошибку EOF
                       this->sock->async_read_some(boost::asio::buffer(this->buf, 1024),
                                                   [this](const boost::system::error_code& error, size_t bytes)
                       {
                          if(!error)
                          {
#ifdef DEBUG
                              std::cout << "client read after connect: " << login + ", msg = "+std::string(this->buf, bytes) << std::endl;
#endif
                              if(std::string(this->buf, bytes) == "ok")
                              {
                                  already_logged = true;
                              }
                          }
                          else
                          {
#ifdef DEBUG
                              std::cout << "client read after connect: " << login + " "+error.message() << std::endl;
#endif
                          }
                       });
                   }
                   else
                   {
#ifdef DEBUG
                       std::cout << "client write after connect: " << login + " "+error.message() << std::endl;
#endif
                   }
                });
            }
            else
            {
#ifdef DEBUG
                std::cout << "client connect: " << login + " "+error.message() << std::endl;
#endif
            }
        });
    }
    void request(const std::string& request, std::shared_ptr<std::string> answer)
    {
#ifdef DEBUG
        std::cout << "client start request: " << login << std::endl;
#endif
        std::copy(request.begin(), request.end(), this->buf);
        this->sock->async_write_some(boost::asio::buffer(this->buf, request.size()),
                                     [this, &answer](const boost::system::error_code& error, size_t bytes)
        {
            if(!error)
            {
                this->sock->async_read_some(boost::asio::buffer(this->buf, 1024),
                                            [this, &answer](const boost::system::error_code& error, size_t bytes)
                {
                   if(!error)
                   {
                       *answer = std::string(this->buf, bytes);
#ifdef DEBUG
                       std::cout << "client read after request without error: " << login << std::endl;
#endif
                   }
                   else
                   {
#ifdef DEBUG
                       std::cout << "client request after connect: " << login + " "+error.message() << std::endl;
#endif
                   }
                });
            }
            else
            {
#ifdef DEBUG
                std::cout << "client write after request: " << login + " "+error.message() << std::endl;
#endif
            }
        });
    }
    bool is_logged()const{return this->already_logged;}
    ~client()
    {
        if(sock->is_open())sock->close();
        delete[] buf;
        buf = nullptr;
    }
private:
    std::shared_ptr<boost::asio::ip::tcp::socket> sock;
    std::string login, pass;
    char* buf;
    bool already_logged;
};

class server : boost::noncopyable
{
private:
    std::string request(const std::string& msg)
    {
        std::string answer;
        try
        {
            pqxx::result r(act.exec(msg));
            for(const auto & i : r)
            {
                for(const auto & j : i)
                {
                    answer+=(j.c_str());
                    answer+=" ";
                }
                answer+="\n";
            }
        }
        catch (pqxx::sql_error const &e)
        {
            answer = "error";
        }
        return answer;
    }
    void handle_accept(std::shared_ptr<boost::asio::ip::tcp::socket> sock, const boost::system::error_code& error)
    {
#ifdef DEBUG
        std::cout << "SERVER start handle accept: " << std::endl;
#endif
        if(!error)
        {
#ifdef DEBUG
            std::cout << "SERVER handle accept without error"<< std::endl;
#endif
            this->clients.emplace_back(std::move(sock));
            this->buf.emplace_back(new char[1024]);
            this->clients.back()->async_read_some(boost::asio::buffer(this->buf.back(), 1024),
                                  [this](const boost::system::error_code& error, size_t bytes)
            {
               if(!error)
               {
#ifdef DEBUG
                   std::string help;
                   help.resize(bytes);
                   std::copy(buf.back(), buf.back() + bytes, help.begin());
                   std::cout << "SERVER read in handle accept, login and pass of client: " + help << std::endl;
#endif
                   this->clients.back()->async_write_some(boost::asio::buffer("ok", 2),
                                          [this](const boost::system::error_code& error, size_t bytes)
                   {
                       if(!error)
                       {
#ifdef DEBUG
                           std::cout << "SERVER write in handle accept without error"<< std::endl;
#endif
                       }
                       else//error write in handle_accept
                       {
                           delete[] buf.back();
                           buf.back() = nullptr;
                           this->buf.pop_back();
                           this->clients.pop_back();
#ifdef DEBUG
                           std::cout << "SERVER write in handle accept: " + error.message() << std::endl;
#endif
                       }
                   });
               }
               else//error read in handle_accept
               {
                   delete[] buf.back();
                   buf.back() = nullptr;
                   this->buf.pop_back();
                   this->clients.pop_back();
#ifdef DEBUG
                   std::cout << "SERVER read in handle accept: " + error.message() << std::endl;
#endif
               }
            });
        }
        else//error in handle_accept
        {
#ifdef DEBUG
            std::cout << "SERVER handle accept: " + error.message() << std::endl;
#endif
        }
        start_accept();
    }
    void start_accept()
    {
#ifdef DEBUG
        std::cout << "SERVER start accept"<< std::endl;
#endif
        std::shared_ptr<boost::asio::ip::tcp::socket> sock(new boost::asio::ip::tcp::socket(this->context));
        this->acc.async_accept(*sock, boost::bind(&server::handle_accept, this, sock, _1));
    }
public:
    server(boost::asio::io_context& context, std::string ip, unsigned short port) : context(context),
        acc(context, boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address(ip), port)),
        connection("host=localhost dbname=MainDB user=postgres password=postgres"), act(connection)
    {
        this->start_accept();
    }
    void check_request(size_t index)
    {
        if(index >= this->clients.size())return;
#ifdef DEBUG
        std::cout << "SERVER start check_request"<< std::endl;
#endif
        this->clients[index]->async_read_some(boost::asio::buffer(this->buf[index], 1024),
                                              [this, index](const boost::system::error_code& error, size_t bytes)
        {
           if(!error)
           {
#ifdef DEBUG
               std::cout << "SERVER read in check_request without error"<< std::endl;
#endif
               std::string r(this->buf[index], bytes);
               std::string ans(request(r));
               std::copy(ans.begin(), ans.end(), this->buf[index]);
               this->clients[index]->async_write_some(boost::asio::buffer(this->buf[index], ans.size()),
                                                      [](const boost::system::error_code& error, size_t bytes)
               {
                   if(!error)
                   {
#ifdef DEBUG
                       std::cout << "SERVER write in check_request without error"<< std::endl;
#endif
                   }
                   else//error check_request write
                   {
#ifdef DEBUG
                       std::cout << "SERVER write in check_request: " + error.message() << std::endl;
#endif
                   }
               });
           }
           else//error check_request read
           {
#ifdef DEBUG
               std::cout << "SERVER read in check_request: " + error.message() << std::endl;
#endif
           }
        });
    }
    ~server()
    {
        for(int i=0;i<this->clients.size();i++)
        {
            delete[] this->buf[i];
            this->buf[i] = nullptr;
            if(this->clients[i]->is_open())this->clients[i]->close();
        }
        this->connection.disconnect();
    }
private:
    std::vector<std::shared_ptr<boost::asio::ip::tcp::socket>> clients;
    boost::asio::ip::tcp::acceptor acc;
    boost::asio::io_context& context;
    std::vector<char*> buf;
    pqxx::connection connection;
    pqxx::work act;
};

#endif //SAFE_SERVER_DIALOG_H
