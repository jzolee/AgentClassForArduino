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
