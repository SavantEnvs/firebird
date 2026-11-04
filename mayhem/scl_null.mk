# Read via MAKEFILES by mayhem/build.sh. SCL_check_access (jrd/scl.epp) forms
# `s_class->sclClassUser.second` before testing s_class for null, so -fsanitize=null trips on
# every statement run without a security class. Relax `null` for this one object only.
# AC_CXXFLAGS (configure's CXXFLAGS) comes last on the compile line, so the relaxation must be
# appended there to win over the -fsanitize=undefined it carries.
%/jrd/scl.o: override AC_CXXFLAGS += -fno-sanitize=null
