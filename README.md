# A simple threadpool
## update
1. Simplified the implement of the way how user can submit their tasks by  using **variadic tempalte**,**future**, **packaged_task** in c++11; 
put all functions in the improved_threadpool.h
## How to use:
```c++
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
```

## some features about this project:

### 1. uisng c++11 template  implemented the any class in c++17 to receive the result of user task.

```c++

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

```

### 2. implemented cached and fixed pool-mode,  
   #### in the "fixed" mode: 
   *the amount of threads is fixed.*   
   #### in the "cached" mode:   
   *when there are more tasks than threads, new threads will be created,*  
    *when there are threads idle for a long preriod(1 second), these threads will be removed* 
 
```c++
    ThreadPool pool;
    //用户自己设置线程池的工作模式
    pool.setMode(PoolMode::MODE_CACHED);
    //开始启动线程池之前相关参数已经设置好
    //设置初始线程数，默认为std::thread::hardware_concurrency()
    pool.start(4);
  
```


### 3. using bind to pass threadfunc into thread object for initalizing new thread object.   
   *After user submitting task,task will be pushed into task queue,*  
    *then the threadfunc will be passed to threads for initialization,*   
    *then in the threadfunc funciton, this running thread will consume task from task queue.*  
    
```c++
  //cached模式且任务数量大于空闲线程数量，且当前线程数量少于线程数量上限（根据机器来定）
    if(poolMode_ == PoolMode::MODE_CACHED && taskSize_ > idleThreadSize_ && curThreadSize_< threadSizeThreshHold_)
    {
        //创建新线程
        //创建线程对象的时候，把线程函数给到thread线程对象
        auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
        int threadId = ptr->getId();
        threads_.emplace(threadId, std::move(ptr));
        //开启线程，std::thread t(func_)
        threads_[threadId]->start();
        //修改线程数量相关变量
        idleThreadSize_++;
        //unique_ptr无左值的拷贝赋值
        // threads_.emplace_back(std::move(ptr));
        curThreadSize_++;
    }
```
  
  
