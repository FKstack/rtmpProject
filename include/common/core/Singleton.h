#pragma once

/**
 * @brief 为进程级服务提供延迟创建的单例基类。
 *
 * 使用 C++11 起保证线程安全的函数内静态对象创建实例。派生类必须将构造函数设为
 * 私有或受保护，并声明 `friend class Singleton<派生类>`，以限制实例只能由本模板创建。
 *
 * @tparam T 单例服务的派生类型。
 */
template <typename T>
class Singleton
{
public:
    /**
     * @brief 获取进程内唯一的服务实例。
     *
     * 首次调用时创建实例；后续调用返回同一对象。该初始化过程由 C++17 保证线程安全。
     *
     * @return 派生类型的唯一实例。
     */
    static T &instance()
    {
        static T instance;
        return instance;
    }

    Singleton(const Singleton &) = delete;
    Singleton &operator=(const Singleton &) = delete;
    Singleton(Singleton &&) = delete;
    Singleton &operator=(Singleton &&) = delete;

protected:
    Singleton() = default;
    ~Singleton() = default;
};
