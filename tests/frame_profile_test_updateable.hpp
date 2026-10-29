#pragma once
#include "IUpdateable.hpp"
/// <summary>Provides a real generated-interface updateable without starting a scene or GPU.</summary>
class ProfileTestUpdateable final : public ::IUpdateable {
public:
    /// <summary>Returns a fixed update order for this diagnostic item.</summary>
    uint8_t get_UpdateOrder() override { return Order; }
    /// <summary>Preserves the interface's mutable order contract.</summary>
    void set_UpdateOrder(uint8_t value) override { Order=value; }
    /// <summary>Leaves timing injection under the test's explicit control.</summary>
    void Update() override {}
private:
    /// <summary>Stores the diagnostic item's update order.</summary>
    uint8_t Order=0;
};
