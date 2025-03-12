#include "coroutine.h"

co_async_cliiii::Task<int> hello1() {
    cout << "hello1开始睡1秒" << endl;
    co_await co_async_cliiii::sleep_for(1s);
    cout << "hello1睡醒了" << endl;
    co_return 1;
}

co_async_cliiii::Task<int> hello2() {
    cout << "hello2开始睡2秒" << endl;
    co_await co_async_cliiii::sleep_for(2s);
    cout << "hello2睡醒了" << endl;
    co_return 2;
}

co_async_cliiii::Task<void> hello3() {
    cout << "hello3开始睡3秒" << endl;
    co_await co_async_cliiii::sleep_for(3s);
    cout << "hello3睡醒了" << endl;
    co_return;
}

co_async_cliiii::Task<int> hello() {
    //当任务都完成时才会返回.
    auto [i, j, k] = co_await when_all(hello1(), hello2(), hello3());
    co_return i + j;

    // auto v = co_await when_any(hello2(), hello1(), hello3());
    // co_return std::get<1>(v);
}

int main() {
    auto t = hello();
    co_async_cliiii::getLoop().addTask(t);
    co_async_cliiii::getLoop().runAll();
    cout << t.h_.promise().result() << endl;
}
