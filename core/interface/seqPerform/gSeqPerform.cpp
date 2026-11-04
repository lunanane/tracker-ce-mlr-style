// Sequencer tracks mod — PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md)
#include "seqPerform/seqPerform.h"

void cSeqPerform::initDisplayControls()
{
    strControlProperties prop;

    // the 8 labels over the screen buttons: the tracks the perform FX / sends affect
    for (uint8_t i = 0; i < 8; i++)
    {
        prop.colors = display.getTheme().activeLabelsColors;
        prop.text = (char*)"";
        prop.style = (controlStyleShow | controlStyleCenterX | controlStyleCenterY | controlStyleFont3);
        prop.x = (800 / 8) * i + (800 / 16);
        prop.y = 424;
        prop.w = 800 / 8 - 6;
        prop.h = 55;
        prop.value = 0;
        label[i] = display.createControl<cLabel>(&prop);
    }

    strControlProperties prop2;
    prop2.text = nullptr;
    prop2.colors = nullptr;
    prop2.value = 255;
    prop2.style = controlStyleNoTransparency | controlStyleShow;
    prop2.x = 0;
    prop2.w = 800;
    prop2.y = 425;
    prop2.h = 54;
    bgLabel = display.createControl<cBgLabel>(&prop2);

    // the fader screen: one custom control below the stock title bar (y 29-422)
    strControlProperties prop3;
    prop3.colors = nullptr;
    prop3.text = nullptr;
    prop3.style = controlStyleShow;
    prop3.x = 0;
    prop3.y = 29;
    prop3.w = 800;
    prop3.h = 394;
    prop3.value = 0;
    prop3.data = &viewData;
    viewCtrl = display.createControl<cSeqFaderView>(&prop3);
    display.setControlData(viewCtrl, &viewData);
}
