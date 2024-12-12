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
