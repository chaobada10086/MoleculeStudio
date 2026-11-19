#pragma once
// Included in r62::arrows after MechanismObstacle and mechanismDistance.
inline bool routeClear(const core::Vec2& a,const core::Vec2& b,const ArrowParams& p,const std::vector<MechanismObstacle>& obstacles,double extra=1.2) {
    int steps=std::max(1,int(std::ceil(core::length(b-a)/1.2)));
    for(int i=0;i<=steps;++i){core::Vec2 q=a+(b-a)*(double(i)/steps);
        for(const auto& o:obstacles){
            if(o.glyphOwner.valid()){
                if(mechanismGlyphContact(o,p,q))continue;
                // Grid padding must leave room to dock beside a letter.
                const double proximity=std::min(core::length(q-p.begin),core::length(q-p.end));
                if(mechanismObstacleDistance(q,o)<o.radius+std::min(extra,proximity*.3))return false;
                continue;
            }
            // Only the actual endpoint object gets a contact exemption.
            if(mechanismGeometryContact(o,p,q))continue;
            if(mechanismDistance(q,o.a,o.b)<o.radius+extra)return false;
        }
    }return true;
}
// Open, clamped B-spline: endpoints interpolate and interior cubic spans are
// C2 continuous. Unlike rounded polylines, curvature changes along the stroke.
inline std::vector<core::Vec2> mechanismSpline(const std::vector<core::Vec2>& control) {
    int n=int(control.size())-1,degree=std::min(3,n);
    std::vector<double> knots(n+degree+2);
    for(int i=0;i<int(knots.size());++i)knots[i]=i<=degree?0:i>=n+1?1:double(i-degree)/(n-degree+1);
    std::vector<core::Vec2> out;int samples=std::min(2048,std::max(64,n*32));
    for(int j=0;j<=samples;++j){double t=double(j)/samples;int k=degree;
        while(k<n&&t>=knots[k+1])++k;
        std::vector<core::Vec2> d;for(int i=0;i<=degree;++i)d.push_back(control[k-degree+i]);
        for(int level=1;level<=degree;++level)for(int i=degree;i>=level;--i){int index=k-degree+i;double den=knots[index+degree-level+1]-knots[index];double a=den>1e-12?(t-knots[index])/den:0;d[i]=d[i-1]*(1-a)+d[i]*a;}
        out.push_back(d[degree]);
    }
    out.front()=control.front();out.back()=control.back();return out;
}
inline bool findMechanismRoute(ArrowParams& p,const std::vector<MechanismObstacle>& obstacles) {
    const double span=core::length(p.end-p.begin),margin=std::min(100.0,std::max(25.0,span*.65));
    double left=std::min(p.begin.x,p.end.x)-margin,top=std::min(p.begin.y,p.end.y)-margin;
    double width=std::fabs(p.end.x-p.begin.x)+2*margin,height=std::fabs(p.end.y-p.begin.y)+2*margin;
    const double cell=std::max(2.5,std::max(width,height)/100);
    int nx=int(std::ceil(width/cell))+1,ny=int(std::ceil(height/cell))+1;
    auto index=[&](core::Vec2 q){return int(std::lround((q.y-top)/cell))*nx+int(std::lround((q.x-left)/cell));};
    int start=index(p.begin),goal=index(p.end);
    auto point=[&](int k){return k==start?p.begin:k==goal?p.end:core::Vec2(left+(k%nx)*cell,top+(k/nx)*cell);};
    const int count=nx*ny*8;std::vector<double> costs(count,std::numeric_limits<double>::infinity());std::vector<int> previous(count,-1);
    using Item=std::pair<double,int>;std::priority_queue<Item,std::vector<Item>,std::greater<Item>> open;
    for(int d=0;d<8;++d){costs[start*8+d]=0;open.push({span,start*8+d});}
    int dx[]={1,1,0,-1,-1,-1,0,1},dy[]={0,1,1,1,0,-1,-1,-1};
    std::vector<signed char> edgeClear(nx*ny*8,-1);
    int found=-1,visited=0;
    while(!open.empty()&&visited++<160000){auto item=open.top();open.pop();int state=item.second,k=state/8,dir=state%8;
        if(item.first>costs[state]+core::length(point(k)-p.end)+1e-7)continue;
        if(k==goal){found=state;break;}
        for(int d=0;d<8;++d){int x=k%nx+dx[d],y=k/nx+dy[d];if(x<0||x>=nx||y<0||y>=ny)continue;int next=y*nx+x;
            auto a=point(k),b=point(next);auto& clear=edgeClear[k*8+d];if(clear<0)clear=routeClear(a,b,p,obstacles,2.4)?1:0;if(!clear)continue;
            int turn=std::abs(d-dir);turn=std::min(turn,8-turn);
            double cost=costs[state]+core::length(b-a)+turn*turn*cell*.28;
            // Keep a blocked gesture on its chosen side while searching for a
            // corridor. This remains a soft cost so obstacles can force a swap.
            auto chord=p.end-p.begin,relative=(a+b)*.5-p.begin;
            double lateral=(chord.x*relative.y-chord.y*relative.x)/std::max(1.,span);
            if(p.mechanismSide&&lateral*p.mechanismSide < -cell*.25)cost+=core::length(b-a)*5.;
            int ns=next*8+d;if(cost>=costs[ns])continue;costs[ns]=cost;previous[ns]=state;open.push({cost+core::length(b-p.end),ns});
        }
    }
    if(found<0)return false;
    std::vector<core::Vec2> path;for(int s=found;s>=0;s=previous[s])path.push_back(point(s/8));std::reverse(path.begin(),path.end());
    // Remove grid stair steps with visibility checks, then round the remaining
    // corners with quadratic Beziers. Recheck every rounded candidate.
    std::vector<core::Vec2> simple;simple.push_back(path.front());
    for(size_t i=0;i+1<path.size();){size_t j=path.size()-1;while(j>i+1&&!routeClear(path[i],path[j],p,obstacles,2.4))--j;simple.push_back(path[j]);i=j;}
    if(simple.size()<3)return false; // an unobstructed span uses the light arc preset
    std::vector<core::Vec2> smooth;smooth.push_back(simple.front());
    for(size_t i=1;i+1<simple.size();++i){auto a=simple[i-1],b=simple[i],c=simple[i+1];double radius=std::min(core::length(b-a),core::length(c-b))*.4;
        std::vector<core::Vec2> corner;
        for(int attempt=0;attempt<8;++attempt){auto u=b+(a-b)*(radius/core::length(a-b)),v=b+(c-b)*(radius/core::length(c-b));corner.clear();
            for(int j=0;j<=24;++j){double t=j/24.;corner.push_back(u*((1-t)*(1-t))+b*(2*t*(1-t))+v*(t*t));}
            bool clear=true;for(size_t j=1;j<corner.size();++j)if(!routeClear(corner[j-1],corner[j],p,obstacles,.3)){clear=false;break;}
            if(clear)break;radius*=.5;
        }
        smooth.insert(smooth.end(),corner.begin(),corner.end());
    }
    smooth.push_back(simple.back());
    for(size_t i=1;i<smooth.size();++i)if(!routeClear(smooth[i-1],smooth[i],p,obstacles,0))return false;
    // Try a continuous spline first. If it clips a tight corridor, insert
    // supporting controls along the already collision-tested rounded route.
    // Never trade chemical-object clearance for a smoother-looking stroke.
    bool flowing=false;
    // A human-drawn curve starts turning well before the tightest point. Let
    // the guide polygon breathe laterally, then fit one low-control-count spline.
    // The original endpoints remain fixed and every candidate is revalidated.
    for(double expansion:{1.0,1.35,1.75,2.25}) {
        auto controls=simple;auto chord=p.end-p.begin;double squared=span*span;
        for(size_t i=1;i+1<controls.size();++i){auto d=controls[i]-p.begin;double t=(d.x*chord.x+d.y*chord.y)/squared;auto projection=p.begin+chord*t;controls[i]=projection+(controls[i]-projection)*expansion;}
        auto candidate=mechanismSpline(controls);bool clear=true;
        for(size_t i=1;i<candidate.size();++i)if(!routeClear(candidate[i-1],candidate[i],p,obstacles,.5)){clear=false;break;}
        if(clear){smooth.swap(candidate);flowing=true;break;}
    }
    for(int stride:{12,6,3,1}) {
        if(flowing)break;
        std::vector<core::Vec2> controls;controls.push_back(smooth.front());
        for(size_t i=stride;i+1<smooth.size();i+=stride)controls.push_back(smooth[i]);
        controls.push_back(smooth.back());if(controls.size()<4)continue;
        auto candidate=mechanismSpline(controls);bool clear=true;
        for(size_t i=1;i<candidate.size();++i)if(!routeClear(candidate[i-1],candidate[i],p,obstacles,.5)){clear=false;break;}
        if(clear){smooth.swap(candidate);break;}
    }
    core::Vec2 chord=p.end-p.begin,n(-chord.y,chord.x);double squared=span*span;p.route.clear();
    for(auto q:smooth){auto d=q-p.begin;p.route.push_back(core::Vec2((d.x*chord.x+d.y*chord.y)/squared,(d.x*n.x+d.y*n.y)/squared));}
    return true;
}
