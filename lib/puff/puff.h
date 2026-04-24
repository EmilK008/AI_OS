/* zlib contrib/puff — see puff.c for license (zlib). */
#ifndef PUFF_H
#define PUFF_H

#ifndef NIL
#define NIL ((unsigned char *)0)
#endif

int puff(unsigned char *dest, unsigned long *destlen,
         const unsigned char *source, unsigned long *sourcelen);

#endif
