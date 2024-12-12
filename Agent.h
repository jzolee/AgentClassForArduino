#pragma once

#include <Arduino.h>
#include <vector>

#if !defined(AGENT_LOCK)  ||  !defined(AGENT_UNLOCK)
#define AGENT_LOCK
#define AGENT_UNLOCK
#endif

template <typename Type>
class Agent {
    //using FuncPtr =  std::function<void(Type)>;
    using FuncPtr = void(*)(Type);

public:
    Agent(Type init) : _value(init) {}
    int attach(const FuncPtr callback) {
        AGENT_LOCK;
        _callbacks.push_back({ callback, ++_client_id });
        AGENT_UNLOCK;
        return _client_id;
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
