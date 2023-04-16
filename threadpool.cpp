#include "threadpool.h"
#include <thread>
#include <iostream>
#include <functional>


//最大任务数量
const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THREAD_MAX_THRESHHOLD = 100;
const int THREAD_MAX_IDLE_TIME = 60; //单位：秒


//线程池构造
ThreadPool::ThreadPool()
:initThreadSize_(0)
,taskSize_(0)
,idleThreadSize_(0)
,threadSizeThreshHold_(300)
,taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
,poolMode_(PoolMode::MODE_FIXED)
,isPoolRunning_(false)
{}



//线程池析构
ThreadPool::~ThreadPool()
{
    isPoolRunning_ = false;
    notEmpty_.notify_all();
    //等待线程池所有线程返回  有两种状态：阻塞&正在执行任务
    std::unique_lock<std::mutex> lock(taskQueMtx_);
    exitCond_.wait(lock, [&]()->bool{return threads_.size()== 0;});  //队列还有就阻塞
}



//设置工作模式
void ThreadPool::setMode(PoolMode mode)
{
    if(checkRunningState()) return;
    poolMode_ = mode;
}


//设置task任务队列上限阈值
void ThreadPool::setTaskQueMaxThreshHold(int threshold){
    if(checkRunningState()) return;
    taskQueMaxThreshHold_ = threshold;
}

//设置cached模式下线程阈值
void ThreadPool::setThreadSizeThreshHold(int threshold){
    if(checkRunningState()) return;
    if(poolMode_==PoolMode::MODE_CACHED) threadSizeThreshHold_= threshold;
}
    
   
 //给线程池提交任务，生产任务
Result ThreadPool::submitTask(std::shared_ptr<Task> sp){
    //生产者获取锁，任务队列是临界区
    std::unique_lock<std::mutex> lock(taskQueMtx_);

    //线程通信，等待任务队列有空间，size<task_max_threshold,否则条件变量阻塞并释放锁
    //如果阻塞了一秒钟，返回任务提交失败
    if(!notFull_.wait_for(lock,std::chrono::seconds(1),
    [&]()->bool {return taskQue_.size()<(size_t)taskQueMaxThreshHold_ ;}));
    {
        std::cerr<<"task queue is full , submit task failed"<<std::endl;
        //return task->getResult(); //任务成员方法返回任务不可以：task执行完，task对象已经析构了
        return Result(sp,false);
    }
    //wait(lock)  wait_for()  wait_until()  等到条件满足  
    //wait_for返回false，表示等1秒条件依然不满足
    //如果有空余，把任务放入任务队列
    taskQue_.emplace(sp);
    taskSize_++;
    
    //提交之后任务队列不为空，通知消费者消费任务，notEmpty_上进行通知
    notEmpty_.notify_all();


    //需要根据任务数量和空闲线程的数量，判断是否需要创建新的线程出来
    //cached模式任务处理比较紧急，但是场景：小而快的任务，耗时任务不适合cached，因为长时间占用线程会导致线程创建过多
    //cached模式且任务数量大于空闲线程数量，且当前线程数量少于线程数量上限（根据机器来定）
    if(poolMode_ == PoolMode::MODE_CACHED && taskSize_ > idleThreadSize_ && curThreadSize_< threadSizeThreshHold_)
    {
        //创建新线程
        //创建线程对象的时候，把线程函数给到thread线程对象
        auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
        int threadId = ptr->getId();
        threads_.emplace(threadId, std::move(ptr));
        threads_[threadId]->start();
        //修改线程数量相关变量
        idleThreadSize_++;
        //unique_ptr无左值的拷贝赋值
        // threads_.emplace_back(std::move(ptr));
        curThreadSize_++;
    }
    
    return Result(sp);
}

//开启线程池
void ThreadPool::start(int initThreadSize)
{
    //设置线程运行状态
    isPoolRunning_ = true;
    
    //记录初始线程的数量
    initThreadSize_ = initThreadSize;
    curThreadSize_ = initThreadSize;
    
    //创建线程对象
    for(int i = 0;i< initThreadSize_; i++)
    {
        //创建线程对象的时候，把线程函数给到thread线程对象
        auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1));
        //unique_ptr无左值的拷贝赋值
        int threadId = ptr->getId();
        threads_.emplace(threadId,std::move(ptr));

    }

    //集中启动所有线程
    for(int i = 0;i< initThreadSize_; i++)
    {
        threads_[i]->start();
        idleThreadSize_++; //每启动一个线程，空闲++

    }
}

//线程池从任务队列消费任务
void ThreadPool::threadFunc(int threadid)//线程函数返回，线程就结束了
{
    auto lastTime = std::chrono::high_resolution_clock().now();
    // std::cout<<"begin threadFunc tid:"<<std::this_thread::get_id()
    // <<std::endl;
    // std::cout<<"end threadFunc tid:"<<std::this_thread::get_id()
    // <<std::endl;
    for(;;)
    {
        //先获取锁
        std::unique_lock<std::mutex> lock(taskQue_);
        std::cout<< "tid:" <<std::this_thread::get_id()
        << "尝试获取任务" <<std::endl;

        //cached模式下， 有可能已经创建了很多的线程，但是空闲时间超过60s应该回收多余的线程
        //超过initThreadsize的数量需要进行回收
        //当前时间  上一次线程执行时间如果间隔60s,
        //锁加双重判断
        while(taskQue_.size() == 0) //修改过后只有无任务执行的时候才判断线程池是否析构
        {
            if(!isPoolRunning_)
            {
                threads_.erase(threadid);  //std::this_thread::getid()
                std::cout<<"threadid:"<<std::this_thread::get_id()<<"exit"<<std::endl;
                exitCond_.notify_all();
                return;
            }

            if(poolMode_ == PoolMode::MODE_CACHED)
            {
                //cached模式下线程空闲超过一定时间则释放
                if( std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))
                {
                    auto now = std::chrono::high_resolution_clock().now();
                    auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
                    if(dur.count() >= THREAD_MAX_IDLE_TIME 
                    && curThreadSize_ > initThreadSize_)
                    {
                        //回收当前线程
                        //线程数量相关变量的修改
                        //把线程对象从线程列表容器中删除 通过线程id
                        threads_.erase(threadid);  //std::this_thread::getid()
                        curThreadSize_--;
                        idleThreadSize_--;
                        std::cout<<"threadid:"<<std::this_thread::get_id()<<"exit"<<std::endl;
                        return;
                        
                    }
                }
            }
            else
            {
                //等待notEmpty条件
                notEmpty_.wait(lock);
            }
            //检查一下有任务被唤醒还是线程池结束被唤醒
            // if(!isPoolRunning_)
            // {
            //     threads_.erase(threadid);  //std::this_thread::getid()
            //     std::cout<<"threadid:"<<std::this_thread::get_id()<<"exit"<<std::endl;
            //     exitCond_.notify_all();
            //     return;
            // }
        }
         

        //从wait返回
        idleThreadSize_--;

        std::cout<< "tid:" <<std::this_thread::get_id()
        <<  "获取任务成功......" <<std::endl;
        //从任务队列中取一个任务出来
        auto task = taskQue_.front();
        taskQue_.pop();
        taskSize_--;
        
        //访问临界区结束，应该释放锁
        lock.unlock(); 
        
        //如果依然有剩余任务，继续通知其他线程执行任务
        if(taskQue_.size() > 0)
        {
            notEmpty_.notify_all();
        }

        //取出一个任务，进行通知，通知可以继续提交生产任务
        notFull_.notify_all();   
        
        //当前线程负责执行这个任务
        if(task!= nullptr)
        task->exec();
        lastTime = std::chrono::high_resolution_clock().now();//更新线程执行完任务的时间

        //任务处理结束空闲线程++
        idleThreadSize_++;
    }
    
    
}


bool ThreadPool::checkRunningState()const
{
    return isPoolRunning_;
}

///////////线程方法实现
//启动线程
Thread::Thread(ThreadFunc func)
    :func_(func)
    ,threadId_(generateId_++)
{}


Thread::~Thread(){

}


int Thread::generateId_ = 0;

int Thread::getId()const
{
    return  threadId_;
}

//启动线程
void Thread::start()
{
    //创建一个线程来执行一个线程函数
    std::thread t(func_);   //c++11线程对象 和线程函数func_
    t.detach(); //设置分离线程 pthread_detach    phread_t设置成分离线程
}

//////////Result方法的实现
Result::Result(std::shared_ptr<Task> task, bool isValid)
    :isvalid_(isValid)
    ,task_(task)
    {

        task_->setResult(this);
    }



Any Result::get()  //用户调用
{
    if(!isvalid_)
    {
        return "";
    }
    sem_.wait(); //任务如果没有执行完会阻塞用户线程  
    //没有左值拷贝赋值和拷贝构造
    return std::move(any_);

}

void Result::setVal(Any any)  // 
{
    this->any_ = std::move(any);
    sem_.post();
}

/////////Task类
Task::Task(): result_(nullptr)
{

}

void Task::exec()
{
    if(result_ != nullptr)
    {
        result_->setVal(run());
    }
    
}

void Task::setResult(Result* res)
{
    result_ = res;
}