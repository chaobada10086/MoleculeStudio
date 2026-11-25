#pragma once
#include "atom_hydrogen_actions.hpp"

bool App::cycleAtomHydrogens(ObjectId id,bool initialize,int replacement){
    return studio::atom_hydrogen::cycle(s,status,id,initialize,replacement);
}

bool App::preferAtomHydride(ObjectId id,int element){
    return studio::atom_hydrogen::prefer(s,status,id,element);
}
