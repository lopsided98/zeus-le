#pragma once

#include <limits.h>

enum class co_result_type {
    READY,
    PENDING,
};

template <typename T>
struct co_result final {
    T result;
    enum class co_result_type {
        READY,
        PENDING,
    } type;

    static constexpr co_result<T> ready(T result) {
        return co_result<T>{.result = result, .type = co_result_type::READY};
    }
    static constexpr co_result<T> pending() {
        return co_result<T>{.type = co_result_type::PENDING};
    }

    constexpr bool is_pending() const {
        return type == co_result_type::PENDING;
    }
    constexpr T get() const { return result; }
};

class co_cancel {
    const co_cancel* parent{nullptr};
    bool canceled{false};

   public:
    constexpr co_cancel() {}
    co_cancel(bool canceled) : canceled(canceled) {}
    co_cancel(const co_cancel& parent) : parent(&parent) {}

    void cancel() { canceled = true; }

    explicit operator bool() const {
        if (canceled) return true;
        if (parent && *parent) return true;
        return false;
    }
};

class co_routine {
   protected:
    void* state___;
};

#define CO_DECLARE(rtype, name, ...)              \
    class name final : public co_routine {        \
       public:                                    \
        co_result<rtype> operator()(__VA_ARGS__); \
                                                  \
       private:                                   \
        typedef rtype rtype___;

#define CO_CALLEE(name) class name name;

#define CO_DECLARE_END }

#define CO_DEFINE(rtype, name, ...) \
    co_result<rtype> name::operator()(__VA_ARGS__)

#define CO_BEGIN                                  \
    do {                                          \
        if (this->state___) goto* this->state___; \
        *this = __typeof__(*this){};              \
    } while (0)

#define _CO_LABEL_IMPL(suffix) coroutine_##suffix
#define _CO_LABEL(suffix) _CO_LABEL_IMPL(suffix)

#define _CO_YIELD_IMPL(counter)                \
    do {                                       \
        this->state___ = &&_CO_LABEL(counter); \
        return co_result<rtype___>::pending(); \
        _CO_LABEL(counter) :;                  \
    } while (0)

#define CO_YIELD _CO_YIELD_IMPL(__COUNTER__)

#define CO_AWAIT(func)                           \
    ({                                           \
        decltype(func) ret___;                   \
        while ((ret___ = (func)).is_pending()) { \
            CO_YIELD;                            \
        }                                        \
        ret___.get();                            \
    })

#define CO_RETURN(val)                          \
    do {                                        \
        this->state___ = nullptr;               \
        return co_result<rtype___>::ready(val); \
    } while (0)

template <typename C, typename R>
class co_fuse {
    co_result<R> res;
    C co;

   public:
    template <typename... Args>
    co_result<R> operator()(Args... args) {
        if (!res.is_pending()) return res;
        res = co(args...);
        return res;
    }
};

template <typename R, typename... Args>
co_result<R> _co_select(bool cancel, Args... res) {
    bool done = true;
    for (const auto p : {res...}) {
        if (p.is_pending()) done = false;
    }
    // if (!done) CO_YIELD;
}

#define CO_SELECT(select, cancel, ...)             \
    do {                                           \
        bool cancel = (cancel) || (select).cancel; \
        (select)(__VA_ARGS__)                      \
    } while (0)

template <typename T>
class co_loan_guard;

template <typename T>
class co_loan final {
    T* ptr;

   public:
    class guard final {
        friend class co_loan;

        T** loan_ptr;
        guard(T** loan_ptr) : loan_ptr(loan_ptr) {}

       public:
        ~guard() { *loan_ptr = nullptr; }
    };

    [[nodiscard]] guard loan(T& data) {
        ptr = &data;
        return guard{&ptr};
    }

    co_result<T*> get(bool cancel = false) {
        if (cancel) return co_result<T*>::ready(nullptr);
        if (!ptr) return co_result<T*>::pending();
        T* data = ptr;
        ptr = nullptr;
        return co_result<T*>::ready(data);
    }

    CO_DECLARE(T*, get2, co_loan& loan, bool cancel = false);
    CO_DECLARE_END;
};

template <typename T>
CO_DEFINE(T*, co_loan<T>::get2, co_loan& loan, bool cancel) {
    while (true) {
        if (cancel) CO_RETURN(nullptr);
        if (loan.ptr) break;
        CO_YIELD;
    }
    T* data = loan.ptr;
    loan.ptr = nullptr;
    CO_RETURN(data);
}