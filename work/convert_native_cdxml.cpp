#include <windows.h>
#include <gdiplus.h>
#include "next/integration/session.hpp"
#include <iostream>
int main(int argc,char** argv){if(argc!=3)return 2;Gdiplus::GdiplusStartupInput input;ULONG_PTR token;Gdiplus::GdiplusStartup(&token,&input,0);int code=0;{r62::integration::Session session;auto e=session.open(argv[1]);if(e.ok())e=session.save(argv[2],true);if(!e.ok()){std::cerr<<e.message;code=3;}else std::cout<<"atoms="<<session.doc.stats().aliveAtoms<<" bonds="<<session.doc.stats().aliveBonds<<"\n";}Gdiplus::GdiplusShutdown(token);return code;}
