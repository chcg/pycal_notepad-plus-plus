#ifndef WORKER_H
#define WORKER_H

#include <condition_variable>
#include <future>
#include <iostream>
#include <queue>

#include <Python.h>


using namespace std;

class Worker {
public:
    condition_variable input_cv;
    condition_variable output_cv;

    mutex input_mutex;
    mutex output_mutex;

    queue<string> stdin_queue;
    queue<string> stdout_queue;
    queue<string> stderr_queue;

    unique_ptr<chrono::time_point<chrono::steady_clock>> heartbeat;

    static void process(const std::stop_token& stop_token);
private:
    static void overrideInput();
    static void captureStdoutStderr(const string& code);
};


unique_ptr<chrono::time_point<chrono::steady_clock>> steady_now();

inline unique_ptr<Worker> worker = make_unique<Worker>();
inline PyThreadState* py_thread_state;


#endif // WORKER_H
