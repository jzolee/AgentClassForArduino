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
