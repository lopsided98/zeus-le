#include <string.h>
#include <ctype.h>

#include "private/lftpd_string.h"

char* lftpd_string_trim(char* s) {
	size_t len = strlen(s);
	for (size_t i = 0; i < len && isspace(s[i]); ++i) {
		s++;
	}
	len = strlen(s);
	if (len == 0) return s;
	for (size_t i = len - 1; i >= 0 && isspace(s[i]); --i) {
		s[i] = '\0';
	}
	return s;
}
