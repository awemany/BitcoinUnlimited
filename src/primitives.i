%module primitives
%include defaults.i

%ignore csBlockHashToIdx;
%ignore CBlockHeader::CURRENT_VERSION;

%include "uint256.h"

%include "amount.h"
%include "primitives/transaction.h"
%include "primitives/block.h"


%{
    #include "uint256.h"
    #include "amount.h"
    #include "primitives/transaction.h"
%}

%template(transactionVector) std::vector<CTransaction>;
%template(txInVector) std::vector<CTxIn>;
%template(txOutVector) std::vector<CTxOut>;


%{
    #include "primitives/block.h"
%}
