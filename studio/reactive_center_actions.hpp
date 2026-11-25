#pragma once

void App::insertReactiveCenter(bool allene) {
    if(textEditor.active&&!finishText(true))return;
    const auto result=studio::reactive_centers::insert(s,allene);
    error(result);
    s.activateCommand("chooseSelectionTool_Marquee");
    updateInfo();
    invalidate();
}
