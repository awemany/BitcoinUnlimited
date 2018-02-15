%include <stdint.i>;
%include <std_string.i>;
%include <std_vector.i>;
%include <std_map.i>;

// FIXME: put these into own unit
%template(strVector) std::vector<std::string>;

%rename(Equals) operator ==;
