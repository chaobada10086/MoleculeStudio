from pathlib import Path
p=Path('work/r43_probe.cpp');s=p.read_text();s=s.replace('k<6','k<8').replace('b.arcSagitta=(k<2?65:k<4?-65:100);','b.arcSagitta=k<4?65:-65;').replace('ObjectId first,last;', 'if(k%4>=2)std::swap(b.begin,b.end);ObjectId first,last;')
s=s.replace('std::cout<<k<<', '''auto sampled=points(p);double positive=0,negative=0;for(size_t i=1;i+1<sampled.size();++i){double side=cross(p.end-p.begin,sampled[i]-p.begin);positive=std::max(positive,side);negative=std::max(negative,-side);if(dot(unit(sampled[i]-sampled[i-1]),unit(sampled[i+1]-sampled[i]))<.99){std::cerr<<"kink "<<k;return 2;}}if(positive<1||negative<1||std::max(positive,negative)/std::min(positive,negative)>=3){std::cerr<<"imbalance "<<k<<" "<<positive<<" "<<negative;return 3;}std::cout<<k<<''')
p.write_text(s)
