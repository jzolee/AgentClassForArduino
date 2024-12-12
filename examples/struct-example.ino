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
