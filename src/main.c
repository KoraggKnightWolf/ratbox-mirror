/*
 * main.c: stub main program
 *
 * There is a good point to this.  On platforms where shared libraries cannot
 * have unresolved symbols, we solve this by making the core of the ircd itself
 * a shared library.  Its kinda funky, but such is life
 */
#include <stdio.h>
int ratbox_main(int, char **);

int main(int argc, char **argv)
{
	fprintf(stderr, "Starting..\n");
	return ratbox_main(argc, argv);
}

