# Agent Class for Arduino

The `Agent` class is a lightweight utility designed for the Arduino platform, allowing efficient management of event-based callbacks for variable changes. The class is thread-safe and can be extended or customized for various use cases.

---

## Features

- Attach multiple callback functions to a variable.
- Notify callbacks when the variable changes.
- Exclude specific callbacks during notifications.
- Thread-safe operation with customizable lock and unlock macros.

---

## Usage

### Include the Library
```cpp
#include "Agent.h"
```

### Define Lock and Unlock Macros (Optional)
To ensure thread safety, you can define `AGENT_LOCK` and `AGENT_UNLOCK` for your platform. For example:

```cpp
#define AGENT_LOCK   noInterrupts()
#define AGENT_UNLOCK interrupts()
```
If you do not define these macros, the class will default to empty definitions.

### Creating an Agent
Create an `Agent` instance, specifying the data type of the variable being managed.

```cpp
Agent<int> myAgent(0); // Initialize with a value of 0
```

### Attaching Callbacks
Attach a callback function to be notified when the variable changes.

```cpp
void myCallback(int value) {
    Serial.print("Value changed to: ");
    Serial.println(value);
}

int id = myAgent.attach(myCallback);
```
The `attach` method returns a unique ID for the callback.

### Setting a Value
Set a new value using the `set()` method. This triggers all attached callbacks (except the one excluded by ID, if specified).

```cpp
myAgent.set(42); // Triggers all callbacks

myAgent.set(100, id); // Excludes the callback with ID `id`
```

### Getting the Current Value
Retrieve the current value with the `get()` method:

```cpp
int value = myAgent.get();
```

---

## Example Code

### Basic Example
```cpp
#include <Arduino.h>
#include "Agent.h"

Agent<int> myAgent(0);

void callback1(int value) {
    Serial.print("Callback 1 - Value: ");
    Serial.println(value);
}

void callback2(int value) {
    Serial.print("Callback 2 - Value: ");
    Serial.println(value);
}

void setup() {
    Serial.begin(9600);

    // Attach callbacks
    myAgent.attach(callback1);
    myAgent.attach(callback2);

    // Change value to trigger callbacks
    myAgent.set(10);
    myAgent.set(20);
}

void loop() {
    // No loop action required in this example
}
```

### Thread-Safe Example
This example includes locking and unlocking using interrupt control for thread safety.

```cpp
#include <Arduino.h>
#include "Agent.h"

#define AGENT_LOCK   noInterrupts()
#define AGENT_UNLOCK interrupts()

Agent<float> myAgent(0.0f);

void callback(float value) {
    Serial.print("Value updated: ");
    Serial.println(value);
}

void setup() {
    Serial.begin(9600);

    myAgent.attach(callback);

    myAgent.set(3.14f); // Trigger callback
}

void loop() {
    delay(1000);
    myAgent.set(random(0, 100) / 10.0f); // Update value randomly
}
```

### Struct Example with Equality Operator
When using `Agent` with a struct, ensure the struct defines the `operator==` to allow the `Agent` class to detect changes.

```cpp
#include <Arduino.h>
#include "Agent.h"

struct Data {
    int value1;
    float value2;

    bool operator==(const Data& other) const {
        return value1 == other.value1 && value2 == other.value2;
    }
};

Agent<Data> myAgent({0, 0.0f});

void callback(const Data& data) {
    Serial.print("Value1: ");
    Serial.print(data.value1);
    Serial.print(", Value2: ");
    Serial.println(data.value2);
}

void setup() {
    Serial.begin(9600);

    // Attach callback
    myAgent.attach(callback);

    // Update values
    myAgent.set({10, 3.14f});
    myAgent.set({20, 2.71f});
}

void loop() {
    // No loop action required in this example
}
```

### Example with Callback Exclusion
In some cases, you may want to update the value but exclude a specific callback from being notified. This can be achieved using the `set` method's `exclude_id` parameter.

```cpp
#include <Arduino.h>
#include "Agent.h"

Agent<int> myAgent(0);

void callback1(int value) {
    Serial.print("Callback 1 - Value: ");
    Serial.println(value);
}

void callback2(int value) {
    Serial.print("Callback 2 - Value: ");
    Serial.println(value);
}

void setup() {
    Serial.begin(9600);

    // Attach callbacks and store their IDs
    int id1 = myAgent.attach(callback1);
    int id2 = myAgent.attach(callback2);

    // Trigger all callbacks
    myAgent.set(42);

    // Update value but exclude callback1
    Serial.println("Excluding callback1:");
    myAgent.set(100, id1);
}

void loop() {
    // No loop action required in this example
}
```

---

## Notes

- The `Agent` class supports any type that has a valid equality operator (`==`) for detecting value changes.
- The class uses `std::vector` for managing callbacks. On memory-constrained systems, you may optimize further by using a different container type if needed.
- Ensure that callback functions are lightweight to avoid delays in notification.

---

## Contribution
Feel free to contribute to this project by submitting pull requests or reporting issues on GitHub.

---

## License
This project is licensed under the MIT License. See the `LICENSE` file for more details.

---

### If you want to cheer me up with a coffee:
<a href="https://www.buymeacoffee.com/jzolee" target="_blank"><img src="https://www.buymeacoffee.com/assets/img/custom_images/orange_img.png" alt="Buy Me A Coffee" style="height: 41px !important;width: 174px !important;box-shadow: 0px 3px 2px 0px rgba(190, 190, 190, 0.5) !important;-webkit-box-shadow: 0px 3px 2px 0px rgba(190, 190, 190, 0.5) !important;" ></a>
