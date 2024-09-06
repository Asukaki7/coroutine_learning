#include <co_async/debug.hpp>
#include <co_async/task.hpp>
#include <co_async/timer_loop.hpp>
#include <co_async/when_any.hpp>
#include <co_async/when_all.hpp>
#include <sys/epoll.h>
#include <fcntl.h> // io ctl
#include <cerrno>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>
#include <cstring>

using namespace std::chrono_literals;

co_async::TimerLoop loop;

co_async::Task<int> hello1() {
    debug(), "hello1开始睡1秒";
    co_await sleep_for(loop, 1s);
    debug(), "hello1睡醒了";
    co_return 1;
}

co_async::Task<int> hello2() {
    debug(), "hello2开始睡2秒";
    co_await sleep_for(loop, 2s);
    debug(), "hello2睡醒了";
    co_return 2;
}

co_async::Task<int> hello() {
    debug(), "hello开始等1和2";
    auto v = co_await when_any(hello1(), hello2());
    debug(), "hello看到", (int)v.index() + 1, "睡醒了";
    co_return std::get<0>(v);
}

int main() {
    // 把0号输入流设置为非阻塞的，这样当你的read时如果没有数据
    // 会直接范围 EWOULDBLOCK 错误
    int attr = 1;
    ioctl(0, FIONBIO, &attr);
    /* int flags = fcntl(0, F_GETFL);
    flags |= O_NONBLOCK;
    fcntl(0, F_SETFL); */


    // 创建异步控制器
    int epfd = epoll_create1(0);

    //创建异步监听器
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = 0;
    epoll_ctl(epfd, EPOLL_CTL_ADD, fileno(stdin), &event);

    while (true) {
        struct epoll_event ebuf[10];
        // 让它无限制等下去
        int res = epoll_wait(epfd, ebuf, 10, 1000);
        if (res == -1) {
            debug(), "epoll出错了";
        }
        if (res == 0) {
            debug(), "epoll 超时了 1s内没有等到任何输入";
        }
        for (int i = 0; i < res; i++) {
            debug(), "等到了输入事件";
            int fd = ebuf[i].data.fd;
            char c;
            while (true) {
                int len = read(fd, &c, 1);
                if (len <= 0) {
                    if (errno == EWOULDBLOCK) {
                        debug(), "前面的区域之后再探索吧";
                        break;
                    }
                    debug(), "read error ", strerror(errno);
                    break;
                }
                debug(), c;
            }
                
        }
    }
    debug(), "等到stdin的输入事件";
}