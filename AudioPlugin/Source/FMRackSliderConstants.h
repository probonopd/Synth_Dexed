#pragma once

/**
 * Global slider sizing constants for the FMRack plugin.
 * All sliders throughout the application should use these values
 * to ensure consistent, tight packing everywhere.
 */
namespace FMRackSliderConstants
{
    // Slider dimensions - compact for tight packing
    constexpr int kSliderWidth = 24;
    constexpr int kSliderGap = 2;
    
    // Text box dimensions (inside slider)
    constexpr int kTextBoxWidth = 24;
    constexpr int kTextBoxHeight = 12;
    
    // Label dimensions and font sizes
    constexpr int kLabelHeight = 10;
    constexpr float kSliderLabelFontSize = 9.0f;      // Label below slider
    constexpr float kSliderValueFontSize = 12.0f;     // Value text box font
    constexpr float kGroupLabelFontSize = 12.0f;      // Group/section labels
    
    // Height constraints
    constexpr int kMinSliderHeight = 100;
    constexpr int kMaxSliderHeight = 100;
    
    // Labeled slider total height (slider + label + gap)
    constexpr int kLabeledSliderMinHeight = kMinSliderHeight + kLabelHeight + 4;
}
