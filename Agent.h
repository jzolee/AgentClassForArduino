#pragma once

#include <Arduino.h>
#include <vector>
#include <functional>
#include <algorithm> // For std::remove_if

#if !defined(AGENT_LOCK) || !defined(AGENT_UNLOCK)
#define AGENT_LOCK
#define AGENT_UNLOCK
#endif

template <typename Type>
class Agent {
    using FuncPtr = std::function<void(Type)>;

public:
    Agent(Type init) : _value(init) {}

    Agent(const Agent& other) = default;
    Agent(Agent&& other) noexcept = default;
    Agent& operator=(Agent&& other) noexcept = default;
    Agent& operator=(const Agent& other) { if (this != &other) { set(other.get()); } return *this; }

    explicit operator Type() const { return _value; }

    Agent& operator=(const Type& other) { set(other); return *this; }

    bool operator==(const Type& other) const { return _value == other; }
    bool operator!=(const Type& other) const { return _value != other; }
    bool operator<(const Type& other) const { return _value < other; }
    bool operator>(const Type& other) const { return _value > other; }
    bool operator<=(const Type& other) const { return _value <= other; }
    bool operator>=(const Type& other) const { return _value >= other; }

    Agent<Type>& operator++() { set(_value + 1); return *this; }
    Agent<Type>& operator--() { set(_value - 1); return *this; }
    Agent<Type> operator++(int) { Agent temp = *this; set(_value + 1); return temp; }
    Agent<Type> operator--(int) { Agent temp = *this; set(_value - 1); return temp; }

    Agent<Type>& operator+=(const Type& other) { set(_value + other); return *this; }
    Agent<Type>& operator-=(const Type& other) { set(_value - other); return *this; }
    Agent<Type>& operator*=(const Type& other) { set(_value * other); return *this; }
    Agent<Type>& operator/=(const Type& other) { set(_value / other); return *this; }

    int attach(const FuncPtr callback) {
        AGENT_LOCK;
        _callbacks.push_back({ callback, ++_client_id });
        AGENT_UNLOCK;
        return _client_id;
    }

    void detach(const int id) {
        AGENT_LOCK;
        _callbacks.erase(
            std::remove_if(_callbacks.begin(), _callbacks.end(),
                [id](const Agent<Type>::cb_s& cb) {
                    return cb.id == id;
                }),
            _callbacks.end());
        AGENT_UNLOCK;
    }

    void detachAll() {
        AGENT_LOCK;
        _callbacks.clear();
        AGENT_UNLOCK;
    }

    Type get() const { return _value; }

    void set(const Type value, const int exclude_id = 0) {
        AGENT_LOCK;
        if (!(_value == value)) {
            _value = value;
            for (const auto& element : _callbacks)
                if (element.id != exclude_id)
                    element.fn(_value);
        }
        AGENT_UNLOCK;
    }

private:
    Type _value;
    int _client_id = 0;

    struct cb_s {
        FuncPtr fn;
        int id;
    };

    std::vector<cb_s> _callbacks;
};

template <typename Type>
Type operator+(const Agent<Type>& lhs, const Type& rhs) {
    return lhs.get() + rhs;
}

template <typename Type>
Type operator+(const Type& lhs, const Agent<Type>& rhs) {
    return lhs + rhs.get();
}

template <typename Type>
Type operator-(const Agent<Type>& lhs, const Type& rhs) {
    return lhs.get() - rhs;
}

template <typename Type>
Type operator-(const Type& lhs, const Agent<Type>& rhs) {
    return lhs - rhs.get();
}

template <typename Type>
Type operator*(const Agent<Type>& lhs, const Type& rhs) {
    return lhs.get() * rhs;
}

template <typename Type>
Type operator*(const Type& lhs, const Agent<Type>& rhs) {
    return lhs * rhs.get();
}

template <typename Type>
Type operator/(const Agent<Type>& lhs, const Type& rhs) {
    return lhs.get() / rhs;
}

template <typename Type>
Type operator/(const Type& lhs, const Agent<Type>& rhs) {
    return lhs / rhs.get();
}
