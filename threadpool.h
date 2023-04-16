#ifndef THREADPOOL_H
#define THREADPOOL_H


#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_map>
#include <thread>



enum class PoolMode
{
    MODE_FIXED, //固定数量的线程
    MODE_CACHED, //线程数量可动态增长
};

//Any类型：可以接收任意数据的类型
class Any
{
public:
    Any()= default;
    ~Any() = default;
    Any(const Any&) = delete;
    Any(Any&&) = default;
    Any& operator = (Any&&) = default;
    
    //该构造函数可以让Any类型接收任意其他的数据
    template<typename T>
    Any(T data):base_(std::make_unique<Derive<T>>(data)){};


    template<typename T>
    T cast_()
    {
        //我们怎么从base_找到它所指向的Derive对象，从它里面取出data成员变量
        //基类指针-》派生类指针 RTTI
        Derive<T> *pd = dynamic_cast<Derive<T>*>(base_.get());
        if(pd == nullptr)
        {
            throw "type unmatch";
        }
        return pd->data_;
    }
private:
    //基类类型
    class Base
    {
    public:
        virtual ~Base() = default; 
    };

    //派生类类型
    template<typename T> 
    class  Derive: public Base
    {        
    public:
         Derive(T data): data_(data){};
         T data_;
         ~Derive();
    };
private:
    //定义一个基类的指针
    std::unique_ptr<Base> base_;
    
};


//实现一个信号量类
class Semaphore
{
public:
    Semaphore(int limit = 0)
        :resLimit_(limit)
        ,isExit_(false)
        {
    }
    ~Semaphore(){
        isExit_= true;
    }
    //获取一个信号量资源
    void wait(){
        if(isExit_)
        {
            return;
        }
        std::unique_lock<std::mutex> lock(mtx_);
        //等待信号量有资源，没有的话阻塞当前线程
        cond_.wait(lock, [&]()->bool{return resLimit_>0;});
        resLimit_--;
    }
    //增加一个信号量资源
    void post(){
        if(isExit_)
        {
            return;
        }
        std::unique_lock<std::mutex> lock(mtx_);
        resLimit_++;
        cond_.notify_all();
    }
private:
    std::atomic_bool isExit_;
    int resLimit_;
    std::mutex mtx_;
    std::condition_variable cond_;

};

//Task类型的前置声明
class Task;

//实现接收提交到线程池的task任务执行完成后的返回值类型result
class Result
{
public:
    Result(std::shared_ptr<Task> task, bool isvalid = true);
    ~Result() = default;

    //问题1：setval方法，获取任务执行完的返回值
    void setVal(Any any);

    //问题2：get方法，用户调用这个方法获取task的返回值
    Any get();

private:
    Any any_; //存储任务返回值
    Semaphore sem_; //线程通信的信号量(与任务执行线程通信)
    std::shared_ptr<Task> task_; //指向对应获取返回值的任务对象
    std::atomic_bool isvalid_;  //返回值是否有效
};


//任务抽象基类
class Task 
{
public:
//用户任务继承自该基类
    Task();
    ~Task() =default;
    void exec();
    virtual Any run()=0;
    void setResult(Result* res);
private:
    Result* result_;//result生命周期长于task   

};

//线程类型
class Thread
{
public:
    //线程函数对象类型
    using ThreadFunc = std::function<void(int)>;
    void start();
    int getId()const;


    Thread(ThreadFunc func);
    ~Thread();
    

private:
   ThreadFunc func_;
   static int generateId_;
   int threadId_; //保存线程id
     
};



class ThreadPool
{

public:
    //线程池构造
    ThreadPool();
    //线程池析构
    ~ThreadPool();
    
    //开始任务
    void start(int initThreadSize = std::thread::hardware_concurrency());
    //设置工作模式
    void setMode(PoolMode mode);
    
    //设置task任务队列上限阈值
    void setTaskQueMaxThreshHold(int threshold);
    void setThreadSizeThreshHold(int threshold);
    //给线程池提交任务
    Result submitTask(std::shared_ptr<Task> sp);

    bool checkRunningState()const;


    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
private:
    //定义线程函数
    void threadFunc(int threadid);

private:
    // std::vector<std::unique_ptr<Thread>> threads_;//线程列表
    std::unordered_map<int, std::unique_ptr<Thread>> threads_; //线程列表
    size_t initThreadSize_;  //初始的线程数量
    int threadSizeThreshHold_; //线程数量上限
    std::atomic_int  idleThreadSize_; //空闲线程的数量
    std::atomic_int curThreadSize_;//当前线程总数 vec.size()不是线程安全的


    //需要保证任务对象声明周期，调用run之后才析构
    std::queue<std::shared_ptr<Task>> taskQue_;//任务队列
    std::atomic_int  taskSize_;   //任务的数量
    int taskQueMaxThreshHold_;  //任务队列数量上限阈值
     
    std::mutex taskQueMtx_; //保证任务队列的线程安全
    std::condition_variable notFull_; //任务队列不满
    std::condition_variable notEmpty_; //任务队列不空
    std::condition_variable exitCond_; //等待线程资源全部回收
    

    PoolMode poolMode_; //当前线程池的工作模式
    //当前线程池的启动状态，可能会在多个线程里面使用到
    std::atomic_bool  isPoolRunning_; //当前线程池的启动状态
    

};


#endif