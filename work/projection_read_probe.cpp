#include "engine/projection_export.hpp"
#include "engine/projection_molecule.hpp"
#include "formula.hpp"
#include <fstream>
#include <iostream>
int main(){std::ifstream in("work/newman-tags-chemdraw.cdxml");std::string bytes((std::istreambuf_iterator<char>(in)),{});std::ofstream("work/native-normalized.cdxml")<<studio_projection_export::strip(bytes);r62::core::Document d;auto e=r62::persistence::readCdxml(bytes,d);std::cout<<e.message<<" atoms="<<d.stats().aliveAtoms<<" bonds="<<d.stats().aliveBonds<<"\n";std::vector<r62::core::ObjectRef> refs;for(auto a:d.atoms().records())if(a.isAlive()){refs.emplace_back(r62::core::ObjectKind::Atom,a.id);std::cout<<a.id<<" element="<<a.element<<" role="<<r62::projection_molecule::role(d,a.id)<<" label="<<d.atomLabel(a.id)<<" H="<<int(a.semantics.implicitHydrogens)<<"\n";}auto f=studio::analyze(d,refs);std::cout<<"formula="<<f.plain<<" note="<<f.note<<" valid="<<f.valid<<"\n";for(auto p:d.paths().records())if(p.isAlive())std::cout<<"path "<<p.id<<"\n";for(auto a:d.arrows().records())if(a.isAlive())std::cout<<"arrow "<<a.id<<"\n";}
