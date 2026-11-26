#!/bin/bash

CC=${CC:-gcc}

LANGUAGE="c"
[ "$CC" = "g++" ] && LANGUAGE="c++"

FLAGS="-D _GNU_SOURCE -Wall -Wextra -Werror -fsyntax-only -I include -x $LANGUAGE"

FAILED=0
for header in $(cd include && find cerver -name '*.h' | sort); do
	if ! echo "#include <$header>" | $CC $FLAGS - ; then
		echo "$header does not compile on its own with $CC"
		FAILED=1
	fi
done

exit $FAILED
