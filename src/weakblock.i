%module weakblock
%include defaults.i

%ignore cs_weakblocks;

%rename(Weakblock) std::vector<CTransaction*>;

%include "weakblock.h"
%{
    #include "uint256.h"
%}
%template(WeakChainMap) std::vector<std::pair<uint256, size_t> >;

%{
    #include "weakblock.h"
%}
