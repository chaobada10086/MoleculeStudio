#pragma once
void App::openNmrTab(const std::string& snapshot){
    if(!host){NmrDialog::show(h,"",nullptr,snapshot,nullptr,true);return;}
    const std::string sourceName=fileName;host->newTab();if(host->active<0)return;App* page=host->tabs[host->active];
    page->nmrPage.reset(new NmrDialog());auto& dialog=*page->nmrPage;dialog.source=snapshot;dialog.owner=page->h;
    dialog.asyncPredictor=[snapshot](const std::string& action,std::string* result,const std::atomic<bool>* cancel){return chemservice::run(nullptr,snapshot,"text/xml","text/x-moleculestudio-nmr",action,result,cancel);};
    dialog.closePage=[page](){if(page->host)page->host->closeTab(page);};
    page->fileName="核磁预测 · "+(sourceName.empty()?"未命名结构":sourceName);page->nmrChild=dialog.createEmbedded(page->h);
    if(!page->nmrChild){page->nmrPage.reset();page->host->closeTab(page);throw std::runtime_error("无法创建核磁预测标签页");}
    page->layout();page->invalidate();SetFocus(page->nmrChild);
}
