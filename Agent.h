// Agent.h — Improved, safer and well-documented version
// Copyright: (c) jzolee
#pragma once

#include <Arduino.h>
#include <cstring>   // for std::memmove
#include <vector>
#include <array>
#include <functional>
#include <algorithm>
#include <type_traits>

// -----------------------------------------------------------------------------
// Logging macros (users may override to enable logging)
#ifndef AGENT_LOGI
#define AGENT_LOGI(...) // no-op by default
#endif
#ifndef AGENT_LOGE
#define AGENT_LOGE(...) // no-op by default
#endif

// -----------------------------------------------------------------------------
// Lock macros per platform. The macros below mirror the original design but the
// implementation is intentionally kept minimal so the Agent class can compile
// on many Arduino-compatible platforms. If you use ESP32/ESP8266 define the
// corresponding platform macros in the build system.
#if defined(ARDUINO_ARCH_ESP32)

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define AGENT_MUTEX_DECL  SemaphoreHandle_t _task_mutex; portMUX_TYPE _isr_mux = portMUX_INITIALIZER_UNLOCKED;
#define AGENT_MUTEX_INIT  _task_mutex = xSemaphoreCreateMutex(); if (!_task_mutex) { AGENT_LOGE("[E] Agent: mutex create failed"); }
#define AGENT_MUTEX_DELETE  if (_task_mutex) vSemaphoreDelete(_task_mutex);
#define AGENT_TASK_LOCK   xSemaphoreTake(_task_mutex, portMAX_DELAY)
#define AGENT_TASK_UNLOCK xSemaphoreGive(_task_mutex)
#define AGENT_ISR_LOCK    portENTER_CRITICAL(&_isr_mux)
#define AGENT_ISR_UNLOCK  portEXIT_CRITICAL(&_isr_mux)

#elif defined(ARDUINO_ARCH_ESP8266)
// ESP8266: nincs FreeRTOS, csak globális megszakítás tiltás
#define AGENT_MUTEX_DECL
#define AGENT_MUTEX_INIT
#define AGENT_MUTEX_DELETE
#define AGENT_TASK_LOCK
#define AGENT_TASK_UNLOCK
#define AGENT_ISR_LOCK   noInterrupts()
#define AGENT_ISR_UNLOCK interrupts()

#else
// AVR vagy más platform fallback: egyszerű megoldás — ki- és bekapcsoljuk a megszakításokat
#define AGENT_MUTEX_DECL
#define AGENT_MUTEX_INIT
#define AGENT_MUTEX_DELETE
#define AGENT_TASK_LOCK
#define AGENT_TASK_UNLOCK
#define AGENT_ISR_LOCK   noInterrupts()
#define AGENT_ISR_UNLOCK interrupts()

#endif

// -----------------------------------------------------------------------------
// Helper tag types to distinguish storage mode
struct use_vector_tag {};
struct use_array_tag {};

// Traits: configure storage according to MaxCallbacks template parameter.
// If MaxCallbacks == 0 → dynamic vector of std::function (not ISR safe).
// If MaxCallbacks > 0  → fixed array of plain function pointers (ISR-safe).
template<typename Type, size_t MaxCallbacks>
struct agent_traits {
    typedef typename std::conditional<MaxCallbacks == 0, use_vector_tag, use_array_tag>::type tag;

    // vector mode stores std::function so user lambdas with captures are allowed
    struct cb_vector { std::function<void(Type)> fn; int id; };
    // array mode stores plain function pointer (compatible with ISR usage)
    struct cb_array { void (*fn)(Type); int id; };

    typedef typename std::conditional<
        MaxCallbacks == 0,
        std::vector<cb_vector>,
        std::array<cb_array, MaxCallbacks>
    >::type storage;

    // snapshot types used to safely call callbacks outside locks
    struct snapshot_vector { std::vector<std::function<void(Type)>> value; };
    struct snapshot_array { void (*value[MaxCallbacks])(Type); size_t count; };

    typedef typename std::conditional<
        MaxCallbacks == 0,
        snapshot_vector,
        snapshot_array
    >::type snapshot;
};

// -----------------------------------------------------------------------------
/**
 * @brief Agent<T, MaxCallbacks>
 *
 * "Smart" variable wrapper that notifies subscribers when the value changes.
 * - If MaxCallbacks == 0, uses a dynamic vector of std::function callbacks.
 *   -> Supports full C++ lambdas (not safe to call from ISR).
 * - If MaxCallbacks > 0, uses a fixed-size C array of plain function pointers.
 *   -> ISR-safe if you only attach plain functions and the platform allows it.
 *
 * Design choices in this implementation:
 *  - Implicit conversion to Type by value is supported (operator Type()).
 *  - Implicit conversion to Type& was removed because it exposes internal
 *    storage and allows mutating the value without invoking set() (and thus
 *    would bypass notifications and locks).
 *  - Move-assignment is deleted to avoid tricky mutex ownership/tearing.
 *  - set(...) takes the new value by value: this handles both lvalues and
 *    rvalues uniformly, enabling efficient moves where possible.
 *  - When the value actually changes, callbacks are collected into a
 *    snapshot while holding the lock, then invoked outside the lock.
 */
template <typename Type, size_t MaxCallbacks = 0>
class Agent {
    typedef agent_traits<Type, MaxCallbacks> traits;
    typedef typename traits::tag tag;
    typedef typename traits::storage Storage;
    typedef typename traits::snapshot Snapshot;

    using FuncPtr = std::function<void(Type)>;

public:
    // --- Constructors / Destructor ---

    /** Construct with initial value. */
    explicit Agent(Type init) : _value(std::move(init)) { AGENT_MUTEX_INIT; }

    // non-copyable (copy would duplicate subscribers which is ambiguous)
    Agent(const Agent&) = delete;

    // move-constructor: value and subscribers are moved; a new mutex is created
    // for the destination object. The moved-from object is left in a valid but
    // unspecified state (no subscribers, client id reset).
    Agent(Agent&& other) noexcept
        : _value(std::move(other._value)),
        _client_id(other._client_id),
        _callbacks(std::move(other._callbacks)),
        _count(other._count) {
        AGENT_MUTEX_INIT;
        other._client_id = 0;
        other._count = 0;
    }

    // Delete move-assignment operator to avoid complex mutex ownership transfer
    Agent& operator=(Agent&&) = delete;

    ~Agent() { AGENT_MUTEX_DELETE; }

    // --- Conversions ---
    /** Implicit conversion to Type by value. Safe and simple. */
    operator Type() const noexcept { return _value; }

    // NOTE: operator Type&() intentionally omitted to avoid exposing internal
    // reference to the stored value (which would bypass notification logic).

    // --- Value access helpers ---
    /** Get a reference to the value for short/atomic operations that will
     *  NOT send notifications. Use with care. */
    Type& val() { return _value; }
    const Type& val() const { return _value; }

    /** Pointer access. Useful when T is a struct and you want to access members.
     *  Mutating via pointer does NOT trigger notifications — call set() to
     *  update and notify. */
    Type* ptr() { return &_value; }
    const Type* ptr() const { return &_value; }

    /** operator-> is provided to make object-like usage convenient when Type
     *  is a struct/class: agent->member. Mutations via operator-> do not
     *  trigger notifications; prefer set() for changes that should notify.
     */
    Type* operator->() { return &_value; }
    const Type* operator->() const { return &_value; }

    // --- Assignment and arithmetic operators ---
    /** Assign from raw value; this triggers notifications if the value
     *  changed. */
    Agent& operator=(const Type& other) { set(other); return *this; }

    /** Assign from other Agent (copy the value, not subscribers). */
    Agent& operator=(const Agent& other) { if (this != &other) set(other.get()); return *this; }

    // arithmetic assignment helpers for arithmetic types
    Agent& operator+=(const Type& other) { set(_value + other); return *this; }

    template<typename T = Type>
    typename std::enable_if<std::is_arithmetic<T>::value, Agent&>::type
        operator-=(const Type& other) { set(_value - other); return *this; }

    template<typename T = Type>
    typename std::enable_if<std::is_arithmetic<T>::value, Agent&>::type
        operator*=(const Type& other) { set(_value * other); return *this; }

    template<typename T = Type>
    typename std::enable_if<std::is_arithmetic<T>::value, Agent&>::type
        operator/=(const Type& other) { set(_value / other); return *this; }

    template<typename T = Type>
    typename std::enable_if<std::is_arithmetic<T>::value, Agent&>::type
        operator%=(const Type& other) { set(_value % other); return *this; }

    // increment / decrement (pre/post)
    template<typename T = Type>
    typename std::enable_if<std::is_arithmetic<T>::value, Agent&>::type
        operator++() { set(_value + 1); return *this; }

    template<typename T = Type>
    typename std::enable_if<std::is_arithmetic<T>::value, Agent&>::type
        operator--() { set(_value - 1); return *this; }

    template<typename T = Type>
    typename std::enable_if<std::is_arithmetic<T>::value, Agent>::type
        operator++(int) { Agent temp = *this; set(_value + 1); return temp; }

    template<typename T = Type>
    typename std::enable_if<std::is_arithmetic<T>::value, Agent>::type
        operator--(int) { Agent temp = *this; set(_value - 1); return temp; }

    // comparison against raw values
    bool operator==(const Type& other) const { return _value == other; }
    bool operator!=(const Type& other) const { return _value != other; }
    bool operator<(const Type& other) const { return _value < other; }
    bool operator>(const Type& other) const { return _value > other; }
    bool operator<=(const Type& other) const { return _value <= other; }
    bool operator>=(const Type& other) const { return _value >= other; }

    // --- Subscription API ---
    /**
     * attach(callback)
     * - Dynamic mode (MaxCallbacks == 0): accepts std::function<void(Type)>
     *   (so lambdas with captures are allowed). Not safe to call from ISR.
     * - Static mode (MaxCallbacks > 0): accepts a plain function pointer
     *   void (*)(Type) which *can* be called from ISR contexts (platform
     *   dependent). In static mode attach will return 0 if the fixed table
     *   is full.
     *
     * Returns a positive subscription id (>0) on success, or 0 on failure.
     */
    template <size_t M = MaxCallbacks>
    typename std::enable_if<(M == 0), int>::type
        attach(const FuncPtr& callback) {
        if (!callback) {
            AGENT_LOGE("[E] Agent: null callback provided");
            return 0;
        }

        lock(tag());
        try {
            int id = attach_impl(callback, tag());
            unlock(tag());
            return id;
        }
        catch (const std::exception& e) {
            unlock(tag());
            AGENT_LOGE("[E] Agent: attach failed: %s", e.what());
            return 0;
        }
    }

    template <size_t M = MaxCallbacks>
    typename std::enable_if<(M > 0), int>::type
        attach(void (*callback)(Type)) {
        if (!callback) {
            AGENT_LOGE("[E] Agent: null callback provided");
            return 0;
        }
        lock(tag());
        int id = attach_impl(callback, tag());
        unlock(tag());
        return id;
    }

    /** Detach a subscriber by its id (no-op if id not present). */
    void detach(int id) {
        lock(tag());
        detach_impl(id, tag());
        unlock(tag());
    }

    /** Remove all subscribers. */
    void detachAll() {
        lock(tag());
        detachAll_impl(tag());
        unlock(tag());
    }

    /** Get current value (copy). */
    Type get() const { return _value; }

    /**
     * set(newValue, exclude_id = 0)
     * - Updates the stored value and notifies subscribers if the value
     *   actually changed.
     * - exclude_id: if non-zero, the callback with that id will not be
     *   invoked (useful to avoid notifying the originator).
     *
     * Important: set takes newValue by value (copy or move). This unifies
     * handling of lvalues and rvalues and avoids duplicate overloads.
     */
    void set(Type value, int exclude_id = 0) {
        // Fast path: compare before taking lock to avoid unnecessary locking in
        // the common case where value doesn't change. Note: this assumes the
        // Type::operator== is safe to call without locking.
        if (_value == value) return;

        lock(tag());
        // double-check after locking
        if (_value == value) { unlock(tag()); return; }

        // move the provided temporary value into storage. value is by-value so
        // this will be a move when the caller provided an rvalue.
        _value = std::move(value);

        // collect callbacks while holding the lock into a snapshot object
        auto snap = collect_callbacks(exclude_id, tag());
        unlock(tag());

        // notify outside lock
        notify(snap);
    }

    // --- Introspection helpers ---
    /** Number of current subscribers. */
    size_t subscriberCount() const {
        lock(tag());
        size_t count = 0;
        // Pre-C++17: no if constexpr → use tag dispatch
        count = subscriberCount_impl(tag());
        unlock(tag());
        return count;
    }

    size_t subscriberCount_impl(use_vector_tag) const {
        return _callbacks.size();
    }
    size_t subscriberCount_impl(use_array_tag) const {
        return _count;
    }

    /** Returns true if a subscriber with given id exists. */
    bool hasSubscriber(int id) const {
        lock(tag());
        bool exists = hasSubscriber_impl(id, tag());
        unlock(tag());
        return exists;
    }

    bool hasSubscriber_impl(int id, use_vector_tag) const {
        return std::any_of(_callbacks.begin(), _callbacks.end(),
            [id](const typename traits::cb_vector& cb) { return cb.id == id; });
    }

    bool hasSubscriber_impl(int id, use_array_tag) const {
        for (size_t i = 0; i < _count; ++i) {
            if (_callbacks[i].id == id) return true;
        }
        return false;
    }
private:
    // --- Members ---
    Type _value;
    int _client_id = 0;           // incremental id generator for subscriptions
    Storage _callbacks{};         // callback storage (vector or array)
    size_t _count = 0;            // used for static array mode
    AGENT_MUTEX_DECL;

    // --- attach_impl ---
    int attach_impl(const FuncPtr& cb, use_vector_tag) {
        _callbacks.push_back({ cb, ++_client_id });
        return _client_id;
    }

    int attach_impl(void (*cb)(Type), use_array_tag) {
        if (_count < MaxCallbacks) {
            _callbacks[_count++] = { cb, ++_client_id };
            return _client_id;
        }
        AGENT_LOGE("[E] Agent: too many subscribers, attach not possible");
        return 0;
    }

    // --- detach_impl ---
    void detach_impl(int id, use_vector_tag) {
        _callbacks.erase(
            std::remove_if(_callbacks.begin(), _callbacks.end(),
                [id](const typename traits::cb_vector& c) { return c.id == id; }),
            _callbacks.end());
    }

    void detach_impl(int id, use_array_tag) {
        for (size_t i = 0; i < _count; ++i) {
            if (_callbacks[i].id == id) {
                if (i < _count - 1) {
                    // cb_array is trivially copyable (function pointer + int)
                    std::memmove(&_callbacks[i], &_callbacks[i + 1],
                        (_count - i - 1) * sizeof(_callbacks[0]));
                }
                if (_count > 0) {
                    _callbacks[_count - 1] = { nullptr, 0 };
                }
                --_count;
                break;
            }
        }
    }

    void detachAll_impl(use_vector_tag) { _callbacks.clear(); }
    void detachAll_impl(use_array_tag) { _count = 0; }

    // --- collect_callbacks ---
    typename traits::snapshot_vector collect_callbacks(int exclude_id, use_vector_tag) {
        typename traits::snapshot_vector snap;
        snap.value.reserve(_callbacks.size());
        for (const auto& cb : _callbacks)
            if (cb.id != exclude_id)
                snap.value.push_back(cb.fn);
        return snap;
    }

    typename traits::snapshot_array collect_callbacks(int exclude_id, use_array_tag) {
        typename traits::snapshot_array snap;
        snap.count = 0;
        for (size_t i = 0; i < _count; ++i)
            if (_callbacks[i].id != exclude_id)
                snap.value[snap.count++] = _callbacks[i].fn;
        return snap;
    }

    // --- notify ---
    // Each callback is invoked in a try/catch to avoid exceptions tearing down
    // the program. On Arduino exceptions are rare but on ESP platforms they
    // might be enabled; best to guard the loop.
    void notify(const typename traits::snapshot_vector& snap) {
        for (auto& fn : snap.value) {
            if (!fn) continue;
            try { fn(_value); }
            catch (const std::exception& e) { AGENT_LOGE("[E] Agent: callback threw: %s", e.what()); }
            catch (...) { AGENT_LOGE("[E] Agent: callback threw unknown exception"); }
        }
    }

    void notify(const typename traits::snapshot_array& snap) {
        for (size_t i = 0; i < snap.count; ++i) {
            if (!snap.value[i]) continue;
            // plain function pointers cannot throw C++ exceptions safely in ISR
            // contexts; we still guard in case platform allows exceptions.
            try { snap.value[i](_value); }
            catch (const std::exception& e) { AGENT_LOGE("[E] Agent: callback threw: %s", e.what()); }
            catch (...) { AGENT_LOGE("[E] Agent: callback threw unknown exception"); }
        }
    }

    // --- locking helpers ---
    void lock(use_vector_tag) const { AGENT_TASK_LOCK; }
    void lock(use_array_tag) const { AGENT_ISR_LOCK; }
    void unlock(use_vector_tag) const { AGENT_TASK_UNLOCK; }
    void unlock(use_array_tag) const { AGENT_ISR_UNLOCK; }
};

// -----------------------------------------------------------------------------
// Non-member arithmetic operators to make Agent<T> act like a raw value
// (these simply forward to get()).

template <typename Type, size_t MaxCallbacks> Type operator+(const Agent<Type, MaxCallbacks>& lhs, const Type& rhs) { return lhs.get() + rhs; }
template <typename Type, size_t MaxCallbacks> Type operator+(const Type& lhs, const Agent<Type, MaxCallbacks>& rhs) { return lhs + rhs.get(); }

template <typename Type, size_t MaxCallbacks>
typename std::enable_if<std::is_arithmetic<Type>::value, Type>::type
operator-(const Agent<Type, MaxCallbacks>& lhs, const Type& rhs) { return lhs.get() - rhs; }

template <typename Type, size_t MaxCallbacks>
typename std::enable_if<std::is_arithmetic<Type>::value, Type>::type
operator-(const Type& lhs, const Agent<Type, MaxCallbacks>& rhs) { return lhs - rhs.get(); }

template <typename Type, size_t MaxCallbacks>
typename std::enable_if<std::is_arithmetic<Type>::value, Type>::type
operator*(const Agent<Type, MaxCallbacks>& lhs, const Type& rhs) { return lhs.get() * rhs; }

template <typename Type, size_t MaxCallbacks>
typename std::enable_if<std::is_arithmetic<Type>::value, Type>::type
operator*(const Type& lhs, const Agent<Type, MaxCallbacks>& rhs) { return lhs * rhs.get(); }

template <typename Type, size_t MaxCallbacks>
typename std::enable_if<std::is_arithmetic<Type>::value, Type>::type
operator/(const Agent<Type, MaxCallbacks>& lhs, const Type& rhs) { return lhs.get() / rhs; }

template <typename Type, size_t MaxCallbacks>
typename std::enable_if<std::is_arithmetic<Type>::value, Type>::type
operator/(const Type& lhs, const Agent<Type, MaxCallbacks>& rhs) { return lhs / rhs.get(); }

template <typename Type, size_t MaxCallbacks>
typename std::enable_if<std::is_arithmetic<Type>::value, Type>::type
operator%(const Agent<Type, MaxCallbacks>& lhs, const Type& rhs) { return lhs.get() % rhs; }

template <typename Type, size_t MaxCallbacks>
typename std::enable_if<std::is_arithmetic<Type>::value, Type>::type
operator%(const Type& lhs, const Agent<Type, MaxCallbacks>& rhs) { return lhs % rhs.get(); }

// Free function comparisons (Agent vs raw value)
template <typename Type, size_t MaxCallbacks> bool operator==(const Type& lhs, const Agent<Type, MaxCallbacks>& rhs) { return lhs == rhs.get(); }
template <typename Type, size_t MaxCallbacks> bool operator!=(const Type& lhs, const Agent<Type, MaxCallbacks>& rhs) { return lhs != rhs.get(); }
template <typename Type, size_t MaxCallbacks> bool operator<=(const Type& lhs, const Agent<Type, MaxCallbacks>& rhs) { return lhs <= rhs.get(); }
template <typename Type, size_t MaxCallbacks> bool operator>=(const Type& lhs, const Agent<Type, MaxCallbacks>& rhs) { return lhs >= rhs.get(); }
template <typename Type, size_t MaxCallbacks> bool operator<(const Type& lhs, const Agent<Type, MaxCallbacks>& rhs) { return lhs < rhs.get(); }
template <typename Type, size_t MaxCallbacks> bool operator>(const Type& lhs, const Agent<Type, MaxCallbacks>& rhs) { return lhs > rhs.get(); }

// Agent vs Agent comparisons
template <typename Type, size_t MaxCallbacks> bool operator==(const Agent<Type, MaxCallbacks>& a, const Agent<Type, MaxCallbacks>& b) { return a.get() == b.get(); }
template <typename Type, size_t MaxCallbacks> bool operator!=(const Agent<Type, MaxCallbacks>& a, const Agent<Type, MaxCallbacks>& b) { return a.get() != b.get(); }
template <typename Type, size_t MaxCallbacks> bool operator<=(const Agent<Type, MaxCallbacks>& a, const Agent<Type, MaxCallbacks>& b) { return a.get() <= b.get(); }
template <typename Type, size_t MaxCallbacks> bool operator>=(const Agent<Type, MaxCallbacks>& a, const Agent<Type, MaxCallbacks>& b) { return a.get() >= b.get(); }
template <typename Type, size_t MaxCallbacks> bool operator<(const Agent<Type, MaxCallbacks>& a, const Agent<Type, MaxCallbacks>& b) { return a.get() < b.get(); }
template <typename Type, size_t MaxCallbacks> bool operator>(const Agent<Type, MaxCallbacks>& a, const Agent<Type, MaxCallbacks>& b) { return a.get() > b.get(); }

// -----------------------------------------------------------------------------
// Usage notes (short):
// - For lambdas with captures use Agent<T> (MaxCallbacks==0). Not ISR-safe.
// - For ISR usage (attach from ISR or callbacks invoked in ISR) use fixed-size
//   Agent<T, N> and plain function pointers: void cb(T). Keep functions fast.
// - Mutating the value via val()/ptr()/operator-> bypasses notifications —
//   always call set() when you need subscribers to be notified.
// -----------------------------------------------------------------------------

// === Usage examples (also appended in the project's canvas file) ===
// Basic usage (dynamic mode, allow lambda captures):
// Agent<int> x(0);
// int id = x.attach([](int v){ Serial.print("x changed to: "); Serial.println(v); });
// x.set(5); // triggers callback
// x = 7;    // triggers callback via operator=
// x.detach(id); // remove subscriber

// Fixed callback mode (ISR-safe if functions are simple):
// void onTemp(int v) { /* quick ISR-friendly handler */ }
// Agent<int, 4> temp(0);         // up to 4 subscribers
// temp.attach(onTemp);
// temp.set(25);

// Excluding originator example:
// int id = x.attach([](int v){ /* ... */ });
// x.set(42, id); // all subscribers except the one with 'id' will be notified

// Note: to mutate members of a struct stored inside Agent<MyStruct>, you can
// use agent.ptr()->field or agent->field, but such changes do NOT emit
// notifications automatically. After mutating, call agent.set(agent.get())
// or agent.set(updatedValue) to notify subscribers.
