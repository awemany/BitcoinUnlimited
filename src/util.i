%module util
%include defaults.i

%ignore CTranslationInterface;
%ignore translationInterface;

%include "util.h"
%template(StringToStringVectorMap) std::map<std::string, std::vector<std::string> >;
%{
    #include "util.h"
%}
