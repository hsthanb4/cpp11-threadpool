#include <iostream>
#include <chrono>
#include <thread>

#include "threadpool.h"

using uLong = unsigned long long; 

/*
 main thread: 给每一个线程分配计算的区间，
并等待他们算完返回结果，合并最终的结果
*/
class MyTask : public Task
{
public:
    MyTask(int begin, int end)
        : begin_(begin)
        , end_(end){};


    //怎么设计run函数的返回值，可以表示任意的类型
    //在线程池分配的线程中去执行
    Any run()
    {
        std::cout<<"tid:"<<std::this_thread::get_id()
        <<"begin!"<<std::endl;

        uLong sum = 0;
        for(uLong i = begin_; i<= end_; i++)
        {
            sum+= i;
        }

        std::this_thread::sleep_for(std::chrono::seconds(3));
        std::cout<<"tid:"<<std::this_thread::get_id()
        <<"end!"<<std::endl;

        return sum;
    }

private:
    int begin_;
    int end_;    
};




int main()
{
    ThreadPool pool;
    //用户自己设置线程池的工作模式
    pool.setMode(PoolMode::MODE_CACHED);
    //开始启动线程池之前相关参数已经设置好
    pool.start(4);

    //如何设计Result机制呢
    Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
    Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
    Result res3 = pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

    uLong sum1 = res1.get().cast_<uLong>(); //get返回Any类型，
    uLong sum2 = res2.get().cast_<uLong>(); 
    uLong sum3 = res3.get().cast_<uLong>(); 


    // pool.submitTask(std::make_shared<MyTask>());
    // pool.submitTask(std::make_shared<MyTask>());
    // pool.submitTask(std::make_shared<MyTask>());
    // pool.submitTask(std::make_shared<MyTask>());
    // pool.submitTask(std::make_shared<MyTask>());
    // pool.submitTask(std::make_shared<MyTask>());
    // pool.submitTask(std::make_shared<MyTask>());

    std::cout<<(sum1+sum2+sum3)<<std::endl;

    getchar();

    // std::this_thread::sleep_for(std::chrono::seconds(5));
}