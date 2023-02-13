# WebServer
A C++ Lightweight Web Server
## 项目介绍
该项目是基于C++实现的轻量级HTTP服务器
## 项目功能
使用epoll + 非阻塞IO + 边缘触发(ET) 实现高并发处理请求，使用同步I/O模拟Proactor模式

epoll使用EPOLLONESHOT保证一个socket连接在任意时刻都只被一个线程处理

添加定时器支持HTTP长连接，定时回调handler处理超时连接

使用C++标准库双向链表list来管理定时器

使用epoll与管道结合管理定时信号

目前支持GET方法


注：支持Linux,C++11
