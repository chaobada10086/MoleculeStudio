#include "cdx_binary.hpp"
#include <iostream>
#include <fstream>
int main(int argc,char**argv){std::string xml="<CDXML><page id=\"1\"><tlcplate id=\"2\" ShowBorders=\"yes\"><tlclane id=\"3\"/></tlcplate></page></CDXML>",bytes,back,why;if(argc>1){std::ifstream in(argv[1]);xml.assign(std::istreambuf_iterator<char>(in),{});}if(!cdxbinary::fromXml(xml,bytes,why)){std::cout<<why;return 1;}if(!cdxbinary::toXml(bytes,back,why)){std::cout<<why;return 2;}std::cout<<back;if(argc>2){std::ofstream(argv[2],std::ios::binary).write(bytes.data(),bytes.size());}}

