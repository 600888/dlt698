---
title: 执行器
description: IExecutor、ITimer 接口与 ManualExecutor 虚拟时钟。
---

# 执行器

头文件：`<dlt698/common/executor.hpp>`，命名空间 `dlt698`。

## IExecutor

```cpp
class ITimer {
  public:
    virtual ~ITimer() = default;
    virtual void cancel() = 0;      ///< 幂等取消，不产生取消完成通知
};

class IExecutor {
  public:
    using Task = std::function<void()>;
    using Clock = std::chrono::steady_clock;

    virtual void post(Task task) = 0;
    virtual std::shared_ptr<ITimer> schedule(Clock::duration delay, Task task) = 0;
    virtual Clock::time_point now() const noexcept = 0;
    virtual bool is_current() const noexcept = 0;
};
```

## 实现约定

如果你要自己实现 `IExecutor`，必须满足：

:::warning 四条硬性约定
1. **`post`/`schedule` 必须延后执行任务。** 不能在投递函数内直接调用用户任务。
2. **任务必须串行。** 同一执行器上的任务不得并发。
3. **异常必须隔离。** 用户任务抛出的异常不能让执行器失效。
4. **`is_current()` 要覆盖整个执行环境。** 在本执行环境的任务中返回 `true`；共享 I/O 循环的实现须覆盖循环内**所有 strand 的回调**。
:::

第 4 条是死锁防护的基础。同步适配器用它拒绝会阻塞自己的调用——`is_current()` 为 `true` 时说明当前线程已经在执行器里，再同步等待就是自锁。

`now()` 使用**单调时钟**（`steady_clock`），不受系统日期调整影响。

`ITimer::cancel()` 幂等。**释放计时器句柄本身不会取消回调**——必须显式 `cancel()`。

## 库提供的实现

| 实现 | 位置 | 线程 | 时间 |
| --- | --- | --- | --- |
| `ManualExecutor` | core | 仅单线程 | 虚拟时钟，`advance()` 推进 |
| `IoRuntime::executor()` | transport | 可跨线程投递 | 真实单调计时器 |

### 共享 strand 的执行器

`IoRuntime::executor()` 返回运行在独立 strand 上的会话执行器：

- 由**同一个 runtime** 驱动；
- **可以**从不同线程投递任务；
- 多个泵送线程时，每个通道/监听器及会话执行器**分别**串行处理自身状态，不同实例的回调可以并发。

## ManualExecutor

```cpp
class ManualExecutor final : public IExecutor {
  public:
    ManualExecutor();
    ~ManualExecutor();

    void post(Task task) override;
    std::shared_ptr<ITimer> schedule(Clock::duration delay, Task task) override;
    Clock::time_point now() const noexcept override;
    bool is_current() const noexcept override;

    void run_ready();                            ///< 执行当前可运行任务
    void advance(Clock::duration elapsed);       ///< 推进虚拟时间并执行到期任务
};
```

虚拟时钟从零开始。`run_ready()` 执行当前可运行的任务，**包括任务中新投递的任务**，并隔离回调异常。`advance(elapsed)` 推进虚拟时间并执行到期任务；`elapsed` 为负或超出可表示范围时抛 `std::invalid_argument`。

`~ManualExecutor()` 丢弃尚未驱动的任务和计时器。

:::warning 只限单线程
`ManualExecutor` **仅限单线程使用**。它不创建线程、不跟随真实时间，是确定性模拟的基础。不要在多个线程上共享它。
:::

## 典型用法

```cpp
auto executor = std::make_shared<dlt698::ManualExecutor>();
auto channels = dlt698::transport::MemoryChannel::pair(executor);

// 业务代码推进虚拟时间，超时也能正常触发
executor->advance(std::chrono::milliseconds(100));

// 收尾：把剩余任务跑完
executor->run_ready();
```

```cpp
auto runtime = std::make_shared<dlt698::transport::IoRuntime>();
auto executor = runtime->executor();
// ... 创建通道和会话 ...
std::thread runner([runtime] { runtime->run(); });
```

## 选择建议

| 场景 | 选择 |
| --- | --- |
| 单元测试、协议模拟 | `ManualExecutor` |
| 需要超时行为的确定性测试 | `ManualExecutor` + `advance()` |
| 生产环境、多个会话 | `IoRuntime::executor()` |
| 自建运行时 | 自己实现 `IExecutor`，遵守上面四条约定 |
