
#include <string.h>

size_t strlcat(char *dst, const char *src, size_t siz)
{
        size_t ld = strlen(dst);
        size_t ls = strlen(src);
        if (ld >= siz)
             return 0;
        if (ld + ls >= siz)
            ls = siz - ld -1;
        strncat(dst, src, ls);
        return ld + ls; 
}

size_t
strlcpy(char *dst, const char *src, size_t siz)
{
	char *d = dst;
	const char *s = src;
	size_t n = siz;

	/* Copy as many bytes as will fit */
	if (n != 0) {
		while (--n != 0) {
			if ((*d++ = *s++) == '\0')
				break;
		}
	}

	/* Not enough room in dst, add NUL and traverse rest of src */
	if (n == 0) {
		if (siz != 0)
			*d = '\0';		/* NUL-terminate dst */
		while (*s++)
			;
	}

	return(s - src - 1);	/* count does not include NUL */
}
