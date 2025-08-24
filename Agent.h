#pragma once

#include <Arduino.h>
#include <vector>
#include <functional>

#if !defined(AGENT_LOCK)  ||  !defined(AGENT_UNLOCK)
#define AGENT_LOCK
#define AGENT_UNLOCK
#endif

template <typename Type>
class Agent {
    using FuncPtr = std::function<void(Type)>;
    //using FuncPtr = void(*)(Type);

public:
    Agent(Type init) : _value(init) {}

    // Copy constructor
    Agent(const Agent& other) = default;

    // Move constructor
    Agent(Agent&& other) noexcept = default;

    // Implicit (explicit) conversion
    explicit operator Type() const { return _value; }

    // Assignment operator
    Agent& operator=(const Type& other) { set(other); return *this; }

    // Copy assignment operator
    Agent& operator=(const Agent& other) = default;

    // Move assignment operator
    Agent& operator=(Agent&& other) noexcept = default;

    // Relation operators
    bool operator==(const Type& other) const { return _value == other; }
    bool operator!=(const Type& other) const { return _value != other; }
    bool operator<(const Type& other) const { return _value < other; }
    bool operator>(const Type& other) const { return _value > other; }
    bool operator<=(const Type& other) const { return _value <= other; }
    bool operator>=(const Type& other) const { return _value >= other; }

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
